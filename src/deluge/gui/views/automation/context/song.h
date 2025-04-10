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
#include "gui/views/automation_view.h"
#include "modulation/params/param.h"

// namespace deluge::gui::views {

class AutomationViewSong : public AutomationView {
public:
	AutomationViewSong();

	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::SONG; }

	// rendering
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind,
	                                         int32_t paramID) override;

	// pad action
	bool shortcutPadAction(int32_t xDisplay, int32_t yDisplay, int32_t velocity, int32_t xScroll, int32_t xZoom,
	                       int32_t effectiveLength);
	void editPadAction(int32_t xDisplay, int32_t yDisplay, int32_t velocity, int32_t xScroll, int32_t xZoom,
	                   int32_t effectiveLength);

	// Select encoder action
	void selectEncoderAction(int8_t offset) override;
	void selectParameterWithOffset(int32_t offset);
	void selectParameterWithShortcut(int32_t xDisplay, int32_t yDisplay);
	int32_t getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamID,
	                                          int32_t lastSelectedParamArrayPosition, int32_t numParams);
	void getLastSelectedGlobalParamArrayPosition();

	// model stack with param
	ModelStackWithAutoParam* getModelStackWithParam(void* modelStackMemory) override;
};

//}; // namespace deluge::gui::views

// TODO: should get moved into namespace once project namespacing is complete
extern AutomationViewSong automationViewSong;
