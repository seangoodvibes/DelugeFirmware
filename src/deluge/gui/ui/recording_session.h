#pragma once

#include "gui/ui/ui_session.h"

namespace deluge::gui::ui_session {

// Identity check only: callers must also use a revision invalidated on target changes.
struct RecordingTarget {
	const void* song = nullptr;
	const void* sound = nullptr;
	const void* source = nullptr;
	const void* range = nullptr;
	uint64_t revision = 0;
	bool operator==(const RecordingTarget&) const = default;
};

// Reserve the one shared recorder before allocating it: allocation can yield,
// and another panel must not start a second recording during that interval.
class RecordingSession {
public:
	bool acquire() {
		if (active_) {
			return false;
		}
		owner_ = current();
		active_ = true;
		return true;
	}
	void release() { active_ = false; }
	Id owner() const { return owner_; }
	bool active() const { return active_; }

private:
	Id owner_ = Id::Local;
	bool active_ = false;
};

} // namespace deluge::gui::ui_session
