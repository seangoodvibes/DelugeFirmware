// SPDX-License-Identifier: GPL-3.0-or-later

#include "model/clip/clip_freeze.h"
#include "model/clip/freeze_cycle.h"
#include "model/clip/freeze_transition_plan.h"
#include <array>
#include <cmath>
#include <iostream>

using State = ClipFreeze::State;
static int32_t repeatCount = 0;
static void boundary(ClipFreeze& freeze) {
	AudioEngine::audioSampleTimer += 128;
	playbackHandler.lastSwungTickActioned += 96;
	freeze.loop(++repeatCount);
}
static void finishWrites() {
	for (auto& recorder : recorders)
		recorder->status = RecorderStatus::COMPLETE;
}
static void captureTail(ClipFreeze& freeze, StereoSample value = {1024, 2048}, int32_t sendValue = 0) {
	std::array<StereoSample, 128> audio;
	audio.fill(value);
	std::array<int32_t, 128> send;
	send.fill(sendValue);
	for (int n = 0; freeze.capturing() && n < 20; ++n) {
		freeze.capture(audio, send);
	}
	assert(freeze.state() == State::WRITING);
}
static void ready(ClipFreeze& freeze, InstrumentClip& clip) {
	assert(freeze.arm(clip) == Error::NONE);
	boundary(freeze);
	std::array<StereoSample, 4> audio{{{1024, -2048}, {4096, 8192}, {-8192, 2048}, {0, 0}}};
	std::array<int32_t, 4> reverb{{256, -512, 1024, 0}};
	freeze.capture(audio, reverb);
	boundary(freeze);
	captureTail(freeze);
	boundary(freeze); // SD writing can take arbitrarily long; never switch early.
	assert(freeze.state() == State::WRITING);
	finishWrites();
	boundary(freeze);
	assert(freeze.state() == State::READY);
}
static void cleanup() {
	assert(voices == 0);
	for (auto& r : recorders) {
		assert(r->status == RecorderStatus::ABORTED && !r->pointerHeldElsewhere);
		assert(r->storage.reasons == 0);
	}
	recorders.clear();
	playbackHandler = {};
	AudioEngine::renderInStereo = true;
	failRead = failStart = false;
	failAllocationAt = -1;
}
int main() {
	{
		FreezeTransitionPlan plan;
		std::array<StereoSample, 128> frames;
		frames.fill({4096, 4096});
		plan.reset(44100 * 2);
		assert(!plan.ready());
		for (int n = 0; n < 44100 * 2; n += 128)
			plan.capture({frames.data(), size_t(std::min(128, 44100 * 2 - n))});
		plan.finish(44100 * 2);
		float low[4] = {2.f * 4096 * 4096, 0, 0, 0};
		float high[4] = {0, 0, 0, 2.f * 4096 * 4096};
		assert(plan.ready() && plan.nextWindow(22050, low[0], low) == 0);
		assert(plan.nextWindow(22050, low[0], high) == -1);
		assert(plan.nextWindow(22050, 0, low) == -1);
		assert(plan.nextWindow(0, low[0], low) >= 11025);
		assert(plan.nextWindow(44100 * 2 - 100, low[0], low) == -1);
		// Look ahead to a later compatible passage without changing playback phase.
		plan.reset(44100 * 2);
		for (int second = 0; second < 2; ++second) {
			frames.fill(second ? StereoSample{8192, 8192} : StereoSample{4096, 4096});
			for (int n = 0; n < 44100; n += 128)
				plan.capture({frames.data(), size_t(std::min(128, 44100 - n))});
		}
		plan.finish(44100 * 2);
		int ahead = plan.nextWindow(22050, low[0] * 4, low);
		// The abrupt level step itself is not spectrally stable; plan just after it.
		assert(ahead >= 22050 && ahead <= 26460);
		plan.reset();
		assert(!plan.ready());
		// If a slowed capture outgrows the fixed profile, use the baseline verifier.
		for (int n = 0; n <= FreezeTransitionPlan::capacity * FreezeTransitionPlan::quantum; n += 128)
			plan.capture(frames);
		plan.finish(1);
		assert(!plan.ready());
		plan.reset(44100 * 2);
		frames.fill({4096, -4096});
		for (int n = 0; n < 44100 * 2; n += 128)
			plan.capture({frames.data(), size_t(std::min(128, 44100 * 2 - n))}, false);
		plan.finish(44100 * 2);
		assert(plan.ready() && plan.nextWindow(22050, low[0], low) == -1);
	}
	InstrumentClip clip;
	{
		ClipFreeze freeze;
		assert(!freeze.enabled());
		ready(freeze, clip);
		boundary(freeze);
		assert(freeze.playing());
		std::array<StereoSample, 4> out{};
		std::array<int32_t, 4> send{};
		SampleRecorder track;
		assert(freeze.render(out, send, &track));
		assert(out[0].l == 1024 && out[0].r == -2048 && out[1].r == 8192);
		assert(send[0] == 256 && send[1] == -512);
		assert(track.storage.data[0].l == 4096); // Same x4 track-recording gain as Sound::render.
		freeze.pause();
		assert(freeze.state() == State::READY && voices == 0);
		assert(freeze.start(clip.loopLength / 2, repeatCount));
		out = {};
		send = {};
		assert(freeze.render(out, send));
		assert(out[0].l == -8192 && send[0] == 1024);
		freeze.clear();
		assert(!freeze.enabled());
	}
	cleanup();
	{
		ClipFreeze freeze;
		assert(freeze.arm(clip) == Error::NONE);
		boundary(freeze);
		AudioEngine::audioSampleTimer += 32; // Same tick, but a later audio callback.
		freeze.loop(repeatCount); // Duplicate events at the same audio boundary cannot finish an empty capture.
		assert(freeze.capturing());
		freeze.pause();
		assert(!freeze.enabled());
	}
	cleanup();
	{
		ClipFreeze freeze;
		ready(freeze, clip);
		assert(freeze.validFor(clip));
		AudioEngine::renderInStereo = false;
		assert(!freeze.validFor(clip));
		AudioEngine::renderInStereo = true;
		++clip.loopLength;
		assert(!freeze.validFor(clip));
		--clip.loopLength;
		++playbackHandler.tick;
		assert(freeze.validFor(clip));
		--playbackHandler.tick;
		playbackHandler.external = true;
		assert(freeze.validFor(clip));
		playbackHandler.external = false;
		clip.recording = true;
		assert(!freeze.validFor(clip));
		clip.recording = false;
	}
	cleanup();
	{
		ClipFreeze freeze;
		ready(freeze, clip);
		boundary(freeze);
		failRead = true;
		std::array<StereoSample, 4> out{{{123, 456}}};
		std::array<int32_t, 4> send{{789}};
		assert(!freeze.render(out, send));
		assert(out[0].l == 123 && out[0].r == 456 && send[0] == 789);
		assert(freeze.state() == State::FAILED);
	}
	cleanup();
	{
		ClipFreeze freeze;
		ready(freeze, clip);
		failStart = true;
		boundary(freeze);
		assert(freeze.state() == State::FAILED);
	}
	cleanup();
	{
		ClipFreeze freeze;
		assert(freeze.arm(clip) == Error::NONE);
		recorders[1]->hadCardError = true;
		boundary(freeze);
		assert(freeze.state() == State::FAILED);
	}
	cleanup();
	{
		ClipFreeze freeze;
		failAllocationAt = 1;
		assert(freeze.arm(clip) == Error::INSUFFICIENT_RAM);
		assert(freeze.state() == State::FAILED);
	}
	cleanup();
	{
		FreezeCycle cycle(192);
		assert(cycle.include(192, 2) && cycle.ticks() == 384);
		assert(cycle.include(192, 3) && cycle.ticks() == 1152);
		assert(cycle.include(144, 1) && cycle.ticks() == 1152);
		assert(!cycle.include(191, 8)); // Do not silently shorten a long polyrhythm.
		FreezeCycle huge(INT32_MAX);
		assert(!huge.include(INT32_MAX - 1, 8));
	}
	{
		ClipFreeze freeze;
		clip.freezePeriod = clip.loopLength * 2;
		assert(freeze.arm(clip) == Error::NONE);
		repeatCount = 2;
		boundary(freeze); // Capture begins on repeat 3, not phase zero of the song.
		std::array<StereoSample, 4> on{{{1024, 2048}, {4096, 8192}, {2048, 1024}, {8192, 4096}}};
		std::array<StereoSample, 4> off{};
		std::array<int32_t, 4> send{};
		freeze.capture(on, send);
		boundary(freeze); // Repeat 4 must still be captured.
		assert(freeze.capturing());
		freeze.capture(off, send);
		boundary(freeze); // Repeat 5 ends the complete two-loop pass.
		captureTail(freeze);
		assert(recorders[0]->storage.lengthInSamples == 8 + 2 + SSI_TX_BUFFER_NUM_SAMPLES);
		finishWrites();
		boundary(freeze); // Prepare on repeat 6.
		boundary(freeze); // Switch on repeat 7, same phase as capture start.
		assert(freeze.playing());
		std::array<StereoSample, 4> out{};
		assert(freeze.render(out, send) && out[0].l == 1024);
		boundary(freeze); // Repeat 8: do NOT rewind the file and repeat the audible iteration.
		out = {};
		assert(freeze.render(out, send) && out[0].l == 0 && out[3].r == 0);
		boundary(freeze); // Repeat 9: rewind only at the complete-cycle boundary.
		out = {};
		assert(freeze.render(out, send) && out[0].l == 1024);
		freeze.pause();
		assert(freeze.start(0, 0)); // Restarting the song must map to repeat 4's silent phase.
		out = {};
		assert(freeze.render(out, send) && out[0].l == 0);
		freeze.pause();
		assert(freeze.start(clip.loopLength / 2, 1));
		out = {};
		assert(freeze.render(out, send) && out[0].l == 2048);
	}
	cleanup();
	{
		clip.freezePeriod = 0;
		AudioEngine::renderInStereo = false;
		ClipFreeze freeze;
		ready(freeze, clip);
		boundary(freeze);
		std::array<StereoSample, 4> out{};
		std::array<int32_t, 4> send{};
		assert(freeze.render(out, send));
		assert(out[0].l == -512 && out[0].r == -512);
		assert(out[1].l == 6144 && out[1].r == 6144);
		assert(out[2].l == -3072 && out[2].r == -3072);
	}
	cleanup();
	{
		FreezeCycle even(192);
		assert(even.includeSyncedLfo(1, 0) && even.ticks() == 768);
		assert(even.include(192, 3) && even.ticks() == 2304);
		FreezeCycle triplet(192);
		assert(triplet.includeSyncedLfo(1, 10) && triplet.ticks() == 1536);
		FreezeCycle dotted(192);
		assert(dotted.includeSyncedLfo(1, 19) && dotted.ticks() == 1152);
		FreezeCycle fractional(192);
		assert(fractional.includeSyncedLfo(9, 19) && fractional.ticks() == 576);
		assert(!fractional.includeSyncedLfo(0, 0));
		assert(!fractional.includeSyncedLfo(10, 0));
	}
	{
		clip.continuousPlayback = true;
		ClipFreeze freeze;
		ready(freeze, clip);
		boundary(freeze);
		assert(freeze.playing());
		freeze.pause();
		assert(!freeze.enabled());
		assert(!freeze.start(0, 0));
	}
	cleanup();
	{
		// Audio clips arm the common controller directly, without an InstrumentClip.
		Clip audioClip;
		ClipFreeze freeze;
		assert(freeze.arm(audioClip, audioClip.loopLength) == Error::NONE);
		boundary(freeze);
		std::array<StereoSample, 4> audio{{{1024, 512}, {0, 0}, {-4096, 2048}, {0, 0}}};
		std::array<int32_t, 4> send{{256, 0, -512, 0}};
		freeze.capture(audio, send);
		boundary(freeze);
		captureTail(freeze);
		finishWrites();
		boundary(freeze);
		boundary(freeze);
		assert(freeze.playing() && freeze.validFor(audioClip));
		freeze.pause();
		assert(freeze.start(audioClip.loopLength / 2, repeatCount));
		std::array<StereoSample, 4> out{};
		std::array<int32_t, 4> reverb{};
		assert(freeze.render(out, reverb));
		assert(out[0].l == -4096 && out[0].r == 2048 && reverb[0] == -512);
		audioClip.recording = true;
		assert(!freeze.validFor(audioClip));
	}
	cleanup();
	{
		FreezeCycle snapshot(192);
		assert(snapshot.include(192, 6) && snapshot.extendForSnapshot());
		assert(snapshot.ticks() == 192 * 12); // Do not shorten a six-loop note pattern.
		FreezeCycle bounded(192);
		assert(bounded.include(192, 63) && bounded.extendForSnapshot());
		assert(bounded.ticks() == 192 * 63);
		FreezeCycle fileBound(192);
		assert(fileBound.include(192, 6) && fileBound.extendForSnapshot(192 * 9));
		assert(fileBound.ticks() == 192 * 6);
		assert(!fileBound.extendForSnapshot(192 * 5));
		FreezeCycle invalid(0);
		assert(!invalid.extendForSnapshot());
	}
	{
		Clip audioClip;
		ClipFreeze freeze;
		assert(freeze.arm(audioClip, audioClip.loopLength, false, true) == Error::NONE);
		int32_t captureRepeat = repeatCount + 1;
		boundary(freeze);
		std::array<StereoSample, 4> audio{{{1024, -1024}, {2048, -2048}, {512, -512}, {0, 0}}};
		std::array<int32_t, 4> send{{256, 512, 128, 0}};
		for (int i = 0; i < 8; ++i) {
			assert(freeze.capturing());
			freeze.capture(audio, send);
			boundary(freeze);
		}
		captureTail(freeze, {8192, -4096}, 1024);
		finishWrites();
		boundary(freeze);
		assert(freeze.start(0, captureRepeat));
		std::array<StereoSample, 32> first{};
		std::array<int32_t, 32> firstSend{};
		assert(freeze.render(first, firstSend));
		assert(first[0].l == 1024 && first[31].l == 0);
		assert(freeze.start(0, captureRepeat + 8));
		std::array<StereoSample, 128> joined{};
		std::array<int32_t, 128> joinedSend{};
		assert(freeze.render(joined, joinedSend));
		assert(joined[0].l == 8192 && joined[0].r == -4096 && joinedSend[0] == 1024);
		assert(joined[4].l >= 4608 && joined[4].l < 8192);
		assert(joined[4].r <= -2560 && joinedSend[4] >= 640);
		assert(joined[8].l == 1024 && joined[8].r == -1024 && joinedSend[8] == 256);
		assert(voices == 2); // Outgoing voices are released as soon as the overlap finishes.
		freeze.pause();
		assert(freeze.start(0, captureRepeat));
		first = {};
		firstSend = {};
		assert(freeze.render(first, firstSend) && first[0].l == 1024);
		freeze.pause();
		// Restart at a clip boundary that is not the original capture phase.
		playbackHandler.lastSwungTickActioned += 96;
		freeze.loop(captureRepeat + 1);
		assert(freeze.playing());
	}
	cleanup();
	{
		Clip tone;
		playbackHandler.tick = (uint64_t{44100} << 32) / tone.loopLength;
		ClipFreeze freeze;
		assert(freeze.arm(tone, tone.loopLength) == Error::NONE);
		int32_t startRepeat = repeatCount + 1;
		boundary(freeze);
		int frame = 0;
		auto feedTone = [&](int count) {
			std::array<StereoSample, 128> audio{};
			std::array<int32_t, 128> send{};
			for (int i = 0; i < count; ++i, ++frame) {
				int32_t sample = std::sin(frame * 6.283185307179586 * 261.625565 / 44100) * (1 << 26);
				audio[i] = {sample, sample};
				send[i] = sample / 4;
			}
			freeze.capture({audio.data(), size_t(count)}, {send.data(), size_t(count)});
		};
		while (frame < 44100) {
			feedTone(std::min(128, 44100 - frame));
		}
		boundary(freeze);
		while (freeze.capturing()) {
			feedTone(128);
		}
		assert(recorders[0]->numSamplesCaptured == 44100 + 2048 + 128);
		finishWrites();
		boundary(freeze);
		assert(freeze.start(0, startRepeat));
		for (int n = 0; n < 44100;) {
			std::array<StereoSample, 128> out{};
			std::array<int32_t, 128> send{};
			int count = std::min(128, 44100 - n);
			assert(freeze.render({out.data(), size_t(count)}, {send.data(), size_t(count)}));
			n += count;
		}
		assert(freeze.start(0, startRepeat + 1));
		double power = 0;
		int window = 0;
		for (int n = 0; n < 2048; n += 128) {
			std::array<StereoSample, 128> out{};
			std::array<int32_t, 128> send{};
			assert(freeze.render(out, send));
			for (auto sample : out) {
				power += double(sample.l) * sample.l;
				if (++window == 441) {
					double relative = std::sqrt(power / window) / ((1 << 26) / std::sqrt(2.0));
					assert(relative > .85 && relative < 1.15);
					power = 0;
					window = 0;
				}
			}
		}
		assert(voices == 2);
		failStart = true; // Allocation/start failure during an overlap also frees both outgoing voices.
		assert(!freeze.start(0, startRepeat + 2));
		assert(freeze.state() == State::FAILED && voices == 0);
	}
	cleanup();
	// Snapshot entry starts the benchmark three-second fade without a search.
	for (bool alternateCache : {false, true}) {
		cleanup();
		InstrumentClip snapshot;
		snapshot.snapshot = true;
		playbackHandler.tick = (uint64_t{44100} << 32) / snapshot.loopLength;
		ClipFreeze freeze;
		assert(freeze.arm(snapshot) == Error::NONE);
		boundary(freeze);
		std::array<StereoSample, 128> native;
		std::array<int32_t, 128> send{};
		native.fill({4096, 4096});
		for (int loop = 0; loop < 8; ++loop) {
			for (int frame = 0; frame < 44100; frame += 128)
				freeze.capture({native.data(), size_t(std::min(128, 44100 - frame))},
				               {send.data(), size_t(std::min(128, 44100 - frame))});
			boundary(freeze);
		}
		captureTail(freeze, {4096, 4096});
		finishWrites();
		if (alternateCache) {
			// Entry timing must be independent of the cached waveform.
			auto& recorded = recorders.front()->storage.data;
			for (size_t i = 0; i < recorded.size(); ++i)
				recorded[i] = {(i & 1) ? 4096 : -4096, (i & 1) ? 4096 : -4096};
		}
		for (int loop = 0; !freeze.entering(); ++loop) {
			assert(loop < 10);
			boundary(freeze);
		}
		int blocks = 0;
		while (freeze.entering()) {
			if (blocks && blocks % 345 == 0)
				boundary(freeze);
			for (size_t i = 0; i < native.size(); ++i)
				native[i] = {(alternateCache || (i & 1)) ? 4096 : -4096, (alternateCache || (i & 1)) ? 4096 : -4096};
			freeze.mixHandoff(native, send);
			assert(++blocks < 1100);
		}
		assert(blocks * 128 >= 3 * 44100 && blocks * 128 < 3 * 44100 + 128);
		assert(freeze.playing());
	}
	cleanup();
	for (bool silentNative : {false, true}) {
		cleanup();
		InstrumentClip clip;
		playbackHandler.tick = (uint64_t{44100} << 32) / clip.loopLength;
		ClipFreeze freeze;
		assert(freeze.arm(clip) == Error::NONE);
		boundary(freeze);
		std::array<StereoSample, 128> native;
		std::array<int32_t, 128> send;
		native.fill({4096, 4096});
		send.fill(512);
		for (int i = 0; i < 44100; i += 128) {
			int count = std::min(128, 44100 - i);
			freeze.capture({native.data(), size_t(count)}, {send.data(), size_t(count)});
		}
		boundary(freeze);
		captureTail(freeze, {4096, 4096}, 512);
		finishWrites();
		boundary(freeze);
		boundary(freeze);
		assert(freeze.entering());
		int calls = 0;
		while (freeze.entering()) {
			native.fill({1024, 1024});
			send.fill(128);
			freeze.mixHandoff(native, send);
			if (!calls++)
				assert(native[0].l == 1024 && send[0] == 128);
			assert(calls < 30);
		}
		assert(native.back().l >= 4094 && freeze.playing());
		freeze.beginThaw();
		assert(!freeze.playing() && !freeze.enabled() && freeze.thawing());
		// Both silence and a different timbre keep hearing the cache during
		// the fixed rebuild. Neither may extend the deadline or skip the fade.
		int blocks = 0;
		while (freeze.thawing()) {
			if (blocks % 300 == 0)
				boundary(freeze);
			for (size_t i = 0; i < native.size(); ++i)
				native[i] = silentNative ? StereoSample{0, 0}
				                         : StereoSample{(i & 1) ? 4096 : -4096, (i & 1) ? 4096 : -4096};
			send.fill(0);
			freeze.mixHandoff(native, send);
			if ((blocks + 1) * 128 < 4 * 44100)
				assert(freeze.thawing() && native[0].l == 4096 && send[0] == 512);
			assert(++blocks < 2500);
		}
		assert(blocks * 128 >= 7 * 44100 - 128 && blocks * 128 < 7 * 44100 + 256);
		assert(freeze.state() == State::OFF && voices == 0);
	}
	cleanup();
	{
		ClipFreeze freeze;
		ready(freeze, clip);
		boundary(freeze);
		freeze.beginThaw();
		std::array<StereoSample, 4> native;
		std::array<int32_t, 4> send;
		native.fill({128, 256});
		send.fill(64);
		failRead = true;
		freeze.mixHandoff(native, send);
		assert(freeze.state() == State::FAILED && voices == 0);
		assert(native[0].l == 128 && native[0].r == 256 && send[0] == 64);
	}
	cleanup();
	{
		ClipFreeze freeze;
		ready(freeze, clip);
		boundary(freeze);
		freeze.beginThaw();
		freeze.pause();
		assert(freeze.state() == State::OFF && voices == 0 && !freeze.thawing());
	}
	cleanup();
	{
		ClipFreeze freeze;
		InstrumentClip twoLoops;
		twoLoops.freezePeriod = twoLoops.loopLength * 2;
		assert(freeze.arm(twoLoops) == Error::NONE);
		boundary(freeze);
		std::array<StereoSample, 4> capture{{{1024, -2048}, {4096, 8192}, {-8192, 2048}, {0, 0}}};
		std::array<int32_t, 4> captureSend{};
		freeze.capture(capture, captureSend);
		boundary(freeze);
		freeze.capture(capture, captureSend);
		boundary(freeze);
		captureTail(freeze);
		finishWrites();
		boundary(freeze);
		while (!freeze.playing() || repeatCount % 2 != 0)
			boundary(freeze);
		freeze.beginThaw(true);
		assert(freeze.playing() && freeze.thawing() && !freeze.enabled());
		std::array<StereoSample, 4> out{};
		std::array<int32_t, 4> send{};
		assert(freeze.render(out, send));
		assert(out[0].l != 0); // Waiting still streams the cache.
		freeze.cancelThaw();
		assert(freeze.playing() && freeze.enabled() && !freeze.thawing());
		freeze.beginThaw(true);
		boundary(freeze);
		assert(freeze.playing() && freeze.thawing()); // An intermediate wrap is too early.
		boundary(freeze);
		assert(!freeze.playing() && freeze.thawing()); // Sequencer releases the native restart.
		freeze.mixHandoff(out, send);
		freeze.cancelThaw();
		assert(freeze.playing() && freeze.enabled() && !freeze.thawing());
		freeze.beginThaw();
		freeze.pause();
		assert(freeze.state() == State::OFF && voices == 0);
	}
	cleanup();
	{
		InstrumentClip clip;
		ClipFreeze freeze;
		assert(freeze.arm(clip) == Error::NONE);
		boundary(freeze);
		std::array<StereoSample, 128> audio;
		std::array<int32_t, 128> send{};
		audio.fill({4096, 4096});
		for (int i = 0; i < 9600; i += 128)
			freeze.capture(audio, send);
		boundary(freeze);
		captureTail(freeze, {4096, 4096});
		finishWrites();
		boundary(freeze);
		boundary(freeze);
		auto renderAt = [&](uint32_t samplesPerTick, int32_t expected) {
			playbackHandler.tick = uint64_t{samplesPerTick} << 32;
			assert(freeze.validFor(clip));
			renderedStretch.clear();
			audio.fill({0, 0});
			assert(freeze.render(audio, send));
			assert(renderedStretch.size() == 2);
			assert(renderedStretch[0] == expected && renderedStretch[1] == expected);
		};
		renderAt(100, kMaxSampleValue);
		renderAt(50, kMaxSampleValue * 2); // Faster, same oscillator phase increment.
		renderAt(200, kMaxSampleValue / 2);
		playbackHandler.external = true;
		renderAt(100, kMaxSampleValue + 1);
		testSyncLag = 400; // Even unity speed must correct clock phase drift.
		renderAt(100, kMaxSampleValue + 1);
		testSyncLag = 0;
		renderedSyncAnchors.clear();
		int oldAnchor = playbackHandler.lastSwungTickActioned;
		boundary(freeze);
		assert(freeze.render(audio, send));
		assert(renderedSyncAnchors.size() == 2 && voices == 2); // Stretched streams persist across the wrap.
		assert(renderedSyncAnchors[0] == oldAnchor + 96 && renderedSyncAnchors[1] == oldAnchor + 96);
		exhaustedStretchHead = true;
		boundary(freeze); // An exhausted stretched head must restart, never remain silently frozen.
		exhaustedStretchHead = false;
		assert(freeze.render(audio, send) && freeze.playing() && voices == 2);
		freeze.pause();
		assert(freeze.start(0, repeatCount));
		renderAt(100, kMaxSampleValue); // Transport restart can return to the native-rate fast path.
		renderedSyncAnchors.clear();
		oldAnchor = playbackHandler.lastSwungTickActioned;
		boundary(freeze);
		assert(freeze.render(audio, send));
		assert(renderedSyncAnchors.size() == 4);
		assert(renderedSyncAnchors[0] == oldAnchor + 96 && renderedSyncAnchors[2] == oldAnchor);
		// A native-rate reader can be ahead of the clock by less than its drift
		// tolerance. Consume 256 frames of the tail before the next loop event.
		for (int n = 128; n < 9600 + 256; n += 128) {
			audio.fill({0, 0});
			assert(freeze.render(audio, send));
		}
		boundary(freeze);
		for (int n = 0; n < 2048; n += 128) {
			audio.fill({0, 0});
			assert(freeze.render(audio, send));
		}
		assert(freeze.playing() && voices == 2); // Shortened overlap did not exhaust the outgoing stream.
	}
	cleanup();
	for (bool complete : {false, true}) {
		ClipFreeze freeze;
		InstrumentClip clip;
		assert(freeze.arm(clip) == Error::NONE);
		boundary(freeze);
		std::array<StereoSample, 128> audio;
		std::array<int32_t, 128> send;
		audio.fill({16384, 8192});
		send.fill(2048);
		for (int i = 0; i < 400; ++i)
			freeze.capture(audio, send);
		boundary(freeze);
		captureTail(freeze, {16384, 8192}, 2048);
		finishWrites();
		boundary(freeze);
		boundary(freeze);
		while (freeze.entering()) {
			audio.fill({16384, 8192});
			send.fill(2048);
			freeze.mixHandoff(audio, send);
		}
		assert(freeze.beginReleaseCapture());
		for (int block = 0; block < 1200 && freeze.capturingRelease(); ++block) {
			int value = block < 300 ? 16384 * (300-block) / 300 : 0;
			audio.fill({value, value / 2});
			send.fill(value / 8);
			freeze.captureRelease(audio, send);
		}
		assert(!freeze.capturingRelease());
		if (complete)
			finishWrites();
		assert(freeze.start(clip.loopLength - 1, repeatCount)); // Stop near the recorded continuation end.
		freeze.pause(true);
		assert(freeze.releasing());
		int blocks = 0;
		SampleRecorder track;
		while (freeze.releasing()) {
			audio.fill({0, 0});
			send.fill(0);
			assert(freeze.render(audio, send, &track));
			assert(track.storage.data[blocks * 128].l == audio[0].l * 4);
			if (blocks == 100 && complete)
				assert(audio[0].l > 1000 && audio[0].r > 500 && send[0] > 100);
			assert(++blocks < 1300);
		}
		assert(complete ? blocks > 900 : blocks < 40);
		assert(freeze.state() == State::READY && voices == 0);
		assert(freeze.start(0, repeatCount));
		freeze.pause(true);
		assert(freeze.releasing());
		assert(freeze.start(0, repeatCount)); // Relaunch cancels the release without discarding the loop.
		assert(!freeze.releasing() && freeze.playing() && voices == 2);
		freeze.beginThaw(true);
		freeze.pause(true);
		assert(freeze.releasing());
		assert(!freeze.start(0, repeatCount)); // Unfreeze still wins if restarted during its stop tail.
		assert(freeze.state() == State::OFF && voices == 0);
		freeze.clear();
		cleanup();
	}
	for (int failure : {2, 3}) {
		InstrumentClip clip;
		ClipFreeze freeze;
		ready(freeze, clip);
		boundary(freeze);
		freeze.pause();
		assert(freeze.start(0, repeatCount));
		failAllocationAt = failure;
		assert(!freeze.beginReleaseCapture());
		assert(!freeze.capturingRelease() && freeze.playing() && voices == 2);
		freeze.clear();
		cleanup();
	}
	std::cout << "Clip freeze: 29 lifecycle, cycle-length and streaming scenarios passed\n";
}
