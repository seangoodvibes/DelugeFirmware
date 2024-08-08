/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include "gui/ui/sound_editor.h"
#include "menu_item.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class QuadMenu : public MenuItem {
public:
	QuadMenu(l10n::String newName, MenuItem* item1, MenuItem* item2, MenuItem* item3, MenuItem* item4) :
		MenuItem(newName), items{item1, item2, item3, item4}, currentPos{0} {}
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final;
	void updateDisplay();
	void selectEncoderAction(int32_t offset) final;
	MenuItem* selectButtonPress() final;
	void readValueAgain() final { updateDisplay(); }
	void drawPixelsForOled() final;
	bool hasInputAction()  final { return true; }
private:
	static const uint8_t kNumItems = 4;
	static const size_t kLabelWidth = 4;
	MenuItem* items[kNumItems];
	uint8_t currentPos;
};

} // namespace deluge::gui::menu_item
