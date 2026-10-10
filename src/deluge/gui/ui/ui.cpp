/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "extern.h"
#include "gui/ui/graphics_routing.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "hid/mirror.h"
#include "modulation/automation/parameter_revision.h"
#include "util/misc.h"
#include <utility>

using deluge::hid::display::OLED;

namespace {
auto& navigation() {
	return deluge::gui::ui_session::navigation.active();
}
} // namespace

UI::UI() {
	oledShowsUIUnderneath = false;
}

void UI::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	view_for_session().modEncoderAction(whichModEncoder, offset);
}

void UI::modButtonAction(uint8_t whichButton, bool on) {
	view_for_session().modButtonAction(whichButton, on);
}

void UI::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	view_for_session().modEncoderButtonAction(whichModEncoder, on);
}

void UI::graphicsRoutine() {
	if (getRootUI() && canSeeViewUnderneath()) {
		getRootUI()->graphicsRoutine();
	}
}

void UI::displayOrLanguageChanged() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

void UI::close() {
	closeUI(this);
}

/**
 * @brief Get the greyout rows and columns for the current UI
 *
 * @return std::pair<uint32_t, uint32_t> a pair with [rows, columns]
 */
std::pair<uint32_t, uint32_t> getUIGreyoutColsAndRows() {
	uint32_t cols = 0;
	uint32_t rows = 0;
	for (int32_t u = navigation().depth - 1; u >= 0; u--) {
		bool useThis = navigation().hierarchy[u]->getGreyoutColsAndRows(&cols, &rows);
		if (useThis) {
			return std::make_pair(cols, rows);
		}
	}
	return std::make_pair(0, 0);
}

bool changeUIAtLevel(UI* newUI, int32_t level) {
	if (!newUI || navigation().depth <= 0 || navigation().depth > navigation().capacity || level < 0
	    || level >= navigation().depth)
		return false;
	for (int32_t index = 0; index < navigation().depth; ++index) {
		if (!navigation().hierarchy[index])
			return false;
	}
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	UI* oldUI = getCurrentUI();
	UI* oldRootUI = navigation().hierarchy[level];
	int32_t oldNumUIs = navigation().depth;
	navigation().hierarchy[level] = newUI;
	navigation().depth = level + 1;
	auto expected_hierarchy = navigation().hierarchy;
	auto expected_depth = navigation().depth;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && navigation().depth == expected_depth
		       && navigation().hierarchy == expected_hierarchy;
	};

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	PadLEDs::reassessGreyout();
	if (!context_matches())
		return false;
	bool success = newUI->opened();
	if (!context_matches())
		return false;

	if (!success) {
		navigation().depth = oldNumUIs;
		navigation().hierarchy[level] = oldRootUI;
		expected_depth = oldNumUIs;
		expected_hierarchy[level] = oldRootUI;
		PadLEDs::reassessGreyout();
		if (!context_matches())
			return false;
		oldUI->focusRegained();
		if (!context_matches())
			return false;
	}
	return success;
}

// Called when we navigate between "root" UIs, like sessionView, instrumentClipView, automationView,
// performanceView, etc.
void changeRootUI(UI* newUI) {
	if (!newUI || navigation().depth < 0 || navigation().depth > navigation().capacity)
		return;
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	auto expected_hierarchy = navigation().hierarchy;
	auto expected_depth = navigation().depth;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && navigation().depth == expected_depth
		       && navigation().hierarchy == expected_hierarchy;
	};
	newUI = newUI->getUI();
	if (!newUI || !context_matches())
		return;
	navigation().hierarchy[0] = newUI;
	navigation().depth = 1;
	expected_hierarchy = navigation().hierarchy;
	expected_depth = 1;

	if (currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) {
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	}
	PadLEDs::reassessGreyout();
	if (!context_matches())
		return;
	newUI->opened(); // Root UIs are expected to support opening without rejection.
	if (!context_matches())
		return;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

// Only called when setting up blank song, so don't worry about this
void setRootUILowLevel(UI* newUI) {
	if (!newUI || navigation().depth < 0 || navigation().depth > navigation().capacity)
		return;
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	const auto expected_hierarchy = navigation().hierarchy;
	const auto expected_depth = navigation().depth;
	newUI = newUI->getUI();
	if (!newUI || deluge::gui::ui_session::current() != source_owner || navigation().depth != expected_depth
	    || navigation().hierarchy != expected_hierarchy)
		return;
	navigation().hierarchy[0] = newUI;
	navigation().depth = 1;
	PadLEDs::reassessGreyout();
}

