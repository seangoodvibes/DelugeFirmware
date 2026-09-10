#pragma once

#include "gui/ui/ui_session.h"

namespace deluge::gui::menu_item {

// Values here are presentation caches, not independent copies of the model.
// A peer commit invalidates its cache; the next read resolves the model using
// that panel's context (the same menu may target different clips).
template <typename T>
class SharedValueCache {
public:
	bool needs_reload(uint64_t revision = 0) const {
		const auto& state = states_.active();
		return state.stale || state.revision != revision;
	}
	void set(T value, uint64_t revision = 0) { states_.active() = {value, false, revision}; }
	template <typename Reload>
	T get(Reload&& reload, uint64_t revision = 0) {
		auto& state = states_.active();
		if (state.stale || state.revision != revision) {
			// readCurrentValue implementations can themselves consult getValue().
			state.stale = false;
			state.revision = revision;
			reload();
		}
		return state.value;
	}
	void committed() {
		const auto peer =
		    ui_session::current() == ui_session::Id::Local ? ui_session::Id::Remote : ui_session::Id::Local;
		states_.for_owner(peer).stale = true;
	}

private:
	struct CachedValue {
		T value{};
		bool stale = false;
		uint64_t revision = 0;
	};
	ui_session::State<CachedValue> states_;
};

} // namespace deluge::gui::menu_item
