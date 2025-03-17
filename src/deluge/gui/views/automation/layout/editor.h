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
#include "gui/views/automation/layout.h"
#include "hid/button.h"
#include "hid/display/oled_canvas/canvas.h"
#include "model/note/note_row.h"
#include "modulation/automation/copied_param_automation.h"

class Clip;

class AutomationLayoutEditor : public AutomationLayout {
public:
	AutomationLayoutEditor();

	// Grid render functions
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	void renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                            RGB image[][kDisplayWidth + kSideBarWidth],
	                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
	                            int32_t xScroll, uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay,
	                            bool drawUndefinedArea, deluge::modulation::params::Kind kind, bool isBipolar);
	void renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth, int32_t xScroll,
	                      uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
	                      SquareInfo& squareInfo);
	void renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
	                         RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
	                         TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay);

	// Display render Functions
	void renderAutomationEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Clip* clip,
	                                       OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight);
	void renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, InstrumentClip* clip,
	                                 OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight);
	void renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft,
	                                       bool modEncoderAction);
	void renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType, int32_t knobPosLeft);

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset);

	// Vertical encoder action
	void potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output);

	// Pad action
	uint32_t getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	uint32_t getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);

	// Sound Editor
	void automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                             int32_t yDisplay, int32_t velocity, int32_t effectiveLength, int32_t xScroll,
	                             int32_t xZoom);
	bool toggleAutomationInterpolation();
	bool toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam, int32_t effectiveLength,
	                                      int32_t xScroll, int32_t xZoom);
	int32_t getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);
	void setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                 bool modEncoderAction = false);
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);
	void updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
	                                 bool updateDisplay = true, bool updateIndicatorLevels = true);
	void renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                             int32_t xDisplay = kNoSelection, bool modEncoderAction = false);
	bool automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder,
	                                              int32_t offset, int32_t effectiveLength);
	void automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
	                                                int32_t whichModEncoder, int32_t offset, int32_t effectiveLength);
	void copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll, int32_t xZoom);
	void pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t effectiveLength,
	                     int32_t xScroll, int32_t xZoom);
	void initInterpolation();
	bool interpolation;
	bool interpolationBefore;
	bool interpolationAfter;
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	// Note Editor
	void noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                       int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	void velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                           int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	int32_t getVelocityFromY(int32_t y);
	int32_t getYFromVelocity(int32_t velocity);
	void addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity);
	void adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t velocity,
	                        int32_t newVelocity, uint8_t squareType);
	void setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t newVelocity);
	void setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
	                     SquareInfo rowSquareInfo[kDisplayWidth], int32_t velocityIncrement);
	void recordNoteEditPadAction(int32_t x, int32_t velocity);

	int32_t getEffectiveLength(ModelStackWithTimelineCounter* modelStack);

	bool isOverview() { return false; }
	bool isEditor() { return true; }
};

extern AutomationLayoutEditor automationLayoutEditor;
