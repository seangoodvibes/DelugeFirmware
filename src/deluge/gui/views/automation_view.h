/*
 * Copyright (c) 2023 Sean Ditny
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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/views/clip_view.h"
#include "model/clip/instrument_clip_minder.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"

// namespace deluge::gui::views {

class AutomationView : public ClipView, public InstrumentClipMinder {
public:
	AutomationView();
	bool opened() override;
	void initialize();
	void openedInBackground();
	void focusRegained() override;

	// called by ui_timer_manager - might need to revise this routine for automation clip view since it references notes
	void graphicsRoutine() override;

	// ui
	UI* getUI() override;
	UI* getViewFromUIContextType(UIType uiContextType);
	UIType getUIType() override { return UIType::AUTOMATION; }
	// used to identify the UI as a clip UI or not.
	ClipMinder* toClipMinder() override {
		return getUIModControllableContext() == UIModControllableContext::SONG ? nullptr : this;
	}

	// rendering
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                   bool modEncoderAction = false);
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override {
		InstrumentClipMinder::renderOLED(canvas);
	}

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset) override;
	uint32_t getMaxLength() override;
	uint32_t getMaxZoom() override;
	[[nodiscard]] int32_t getNavSysId() const override;

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;

	// Select encoder action
	void selectEncoderAction(int8_t offset) override;
	void getLastSelectedParamShortcut(Clip* clip);      // public so menu can access it
	void getLastSelectedParamArrayPosition(Clip* clip); // public so menu can access it
	bool isMultiPadPressSelected();                     // public so menu can access it

	// called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) override;

	// called by playback_handler.cpp
	void notifyPlaybackBegun() override;

	void initParameterSelection(bool updateDisplay = true);
	bool onArrangerView;
	bool inAutomationEditor();
	bool inNoteEditor();

	void resetInterpolationShortcutBlinking();
	void blinkInterpolationShortcut();
	bool interpolationBefore();
	bool interpolationAfter();

	void resetPadSelectionShortcutBlinking();
	void blinkPadSelectionShortcut();

	// public so menu can access it
	bool onMenuView;
	UI* previousUI; // previous UI so you can swap back UI after exiting menu
	int32_t getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);
	AutomationParamType automationParamType;

	void resetShortcutBlinking();
};

//}; // namespace deluge::gui::views

// TODO: should get moved into namespace once project namespacing is complete
extern AutomationView automationView;
