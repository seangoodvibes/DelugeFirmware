#pragma once
#include <cstdint>

namespace deluge::gui::ui_session {

// Coalesce notifications until the owning panel reaches a safe render pass.
// Consume before invoking UI code so a nested edit remains pending.
class SharedModelRefresh {
public:
	void request() { requested_ = true; }
	bool consume(uint64_t model_revision) {
		if (!requested_ && seen_revision_ == model_revision)
			return false;
		requested_ = false;
		seen_revision_ = model_revision;
		return true;
	}

private:
	bool requested_ = false;
	uint64_t seen_revision_ = 0;
};

} // namespace deluge::gui::ui_session
