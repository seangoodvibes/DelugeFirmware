#pragma once
#include "gui/ui/automation_selection_state.h"

#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <cstdint>

// View coordinates are panel-owned; note positions and lengths are shared.
struct SongNavigationState {
	SessionLayoutType sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	bool affectEntire = false;
	bool tripletsOn = false;
	uint32_t tripletsLevel = 0;
	AutomationSelectionState automation;
	int32_t songGridScrollX = 0;
	int32_t songGridScrollY = 0;
	// -1: session; 0: arranger; otherwise the entered instance's position.
	int32_t lastClipInstanceEnteredStartPos = -1;
	bool arrangerAutoScrollModeActive = false;
	int32_t songViewYScroll = 1 - kDisplayHeight;
	int32_t arrangementYScroll = -kDisplayHeight;
	uint32_t xZoom[2]{1, 1};
	int32_t xScroll[2]{};
	int32_t xScrollForReturnToSongView = 0;
	int32_t xZoomForReturnToSongView = 1;
};

class SongNavigation {
public:
	void initialize_layout(SessionLayoutType layout) {
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			panels_.for_owner(owner).sessionLayout = layout;
		}
	}
	SongNavigationState& active() { return panels_.active(); }
	const SongNavigationState& active() const { return panels_.active(); }
	void output_inserted_at_start() {
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			++panels_.for_owner(owner).arrangementYScroll;
		}
	}
	void output_removed(int32_t index, int32_t remaining_count) {
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			auto& scroll = panels_.for_owner(owner).arrangementYScroll;
			int32_t bottom = std::max(int32_t{0}, -scroll);
			int32_t top = std::min(int32_t{kDisplayHeight - 1}, -scroll + remaining_count);
			int32_t row = index - scroll;
			if (row - bottom < top - row)
				--scroll;
		}
	}
	// Each viewport balances the rows around the removed clip independently.
	// An explicit movement request belongs only to the panel performing the action.
	void session_clip_removed(int32_t index, int32_t remaining_count, bool force_active_panel) {
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			auto& scroll = panels_.for_owner(owner).songViewYScroll;
			int32_t bottom = std::max(int32_t{0}, -scroll);
			int32_t top = std::min(int32_t{kDisplayHeight - 1}, -scroll + remaining_count);
			int32_t row = index - scroll;
			if ((force_active_panel && owner == deluge::gui::ui_session::current()) || top - row > row - bottom) {
				--scroll;
			}
		}
	}
	void pending_overdub_inserted(int32_t index) {
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			auto& scroll = panels_.for_owner(owner).songViewYScroll;
			if (index != scroll)
				++scroll;
		}
	}
	// Keep the other panel's bottom visible clip anchored when indices shift.
	// The initiating panel already applies its operation-specific navigation.
	void peer_clip_inserted(int32_t index) {
		auto& scroll = peer().songViewYScroll;
		if (index <= scroll)
			++scroll;
	}
	void peer_clip_removed(int32_t index) {
		auto& scroll = peer().songViewYScroll;
		if (index < scroll)
			--scroll;
	}
	void peer_clips_swapped(int32_t first, int32_t second) {
		auto& scroll = peer().songViewYScroll;
		if (scroll == first)
			scroll = second;
		else if (scroll == second)
			scroll = first;
	}
	void initialize(uint32_t clip_zoom, uint32_t arranger_zoom) {
		for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
			auto& panel = panels_.for_owner(owner);
			panel = {};
			panel.xZoom[NAVIGATION_CLIP] = clip_zoom;
			panel.xZoom[NAVIGATION_ARRANGEMENT] = arranger_zoom;
			panel.xZoomForReturnToSongView = clip_zoom;
		}
	}

private:
	SongNavigationState& peer() {
		using namespace deluge::gui::ui_session;
		return panels_.for_owner(current() == Id::Local ? Id::Remote : Id::Local);
	}
	deluge::gui::ui_session::State<SongNavigationState> panels_;
};
