#pragma once

#include "gui/ui/shared_model_refresh.h"
#include "gui/ui/ui_session.h"
#include <array>
#include <cstdint>

class UI;

namespace deluge::gui::ui_session {

// Structural edits wait until the panel can redraw a target-free overview.
class StructuralRefresh {
public:
	void request() {
		pending_ = true;
		++revision_;
	}
	uint64_t revision() const { return revision_; }
	bool consume(bool storage_busy, bool overview, bool idle) {
		if (!pending_ || storage_busy || !overview || !idle)
			return false;
		pending_ = false;
		return true;
	}

private:
	bool pending_ = false;
	uint64_t revision_ = 0;
};

struct Navigation {
	static constexpr size_t capacity = 16;
	std::array<UI*, capacity> hierarchy{};
	int32_t depth = 0;
	UI* last_before_nullifying = nullptr;
	uint32_t mode = 0;
	bool oled_dirty = false;
	uint32_t main_rows_dirty = 0;
	uint32_t side_rows_dirty = 0;
	bool rendering = false;
	SharedModelRefresh shared_model_refresh;
	StructuralRefresh structural_refresh;
};

inline State<Navigation> navigation;

inline void request_peer_structural_refresh() {
	const auto peer = current() == Id::Local ? Id::Remote : Id::Local;
	navigation.for_owner(peer).structural_refresh.request();
}

// Invalidate both pre-existing checkpoints and ones captured during a yielding edit.
class PeerStructuralChange {
public:
	explicit PeerStructuralChange(bool changed = true)
	    : refresh_(changed ? &navigation.for_owner(current() == Id::Local ? Id::Remote : Id::Local).structural_refresh
	                       : nullptr) {
		if (refresh_) {
			refresh_->request();
		}
	}
	~PeerStructuralChange() {
		if (refresh_) {
			refresh_->request();
		}
	}
	PeerStructuralChange(const PeerStructuralChange&) = delete;
	PeerStructuralChange& operator=(const PeerStructuralChange&) = delete;

private:
	StructuralRefresh* refresh_;
};

} // namespace deluge::gui::ui_session
