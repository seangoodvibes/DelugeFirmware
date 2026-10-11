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
		++revision_;
		return true;
	}
	bool begin_finish() {
		if (!active_ || finishing_)
			return false;
		finishing_ = true;
		++revision_;
		return true;
	}
	void release() {
		if (active_)
			++revision_;
		active_ = false;
		finishing_ = false;
	}
	uint64_t revision() const { return revision_; }
	bool finishing() const { return finishing_; }
	Id owner() const { return owner_; }
	bool active() const { return active_; }

private:
	Id owner_ = Id::Local;
	bool active_ = false;
	bool finishing_ = false;
	uint64_t revision_ = 0;
};

} // namespace deluge::gui::ui_session
