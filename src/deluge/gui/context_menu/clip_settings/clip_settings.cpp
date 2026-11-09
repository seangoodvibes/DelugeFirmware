/*
 * Copyright (c) 2024 Sean Ditny
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

#include "gui/context_menu/clip_settings/clip_settings.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/clip_settings/launch_style.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/rename/rename_clip_ui.h"
#include "gui/ui/root_ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/clip_freeze.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

PLACE_SDRAM_BSS ClipSettingsMenu clipSettings{};

char const* ClipSettingsMenu::getTitle() {
	static char const* title = "Clip Settings";
	return title;
}

std::span<char const*> ClipSettingsMenu::getOptions() {
	using enum l10n::String;
	if (clip->type == ClipType::AUDIO) {
		auto* freeze = static_cast<AudioClip*>(clip)->freeze;
		static const char* options[] = {nullptr, nullptr, nullptr};
		options[0] = l10n::get(STRING_FOR_CLIP_MODE);
		options[1] = l10n::get(STRING_FOR_CLIP_NAME);
		options[2] = freeze && freeze->thawing() ? "[~] Unfreezing"
		                                         : (freeze && freeze->enabled() ? "[x] Freeze" : "[ ] Freeze");
		return options;
	}
	else if (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT) {
		auto* freeze = static_cast<InstrumentClip*>(clip)->freeze;
		static const char* options[] = {nullptr, nullptr, nullptr, nullptr};
		options[0] = l10n::get(STRING_FOR_CONVERT_TO_AUDIO);
		options[1] = l10n::get(STRING_FOR_CLIP_MODE);
		options[2] = l10n::get(STRING_FOR_CLIP_NAME);
		options[3] = freeze && freeze->thawing() ? "[~] Unfreezing"
		                                         : (freeze && freeze->enabled() ? "[x] Freeze" : "[ ] Freeze");
		return options;
	}
	else {
		static const char* optionsls[] = {
		    l10n::get(STRING_FOR_CONVERT_TO_AUDIO),
		    l10n::get(STRING_FOR_CLIP_MODE),
		    l10n::get(STRING_FOR_CLIP_NAME),
		};
		return {optionsls, 3};
	}
}

void ClipSettingsMenu::focusRegained() {
	ContextMenu::focusRegained();
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 250);
}

ActionResult ClipSettingsMenu::timerCallback() {
	// Refresh the pending label when the audio thread completes the handoff.
	// UI transitions cancel this timer, so it cannot outlive the open menu.
	drawCurrentOption();
	renderUIsForOled();
	auto* freeze = clip->type == ClipType::AUDIO ? static_cast<AudioClip*>(clip)->freeze
	                                             : static_cast<InstrumentClip*>(clip)->freeze;
	if (freeze && freeze->thawing())
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 250);
	return ActionResult::DEALT_WITH;
}

bool ClipSettingsMenu::setupAndCheckAvailability() {
	this->currentOption = scrollPos = 0; // start at the top
	return true;
}

void ClipSettingsMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
}

bool ClipSettingsMenu::acceptCurrentOption() {
	if ((clip->type == ClipType::AUDIO && currentOption == 2)
	    || (clip->type == ClipType::INSTRUMENT
	        && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)
	        && currentOption == 3)) {
		auto* freeze = clip->type == ClipType::AUDIO ? static_cast<AudioClip*>(clip)->freeze
		                                             : static_cast<InstrumentClip*>(clip)->freeze;
		bool enable = !freeze || !freeze->enabled();
		if (enable && (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip))) {
			display->displayPopup("Play clip first");
			return true;
		}
		if (enable
		    && (clip->sequenceDirectionMode != SequenceDirection::FORWARD || clip->getCurrentlyRecordingLinearly()
		        || playbackHandler.recording == RecordingMode::ARRANGEMENT)) {
			display->displayPopup("Freeze unavailable");
			return true;
		}
		char memory[MODEL_STACK_MAX_SIZE];
		auto* stack = setupModelStackWithTimelineCounter(memory, currentSong, clip);
		Error error = clip->type == ClipType::AUDIO ? static_cast<AudioClip*>(clip)->setFrozen(enable, stack)
		                                            : static_cast<InstrumentClip*>(clip)->setFrozen(enable, stack);
		if (error == Error::UNSPECIFIED) {
			display->displayPopup("Cannot freeze sound");
		}
		else if (error == Error::FILE_TOO_BIG) {
			display->displayPopup("Freeze cycle too long");
		}
		else if (error != Error::NONE) {
			display->displayError(error);
		}
		drawCurrentOption();
		renderUIsForOled();
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 250);
		return true;
	}
	if (clip->type == ClipType::INSTRUMENT && this->currentOption == 0) {
		sessionView.replaceInstrumentClipWithAudioClip(clip);
		return false; // exit UI
	}
	else {
		int32_t option = this->currentOption;
		if (clip->type == ClipType::INSTRUMENT) {
			option--; // rebase option selection to 0
		}
		if (option == 0) {
			launchStyle.clip = clip;
			launchStyle.setupAndCheckAvailability();
			openUI(&launchStyle);
		}
		else {
			currentUIMode = UI_MODE_NONE;
			renameClipUI.clip = clip;
			openUI(&renameClipUI);
		}
		return true;
	}
}

ActionResult ClipSettingsMenu::padAction(int32_t x, int32_t y, int32_t on) {
	if (on) {
		return ContextMenu::padAction(x, y, on);
	}
	else {                                      // this would happen if you release pad after entering the menu
		return sessionView.padAction(x, y, on); // let the grid handle this
	}
}

} // namespace deluge::gui::context_menu::clip_settings
