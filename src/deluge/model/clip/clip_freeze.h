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
	void pause(bool release = false);
	bool beginReleaseCapture();
	void captureRelease(std::span<StereoSample> dry, std::span<int32_t> reverb);
	bool capturingRelease() const { return capturingRelease_; }
	bool releasing() const { return releasing_; }
	bool start(int32_t pos, int32_t repeatCount);
	void loop(int32_t repeatCount);
	bool validFor(Clip& clip) const;
	void capture(std::span<StereoSample> dry, std::span<int32_t> reverb);
	bool render(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder = nullptr);
	void beginThaw(bool alignToPattern = false);
	void cancelThaw();
	void mixHandoff(std::span<StereoSample> dry, std::span<int32_t> reverb);
	bool thawing() const { return thawing_; }
	bool entering() const { return state_ == State::PLAYING && !thawing_ && entering_; }
	bool playing() const { return state_ == State::PLAYING && (!thawing_ || waitingForNativeStart_); }
	bool capturing() const { return state_ == State::CAPTURING; }
	bool enabled() const { return state_ != State::OFF && state_ != State::FAILED && !thawing_; }
	State state() const { return state_; }

private:
	struct Fade {
		int32_t length = 0, remaining = 0;
		float incomingPower = 0, outgoingPower = 0, crossPower = 0;
		void reset(int32_t samples) { length = remaining = samples; }
		void apply(std::span<StereoSample> incoming, std::span<int32_t> send, std::span<const StereoSample> outgoing,
		           std::span<const int32_t> outgoingSend);
	};
	struct Stream {
		SampleRecorder* recorder = nullptr;
		SampleHolder holder;
		SamplePlaybackGuide guide, outgoingGuide;
		VoiceSample* voice = nullptr;
		VoiceSample* outgoing = nullptr;
		int32_t remainingSamples();
		void stopOutgoing();
		void stop();
		void clear();
		void prepare(int32_t length, int64_t loopSamples);
		bool start(int32_t pos, int32_t length, int64_t loopSamples, bool overlap);
		bool render(int32_t* buffer, int32_t count, int32_t channels, int32_t stretch, bool syncedStretch,
		            bool tail = false);
	};
	Stream dry_, reverb_, releaseDry_, releaseReverb_;
	bool capturingRelease_ = false, releasing_ = false, clearAfterRelease_ = false;
	int32_t releaseSamples_ = 0, releaseQuietSamples_ = 0, releasePosition_ = 0;
	float releaseHeadPower_ = 0, playbackPower_ = 0, releaseGain_ = 1;
	Fade releaseFade_;
	bool renderLoop(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder);
	bool renderRelease(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder);
	void finishRelease();
	State state_ = State::OFF;
	Output* output_ = nullptr;
	int32_t length_ = 0;
	int32_t period_ = 0;
	int32_t captureStartRepeat_ = 0;
	int32_t loopsRemaining_ = 0;
	int32_t phase(int32_t pos, int32_t repeatCount) const;
	bool continuousPlayback_ = false;
	bool snapshot_ = false;
	bool syncedStretch_ = false;
	int64_t loopSamples_ = 0;
	int32_t crossfadeLength_ = 0;
	int32_t tailRemaining_ = -1;
	Fade loopFade_, handoffFade_;
	bool thawing_ = false, entering_ = false;
	void resetHandoff();
	bool waitingForNativeStart_ = false;
	int64_t nativeWaitSamples_ = 0;
	int32_t warmupSamples_ = 0;
	bool stereoRendering_ = false;
	uint64_t tickTime_ = 0;
	int64_t lastBoundary_ = 0;
	bool haveBoundary_ = false;
	bool resumePending_ = false;
	void fail();
	bool healthy() const;
	int32_t stretchRatio() const;
};
