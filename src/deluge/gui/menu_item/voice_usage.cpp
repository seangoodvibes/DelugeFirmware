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
#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
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
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include <algorithm>

namespace deluge::gui::menu_item::voice_usage {

namespace {

using hid::display::OLED;

constexpr int32_t kGraphicsSyncFallbackMs = 15;
constexpr uint32_t kShowDescriptionDurationSamples = 44100;
constexpr uint32_t kPlaybackTransitionGuardSamples = 30000;
constexpr int32_t kOledVoiceCountAreaWidth = 21;
constexpr int32_t kOledVoiceCountRight = OLED_MAIN_WIDTH_PIXELS - 1;
constexpr int32_t kOledDescriptionEndX = OLED_MAIN_WIDTH_PIXELS - kOledVoiceCountAreaWidth;
constexpr bool kVoiceUsageCrashTrace = true;

void appendIntSafely(StringBuf& destination, int32_t value, int32_t minDigits = 1) {
	char valueBuffer[16] = {0};
	intToString(value, valueBuffer, minDigits);
	destination.append(valueBuffer);
}

[[nodiscard]] bool isRenderableClip(const Clip* clip) {
	return clip != nullptr;
}

[[nodiscard]] bool isActiveClip(Clip* clip) {
	return currentSong != nullptr && clip != nullptr && currentSong->isClipActive(clip);
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
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU countSynth enter");
	}
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countSynth skip clip");
		}
		return 0;
	}

	Output* output = clip->output;
	if (output == nullptr || output->type != OutputType::SYNTH) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countSynth skip output");
		}
		return 0;
	}

	auto* soundInstrument = static_cast<const SoundInstrument*>(output);
	CriticalSectionGuard guard;
	int32_t voices = static_cast<int32_t>(soundInstrument->numActiveVoices());
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU countSynth voices %d", voices);
	}
	return voices;
}

[[nodiscard]] int32_t countKitClipVoices(Clip* clip) {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU countKit enter");
	}
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countKit skip clip");
		}
		return 0;
	}

	Output* output = clip->output;
	if (output == nullptr || output->type != OutputType::KIT) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countKit skip output");
		}
		return 0;
	}

	auto* instrumentClip = static_cast<InstrumentClip*>(clip);
	int32_t count = 0;
	for (int32_t i = 0; i < instrumentClip->noteRows.getNumElements(); ++i) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countKit row %d", i);
		}
		NoteRow* noteRow = instrumentClip->noteRows.getElement(i);
		if (noteRow == nullptr || noteRow->drum == nullptr || noteRow->drum->type != DrumType::SOUND) {
			continue;
		}

		auto* soundDrum = static_cast<const SoundDrum*>(noteRow->drum);
		CriticalSectionGuard guard;
		int32_t drumVoices = static_cast<int32_t>(soundDrum->numActiveVoices());
		count += drumVoices;
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countKit rowVoices %d total %d", drumVoices, count);
		}
	}
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU countKit done %d", count);
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
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU countMode enter %d", static_cast<int32_t>(mode));
	}
	if (mode == Mode::TOTAL) {
		int32_t total = AudioEngine::getNumVoices() + AudioEngine::getNumAudio();
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countMode total %d", total);
		}
		return total;
	}
	if (mode == Mode::AUDIO) {
		int32_t audio = AudioEngine::getNumAudio();
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countMode audio %d", audio);
		}
		return audio;
	}
	if (currentSong == nullptr) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countMode no song");
		}
		return 0;
	}

	int32_t count = 0;
	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countMode output type %d", static_cast<int32_t>(output->type));
		}
		if (!includesOutputType(mode, output->type)) {
			continue;
		}
		Clip* clip = output->getActiveClip();
		if (clip == nullptr || !currentSong->isClipActive(clip)) {
			if constexpr (kVoiceUsageCrashTrace) {
				D_PRINTLN("VU countMode inactive");
			}
			continue;
		}

		count += countClipVoicesForType(clip, output->type);
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU countMode running %d", count);
		}
	}
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU countMode done %d", count);
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
		appendIntSafely(description, clip->section + 1);
	}
	else {
		appendIntSafely(description, clip->section + 1);
		description.append(": ");
		description.append(clip->name.get());
	}
	description.append(")");
}