bool changeUISideways(UI* newUI) {
	if (!newUI || navigation().depth <= 0 || navigation().depth > navigation().capacity)
		return false;
	for (int32_t index = 0; index < navigation().depth; ++index) {
		if (!navigation().hierarchy[index])
			return false;
	}
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	auto expected_hierarchy = navigation().hierarchy;
	const auto expected_depth = navigation().depth;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && navigation().depth == expected_depth
		       && navigation().hierarchy == expected_hierarchy;
	};
	newUI = newUI->getUI();
	if (!newUI || !context_matches())
		return false;
	bool success = changeUIAtLevel(newUI, expected_depth - 1);
	if (success)
		expected_hierarchy[expected_depth - 1] = newUI;
	if (!context_matches())
		return false;
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	return success;
}

UI* getCurrentUI() {
	if (navigation().depth == 0) {
		return navigation().last_before_nullifying; // Very ugly work-around to stop everything breaking
	}
	return navigation().hierarchy[navigation().depth - 1];
}

// This will be NULL while waiting to swap songs, so you'd better check for this anytime you're gonna call a function on
// the result!
RootUI* getRootUI() {
	if (navigation().depth == 0) {
		return nullptr;
	}
	return (RootUI*)navigation().hierarchy[0];
}

bool currentUIIsClipMinderScreen() {
	UI* currentUI = getCurrentUI();
	return (currentUI != nullptr && (currentUI->toClipMinder() != nullptr));
}

bool rootUIIsClipMinderScreen() {
	UI* rootUI = getRootUI();
	return (rootUI != nullptr && (rootUI->toClipMinder() != nullptr));
}

void swapOutRootUILowLevel(UI* newUI) {
	newUI = newUI->getUI();
	navigation().hierarchy[0] = newUI;
}

UI* getUIUpOneLevel(int32_t numLevelsUp) {
	if (navigation().depth < (1 + numLevelsUp)) {
		return nullptr;
	}
	else {
		return navigation().hierarchy[navigation().depth - 1 - numLevelsUp];
	}
}

// If UI not found, chaos
void closeUI(UI* uiToClose) {
	if (!uiToClose || navigation().depth <= 1 || navigation().depth > navigation().capacity)
		return;
	int32_t target_level = -1;
	for (int32_t level = 0; level < navigation().depth; ++level) {
		if (!navigation().hierarchy[level])
			return;
		if (navigation().hierarchy[level] == uiToClose)
			target_level = level;
	}
	// A stale close request must not remove the root or index below the stack.
	if (target_level <= 0)
		return;

	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	const auto expected_hierarchy = navigation().hierarchy;
	auto expected_depth = navigation().depth;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && navigation().depth == expected_depth
		       && navigation().hierarchy == expected_hierarchy;
	};

	bool redrawMainPads = false;
	bool redrawSidebar = false;

	int32_t u;
	for (u = navigation().depth - 1; u >= 1; u--) {

		UI* thisUI = navigation().hierarchy[u];
		redrawMainPads |= thisUI->renderMainPads();
		if (!context_matches())
			return;
		redrawSidebar |= thisUI->renderSidebar();
		if (!context_matches())
			return;

		if (thisUI == uiToClose) {
			break;
		}
	}

	UI* newUI = navigation().hierarchy[u - 1];
	navigation().depth = u;
	expected_depth = u;

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	PadLEDs::reassessGreyout();
	if (!context_matches())
		return;
	newUI->focusRegained();
	if (!context_matches())
		return;
	if (display->haveOLED()) {
		renderUIsForOled();
		if (!context_matches())
			return;
	}

	bool redrawMainPadsOrig = redrawMainPads;
	bool redrawSidebarOrig = redrawSidebar;

	for (u = navigation().depth - 1; u >= 0; u--) {
		if (!redrawMainPads && !redrawSidebar) {
			break;
		}

		UI* thisUI = navigation().hierarchy[u];
		if (redrawMainPads) {
			redrawMainPads = !thisUI->renderMainPads(0xFFFFFFFF, PadLEDs::image_for_session(),
			                                         PadLEDs::occupancy_mask_for_session());
			if (!context_matches())
				return;
		}
		if (redrawSidebar) {
			redrawSidebar =
			    !thisUI->renderSidebar(0xFFFFFFFF, PadLEDs::image_for_session(), PadLEDs::occupancy_mask_for_session());
			if (!context_matches())
				return;
		}
	}

	if (redrawMainPadsOrig) {
		PadLEDs::sendOutMainPadColours();
		if (!context_matches())
			return;
	}
	if (redrawSidebarOrig) {
		PadLEDs::sendOutSidebarColours();
	}
}

