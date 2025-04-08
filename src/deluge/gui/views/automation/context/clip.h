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

class AutomationViewClip : public AutomationView {
public:
	AutomationViewClip();

	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::CLIP; }

	// used to identify the UI as a clip UI or not.
	ClipMinder* toClipMinder() override { return this; }

	void openedInBackground();

	// rendering
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind,
	                                         int32_t paramID) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	// horizontal encoder action
	[[nodiscard]] int32_t getNavSysId() const override;
	uint32_t getMaxLength() override;
	uint32_t getMaxZoom() override;
};

//}; // namespace deluge::gui::views

// TODO: should get moved into namespace once project namespacing is complete
extern AutomationViewClip automationViewClip;
