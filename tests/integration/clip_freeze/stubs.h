// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Hardware boundary doubles for exercising the production ClipFreeze implementation on the host.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
constexpr int SSI_TX_BUFFER_NUM_SAMPLES = 128;
constexpr int32_t kMaxSampleValue = 1 << 24;
enum class Error { NONE, INSUFFICIENT_RAM, FILE_TOO_BIG, UNSPECIFIED };
enum class SequenceDirection { FORWARD, REVERSE };
enum class RecordingMode { OFF, ARRANGEMENT };
enum class AudioRecordingFolder { CLIPS };
enum class AudioInputChannel { OFFLINE_OUTPUT };
enum class LoopType { LOW_LEVEL, NONE, TIMESTRETCHER_LEVEL_IF_ACTIVE };
enum class InterpolationMode { LINEAR };
enum class VoicePriority { MEDIUM };
namespace ClusterPriority {
constexpr int forAudioClip(VoicePriority) {
	return 1;
}
} // namespace ClusterPriority
struct StereoSample {
	int32_t l, r;
	StereoSample& operator+=(StereoSample other) {
		l += other.l;
		r += other.r;
		return *this;
	}
};
struct Output {};
struct Clip {
	Output* output = nullptr;
	int32_t loopLength = 96;
	SequenceDirection sequenceDirectionMode = SequenceDirection::FORWARD;
	bool recording = false;
	bool getCurrentlyRecordingLinearly() { return recording; }
};
struct InstrumentClip : Clip {
	bool continuousPlayback = false;
	bool snapshot = false;
	int32_t freezePeriod = 0;
	Error getFreezeCycleLength(int32_t& length, bool& continuous, bool& approximate) {
		continuous = continuousPlayback;
		approximate = snapshot;
		length = freezePeriod ? freezePeriod : loopLength;
		return Error::NONE;
	}
};
struct PlaybackHandler {
	int64_t lastSwungTickActioned = 0;
	uint64_t tick = uint64_t{100} << 32;
	bool external = false;
	RecordingMode recording = RecordingMode::OFF;
	uint64_t getTimePerInternalTickBig() { return tick; }
	bool isExternalClockActive() { return external; }
	int64_t getCurrentInternalTickCount() { return lastSwungTickActioned; }
};
inline PlaybackHandler playbackHandler;
struct Sample {
	int64_t lengthInSamples = 0;
	int32_t audioDataStartPosBytes = 0;
	int32_t numChannels = 2;
	int32_t byteDepth = 3;
	int reasons = 0;
	std::vector<StereoSample> data;
};
struct SampleHolder {
	Sample* audioFile = nullptr;
	uint64_t startPos = 0, endPos = 0;
	void setAudioFile(Sample* s) {
		if (audioFile)
			--audioFile->reasons;
		audioFile = s;
		if (s)
			++s->reasons;
	}
	~SampleHolder() { setAudioFile(nullptr); }
};
struct VoiceSample;
inline int32_t testSyncLag = 0;
inline std::vector<int32_t> renderedStretch, renderedSyncAnchors;
struct SamplePlaybackGuide {
	SampleHolder* audioFileHolder = nullptr;
	uint32_t sequenceSyncLengthTicks = 0;
	int32_t sequenceSyncStartedAtTick = 0;
	uint32_t endPlaybackAtByte = 0;
	void setupPlaybackBounds(bool) {
		// Fixtures supply their captured frames directly rather than running a
		// real clock during capture. Establish the matching recording tempo.
		if (sequenceSyncLengthTicks)
			playbackHandler.tick = (audioFileHolder->endPos << 32) / sequenceSyncLengthTicks;
	}
	int32_t getNumSamplesLaggingBehindSync(VoiceSample*) { return testSyncLag; }
};
enum class RecorderStatus {
	CAPTURING_DATA,
	CAPTURING_DATA_WAITING_TO_STOP,
	FINISHED_CAPTURING_BUT_STILL_WRITING,
	COMPLETE,
	ABORTED
};
struct RecorderConfig {
	bool neverUseThreshold = false;
	bool forceStereoRendering = true;
};
struct SampleRecorder {
	Sample storage;
	Sample* sample = &storage;
	RecorderStatus status = RecorderStatus::CAPTURING_DATA;
	bool hadCardError = false, reachedMaxFileSize = false, pointerHeldElsewhere = true;
	bool allowFileAlterationAfter = true;
	uint32_t numSamplesCaptured = 0;
	int numSamplesToRunBeforeBeginningCapturing = 5, numSamplesExtraToCaptureAtEndSyncingWise = 5;
	void abort() {
		assert(storage.reasons == 0);
		status = RecorderStatus::ABORTED;
	}
	void endSyncedRecording(int) {
		assert(status == RecorderStatus::CAPTURING_DATA);
		assert(!storage.data.empty());
		storage.lengthInSamples = storage.data.size();
		status = RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING;
	}
	void feedAudio(std::span<StereoSample> input, bool gain = false, uint8_t amount = 5) {
		assert(status == RecorderStatus::CAPTURING_DATA);
		numSamplesCaptured += input.size();
		for (auto x : input) {
			storage.data.push_back({x.l * (gain ? (1 << amount) : 1), x.r * (gain ? (1 << amount) : 1)});
		}
	}
};
inline bool failRead = false, failStart = false, exhaustedStretchHead = false;
inline int voices = 0, failAllocationAt = -1;
inline std::vector<std::unique_ptr<SampleRecorder>> recorders;
namespace AudioEngine {
inline bool renderInStereo = true;
}
struct VoiceSample {
	size_t pos = 0;
	bool stretching = false;
	bool canContinueTimeStretching() const { return stretching && !exhaustedStretchHead; }
	int32_t getPlaySample(Sample*, SamplePlaybackGuide*) { return pos; }
	void beenUnassigned(bool) { stretching = false; }
	void noteOn(SamplePlaybackGuide*, uint32_t, int32_t) {}
	bool setupClusersForInitialPlay(SamplePlaybackGuide*, Sample* s, int32_t offset, bool, int32_t) {
		pos = offset / (s->numChannels * s->byteDepth);
		return !failStart;
	}
	bool render(SamplePlaybackGuide* guide, int32_t* out, int32_t count, Sample* s, int32_t channels, LoopType loopType,
	            int32_t phase, int32_t stretch, int32_t amplitude, int32_t, int32_t, InterpolationMode, int32_t) {
		assert(phase == kMaxSampleValue && stretch > 0);
		stretching = stretch != kMaxSampleValue;
		// The host double verifies routing, gain and lifecycle. Actual pitch/time
		// stretching is validated against the native DSP in delugemu.
		renderedStretch.push_back(stretch);
		renderedSyncAnchors.push_back(guide->sequenceSyncStartedAtTick);
		// Match the native VoiceSample Q32 gain contract, including its internal left shift.
		assert(amplitude > 0 && amplitude <= (INT32_MAX >> 3));
		if (loopType == LoopType::NONE && pos + count > s->data.size()) {
			return false;
		}
		int64_t gain = int64_t(amplitude) * 8;
		for (int i = 0; i < count; ++i) {
			auto x = s->data[pos++ % s->data.size()];
			if (channels == 2 && !AudioEngine::renderInStereo)
				x.l = (x.l >> 1) + (x.r >> 1);
			*out++ += (int64_t(x.l) * gain + (int64_t{1} << 31)) >> 32;
			if (channels == 2 && AudioEngine::renderInStereo)
				*out++ += (int64_t(x.r) * gain + (int64_t{1} << 31)) >> 32;
		}
		return !failRead; // Deliberately fail after partial rendering to test atomic mixing.
	}
};
namespace AudioEngine {
inline uint32_t audioSampleTimer = 0;
inline SampleRecorder* getNewRecorder(int channels, AudioRecordingFolder, AudioInputChannel, bool, bool, int,
                                      bool normalize, Output*, RecorderConfig config) {
	assert(!normalize && config.neverUseThreshold && !config.forceStereoRendering);
	if (int(recorders.size()) == failAllocationAt)
		return nullptr;
	recorders.push_back(std::make_unique<SampleRecorder>());
	recorders.back()->sample->numChannels = channels;
	return recorders.back().get();
}
inline VoiceSample* solicitVoiceSample() {
	++voices;
	return new VoiceSample;
}
inline void voiceSampleUnassigned(VoiceSample* v) {
	--voices;
	delete v;
}
} // namespace AudioEngine
