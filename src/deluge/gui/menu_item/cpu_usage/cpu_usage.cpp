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
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"
#include "util/cfunctions.h"
#include <algorithm>

namespace deluge::gui::menu_item::cpu_usage {

namespace {

using hid::display::OLED;

// CPU limits
/// Filter impact
constexpr uint8_t k_filter_impact = 1;
constexpr uint8_t k_stereo_impact = 1;

/// Mono unison impact
// 1 unison = no impact
// 2 unison = 95 voices = 100 - ((2 - 1) x 6.5% x 100) = 93.5
// 3 unison = 90 voices = 100 - ((3 - 1) x 6.5% x 100) = 87
// 4 unison = 85 voices = 100 - ((4 - 1) x 6.5% x 100) = 80.5
// 5 unison = 75 voices = 100 - ((5 - 1) x 6.5% x 100) = 74
// 6 unison = 65 voices = 100 - ((6 - 1) x 6.5% x 100) = 67.5
// 7 unison = 60 voices = 100 - ((7 - 1) x 6.5% x 100) = 61
// 8 unison = 55 voices = 100 - ((8 - 1) x 6.5% x 100) = 55.5

/// Stereo unison impact
// 1 unison = no impact
// 2 unison = 75 voices = 100 - ((2 - 1) x 11% x 100) = 89
// 3 unison = 60 voices = 100 - ((3 - 1) x 11% x 100) = 78
// 4 unison = 50 voices = 100 - ((4 - 1) x 11% x 100) = 67
// 5 unison = 40 voices = 100 - ((5 - 1) x 11% x 100) = 56
// 6 unison = 35 voices = 100 - ((6 - 1) x 11% x 100) = 45
// 7 unison = 30 voices = 100 - ((7 - 1) x 11% x 100) = 34
// 8 unison = 25 voices = 100 - ((8 - 1) x 11% x 100) = 23

/// Regular FM and Subtractive oscillator
constexpr int32_t k_max_raw_unfiltered_fm_and_sub_voices = 100;
constexpr int32_t k_max_raw_filtered_fm_and_sub_voices =
    k_max_raw_unfiltered_fm_and_sub_voices >> k_filter_impact; // filters cut voice performance in half
/// Wavetable oscillator
constexpr int32_t k_max_raw_unfiltered_wavetable_voices = 50;
constexpr int32_t k_max_raw_filtered_wavetable_voices =
    k_max_raw_unfiltered_wavetable_voices >> k_filter_impact; // filters cut voice performance in half
/// DX7
constexpr int32_t k_max_raw_unfiltered_dx7_voices = 50;
constexpr int32_t k_max_raw_filtered_dx7_voices =
    k_max_raw_unfiltered_dx7_voices >> k_filter_impact; // filters cut voice performance in half
/// Live Input Pitchshifter
constexpr int32_t k_max_raw_unfiltered_live_input_pitchshifter_voices = 7;
constexpr int32_t k_max_raw_filtered_live_input_pitchshifter_voices =
    k_max_raw_unfiltered_live_input_pitchshifter_voices >> k_filter_impact; // filters cut voice performance in half
/// Short samples
constexpr int32_t k_max_raw_unfiltered_short_sample_mono_voices = 24;
constexpr int32_t k_max_raw_filtered_short_sample_mono_voices =
    k_max_raw_unfiltered_short_sample_mono_voices >> k_filter_impact; // filters cut voice performance in half
constexpr int32_t k_max_raw_unfiltered_short_sample_stereo_voices =
    k_max_raw_unfiltered_short_sample_mono_voices >> k_stereo_impact;
constexpr int32_t k_max_raw_filtered_short_sample_stereo_voices =
    k_max_raw_unfiltered_short_sample_stereo_voices >> k_filter_impact; // filters cut voice performance in half

/// Long Samples
constexpr int32_t k_max_raw_unfiltered_long_sample_mono_voices = 24;
constexpr int32_t k_max_raw_filtered_long_sample_mono_voices =
    k_max_raw_unfiltered_long_sample_mono_voices >> k_filter_impact; // filters cut voice performance in half
constexpr int32_t k_max_raw_unfiltered_long_sample_stereo_voices =
    k_max_raw_unfiltered_long_sample_mono_voices >> k_stereo_impact;
constexpr int32_t k_max_raw_filtered_long_sample_stereo_voices =
    k_max_raw_unfiltered_long_sample_stereo_voices >> k_filter_impact; // filters cut voice performance in half

constexpr int32_t kUpdateIntervalMs = 25;
constexpr uint32_t kMinDisplayUpdateInterval = kSampleRate / 22;

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

[[nodiscard]] float calculateCPUUsagePercentageForDisplay() {
	(void)count_usage_for_type_and_context(CPUUsageType::VOICE, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::VOICE_UNISON, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::OSC_SAMPLE, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::OSC_SAMPLE_STRETCH, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::OSC_SAMPLE_CACHE, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::OSC_WAVETABLE, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::OSC_LIVE_PITCH, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::OSC_DX7, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::FILTER, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::FX_DELAY, CPUUsageContext::TOTAL);
	(void)count_usage_for_type_and_context(CPUUsageType::FX_GRAIN, CPUUsageContext::TOTAL);

	float dx7_usage =
	    static_cast<float>(count_usage_for_type_and_context(CPUUsageType::OSC_DX7, CPUUsageContext::TOTAL));
	float wavetable_usage =
	    static_cast<float>(count_usage_for_type_and_context(CPUUsageType::OSC_WAVETABLE, CPUUsageContext::TOTAL));
	float sample_usage =
	    static_cast<float>(count_usage_for_type_and_context(CPUUsageType::OSC_SAMPLE, CPUUsageContext::TOTAL));
	float live_usage =
	    static_cast<float>(count_usage_for_type_and_context(CPUUsageType::OSC_LIVE_PITCH, CPUUsageContext::TOTAL));
	float filter_usage =
	    static_cast<float>(count_usage_for_type_and_context(CPUUsageType::FILTER, CPUUsageContext::TOTAL)) * 2;

	float regular_synth_usage = filter_usage - dx7_usage - wavetable_usage - sample_usage - live_usage;

	float regular_synth_usage_percentage = (regular_synth_usage / k_max_raw_unfiltered_fm_and_sub_voices) * 100.0f;
	float dx7_usage_percentage = (dx7_usage / k_max_raw_unfiltered_dx7_voices) * 100.0f;
	float wavetable_usage_percentage = (wavetable_usage / k_max_raw_unfiltered_wavetable_voices) * 100.0f;
	float sample_usage_percentage = (sample_usage / k_max_raw_unfiltered_short_sample_mono_voices) * 100.0f;
	float live_usage_percentage = (live_usage / k_max_raw_unfiltered_live_input_pitchshifter_voices) * 100.0f;

	float total_usage_percentage = regular_synth_usage_percentage + dx7_usage_percentage + wavetable_usage_percentage
	                               + sample_usage_percentage + live_usage_percentage;

	return total_usage_percentage;
}

} // namespace

Overall::Overall(l10n::String new_name, l10n::String new_title) : Integer(new_name, new_title) {
}

void Overall::beginSession(MenuItem* navigatedBackwardFrom) {
	Integer::beginSession(navigatedBackwardFrom);
	readCurrentValue();
	last_actual_display_time_ = AudioEngine::audioSampleTimer;
	scheduleTimer();
}

void Overall::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	MenuItem::endSession();
}

