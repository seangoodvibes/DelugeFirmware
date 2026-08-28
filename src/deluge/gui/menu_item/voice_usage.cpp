/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "gui/menu_item/voice_usage.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include <algorithm>

namespace deluge::gui::menu_item::voice_usage {

namespace {

using hid::display::OLED;

constexpr int32_t kGraphicsSyncFallbackMs = 15;
constexpr uint32_t kShowDescriptionDurationSamples = 44100;
constexpr int32_t kOledVoiceCountAreaWidth = 21;
constexpr int32_t kOledVoiceCountRight = OLED_MAIN_WIDTH_PIXELS - 1;
constexpr int32_t kOledDescriptionEndX = OLED_MAIN_WIDTH_PIXELS - kOledVoiceCountAreaWidth;

[[nodiscard]] bool isActiveClip(Clip* clip) {
	return currentSong != nullptr && clip != nullptr && clip->output != nullptr && currentSong->isClipActive(clip)
	       && clip->isActiveOnOutput();
}

[[nodiscard]] bool includesOutputType(Mode mode, OutputType outputType) {
	switch (mode) {
	case Mode::TOTAL:
		return outputType == OutputType::SYNTH || outputType == OutputType::KIT || outputType == OutputType::AUDIO;
	case Mode::SYNTH:
		return outputType == OutputType::SYNTH;
	case Mode::KIT:
		return outputType == OutputType::KIT;
	case Mode::AUDIO:
		return outputType == OutputType::AUDIO;
	}

	return false;
}

[[nodiscard]] int32_t countSynthClipVoices(Clip* clip) {
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		return 0;
	}

	Output* output = clip->output;
	if (output == nullptr || output->type != OutputType::SYNTH) {
		return 0;
	}

	auto* soundInstrument = static_cast<const SoundInstrument*>(output);
	return soundInstrument->voices().size();
}

[[nodiscard]] int32_t countKitClipVoices(Clip* clip) {
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		return 0;
	}

	Output* output = clip->output;
	if (output == nullptr || output->type != OutputType::KIT) {
		return 0;
	}

	auto* instrumentClip = static_cast<InstrumentClip*>(clip);
	int32_t count = 0;
	for (int32_t i = 0; i < instrumentClip->noteRows.getNumElements(); ++i) {
		NoteRow* noteRow = instrumentClip->noteRows.getElement(i);
		if (noteRow == nullptr || noteRow->drum == nullptr || noteRow->drum->type != DrumType::SOUND) {
			continue;
		}

		auto* soundDrum = static_cast<const SoundDrum*>(noteRow->drum);
		count += soundDrum->voices().size();
	}

	return count;
}

[[nodiscard]] int32_t countClipVoicesForType(Clip* clip, OutputType outputType) {
	switch (outputType) {
	case OutputType::SYNTH:
		return countSynthClipVoices(clip);
	case OutputType::KIT:
		return countKitClipVoices(clip);
	case OutputType::AUDIO:
		return 1;
	default:
		return 0;
	}
}

[[nodiscard]] int32_t countClipVoices(Clip* clip) {
	if (clip == nullptr) {
		return 0;
	}

	Output* output = clip->output;
	if (output == nullptr) {
		return 0;
	}

	return countClipVoicesForType(clip, output->type);
}

[[nodiscard]] int32_t countClipsForMode(Mode mode) {
	if (mode == Mode::TOTAL) {
		return AudioEngine::getNumVoices() + AudioEngine::getNumAudio();
	}
	if (mode == Mode::AUDIO) {
		return AudioEngine::getNumAudio();
	}
	if (currentSong == nullptr) {
		return 0;
	}

	int32_t count = 0;
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		if (!isActiveClip(clip)) {
			continue;
		}

		Output* output = clip->output;
		if (output == nullptr || !includesOutputType(mode, output->type)) {
			continue;
		}

		count += countClipVoicesForType(clip, output->type);
	}

	return count;
}

