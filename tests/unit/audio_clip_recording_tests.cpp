#include "CppUTest/TestHarness.h"
#include "model/sample/audio_clip_recording.h"

#include <cstdint>
#include <limits>

namespace {
int32_t nativePlayback(int32_t pcm24) {
	// Steady-state AudioOutput envelope * 2^27, followed by VoiceSample's <<3 and rounded sample multiply.
	constexpr int64_t amplitude = (int64_t{1} << 29) - 8;
	return (static_cast<int64_t>(pcm24) * 256 * amplitude + (int64_t{1} << 31)) >> 32;
}
} // namespace

TEST_GROUP(AudioClipRecording){};

TEST(AudioClipRecording, Every24BitSampleSurvivesNeutralPlaybackAndRecording) {
	for (int32_t pcm = -8388608; pcm < 8388608; ++pcm) {
		int32_t recorded = audioClipRecordingSample(nativePlayback(pcm)) >> 8;
		if (recorded != pcm) {
			CHECK_EQUAL(pcm, recorded);
			return;
		}
	}
}

TEST(AudioClipRecording, OverloadSaturatesInsteadOfWrapping) {
	CHECK_EQUAL(INT32_MAX, audioClipRecordingSample(INT32_MAX));
	CHECK_EQUAL(INT32_MIN, audioClipRecordingSample(INT32_MIN));
	CHECK_EQUAL(8388607, audioClipRecordingSample(std::numeric_limits<int32_t>::max()) >> 8);
	CHECK_EQUAL(-8388608, audioClipRecordingSample(std::numeric_limits<int32_t>::min()) >> 8);
	CHECK_EQUAL(8388607, audioClipRecordingSample(1 << 28) >> 8);
	CHECK_EQUAL(-8388608, audioClipRecordingSample(-(1 << 28)) >> 8);
}

TEST(AudioClipRecording, RoundsToNearest24BitStepIncludingNegativeValues) {
	CHECK_EQUAL(0, audioClipRecordingSample(0) >> 8);
	CHECK_EQUAL(0, audioClipRecordingSample(15) >> 8);
	CHECK_EQUAL(1, audioClipRecordingSample(16) >> 8);
	CHECK_EQUAL(0, audioClipRecordingSample(-16) >> 8);
	CHECK_EQUAL(-1, audioClipRecordingSample(-17) >> 8);
}