void Overall::readCurrentValue() {
	const int32_t previous_value = getValue();
	const int32_t percentage = static_cast<int32_t>(calculateCPUUsagePercentageForDisplay());
	setValue(std::clamp<int32_t>(percentage, getMinValue(), getMaxValue()));
	value_changed_ = value_changed_ || getValue() != previous_value;
}

bool Overall::shouldEnterSubmenu() {
	if (parent != nullptr) {
		return parent->renderingStyle() != Submenu::RenderingStyle::HORIZONTAL;
	}

	return display->have7SEG();
}

ActionResult Overall::timerCallback() {
	readCurrentValue();

	const uint32_t current_time = AudioEngine::audioSampleTimer;
	const bool has_min_time_elapsed = current_time - last_actual_display_time_ >= kMinDisplayUpdateInterval;
	if (consumeValueChanged() && has_min_time_elapsed) {
		if (display->have7SEG()) {
			drawValue();
		}
		else {
			renderUIsForOled();
		}
		last_actual_display_time_ = current_time;
	}

	scheduleTimer();
	return ActionResult::DEALT_WITH;
}

float Overall::normalize(int32_t value) {
	const int32_t clamped = std::clamp<int32_t>(value, 0, max_value_in_horizontal_menu);
	return clamped / static_cast<float>(max_value_in_horizontal_menu);
}

