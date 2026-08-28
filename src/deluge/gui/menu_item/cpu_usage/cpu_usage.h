/*
 * Copyright © 2026 Sean Ditny
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
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/integer.h"
#include "util/d_stringbuf.h"
#include <span>

class Clip;
class Output;

namespace deluge::gui::menu_item::cpu_usage {

class Context final : public Integer {
public:
	Context(l10n::String new_name, l10n::String new_title, CPUUsageType new_type, CPUUsageContext new_context);

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void endSession() override;
	void readCurrentValue() override;
	void selectEncoderAction(int32_t offset) override;
	MenuItem* selectButtonPress() override { return NO_NAVIGATION; }
	bool shouldEnterSubmenu() override;
	bool isSubmenu() override { return false; }
	ActionResult timerCallback() override;
	bool selectEncoderActionIsPermitted() override { return false; }
	bool showNotification() const override { return false; }
	void getColumnLabel(StringBuf& label) override;

private:
	void scheduleTimer();
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 999; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return RenderingStyle::NUMBER; };

	CPUUsageType type_;
	CPUUsageContext context_;
};

class Menu final : public HorizontalMenu {
public:
	Menu(l10n::String new_name, l10n::String new_title, std::span<MenuItem*> new_items, MenuItem* default_item);

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void endSession() override;
	void selectEncoderAction(int32_t offset) override;
	ActionResult timerCallback() override;

private:
	void refresh_child_values();
	void schedule_timer();
};

} // namespace deluge::gui::menu_item::cpu_usage