void copyEntryDescription(char* destination, size_t destinationSize, const Clip* clip, const Output* output,
                          bool includePrefix) {
	if (destination == nullptr || destinationSize == 0) {
		return;
	}

	StringBuf description(destination, destinationSize);
	appendClipDescription(clip, output, includePrefix, description);
}

void drawCenteredValue(int32_t value, const SlotPosition& slot) {
	DEF_STACK_STRING_BUF(valueText, 8);
	appendIntSafely(valueText, value);
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
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU beginSession mode %d", static_cast<int32_t>(mode_));
	}
	MenuItem::beginSession(navigatedBackwardFrom);
	showingDescriptionOn7Seg_ = false;
	sevenSegDescriptionActive_ = false;
	lastSevenSegDescriptionClip_ = nullptr;
	oledScrollerActive_ = false;
	lastOledScrollerClip_ = nullptr;
	lastPlaybackState_ = playbackHandler.playbackState;
	playbackTransitionGuardUntilSamples_ = 0;
	refreshEntries();
	drawValue();
	scheduleTimer();
}

void ClipList::endSession() {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU endSession");
	}
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	sevenSegDescriptionActive_ = false;
	lastSevenSegDescriptionClip_ = nullptr;
	oledScrollerActive_ = false;
	lastOledScrollerClip_ = nullptr;
	MenuItem::endSession();
}

void ClipList::readCurrentValue() {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU readCurrentValue enter now %u", AudioEngine::audioSampleTimer);
	}
	const bool playbackState = playbackHandler.playbackState;
	if (playbackState != lastPlaybackState_) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU readCurrentValue playback transition %d->%d", static_cast<int32_t>(lastPlaybackState_),
			          static_cast<int32_t>(playbackState));
		}
		lastPlaybackState_ = playbackState;
		playbackTransitionGuardUntilSamples_ = AudioEngine::audioSampleTimer + kPlaybackTransitionGuardSamples;
		return;
	}

	if (playbackTransitionGuardUntilSamples_ != 0
	    && AudioEngine::audioSampleTimer < playbackTransitionGuardUntilSamples_) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU readCurrentValue in guard until %u", playbackTransitionGuardUntilSamples_);
		}
		return;
	}

	playbackTransitionGuardUntilSamples_ = 0;
	currentValue_ = getSummaryCount();
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU readCurrentValue done value %d", currentValue_);
	}
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
	appendIntSafely(text, currentValue_);
	display->setScrollingText(text.c_str());
}

void ClipList::selectEncoderAction(int32_t offset) {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU selectEncoderAction offset %d entries %d", offset, static_cast<int32_t>(entries_.size()));
	}
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
	sevenSegDescriptionActive_ = false;
	lastSevenSegDescriptionClip_ = nullptr;
	oledScrollerActive_ = false;
	lastOledScrollerClip_ = nullptr;
	drawValue();
}

MenuItem* ClipList::selectButtonPress() {
	return NO_NAVIGATION;
}

