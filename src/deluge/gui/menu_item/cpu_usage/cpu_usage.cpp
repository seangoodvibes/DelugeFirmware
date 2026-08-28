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

#include "gui/menu_item/cpu_usage/cpu_usage.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>

namespace deluge::gui::menu_item::cpu_usage {

namespace {

using hid::display::OLED;

constexpr int32_t kUpdateIntervalMs = 500;

[[nodiscard]] int32_t count_usage_for_type_and_context(CPUUsageType type, CPUUsageContext context) {
	switch (context) {
	case CPUUsageContext::TOTAL:
		return AudioEngine::getCPUUsageForAllOutputs(type);
	case CPUUsageContext::SONG:
		return AudioEngine::getCPUUsageForSong(type);
	case CPUUsageContext::SYNTH:
		return AudioEngine::getCPUUsageForOutputType(type, OutputType::SYNTH);
	case CPUUsageContext::KIT:
		return AudioEngine::getCPUUsageForOutputType(type, OutputType::KIT);
	case CPUUsageContext::AUDIO:
		return AudioEngine::getCPUUsageForOutputType(type, OutputType::AUDIO);
	default:
		break;
	}

	return 0;
}

} // namespace

Context::Context(l10n::String new_name, l10n::String new_title, CPUUsageType new_type, CPUUsageContext new_context)
    : Integer(new_name, new_title), type_(new_type), context_(new_context) {
}

void Context::beginSession(MenuItem* navigatedBackwardFrom) {
	Integer::beginSession(navigatedBackwardFrom);
	scheduleTimer();
}

void Context::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	MenuItem::endSession();
}

void Context::readCurrentValue() {
	setValue(count_usage_for_type_and_context(type_, context_));
}

void Context::selectEncoderAction(int32_t offset) {
	(void)offset;
}

bool Context::shouldEnterSubmenu() {
	if (parent != nullptr) {
		return parent->renderingStyle() != Submenu::RenderingStyle::HORIZONTAL;
	}

	return display->have7SEG();
}

ActionResult Context::timerCallback() {
	readValueAgain();
	scheduleTimer();
	return ActionResult::DEALT_WITH;
}

void Context::getColumnLabel(StringBuf& label) {
	label.append(deluge::l10n::get(l10n::built_in::seven_segment, this->name));
}

void Context::scheduleTimer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kUpdateIntervalMs);
}

Menu::Menu(l10n::String new_name, l10n::String new_title, std::span<MenuItem*> new_items, MenuItem* default_item)
    : HorizontalMenu(new_name, new_title, new_items) {
	auto candidate_item = std::find(items.begin(), items.end(), default_item);
	if (candidate_item != items.end()) {
		initial_index_ = candidate_item - items.begin();
	}
}

void Menu::beginSession(MenuItem* navigatedBackwardFrom) {
	HorizontalMenu::beginSession(navigatedBackwardFrom);
	refresh_child_values();
	if (display->have7SEG()) {
		updateDisplay();
	}
	else {
		renderUIsForOled();
	}

	if (renderingStyle() == HORIZONTAL) {
		schedule_timer();
	}
}

void Menu::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	HorizontalMenu::endSession();
}

void Menu::selectEncoderAction(int32_t offset) {
	if (renderingStyle() != HORIZONTAL) {
		HorizontalMenu::selectEncoderAction(offset);
	}
}

ActionResult Menu::timerCallback() {
	if (renderingStyle() != HORIZONTAL) {
		return ActionResult::DEALT_WITH;
	}

	refresh_child_values();

	if (display->have7SEG()) {
		updateDisplay();
	}
	else {
		renderUIsForOled();
	}

	schedule_timer();

	return ActionResult::DEALT_WITH;
}

void Menu::refresh_child_values() {
	for (MenuItem* item : items) {
		if (item != nullptr) {
			item->readCurrentValue();
		}
	}
}

void Menu::schedule_timer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kUpdateIntervalMs);
}

} // namespace deluge::gui::menu_item::cpu_usage