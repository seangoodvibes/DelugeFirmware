/*
 * Copyright (c) 2024 Sean Ditny
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "automation.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"

namespace deluge::gui::menu_item {

MenuItem* Automation::selectButtonPress() {
	// If shift held down, delete automation
	if (Buttons::isShiftButtonPressed()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStack = getModelStackWithParam(modelStackMemory);
		if (modelStack && modelStack->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);

			modelStack->autoParam->deleteAutomation(action, modelStack);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			// if automation view is open in background and automation is deleted
			// then refresh automation view UI
			if (getRootUI() == &automation_view_for_session()) {
				uiNeedsRendering(&automation_view_for_session());
			}
		}

		return NO_NAVIGATION;
	}
	return nullptr; // Navigate back
}

ActionResult Automation::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	bool clipMinder = rootUIIsClipMinderScreen();
	bool in_arranger_view =
	    !clipMinder && currentSong && (currentSong->last_clip_instance_entered_start_pos_for_session() != -1);
	RootUI* rootUI = getRootUI();

	// Clip or Song button
	// Used to enter automation view from sound editor
	if (clipMinder || in_arranger_view) {
		if (b == CLIP_VIEW) {
			if (on) {
				// if we're not in automation view yet
				// save current UI so you can switch back to it once we exit out of current menu
				// flag automation view as onMenuView so we know that we're dealing with the background
				// automation view used exclusively with the menu
				if (rootUI != &automation_view_for_session()) {
					if (!select_automation_view_parameter(clipMinder)) {
						return ActionResult::DEALT_WITH;
					}
					automation_view_for_session().onMenuView = true;
					automation_view_for_session().previousUI = rootUI;
					swapOutRootUILowLevel(&automation_view_for_session());
					automation_view_for_session().initializeView();
					automation_view_for_session().openedInBackground();
				}
				// if we're in automation view and it's the menu view
				// swap out background UI from automation view to the previous UI
				else if (automation_view_for_session().onMenuView) {
					if (!restore_previous_view()) {
						return ActionResult::DEALT_WITH;
					}
				}
				view_for_session().setModLedStates();
				PadLEDs::reassessGreyout();
			}
			return ActionResult::DEALT_WITH;
		}
		// Select encoder button, used to change current parameter selection in automation view
		// Back button, used to back out of current automatable parameter menu
		else if (b == SELECT_ENC || b == BACK) {
			if (on) {
				if (rootUI == &automation_view_for_session()) {
					// if we got here, and we're in the automation menu view
					// then we want to reset the background root UI to the previous UI
					// because you just entered a new menu or backed out of the current param menu
					if (automation_view_for_session().onMenuView) {
						if (!restore_previous_view()) {
							return ActionResult::DEALT_WITH;
						}
					}
					// if you are already in automation view and entered an automatable parameter menu
					else {
						if (!select_automation_view_parameter(clipMinder)) {
							return ActionResult::DEALT_WITH;
						}
						uiNeedsRendering(rootUI);
					}
					view_for_session().setModLedStates();
					PadLEDs::reassessGreyout();
				}
			}
			return ActionResult::DEALT_WITH;
		}
		else if (b == X_ENC) {
			// Horizontal encoder button to zoom in/out of underlying automation view
			if (rootUI == &automation_view_for_session()) {
				automation_view_for_session().buttonAction(b, on, inCardRoutine);
				return ActionResult::DEALT_WITH;
			}
		}
	}
	return ActionResult::NOT_DEALT_WITH;
}

bool Automation::restore_previous_view() {
	auto& automation_view = automation_view_for_session();
	auto* previous_view = automation_view.previousUI;
	if (!previous_view || previous_view == &automation_view) {
		return false;
	}
	automation_view.onMenuView = false;
	automation_view.previousUI = nullptr;
	automation_view.resetInterpolationShortcutBlinking();
	automation_view.resetPadSelectionShortcutBlinking();
	swapOutRootUILowLevel(previous_view);
	uiNeedsRendering(previous_view);
	view_for_session().setKnobIndicatorLevels();
	return true;
}

bool Automation::select_automation_view_parameter(bool clipMinder) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStackWithParam(modelStackMemory);
	Clip* clip = getCurrentClip();
	if (!modelStack || !modelStack->autoParam || !modelStack->paramCollection
	    || (clipMinder && (!clip || !clip->output)) || (!clipMinder && !currentSong)) {
		return false;
	}
	automation_view_for_session().onArrangerView = !clipMinder;
	int32_t knobPos = automation_view_for_session().getAutomationParameterKnobPos(modelStack, view_for_session().modPos)
	                  + kKnobPosOffset;
	automation_view_for_session().setAutomationKnobIndicatorLevels(modelStack, knobPos, knobPos);

	int32_t p = modelStack->paramId;
	modulation::params::Kind kind = modelStack->paramCollection->getParamKind();

	if (clipMinder) {
		clip->last_selected_param_id_for_session() = p;
		clip->last_selected_param_kind_for_session() = kind;
		clip->last_selected_output_type_for_session() = clip->output->type;
		clip->last_selected_patch_source_for_session() = getPatchSource();
		clip->last_selected_param_shortcut_x_for_session() = kNoSelection;
		clip->last_selected_param_shortcut_y_for_session() = kNoSelection;
		clip->last_selected_param_array_position_for_session() = 0;
	}
	else {
		currentSong->last_selected_param_id_for_session() = p;
		currentSong->last_selected_param_kind_for_session() = kind;
		currentSong->last_selected_param_shortcut_x_for_session() = kNoSelection;
		currentSong->last_selected_param_shortcut_y_for_session() = kNoSelection;
		currentSong->last_selected_param_array_position_for_session() = 0;
	}
	// not blinking any shortcuts for patch cables
	// no scroll selection for patch cables
	if (kind != deluge::modulation::params::Kind::PATCH_CABLE) {
		automation_view_for_session().getLastSelectedParamShortcut(clip);
		automation_view_for_session().getLastSelectedParamArrayPosition(clip);
	}

	automation_view_for_session().automationParamType = AutomationParamType::PER_SOUND;
	return true;
}

} // namespace deluge::gui::menu_item
