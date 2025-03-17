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

class AutomationParameterSelection : public AutomationLayout {
public:
	AutomationParameterSelection();

	void initialize();
	void focusRegained(Clip* clip);

	void handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
	                              int32_t yDisplay);
	void setAutomationParamType();

	// Select encoder action
	bool selectEncoderAction(Clip* clip, Output* output, OutputType outputType, int8_t offset);
	void selectGlobalParam(int32_t offset, Clip* clip);
	void selectNonGlobalParam(int32_t offset, Clip* clip);
	bool selectPatchCable(int32_t offset, Clip* clip);
	bool selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex, bool& foundCurrentPatchCable);
	void selectMIDICC(int32_t offset, Clip* clip);
	int32_t getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
	                                          int32_t numParams);
	void getLastSelectedParamShortcut(Clip* clip);      // public so menu can access it
	void getLastSelectedParamArrayPosition(Clip* clip); // public so menu can access it
	void getLastSelectedNonGlobalParamArrayPosition(Clip* clip);
	void getLastSelectedGlobalParamArrayPosition(Clip* clip);

	// public so instrument clip view can access it
	void initParameterSelection(bool updateDisplay = true);
};

extern AutomationParameterSelection automationParameterSelection;
