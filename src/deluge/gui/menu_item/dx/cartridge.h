
/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/menu_item.h"
#include <string_view>

class DX7Cartridge;

namespace deluge::gui::menu_item {

class DxCartridge final : public MenuItem {
public:
	using MenuItem::MenuItem;
	DxCartridge(l10n::String newName) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	bool tryLoad(std::string_view path);
	void drawPixelsForOled() override;
	void readValueAgain() final;
	void loadPatch();
	void selectEncoderAction(int32_t offset) final;
	MenuItem* selectButtonPress() final;
	void drawValue();

private:
	struct session_state {
		// Allocate each session's cartridge on demand.
		DX7Cartridge* cartridge = nullptr;
		int32_t current_value = 0;
		int scroll_position = 0;
	};
	ui_session::State<session_state> session_states;
	session_state& state_for_session();
};

extern DxCartridge dxCartridge;
} // namespace deluge::gui::menu_item
