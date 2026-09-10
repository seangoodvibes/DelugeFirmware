#pragma once

#include "gui/ui/ui_session.h"

// Skip peer entries, but never skip a newer action belonging to this panel.
template <typename ActionType>
ActionType* newest_action_for_panel(ActionType* head, deluge::gui::ui_session::Id owner) {
	for (auto* action = head; action; action = action->nextAction) {
		if (action->navigation_owner == owner) {
			return action;
		}
	}
	return nullptr;
}