void appendPrefixForType(OutputType outputType, StringBuf& description) {
	switch (outputType) {
	case OutputType::SYNTH:
		description.append("S: ");
		break;
	case OutputType::KIT:
		description.append("K: ");
		break;
	case OutputType::AUDIO:
		description.append("A: ");
		break;
	default:
		break;
	}
}

void appendClipDescription(const Clip* clip, const Output* output, bool includePrefix, StringBuf& description) {
	if (clip == nullptr || output == nullptr) {
		return;
	}

	if (includePrefix) {
		appendPrefixForType(output->type, description);
	}

	description.append(output->name.get());
	description.append(" (");
	if (clip->name.get()[0] == '\0') {
		description.appendInt(clip->section + 1);
	}
	else {
		description.appendInt(clip->section + 1);
		description.append(": ");
		description.append(clip->name.get());
	}
	description.append(")");
}

void drawCenteredValue(int32_t value, const SlotPosition& slot) {
	DEF_STACK_STRING_BUF(valueText, 8);
	valueText.appendInt(value);
	const int32_t textWidth = OLED::main.getStringWidthInPixels(valueText.c_str(), kTextTitleSizeY);
	const int32_t x = slot.start_x + std::max<int32_t>(0, (slot.width - textWidth) >> 1);
	const int32_t y = slot.start_y + std::max<int32_t>(0, (slot.height - kTextTitleSizeY) >> 1);
	OLED::main.drawString(valueText.c_str(), x, y, kTextSpacingX, kTextTitleSizeY);
}

} // namespace

ClipList::ClipList(l10n::String newName, l10n::String newTitle, Mode newMode, const char* newColumnLabel)
    : MenuItem(newName, newTitle), mode_(newMode), columnLabel_(newColumnLabel) {
}

void ClipList::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	showingDescriptionOn7Seg_ = false;
	refreshEntries();
	drawValue();
	scheduleTimer();
}

void ClipList::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	MenuItem::endSession();
}

void ClipList::readCurrentValue() {
	currentValue_ = getSummaryCount();
}

void ClipList::drawPixelsForOled() {
	drawOledRows();
}

void ClipList::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawSevenSegmentValue();
	}
}

void ClipList::drawName() {
	readCurrentValue();
	DEF_STACK_STRING_BUF(text, 16);
	text.append(columnLabel_);
	text.append(" ");
	text.appendInt(currentValue_);
	display->setScrollingText(text.c_str());
}

void ClipList::selectEncoderAction(int32_t offset) {
	if (entries_.empty() || offset == 0) {
		return;
	}

	Clip* selectedClip = nullptr;
	if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int32_t>(entries_.size())) {
		selectedClip = entries_[selectedIndex_].clip;
	}

	refreshEntries(selectedClip);
	setSelectedIndex(selectedIndex_ + offset);
	lastSelectionMoveTime_ = AudioEngine::audioSampleTimer;
	showingDescriptionOn7Seg_ = true;
	drawValue();
}

MenuItem* ClipList::selectButtonPress() {
	return NO_NAVIGATION;
}

ActionResult ClipList::timerCallback() {
	Clip* selectedClip = nullptr;
	if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int32_t>(entries_.size())) {
		selectedClip = entries_[selectedIndex_].clip;
	}

	refreshEntries(selectedClip);

	if (display->have7SEG() && showingDescriptionOn7Seg_) {
		if (AudioEngine::audioSampleTimer - lastSelectionMoveTime_ < kShowDescriptionDurationSamples) {
			scheduleTimer();
			return ActionResult::DEALT_WITH;
		}

		showingDescriptionOn7Seg_ = false;
	}

	drawValue();
	scheduleTimer();
	return ActionResult::DEALT_WITH;
}

bool ClipList::isSubmenu() {
	return true;
}