void Overall::renderInHorizontalMenu(const SlotPosition& slot) {
	drawBar(slot);

	if (getValue() > max_value_in_horizontal_menu) {
		hid::display::oled_canvas::Canvas& image = OLED::main;
		constexpr uint8_t excl_mark_width = 3;
		constexpr uint8_t excl_mark_height = 11;
		const uint8_t center_x = slot.start_x + slot.width / 2;
		const uint8_t excl_mark_start_y = slot.start_y + kHorizontalMenuSlotYOffset - 1;
		const uint8_t excl_mark_end_y = excl_mark_start_y + excl_mark_height - 1;
		const uint8_t excl_mark_start_x = center_x - 1;

		for (uint8_t x = center_x - 2; x <= center_x + 2; x++) {
			for (uint8_t y = excl_mark_start_y - 1; y <= excl_mark_end_y + 1; y++) {
				image.drawPixel(x, y);
			}
		}

		image.invertArea(excl_mark_start_x, excl_mark_width, excl_mark_start_y, excl_mark_start_y + 6);
		image.invertArea(excl_mark_start_x, excl_mark_width, excl_mark_start_y + 8, excl_mark_start_y + 10);
	}
}

void Overall::getColumnLabel(StringBuf& label) {
	label.append(deluge::l10n::get(l10n::built_in::seven_segment, this->name));
}

void Overall::scheduleTimer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kUpdateIntervalMs);
}

bool Overall::consumeValueChanged() {
	const bool changed = value_changed_;
	value_changed_ = false;
	return changed;
}

Context::Context(l10n::String new_name, l10n::String new_title, CPUUsageType new_type, CPUUsageContext new_context)
    : Integer(new_name, new_title), type_(new_type), context_(new_context) {
}

void Context::beginSession(MenuItem* navigatedBackwardFrom) {
	Integer::beginSession(navigatedBackwardFrom);
	last_actual_display_time_ = AudioEngine::audioSampleTimer;
	scheduleTimer();
}

void Context::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	MenuItem::endSession();
}

void Context::readCurrentValue() {
	const int32_t previous_value = getValue();
	const int32_t new_value = count_usage_for_type_and_context(type_, context_);
	setValue(new_value);
	value_changed_ = value_changed_ || new_value != previous_value;
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
	readCurrentValue();

	const uint32_t current_time = AudioEngine::audioSampleTimer;
	const bool has_min_time_elapsed = current_time - last_actual_display_time_ >= kMinDisplayUpdateInterval;
	if (consumeValueChanged() && has_min_time_elapsed) {
		if (display->have7SEG()) {
			drawValue();
		}
		else {
			renderUIsForOled();
		}
		last_actual_display_time_ = current_time;
	}

	scheduleTimer();
	return ActionResult::DEALT_WITH;
}

void Context::getColumnLabel(StringBuf& label) {
	label.append(deluge::l10n::get(l10n::built_in::seven_segment, this->name));
}

void Context::scheduleTimer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kUpdateIntervalMs);
}

bool Context::consumeValueChanged() {
	const bool changed = value_changed_;
	value_changed_ = false;
	return changed;
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
	(void)refresh_child_values();
	last_actual_display_time_ = AudioEngine::audioSampleTimer;
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

	const bool values_changed = refresh_child_values();
	const uint32_t current_time = AudioEngine::audioSampleTimer;
	const bool has_min_time_elapsed = current_time - last_actual_display_time_ >= kMinDisplayUpdateInterval;

	if (values_changed && has_min_time_elapsed) {
		if (display->have7SEG()) {
			updateDisplay();
		}
		else {
			renderUIsForOled();
		}
		last_actual_display_time_ = current_time;
	}

	schedule_timer();

	return ActionResult::DEALT_WITH;
}