bool openUI(UI* newUI) {
	if (!newUI || navigation().depth < 0 || navigation().depth >= navigation().capacity) {
		return false;
	}
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	auto expected_hierarchy = navigation().hierarchy;
	auto expected_depth = navigation().depth;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && navigation().depth == expected_depth
		       && navigation().hierarchy == expected_hierarchy;
	};
	newUI = newUI->getUI();
	if (!newUI || !context_matches())
		return false;
	UI* oldUI = getCurrentUI();
	navigation().hierarchy[navigation().depth] = newUI;
	navigation().depth++;
	expected_hierarchy = navigation().hierarchy;
	expected_depth = navigation().depth;

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	PadLEDs::reassessGreyout();
	if (!context_matches())
		return false;
	bool success = newUI->opened();
	// A callback may have replaced the stack or opened another UI. Preserve it.
	if (!context_matches())
		return false;

	if (!success) {
		navigation().depth--;
		expected_depth = navigation().depth;
		PadLEDs::reassessGreyout();
		if (!context_matches())
			return false;
		if (oldUI)
			oldUI->focusRegained();
		if (!context_matches())
			return false;
	}
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	return success;
}

bool isUIOpen(UI* ui) {
	for (int32_t u = 0; u < navigation().depth; u++) {
		if (navigation().hierarchy[u] == ui) {
			return true;
		}
	}
	return false;
}

void nullifyUIs() {
	navigation().last_before_nullifying = getCurrentUI();
	navigation().depth = 0;
	navigation().oled_dirty = false;
}

void renderUIsForOled() {
	navigation().oled_dirty = true;
}

void clearPendingUIRendering() {
	navigation().main_rows_dirty = navigation().side_rows_dirty = 0;
}

void renderingNeededRegardlessOfUI(uint32_t whichMainRows, uint32_t whichSideRows) {
	navigation().main_rows_dirty |= whichMainRows;
	navigation().side_rows_dirty |= whichSideRows;
}

void uiNeedsRendering(UI* ui, uint32_t whichMainRows, uint32_t whichSideRows) {
	if (!ui || navigation().depth <= 0 || navigation().depth > navigation().capacity)
		return;
	for (int32_t level = 0; level < navigation().depth; ++level) {
		if (!navigation().hierarchy[level])
			return;
	}
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	const auto expected_depth = navigation().depth;
	const auto expected_hierarchy = navigation().hierarchy;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && navigation().depth == expected_depth
		       && navigation().hierarchy == expected_hierarchy;
	};

	// We might be in the middle of an audio routine or something, so just see whether the selected bit of the UI is
	// visible

	for (int32_t u = navigation().depth - 1; u >= 0; u--) {
		UI* thisUI = navigation().hierarchy[u];
		if (ui == thisUI) {
			navigation().main_rows_dirty |= whichMainRows;
			navigation().side_rows_dirty |= whichSideRows;
			break;
		}

		if (whichMainRows && thisUI->renderMainPads()) {
			whichMainRows = 0;
		}
		if (!context_matches())
			return;
		if (whichSideRows && thisUI->renderSidebar()) {
			whichSideRows = 0;
		}

		if (!context_matches())
			return;

		if (!whichMainRows && !whichSideRows) {
			break;
		}
	}
}

