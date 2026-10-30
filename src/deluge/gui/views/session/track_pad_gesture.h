#pragma once

#include <cstdint>

// A tap commits on release. Editing cancels it; Shift is captured at press time so releasing
// Shift before the pad cannot accidentally mute or audition a locked track.
class TrackPadGesture {
public:
	void press(uint32_t now, bool silent) {
		started = now;
		silentPress = silent;
		used = false;
	}
	void markUsed() { used = true; }
	bool shouldAudition(uint32_t now, uint32_t holdTime) const { return !silentPress && now - started >= holdTime; }
	bool shouldMute(uint32_t now, uint32_t holdTime) const { return !silentPress && !used && now - started < holdTime; }

private:
	uint32_t started = 0;
	bool silentPress = false;
	bool used = false;
};
