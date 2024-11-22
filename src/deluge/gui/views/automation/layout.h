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
	void initializeView();
	void focusRegained();
	void openedInBackground();

	void graphicsRoutine();

	// ui
	AutomationSubType getAutomationSubType();

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
	uint32_t getMaxLength();
	uint32_t getMaxZoom();
	int32_t getNavSysId() const;
	int32_t navSysId;

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult scrollVertical(int32_t scrollAmount);

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);

	// Select encoder action
	void selectEncoderAction(int8_t offset);
	void getLastSelectedParamShortcut(Clip* clip);      // public so menu can access it
	void getLastSelectedParamArrayPosition(Clip* clip); // public so menu can access it
	bool isMultiPadPressSelected();                     // public so menu can access it
	bool multiPadPressSelected;

	// called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);

	// called by playback_handler.cpp
	void notifyPlaybackBegun();

	void setAutomationParamType();

	bool onAutomationOverview();
	bool inAutomationEditor();
	bool inNoteEditor();

	ModelStackWithAutoParam*
	getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                              int32_t paramID = deluge::modulation::params::kNoParamID,
	                              deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE);

	// public so instrument clip view can access it
	void initParameterSelection(bool updateDisplay = true);

	// public so uiTimerManager can access it
	void blinkInterpolationShortcut();
	void blinkPadSelectionShortcut();
	bool interpolationBefore();
	bool interpolationAfter();

	// public so menu can access it
	// UI* previousUI; // previous UI so you can swap back UI after exiting menu
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);
	void resetInterpolationShortcutBlinking();
	void resetPadSelectionShortcutBlinking();
	bool getAffectEntire();

	void resetShortcutBlinking();

	uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight];

	// protected:
	void initPadSelection();
	void updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
	                                 bool updateDisplay = true, bool updateIndicatorLevels = true);
	void renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                             int32_t xDisplay = kNoSelection, bool modEncoderAction = false);
	void handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                                    int32_t yDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	void handleAutomationMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t firstPadX,
	                                   int32_t firstPadY, int32_t secondPadX, int32_t secondPadY,
	                                   int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                   bool modEncoderAction = false);
	int32_t lastPadSelectedKnobPos;
	bool padSelectionOn;
	bool multiPadPressActive;
	bool middlePadPressSelected;
	int32_t leftPadSelectedX;
	int32_t leftPadSelectedY;
	int32_t rightPadSelectedX;
	int32_t rightPadSelectedY;
	int32_t numNotesSelected;
	int32_t selectedPadPressed;

	void handleAutomationParameterChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                     OutputType outputType, int32_t xDisplay, int32_t yDisplay,
	                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	int32_t calculateAutomationKnobPosForPadPress(ModelStackWithAutoParam* modelStackWithParam, OutputType outputType,
	                                              int32_t yDisplay);
	int32_t calculateAutomationKnobPosForMiddlePadPress(deluge::modulation::params::Kind kind, int32_t yDisplay);
	int32_t calculateAutomationKnobPosForSinglePadPress(deluge::modulation::params::Kind kind, int32_t yDisplay);
	int32_t calculateAutomationKnobPosForModEncoderTurn(ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos,
	                                                    int32_t offset);

private:
	// button action functions
	void handleSessionButtonAction(Clip* clip, bool on);
	void handleKeyboardButtonAction(bool on);
	void handleClipButtonAction(bool on, bool isAudioClip);
	void handleCrossScreenButtonAction(bool on);
	void handleKitButtonAction(OutputType outputType, bool on);
	void handleSynthButtonAction(OutputType outputType, bool on);
	void handleMidiButtonAction(OutputType outputType, bool on);
	void handleCVButtonAction(OutputType outputType, bool on);
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
	void auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown);

	// Automation View Render Functions
	void performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll, uint32_t xZoom,
	                         int32_t renderWidth, int32_t imageWidth, bool drawUndefinedArea = true);
	void renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       int32_t knobPosRight = kNoSelection);
	void renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       bool modEncoderAction = false);

	// Horizontal Encoder Action
	void shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
	                                 int32_t effectiveLength);

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

	void blinkShortcuts();
	void resetParameterShortcutBlinking();

	bool parameterShortcutBlinking;

	bool interpolationShortcutBlinking;
	bool padSelectionShortcutBlinking;

	// grid sized array to assign midi cc values to each pad on the grid
	void initMIDICCShortcutsForAutomation();
	bool midiCCShortcutsLoaded;

	bool probabilityChanged;
	uint32_t timeSelectKnobLastReleased;
};

extern AutomationLayout automationLayout;