void doAnyPendingGridRendering() {
	if (navigation().depth <= 0 || navigation().depth > navigation().capacity)
		return;
	for (int32_t level = 0; level < navigation().depth; ++level) {
		if (!navigation().hierarchy[level])
			return;
	}
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	auto& source_navigation = navigation();
	const auto expected_depth = source_navigation.depth;
	const auto expected_hierarchy = source_navigation.hierarchy;
	const auto requested_main_rows = source_navigation.main_rows_dirty;
	const auto requested_side_rows = source_navigation.side_rows_dirty;
	const auto context_matches = [&] {
		if (deluge::gui::ui_session::current() == source_owner && source_navigation.depth == expected_depth
		    && source_navigation.hierarchy == expected_hierarchy)
			return true;
		// Retry on the initiating panel without dropping redraws queued by callbacks.
		source_navigation.main_rows_dirty |= requested_main_rows;
		source_navigation.side_rows_dirty |= requested_side_rows;
		return false;
	};

	if (!navigation().main_rows_dirty && !navigation().side_rows_dirty) {
		return;
	}

	if (currentUIMode == UI_MODE_HORIZONTAL_SCROLL || currentUIMode == UI_MODE_HORIZONTAL_ZOOM) {
		return;
	}
	// Make a local copy of our instructions
	uint32_t mainRowsNow = navigation().main_rows_dirty;
	uint32_t sideRowsNow = navigation().side_rows_dirty;

	// Clear the overall instructions - so it may now be written to again during this function call
	clearPendingUIRendering();

	for (int32_t u = navigation().depth - 1; u >= 0; u--) {

		if (!mainRowsNow && !sideRowsNow) {
			break;
		}

		UI* thisUI = navigation().hierarchy[u];

		if (mainRowsNow) {
			bool usedUp = thisUI->renderMainPads(mainRowsNow, PadLEDs::image_for_session(),
			                                     PadLEDs::occupancy_mask_for_session());
			if (!context_matches())
				return;
			if (usedUp) {
				if (!navigation().main_rows_dirty) {
					PadLEDs::sendOutMainPadColours();
					if (!context_matches())
						return;
				}
				mainRowsNow = 0;
			}
		}

		if (sideRowsNow) {
			bool usedUp =
			    thisUI->renderSidebar(sideRowsNow, PadLEDs::image_for_session(), PadLEDs::occupancy_mask_for_session());
			if (!context_matches())
				return;
			if (usedUp) {
				if (!navigation().side_rows_dirty) {
					PadLEDs::sendOutSidebarColours();
					if (!context_matches())
						return;
				}
				sideRowsNow = 0;
			}
		}
	}
}

void doAnyPendingOLEDRendering() {
	if (navigation().oled_dirty) {
		int32_t u = navigation().depth - 1;
		while ((u > 0) && navigation().hierarchy[u]->oledShowsUIUnderneath) {
			u--;
		}

		OLED::clearMainImage();
		u = std::max(u, 0L);
		for (; u < navigation().depth; u++) {
			OLED::stopScrollingAnimation();
			navigation().hierarchy[u]->renderOLED(deluge::hid::display::OLED::main_for_session());
		}

		// Don't need to mark dirty because clearMainImage has already done that for us

		navigation().oled_dirty = false;
	}

	OLED::sendMainImage();
}

void doAnyPendingUIRendering() {
	if (deluge::hid::mirror::is_client())
		return;
	if (navigation().rendering) {
		return; // There's no point going in here multiple times inside each other
	}

	if (!deluge::gui::ui_session::graphics_output_ready([] {
		    return uartGetTxBufferSpace(UART_ITEM_PIC_PADS) > (kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) * 2;
	    })) {
		return; // Trialling the *2 to fix flickering when flicking through presets very fast
	}

	navigation().rendering = true;

	// Re-reading menu targets is deferred until storage has finished yielding.
	// Each panel consumes its own notifications; this never switches UI owners.
	if (!sdRoutineLock && !currentlyAccessingCard && navigation().depth > 0
	    && navigation().shared_model_refresh.consume(deluge::modulation::automation::parameter_revision)) {
		getCurrentUI()->refresh_shared_model();
	}

	const bool overview =
	    navigation().depth == 1
	    && (getCurrentUI() == &session_view_for_session() || getCurrentUI() == &arranger_view_for_session());
	if (navigation().structural_refresh.consume(sdRoutineLock || currentlyAccessingCard, overview,
	                                            currentUIMode == 0)) {
		uiNeedsRendering(getCurrentUI());
		renderUIsForOled();
	}

	doAnyPendingGridRendering();
	doAnyPendingOLEDRendering();

	navigation().rendering = false;
}

