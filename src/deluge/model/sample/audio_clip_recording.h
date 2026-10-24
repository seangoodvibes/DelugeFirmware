#pragma once

#include "util/fixedpoint.h"

// AudioOutput's envelope/amplitude and VoiceSample's gain reduce native-rate sample playback to just under 1/8.
// Undo that scale when recording an internal signal for audio-clip playback. Round before the recorder discards
// the low eight bits for 24-bit PCM, so a neutral self-overdub does not lose a PCM step on every pass.
inline int32_t audioClipRecordingSample(int32_t value) {
	int32_t scaled = signed_saturate<29>(value) * 8;
	// Preserve the negative rail so the recorder's clipping detector sees negative overloads too.
	return scaled == INT32_MIN ? scaled : add_saturate(scaled, 128);
}
