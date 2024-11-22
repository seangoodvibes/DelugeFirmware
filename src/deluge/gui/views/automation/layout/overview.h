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

#include "definitions_cxx.hpp"
#include "gui/views/automation/parameter_selection.h"
#include "hid/button.h"
#include "hid/display/oled_canvas/canvas.h"
#include "model/note/note_row.h"
#include "modulation/automation/copied_param_automation.h"

class Clip;

class AutomationLayoutOverview : public AutomationParameterSelection {
public:
	AutomationLayoutOverview();

	// Grid render Functions
	void renderMainPads(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                    ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip, OutputType outputType,
	                    RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, bool isMIDICVDrum);

	// Display render Functions
	void renderDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Output* output, OutputType outputType);
	void renderDisplay7SEG(Output* output, OutputType outputType);

	// Encoder actions
	ActionResult horizontalEncoderAction(int32_t offset) { return ActionResult::DEALT_WITH; }

	// Button action functions
	bool handleBackAndHorizontalEncoderButtonComboAction(Clip* clip, bool on);

	bool isOverview() { return true; }
	bool isEditor() { return false; }
};

extern AutomationLayoutOverview automationLayoutOverview;
