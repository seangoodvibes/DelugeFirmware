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
#include "gui/views/automation/context/song.h"

// namespace deluge::gui::views {

class AutomationViewArranger final : public AutomationViewSong {
public:
	AutomationViewArranger();

	UIType getUIContextType() override { return UIType::ARRANGER; }

	bool opened() override;
	void openedInBackground();
	void focusRegained() override;

	// called by ui_timer_manager
	void graphicsRoutine() override;

	// rendering
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	// horizontal encoder action
	[[nodiscard]] int32_t getNavSysId() const override;
	uint32_t getMaxLength() override;
	uint32_t getMaxZoom() override;
};

//}; // namespace deluge::gui::views

// TODO: should get moved into namespace once project namespacing is complete
extern AutomationViewArranger automationViewArranger;