bool ClipList::selectEncoderActionIsPermitted() {
	return false;
}

bool ClipList::showNotification() const {
	return false;
}

void ClipList::getColumnLabel(StringBuf& label) {
	label.append(columnLabel_);
}

void ClipList::getNotificationValue(StringBuf& valueBuf) {
	valueBuf.appendInt(currentValue_);
}

void ClipList::renderInHorizontalMenu(const SlotPosition& slot) {
	readCurrentValue();
	drawCenteredValue(currentValue_, slot);
}

void ClipList::refreshEntries(Clip* clipToKeepSelected) {
	const int32_t previousIndex = selectedIndex_;
	entries_.clear();
	currentValue_ = getSummaryCount();

	if (currentSong != nullptr) {
		int32_t sortIndex = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			if (!isActiveClip(clip)) {
				++sortIndex;
				continue;
			}

			Output* output = clip->output;
			if (output == nullptr || !includesOutputType(mode_, output->type)) {
				++sortIndex;
				continue;
			}

			const int32_t voiceCount = countClipVoicesForType(clip, output->type);
			entries_.push_back({clip, output->type, voiceCount, sortIndex});
			++sortIndex;
		}
	}

	std::sort(entries_.begin(), entries_.end(), [this](const Entry& lhs, const Entry& rhs) {
		if (mode_ == Mode::AUDIO) {
			return lhs.sortIndex < rhs.sortIndex;
		}
		if (lhs.voiceCount != rhs.voiceCount) {
			return lhs.voiceCount > rhs.voiceCount;
		}
		return lhs.sortIndex < rhs.sortIndex;
	});

	preserveSelection(clipToKeepSelected, previousIndex);
}

void ClipList::drawOledRows() {
	if (entries_.empty()) {
		const char* message = l10n::get(l10n::String::STRING_FOR_NO_ACTIVE_CLIPS);
		OLED::main.drawStringCentered(message, 0, 18, kTextSpacingX, kTextSpacingY, OLED_MAIN_WIDTH_PIXELS);
		return;
	}

	constexpr int32_t maxVisible = OLED_HEIGHT_CHARS - 1;
	const int32_t baseY = ((OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14) + OLED_MAIN_TOPMOST_PIXEL;
	const int32_t numEntries = entries_.size();
	setSelectedIndex(selectedIndex_);
	const int32_t selectedIndex = selectedIndex_;
	const int32_t numVisible = std::min(maxVisible, numEntries);
	int32_t firstVisible = selectedIndex - (numVisible >> 1);
	firstVisible = std::clamp<int32_t>(firstVisible, 0, std::max<int32_t>(0, numEntries - numVisible));

	for (int32_t visibleIndex = 0; visibleIndex < numVisible; ++visibleIndex) {
		const int32_t entryIndex = firstVisible + visibleIndex;
		const Entry& entry = entries_[entryIndex];
		const int32_t y = visibleIndex * kTextSpacingY + baseY;

		DEF_STACK_STRING_BUF(description, 128);
		formatEntryDescription(entry, description);

		DEF_STACK_STRING_BUF(countText, 4);
		countText.appendInt(entry.voiceCount, 2);

		OLED::main.drawString(description.c_str(), kTextSpacingX, y, kTextSpacingX, kTextSpacingY, 0,
		                      kOledDescriptionEndX);
		OLED::main.drawStringAlignRight(countText.c_str(), y, kTextSpacingX, kTextSpacingY, kOledVoiceCountRight);

		if (entryIndex == selectedIndex) {
			OLED::main.invertArea(0, y, OLED_MAIN_WIDTH_PIXELS, kTextSpacingY);
			OLED::setupSideScroller(0, description.c_str(), kTextSpacingX, kOledDescriptionEndX, y, y + kTextSpacingY,
			                        kTextSpacingX, kTextSpacingY, true);
		}
	}
}

void ClipList::drawSevenSegmentValue() {
	if (entries_.empty()) {
		display->setText(l10n::get(l10n::String::STRING_FOR_NO_ACTIVE_CLIPS));
		return;
	}

	if (showingDescriptionOn7Seg_) {
		DEF_STACK_STRING_BUF(description, 128);
		formatSelectedDescription(description);
		display->setScrollingText(description.c_str(), 0, 300, 1);
	}
	else {
		DEF_STACK_STRING_BUF(value, 4);
		value.appendInt(getSelectedVoiceCount(), 2);
		display->setText(value.c_str(), true);
	}
}

void ClipList::setSelectedIndex(int32_t newIndex) {
	if (entries_.empty()) {
		selectedIndex_ = 0;
		return;
	}

	selectedIndex_ = std::clamp<int32_t>(newIndex, 0, static_cast<int32_t>(entries_.size()) - 1);
}

void ClipList::preserveSelection(Clip* clipToKeepSelected, int32_t previousIndex) {
	if (entries_.empty()) {
		selectedIndex_ = 0;
		return;
	}

	if (clipToKeepSelected != nullptr) {
		for (int32_t i = 0; i < static_cast<int32_t>(entries_.size()); ++i) {
			if (entries_[i].clip == clipToKeepSelected) {
				selectedIndex_ = i;
				return;
			}
		}
	}

	selectedIndex_ = std::clamp<int32_t>(previousIndex, 0, static_cast<int32_t>(entries_.size()) - 1);
}

int32_t ClipList::getSummaryCount() const {
	return countClipsForMode(mode_);
}

int32_t ClipList::getSelectedVoiceCount() const {
	if (entries_.empty()) {
		return 0;
	}
	if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int32_t>(entries_.size())) {
		return 0;
	}

	return entries_[selectedIndex_].voiceCount;
}