bool Menu::refresh_child_values() {
	bool values_changed = false;
	for (MenuItem* item : items) {
		if (item != nullptr) {
			item->readCurrentValue();
			if (item->isSubmenu()) {
				values_changed = static_cast<SummaryMenu*>(item)->consumeValueChanged() || values_changed;
			}
			else if (item->name == l10n::String::STRING_FOR_CPU_USAGE) {
				values_changed = static_cast<Overall*>(item)->consumeValueChanged() || values_changed;
			}
			else {
				values_changed = static_cast<Context*>(item)->consumeValueChanged() || values_changed;
			}
		}
	}
	return values_changed;
}

void Menu::schedule_timer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kUpdateIntervalMs);
}

SummaryMenu::SummaryMenu(l10n::String new_name, l10n::String new_title, const char* new_column_label,
                         std::span<const CPUUsageType> new_types, std::span<MenuItem*> new_items,
                         MenuItem* default_item)
    : HorizontalMenu(new_name, new_title, new_items), types_(new_types), column_label_(new_column_label) {
	auto candidate_item = std::find(items.begin(), items.end(), default_item);
	if (candidate_item != items.end()) {
		initial_index_ = candidate_item - items.begin();
	}
}

void SummaryMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	HorizontalMenu::beginSession(navigatedBackwardFrom);
	(void)refresh_child_values();
	last_actual_display_time_ = AudioEngine::audioSampleTimer;

	if (renderingStyle() == HORIZONTAL) {
		schedule_timer();
	}
}

void SummaryMenu::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	HorizontalMenu::endSession();
}

void SummaryMenu::readCurrentValue() {
	const int32_t previous_value = value_;
	int32_t value = 0;
	for (CPUUsageType type : types_) {
		value += count_usage_for_type_and_context(type, CPUUsageContext::TOTAL);
	}
	value_ = std::min<int32_t>(value, 999);
	value_changed_ = value_changed_ || value_ != previous_value;
}

ActionResult SummaryMenu::timerCallback() {
	if (renderingStyle() != HORIZONTAL) {
		return ActionResult::DEALT_WITH;
	}

	const bool values_changed = refresh_child_values();
	const uint32_t current_time = AudioEngine::audioSampleTimer;
	const bool has_min_time_elapsed = current_time - last_actual_display_time_ >= kMinDisplayUpdateInterval;

	if (values_changed && has_min_time_elapsed) {
		if (display->have7SEG()) {
			updateDisplay();
		}
		else {
			renderUIsForOled();
		}
		last_actual_display_time_ = current_time;
	}

	schedule_timer();

	return ActionResult::DEALT_WITH;
}

void SummaryMenu::renderInHorizontalMenu(const SlotPosition& slot) {
	DEF_STACK_STRING_BUF(value, 10);
	value.appendInt(value_);
	OLED::main.drawStringCentered(value.c_str(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
	                              kTextTitleSpacingX, kTextTitleSizeY, slot.width);
}

void SummaryMenu::getColumnLabel(StringBuf& label) {
	label.append(column_label_);
}

bool SummaryMenu::consumeValueChanged() {
	const bool changed = value_changed_;
	value_changed_ = false;
	return changed;
}

bool SummaryMenu::refresh_child_values() {
	bool values_changed = false;
	for (MenuItem* item : items) {
		if (item != nullptr) {
			item->readCurrentValue();
			if (item->isSubmenu()) {
				values_changed = static_cast<SummaryMenu*>(item)->consumeValueChanged() || values_changed;
			}
			else if (item->name == l10n::String::STRING_FOR_CPU_USAGE) {
				values_changed = static_cast<Overall*>(item)->consumeValueChanged() || values_changed;
			}
			else {
				values_changed = static_cast<Context*>(item)->consumeValueChanged() || values_changed;
			}
		}
	}
	return values_changed;
}

void SummaryMenu::schedule_timer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kUpdateIntervalMs);
}

} // namespace deluge::gui::menu_item::cpu_usage