ActionResult ClipList::timerCallback() {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU timer enter now %u entries %d", AudioEngine::audioSampleTimer,
		          static_cast<int32_t>(entries_.size()));
	}
	const bool playbackState = playbackHandler.playbackState;
	if (playbackState != lastPlaybackState_) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU timer playback transition %d->%d", static_cast<int32_t>(lastPlaybackState_),
			          static_cast<int32_t>(playbackState));
		}
		lastPlaybackState_ = playbackState;
		playbackTransitionGuardUntilSamples_ = AudioEngine::audioSampleTimer + kPlaybackTransitionGuardSamples;
		scheduleTimer();
		return ActionResult::DEALT_WITH;
	}

	if (playbackTransitionGuardUntilSamples_ != 0
	    && AudioEngine::audioSampleTimer < playbackTransitionGuardUntilSamples_) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU timer in guard until %u", playbackTransitionGuardUntilSamples_);
		}
		scheduleTimer();
		return ActionResult::DEALT_WITH;
	}

	playbackTransitionGuardUntilSamples_ = 0;

	const int32_t previousSummaryCount = currentValue_;
	const int32_t previousSelectedVoiceCount = getSelectedVoiceCount();
	const int32_t previousEntryCount = static_cast<int32_t>(entries_.size());
	Clip* previousSelectedClip = getSelectedClip();

	refreshEntries(previousSelectedClip);

	const bool listChanged =
	    previousEntryCount != static_cast<int32_t>(entries_.size()) || previousSelectedClip != getSelectedClip();
	const bool valueChanged =
	    previousSummaryCount != currentValue_ || previousSelectedVoiceCount != getSelectedVoiceCount();
	bool shouldRedraw = listChanged || valueChanged;

	if (display->have7SEG() && showingDescriptionOn7Seg_) {
		if (AudioEngine::audioSampleTimer - lastSelectionMoveTime_ < kShowDescriptionDurationSamples) {
			if (shouldRedraw) {
				drawValue();
			}
			scheduleTimer();
			return ActionResult::DEALT_WITH;
		}

		showingDescriptionOn7Seg_ = false;
		shouldRedraw = true;
	}

	if (shouldRedraw) {
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU timer redraw listChanged %d valueChanged %d", static_cast<int32_t>(listChanged),
			          static_cast<int32_t>(valueChanged));
		}
		drawValue();
	}
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU timer exit");
	}
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
	appendIntSafely(valueBuf, currentValue_);
}

void ClipList::renderInHorizontalMenu(const SlotPosition& slot) {
	readCurrentValue();
	drawCenteredValue(currentValue_, slot);
}

void ClipList::refreshEntries(Clip* clipToKeepSelected) {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU refresh enter keep %d", clipToKeepSelected != nullptr ? 1 : 0);
	}
	const int32_t previousIndex = selectedIndex_;
	entries_.clear();
	if (currentSong != nullptr) {
		int32_t reserveCount = 0;
		for (Output* output = currentSong->firstOutput; output; output = output->next) {
			if (includesOutputType(mode_, output->type) && output->getActiveClip() != nullptr) {
				++reserveCount;
			}
		}
		if (reserveCount > static_cast<int32_t>(entries_.capacity())) {
			entries_.reserve(reserveCount);
		}
	}

	if (currentSong != nullptr) {
		int32_t sortIndex = 0;
		for (Output* output = currentSong->firstOutput; output; output = output->next) {
			if constexpr (kVoiceUsageCrashTrace) {
				D_PRINTLN("VU refresh output sort %d type %d", sortIndex, static_cast<int32_t>(output->type));
			}
			if (!includesOutputType(mode_, output->type)) {
				++sortIndex;
				continue;
			}

			Clip* clip = output->getActiveClip();
			if (clip == nullptr || !currentSong->isClipActive(clip)) {
				if constexpr (kVoiceUsageCrashTrace) {
					D_PRINTLN("VU refresh skip inactive sort %d", sortIndex);
				}
				++sortIndex;
				continue;
			}

			int32_t voiceCount = countClipVoicesForType(clip, output->type);
			if constexpr (kVoiceUsageCrashTrace) {
				D_PRINTLN("VU refresh add sort %d voices %d", sortIndex, voiceCount);
			}
			Entry entry = {clip, output->type, voiceCount, sortIndex, {0}};
			copyEntryDescription(entry.description, sizeof(entry.description), clip, output, mode_ == Mode::TOTAL);
			entries_.push_back(entry);
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
	currentValue_ = getSummaryCount();
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU refresh summary %d entries %d", currentValue_, static_cast<int32_t>(entries_.size()));
	}

	preserveSelection(clipToKeepSelected, previousIndex);
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU refresh selected %d", selectedIndex_);
	}
}

