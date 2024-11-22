/*
 * Copyright (c) 2025 Sean Ditny
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

#include "gui/views/automation/layout/editor.h"
#include "modulation/automation/copied_param_automation.h"

class Clip;

class AutomationLayoutEditorModControllable : public AutomationLayoutEditor {
public:
	AutomationLayoutEditorModControllable();

	// Grid render functions
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	void renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                            RGB image[][kDisplayWidth + kSideBarWidth],
	                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
	                            int32_t xScroll, uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay,
	                            bool drawUndefinedArea, deluge::modulation::params::Kind kind, bool isBipolar);
	void renderAutomationColumn(ModelStackWithAutoParam* modelStackWithParam,
	                            RGB image[][kDisplayWidth + kSideBarWidth],
	                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t lengthToDisplay,
	                            int32_t xDisplay, bool isAutomated, int32_t xScroll, int32_t xZoom,
	                            deluge::modulation::params::Kind kind, bool isBipolar);
	void renderAutomationBipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                                   int32_t yDisplay, bool isAutomated, deluge::modulation::params::Kind kind,
	                                   int32_t knobPos);
	void renderAutomationUnipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                                    int32_t yDisplay, bool isAutomated, int32_t knobPos);

	// Display render Functions
	void renderAutomationEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Clip* clip,
	                                       OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight);
	void renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft,
	                                       bool modEncoderAction);
	void getAutomationParameterName(Clip* clip, OutputType outputType, StringBuf& parameterName);

	// Pad action
	uint32_t getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	uint32_t getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);

	// Horizontal Encoder Action
	ActionResult horizontalEncoderAction(int32_t offset);
	void shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
	                                 int32_t effectiveLength);

	// Sound Editor
	void automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                             int32_t yDisplay, int32_t velocity, int32_t effectiveLength, int32_t xScroll,
	                             int32_t xZoom);
	bool recordAutomationSinglePadPress(int32_t xDisplay, int32_t yDisplay);
	bool toggleAutomationInterpolation();
	bool toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam, int32_t effectiveLength,
	                                      int32_t xScroll, int32_t xZoom);
	int32_t getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);
	bool getAutomationNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed);
	void setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                 bool modEncoderAction = false);
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);
	void updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
	                                 bool updateDisplay = true, bool updateIndicatorLevels = true);
	void handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                                    int32_t yDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	void handleAutomationParameterChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                     OutputType outputType, int32_t xDisplay, int32_t yDisplay,
	                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	int32_t calculateAutomationKnobPosForPadPress(ModelStackWithAutoParam* modelStackWithParam, OutputType outputType,
	                                              int32_t yDisplay);
	int32_t calculateAutomationKnobPosForMiddlePadPress(deluge::modulation::params::Kind kind, int32_t yDisplay);
	int32_t calculateAutomationKnobPosForSinglePadPress(deluge::modulation::params::Kind kind, int32_t yDisplay);
	void handleAutomationMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t firstPadX,
	                                   int32_t firstPadY, int32_t secondPadX, int32_t secondPadY,
	                                   int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                   bool modEncoderAction = false);
	void renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                             int32_t xDisplay = kNoSelection, bool modEncoderAction = false);
	int32_t calculateAutomationKnobPosForModEncoderTurn(ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos,
	                                                    int32_t offset);
	bool automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder,
	                                              int32_t offset, int32_t effectiveLength);
	void automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
	                                                int32_t whichModEncoder, int32_t offset, int32_t effectiveLength);
	void copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll, int32_t xZoom);
	void pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t effectiveLength,
	                     int32_t xScroll, int32_t xZoom);
	CopiedParamAutomation copiedParamAutomation;
	void initInterpolation();
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	int32_t getEffectiveLength(ModelStackWithTimelineCounter* modelStack);
};

extern AutomationLayoutEditorModControllable automationLayoutEditorModControllable;
