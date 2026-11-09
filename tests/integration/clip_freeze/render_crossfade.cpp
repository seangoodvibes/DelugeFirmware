// SPDX-License-Identifier: GPL-3.0-or-later
// Render the production freeze controller against a fixed native PCM passage.
#include "model/clip/clip_freeze.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
	assert(argc == 5);
	std::ifstream input(argv[1], std::ios::binary);
	std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
	assert(bytes.size() % sizeof(StereoSample) == 0);
	std::vector<StereoSample> source(bytes.size() / sizeof(StereoSample));
	std::copy(bytes.begin(), bytes.end(), reinterpret_cast<char*>(source.data()));
	int start = std::stoi(argv[2]), length = std::stoi(argv[3]);
	assert(start >= 0 && start + length + 2176 <= int(source.size()));
	Clip clip;
	playbackHandler.tick = (uint64_t{static_cast<uint32_t>(length)} << 32) / clip.loopLength;
	ClipFreeze freeze;
	assert(freeze.arm(clip, clip.loopLength) == Error::NONE);
	int repeat = 0;
	auto boundary = [&] {
		playbackHandler.lastSwungTickActioned += clip.loopLength;
		AudioEngine::audioSampleTimer += 128;
		freeze.loop(++repeat);
	};
	boundary();
	auto feed = [&](int offset, int count) {
		std::array<int32_t, 128> send{};
		freeze.capture({source.data() + offset, size_t(count)}, {send.data(), size_t(count)});
	};
	for (int i = 0; i < length; i += 128) {
		feed(start + i, std::min(128, length - i));
	}
	boundary();
	for (int i = 0; freeze.capturing(); i += 128) {
		feed(start + length + i, 128);
	}
	for (auto& recorder : recorders)
		recorder->status = RecorderStatus::COMPLETE;
	boundary();
	assert(freeze.start(0, repeat));
	std::ofstream output(argv[4], std::ios::binary);
	for (int cycle = 0; cycle < 2; ++cycle) {
		if (cycle)
			assert(freeze.start(0, ++repeat));
		for (int i = 0; i < length; i += 128) {
			std::array<StereoSample, 128> audio{};
			std::array<int32_t, 128> send{};
			int count = std::min(128, length - i);
			assert(freeze.render({audio.data(), size_t(count)}, {send.data(), size_t(count)}));
			// Second cycle includes the real outgoing continuation at its beginning.
			if (cycle)
				output.write(reinterpret_cast<char*>(audio.data()), count * sizeof(StereoSample));
		}
	}
	freeze.clear();
	assert(voices == 0);
}
