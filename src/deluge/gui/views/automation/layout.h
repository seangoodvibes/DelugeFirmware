/*
 * Copyright © 2014-2024 Synthstrom Audible Limited
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
#include "hid/button.h"
#include "hid/display/oled_canvas/canvas.h"
#include "model/note/note_row.h"
#include "modulation/automation/copied_param_automation.h"

// shortcuts for toggling interpolation and pad selection mode
constexpr uint8_t kInterpolationShortcutX = 0;
constexpr uint8_t kInterpolationShortcutY = 6;
constexpr uint8_t kPadSelectionShortcutX = 0;
constexpr uint8_t kPadSelectionShortcutY = 7;
constexpr uint8_t kVelocityShortcutX = 15;
constexpr uint8_t kVelocityShortcutY = 1;

class Clip;

class AutomationLayout {
public:
	AutomationLayout();
	bool opened();
	void initialize();
	void focusRegained();
	void openedInBackground();

	void graphicsRoutine();

	// grid sized array to assign midi cc values to each pad on the grid
	void initMIDICCShortcutsForAutomation();
	bool midiCCShortcutsLoaded = false;
	uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight];

	// rendering
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                   bool modEncoderAction = false);
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset);
	int32_t getNavSysId() const;
	int32_t navSysId = NAVIGATION_CLIP;

	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	void commandVerticalScroll(InstrumentClip* clip, int32_t scrollAmount);
	void scrollVertical(InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack, int32_t scrollAmount);
	void potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output);

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);

	// Select encoder action
	void selectEncoderAction(int8_t offset);
	void getLastSelectedParamShortcut(Clip* clip);      // public so menu can access it
	void getLastSelectedParamArrayPosition(Clip* clip); // public so menu can access it
	bool isMultiPadPressSelected();                     // public so menu can access it
	bool multiPadPressSelected = false;

	bool onAutomationOverview();
	bool inAutomationEditor();
	bool inNoteEditor();

	// public so instrument clip view can access it
	void initParameterSelection(bool updateDisplay = true);

	// public so uiTimerManager can access it
	void blinkInterpolationShortcut();
	void blinkPadSelectionShortcut();
	bool interpolationBefore();
	bool interpolationAfter();

	// public so menu can access it
	// UI* previousUI; // previous UI so you can swap back UI after exiting menu
	void resetInterpolationShortcutBlinking();
	void resetPadSelectionShortcutBlinking();
	bool getAffectEntire();

	void resetShortcutBlinking();

	// protected:
	// used to enter pad selection mode
	void initPadSelection();
	int32_t lastPadSelectedKnobPos = kNoSelection;
	bool padSelectionOn = false;
	bool multiPadPressActive = false;
	bool middlePadPressSelected = false;
	int32_t leftPadSelectedX = kNoSelection;
	int32_t leftPadSelectedY = kNoSelection;
	int32_t rightPadSelectedX = kNoSelection;
	int32_t rightPadSelectedY = kNoSelection;

	void blinkShortcuts();
	void resetParameterShortcutBlinking();

private:
	// button action functions
	void handleSessionButtonAction(Clip* clip, bool on);
	void handleClipButtonAction(bool on, bool isAudioClip);
	void handleCrossScreenButtonAction(bool on);
	bool handleHorizontalEncoderButtonAction(bool on, bool isAudioClip);
	bool handleBackAndHorizontalEncoderButtonComboAction(Clip* clip, bool on);
	void handleVerticalEncoderButtonAction(bool on);
	void handleSelectEncoderButtonAction(bool on);
	void handleAffectEntireButtonAction(bool on);

	// edit pad action
	ActionResult handleEditPadAction(Clip* clip, Output* output, OutputType outputType, int32_t x, int32_t y,
	                                 int32_t velocity);
	bool shortcutPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, Output* output,
	                       OutputType outputType, int32_t effectiveLength, int32_t x, int32_t y, int32_t velocity,
	                       int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo);

	void handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
	                              int32_t yDisplay);

	// mute pad action
	ActionResult handleMutePadAction(InstrumentClip* instrumentClip, Output* output, OutputType outputType, int32_t y,
	                                 int32_t velocity);

	// audition pad action
	ActionResult handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output, OutputType outputType,
	                                     int32_t y, int32_t velocity);
	ActionResult auditionPadAction(InstrumentClip* clip, Output* output, OutputType outputType, int32_t yDisplay,
	                               int32_t velocity, bool shiftButtonDown);

	// Automation View Render Functions
	void performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll, uint32_t xZoom,
	                         int32_t renderWidth, int32_t imageWidth, bool drawUndefinedArea = true);
	void renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       int32_t knobPosRight = kNoSelection);
	void renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       bool modEncoderAction = false);

	// Select Encoder Action
	void selectGlobalParam(int32_t offset, Clip* clip);
	void selectNonGlobalParam(int32_t offset, Clip* clip);
	bool selectPatchCable(int32_t offset, Clip* clip);
	bool selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex, bool& foundCurrentPatchCable);
	void selectMIDICC(int32_t offset, Clip* clip);
	int32_t getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
	                                          int32_t numParams);
	void getLastSelectedNonGlobalParamArrayPosition(Clip* clip);
	void getLastSelectedGlobalParamArrayPosition(Clip* clip);

	// Automation Lanes Functions
	ParamManagerForTimeline* getParamManagerForClip(Clip* clip);

	// used to set parameter shortcut blinking
	bool parameterShortcutBlinking = false;
	// used to set interpolation shortcut blinking
	bool interpolationShortcutBlinking = false;
	// used to set pad selection shortcut blinking
	bool padSelectionShortcutBlinking = false;

	bool probabilityChanged = false;
	uint32_t timeSelectKnobLastReleased = 0;
};

extern AutomationLayout automationLayout;