bool isUIModeActive(uint32_t uiMode) {
	if (uiMode > EXCLUSIVE_UI_MODES_MASK) {
		return (currentUIMode & uiMode);
	}
	else {
		uint32_t exclusivesOnly = currentUIMode & EXCLUSIVE_UI_MODES_MASK;
		return (exclusivesOnly == uiMode);
	}
}

bool isUIModeActiveExclusively(uint32_t uiMode) {
	return (currentUIMode == uiMode);
}

// Checks that all of the currently active UI modes are within the list of modes provided. As well as making things
// tidy, the main point of this is to still return true when more than one of the modes on the list provided is active.
// Terminate the list with a 0.
bool isUIModeWithinRange(const uint32_t* modes) {
	uint32_t exclusivesOnly = currentUIMode & EXCLUSIVE_UI_MODES_MASK;
	uint32_t nonExclusivesOnly = currentUIMode & ~EXCLUSIVE_UI_MODES_MASK;
	while (*modes) {
		// If looking at an exclusive mode...
		if (*modes <= EXCLUSIVE_UI_MODES_MASK) {
			if (*modes == exclusivesOnly) {
				exclusivesOnly = 0;
			}
		}

		// Or if looking at a non-exclusive mode...
		else {
			nonExclusivesOnly &= ~*modes;
		}
		modes++;
	}

	return (!exclusivesOnly && !nonExclusivesOnly);
}

bool isNoUIModeActive() {
	return !currentUIMode;
}

// You can safely call this even if you don't know whether said UI mode is active.
void exitUIMode(uint32_t uiMode) {
	if (uiMode > EXCLUSIVE_UI_MODES_MASK) {
		currentUIMode = currentUIMode & ~uiMode;
	}
	else {
		if ((currentUIMode & EXCLUSIVE_UI_MODES_MASK) == uiMode) {
			currentUIMode = currentUIMode & ~EXCLUSIVE_UI_MODES_MASK;
		}
	}
}

void enterUIMode(uint32_t uiMode) {
	if (uiMode > EXCLUSIVE_UI_MODES_MASK) {
		currentUIMode |= uiMode;
	}
	else {
		currentUIMode = (currentUIMode & ~EXCLUSIVE_UI_MODES_MASK) | uiMode;
	}
}

#if ENABLE_MATRIX_DEBUG
EnumStringMap<UIType, util::to_underlying(UIType::UI_TYPE_COUNT)> uiTypeMap = {
    {{{UIType::ARRANGER, "arranger"},
      {UIType::AUDIO_CLIP, "audio_clip"},
      {UIType::AUDIO_RECORDER, "audio_recorder"},
      {UIType::AUTOMATION, "automation"},
      {UIType::CONTEXT_MENU, "context_menu"},
      {UIType::DX_BROWSER, "dx_browser"},
      {UIType::INSTRUMENT_CLIP, "instrument_clip"},
      {UIType::KEYBOARD_SCREEN, "keyboard_screen"},
      {UIType::LOAD_INSTRUMENT_PRESET, "load_instrument_preset"},
      {UIType::LOAD_MIDI_DEVICE_DEFINITION, "load_midi_device_definition"},
      {UIType::LOAD_PATTERN, "load_pattern"},
      {UIType::LOAD_SONG, "load_song"},
      {UIType::PERFORMANCE, "performance"},
      {UIType::RENAME, "rename"},
      {UIType::SAMPLE_BROWSER, "sample_browser"},
      {UIType::SAMPLE_MARKER_EDITOR, "sample_marker_editor"},
      {UIType::SAVE_INSTRUMENT_PRESET, "save_instrument_preset"},
      {UIType::SAVE_KIT_ROW, "save_kit_row"},
      {UIType::SAVE_MIDI_DEVICE_DEFINITION, "save_midi_device_definition"},
      {UIType::SAVE_PATTERN, "save_pattern"},
      {UIType::SAVE_SONG, "save_song"},
      {UIType::SESSION, "session"},
      {UIType::SLICER, "slicer"},
      {UIType::SOUND_EDITOR, "sound_editor"}}}};

const char* UI::getUIName() {
	return uiTypeMap(getUIType());
}
#endif
