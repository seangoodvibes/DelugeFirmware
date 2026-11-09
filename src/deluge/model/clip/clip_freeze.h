// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "model/sample/sample_holder.h"
#include "model/sample/sample_playback_guide.h"
#include <span>

class InstrumentClip;
class Clip;
class Output;
class SampleRecorder;
class VoiceSample;

// An ephemeral render cache, owned by a clip. No replacement clip or output is created.
// The dry output and the contribution to the song's shared reverb bus are cached independently.
class ClipFreeze {
public:
	enum class State { OFF, ARMED, CAPTURING, WRITING, READY, PLAYING, FAILED };
	ClipFreeze() = default;
	ClipFreeze(const ClipFreeze&) = delete;
	ClipFreeze& operator=(const ClipFreeze&) = delete;
	~ClipFreeze();
	Error arm(InstrumentClip& clip);
	Error arm(Clip& clip, int32_t period, bool continuousPlayback = false, bool snapshot = false);
	void clear();
	void pause();
	bool start(int32_t pos, int32_t repeatCount);
	void loop(int32_t repeatCount);
	bool validFor(Clip& clip) const;
	void capture(std::span<StereoSample> dry, std::span<int32_t> reverb);
	bool render(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder = nullptr);
	bool playing() const { return state_ == State::PLAYING; }
	bool capturing() const { return state_ == State::CAPTURING; }
	bool enabled() const { return state_ != State::OFF && state_ != State::FAILED; }
	State state() const { return state_; }

private:
	struct Stream {
		SampleRecorder* recorder = nullptr;
		SampleHolder holder;
		SamplePlaybackGuide guide;
		VoiceSample* voice = nullptr;
		VoiceSample* outgoing = nullptr;
		void stopOutgoing();
		void stop();
		void clear();
		void prepare();
		bool start(int32_t pos, int32_t length, int64_t loopSamples, bool overlap);
		bool render(int32_t* buffer, int32_t count, int32_t channels, bool tail = false);
	};
	Stream dry_, reverb_;
	State state_ = State::OFF;
	Output* output_ = nullptr;
	int32_t length_ = 0;
	int32_t period_ = 0;
	int32_t captureStartRepeat_ = 0;
	int32_t loopsRemaining_ = 0;
	int32_t phase(int32_t pos, int32_t repeatCount) const;
	bool continuousPlayback_ = false;
	bool snapshot_ = false;
	int64_t loopSamples_ = 0;
	int32_t crossfadeLength_ = 0;
	int32_t crossfadeRemaining_ = 0;
	int32_t tailRemaining_ = -1;
	float outgoingPower_ = 0, incomingPower_ = 0, crossPower_ = 0;
	bool stereoRendering_ = false;
	uint64_t tickTime_ = 0;
	int64_t lastBoundary_ = 0;
	bool haveBoundary_ = false;
	bool resumePending_ = false;
	void fail();
	bool healthy() const;
};