void ClipList::formatSelectedDescription(StringBuf& description) const {
	if (entries_.empty()) {
		return;
	}
	if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int32_t>(entries_.size())) {
		return;
	}

	formatEntryDescription(entries_[selectedIndex_], description);
}

void ClipList::formatEntryDescription(const Entry& entry, StringBuf& description) const {
	appendClipDescription(entry.clip, entry.clip->output, mode_ == Mode::TOTAL, description);
}

void ClipList::scheduleTimer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kGraphicsSyncFallbackMs);
}

VoiceUsageMenu::VoiceUsageMenu(l10n::String newName, l10n::String newTitle, std::span<MenuItem*> newItems,
                               MenuItem* defaultItem)
    : HorizontalMenu(newName, newTitle, newItems) {
	auto candidate = std::find(items.begin(), items.end(), defaultItem);
	if (candidate != items.end()) {
		initial_index_ = candidate - items.begin();
	}
}

void VoiceUsageMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	HorizontalMenu::beginSession(navigatedBackwardFrom);
	refreshChildValues();
	if (display->have7SEG()) {
		updateDisplay();
	}
	else {
		renderUIsForOled();
	}
	scheduleTimer();
}

void VoiceUsageMenu::endSession() {
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	HorizontalMenu::endSession();
}

void VoiceUsageMenu::selectEncoderAction(int32_t offset) {
	if (renderingStyle() != HORIZONTAL) {
		HorizontalMenu::selectEncoderAction(offset);
	}
}

ActionResult VoiceUsageMenu::timerCallback() {
	refreshChildValues();
	if (display->have7SEG()) {
		updateDisplay();
	}
	else {
		renderUIsForOled();
	}
	scheduleTimer();
	return ActionResult::DEALT_WITH;
}

void VoiceUsageMenu::refreshChildValues() {
	for (MenuItem* item : items) {
		if (item != nullptr) {
			item->readCurrentValue();
		}
	}
}

void VoiceUsageMenu::scheduleTimer() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kGraphicsSyncFallbackMs);
}

} // namespace deluge::gui::menu_item::voice_usage
