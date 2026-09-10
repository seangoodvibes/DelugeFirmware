#pragma once
#include "gui/ui/automation_selection_state.h"
#include "gui/ui/ui_session.h"

struct ClipViewState {
	AutomationSelectionState automation;
	bool affectEntire = true;
	bool wrapEditing = false;
	uint32_t wrapEditLevel = 0;
	int32_t yScroll = 0;
	bool onKeyboardScreen = false;
	bool onAutomationClipView = false;
};

// Shared row movement changes both view coordinates without changing their distance.
inline void shift_clip_views(deluge::gui::ui_session::State<ClipViewState>& panels, int32_t amount) {
	for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
		panels.for_owner(owner).yScroll += amount;
	}
}

// Replacing the shared output invalidates both panels' previous edit target mode.
inline void reset_clip_affect_entire(deluge::gui::ui_session::State<ClipViewState>& panels, bool affectEntire) {
	for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
		panels.for_owner(owner).affectEntire = affectEntire;
	}
}