void ClipList::drawOledRows() {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU drawOledRows enter entries %d", static_cast<int32_t>(entries_.size()));
	}
	if (entries_.empty()) {
		const char* message = l10n::get(l10n::String::STRING_FOR_NO_ACTIVE_CLIPS);
		OLED::main.drawStringCentered(message, 0, 18, kTextSpacingX, kTextSpacingY, OLED_MAIN_WIDTH_PIXELS);
		oledScrollerActive_ = false;
		lastOledScrollerClip_ = nullptr;
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
		if constexpr (kVoiceUsageCrashTrace) {
			D_PRINTLN("VU draw row vis %d entry %d voices %d", visibleIndex, entryIndex, entry.voiceCount);
		}
		if (!isRenderableClip(entry.clip)) {
			FREEZE_WITH_ERROR("VU02");
			continue;
		}
		const int32_t y = visibleIndex * kTextSpacingY + baseY;

		DEF_STACK_STRING_BUF(countText, 4);
		appendIntSafely(countText, entry.voiceCount, 2);

		OLED::main.drawString(entry.description, kTextSpacingX, y, kTextSpacingX, kTextSpacingY, 0,
		                      kOledDescriptionEndX);
		OLED::main.drawStringAlignRight(countText.c_str(), y, kTextSpacingX, kTextSpacingY, kOledVoiceCountRight);

		if (entryIndex == selectedIndex) {
			if constexpr (kVoiceUsageCrashTrace) {
				D_PRINTLN("VU draw selected entry %d", entryIndex);
			}
			OLED::main.invertArea(0, y, OLED_MAIN_WIDTH_PIXELS, kTextSpacingY);
			//	if (!oledScrollerActive_ || lastOledScrollerClip_ != entry.clip) {
			//	OLED::setupSideScroller(0, entry.description, kTextSpacingX, kOledDescriptionEndX, y,
			//	                        y + kTextSpacingY, kTextSpacingX, kTextSpacingY, true);
			//	oledScrollerActive_ = true;
			//	lastOledScrollerClip_ = entry.clip;
			//	}
		}
	}
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU drawOledRows exit");
	}
}

void ClipList::drawSevenSegmentValue() {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU draw7seg enter entries %d showDesc %d", static_cast<int32_t>(entries_.size()),
		          static_cast<int32_t>(showingDescriptionOn7Seg_));
	}
	if (entries_.empty()) {
		display->setText(l10n::get(l10n::String::STRING_FOR_NO_ACTIVE_CLIPS));
		sevenSegDescriptionActive_ = false;
		lastSevenSegDescriptionClip_ = nullptr;
		return;
	}

	if (showingDescriptionOn7Seg_) {
		Clip* selectedClip = getSelectedClip();
		if (selectedClip == nullptr) {
			sevenSegDescriptionActive_ = false;
			lastSevenSegDescriptionClip_ = nullptr;
			return;
		}

		if (!sevenSegDescriptionActive_ || lastSevenSegDescriptionClip_ != selectedClip) {
			DEF_STACK_STRING_BUF(description, 128);
			formatSelectedDescription(description);
			display->setScrollingText(description.c_str(), 0, 300, 1);
			sevenSegDescriptionActive_ = true;
			lastSevenSegDescriptionClip_ = selectedClip;
		}
	}
	else {
		sevenSegDescriptionActive_ = false;
		lastSevenSegDescriptionClip_ = nullptr;
		DEF_STACK_STRING_BUF(value, 4);
		appendIntSafely(value, getSelectedVoiceCount(), 2);
		display->setText(value.c_str(), true);
	}
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU draw7seg exit");
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

Clip* ClipList::getSelectedClip() const {
	if (entries_.empty()) {
		return nullptr;
	}
	if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int32_t>(entries_.size())) {
		return nullptr;
	}

	return entries_[selectedIndex_].clip;
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
	description.append(entry.description);
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
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU menu begin");
	}
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
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU menu end");
	}
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	HorizontalMenu::endSession();
}

void VoiceUsageMenu::selectEncoderAction(int32_t offset) {
	if (renderingStyle() != HORIZONTAL) {
		HorizontalMenu::selectEncoderAction(offset);
	}
}

ActionResult VoiceUsageMenu::timerCallback() {
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU menu timer enter");
	}
	refreshChildValues();
	if (display->have7SEG()) {
		updateDisplay();
	}
	else {
		renderUIsForOled();
	}
	scheduleTimer();
	if constexpr (kVoiceUsageCrashTrace) {
		D_PRINTLN("VU menu timer exit");
	}
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
