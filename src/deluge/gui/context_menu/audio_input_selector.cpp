/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/audio_input_selector.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/views/session_view.h"
#include "model/song/song.h"
#include "processing/audio_output.h"

extern AudioInputChannel defaultAudioOutputInputChannel;

namespace deluge::gui::context_menu {

enum class AudioInputSelector::Value {
	OFF,
	LEFT,
	RIGHT,
	STEREO,
	BALANCED,
	MASTER,
	OUTPUT,
	TRACK,
};
constexpr size_t kNumValues = 8;

namespace {
AudioInputSelector local_audio_input_selector{};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<AudioInputSelector> remote_audio_input_selector;
} // namespace
AudioInputSelector& audio_input_selector_for_session() {
	return remote_audio_input_selector.get(local_audio_input_selector);
}

namespace {
// A saved source pointer can become stale if its instrument leaves the active song list, e.g. by being hibernated.
Output* getRecordableOutputInSong(AudioOutput* audioOutput, Output* selectedOutput) {
	if (!currentSong || !audioOutput || !selectedOutput) {
		return nullptr;
	}

	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (output == selectedOutput) {
			return audioOutput->canRecordFrom(output) ? output : nullptr;
		}
	}

	return nullptr;
}

// Used when entering Track mode without a valid previous source.
Output* getFirstRecordableOutput(AudioOutput* audioOutput) {
	if (!currentSong || !audioOutput)
		return nullptr;
	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (audioOutput->canRecordFrom(output)) {
			return output;
		}
	}

	return nullptr;
}
void request_peer_input_refresh() {
	const auto peer = ui_session::current() == ui_session::Id::Local ? ui_session::Id::Remote : ui_session::Id::Local;
	ui_session::navigation.for_owner(peer).shared_model_refresh.request();
}
} // namespace

char const* AudioInputSelector::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_AUDIO_SOURCE);
}

std::span<const char*> AudioInputSelector::getOptions() {
	using enum l10n::String;
	static const char* options[] = {
	    l10n::get(STRING_FOR_DISABLED),     l10n::get(STRING_FOR_LEFT_INPUT),     l10n::get(STRING_FOR_RIGHT_INPUT),
	    l10n::get(STRING_FOR_STEREO_INPUT), l10n::get(STRING_FOR_BALANCED_INPUT), l10n::get(STRING_FOR_MIX_PRE_FX),
	    l10n::get(STRING_FOR_MIX_POST_FX),  l10n::get(STRING_FOR_TRACK),
	};
	return {options, kNumValues};
}

bool AudioInputSelector::setupAndCheckAvailability() {
	if (!read_input_selection())
		return false;
	scrollPos = currentOption;
	return true;
}

void AudioInputSelector::refresh_shared_model() {
	if (!read_input_selection())
		return;
	// Track names can change even when the channel selection stays the same.
	if (display->haveOLED())
		renderUIsForOled();
	else
		drawCurrentOption();
}

bool AudioInputSelector::has_current_output() const {
	if (!currentSong || !audioOutput)
		return false;
	// Check membership before reading a retained target. It may have left the
	// song while the other panel was editing, deleting or restoring clips.
	for (auto* output = currentSong->firstOutput; output; output = output->next) {
		if (output == audioOutput)
			return output->type == OutputType::AUDIO;
	}
	return false;
}

bool AudioInputSelector::read_input_selection() {
	if (!has_current_output())
		return false;
	Value valueOption = Value::OFF;

	switch (audioOutput->inputChannel) {
	case AudioInputChannel::LEFT:
		valueOption = Value::LEFT;
		break;

	case AudioInputChannel::RIGHT:
		valueOption = Value::RIGHT;
		break;

	case AudioInputChannel::STEREO:
		valueOption = Value::STEREO;
		break;

	case AudioInputChannel::BALANCED:
		valueOption = Value::BALANCED;
		break;

	case AudioInputChannel::MIX:
		valueOption = Value::MASTER;
		break;

	case AudioInputChannel::OUTPUT:
		valueOption = Value::OUTPUT;
		break;

	case AudioInputChannel::SPECIFIC_OUTPUT:
		valueOption = Value::TRACK;
		break;

	default:
		valueOption = Value::OFF;
	}

	if (currentOption != static_cast<int32_t>(valueOption)) {
		currentOption = static_cast<int32_t>(valueOption);
		scrollPos = currentOption;
	}
	return true;
}

bool AudioInputSelector::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	auto* const root_ui = getRootUI();
	if (!root_ui || !has_current_output())
		return false;
	*rows = root_ui->getGreyedOutRowsNotRepresentingOutput(audioOutput);
	return true;
}

void AudioInputSelector::selectEncoderAction(int8_t offset) {
	if (currentUIMode != 0u || !read_input_selection()) {
		return;
	}

	auto* const source_song = currentSong;
	auto* const source_output = audioOutput;
	const auto source_owner = ui_session::current();
	const auto previous_channel = audioOutput->inputChannel;
	auto* const previous_source = audioOutput->getOutputRecordingFrom();
	ContextMenu::selectEncoderAction(offset);
	// Seven-segment feedback can service callbacks before the routing write.
	if (ui_session::current() != source_owner || currentSong != source_song || audioOutput != source_output
	    || !has_current_output() || source_output->inputChannel != previous_channel
	    || source_output->getOutputRecordingFrom() != previous_source) {
		return;
	}

	auto valueOption = static_cast<Value>(currentOption);
	if (display->haveOLED() && valueOption == Value::TRACK) {
		// Keep Track on the first visible row so the selected track name has room below it.
		scrollPos = currentOption;
	}

	// When switching away from SPECIFIC_OUTPUT, clear the recording-from state
	// so the previously-selected track is no longer silently muted
	if (audioOutput->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT && valueOption != Value::TRACK) {
		audioOutput->clearRecordingFrom();
	}

	switch (valueOption) {

	case Value::LEFT:
		audioOutput->inputChannel = AudioInputChannel::LEFT;
		break;

	case Value::RIGHT:
		audioOutput->inputChannel = AudioInputChannel::RIGHT;
		break;

	case Value::STEREO:
		audioOutput->inputChannel = AudioInputChannel::STEREO;
		break;

	case Value::BALANCED:
		audioOutput->inputChannel = AudioInputChannel::BALANCED;
		break;

	case Value::MASTER:
		audioOutput->inputChannel = AudioInputChannel::MIX;
		break;

	case Value::OUTPUT:
		audioOutput->inputChannel = AudioInputChannel::OUTPUT;
		break;
	case Value::TRACK: {
		audioOutput->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		// Preserve the chosen source if possible; only choose a default when the previous source is gone.
		Output* recordFrom = getRecordableOutputInSong(audioOutput, audioOutput->getOutputRecordingFrom());
		if (!recordFrom) {
			recordFrom = getFirstRecordableOutput(audioOutput);
		}
		audioOutput->setOutputRecordingFrom(recordFrom);
		break;
	}

	default:
		audioOutput->inputChannel = AudioInputChannel::NONE;
	}

	defaultAudioOutputInputChannel = audioOutput->inputChannel;
	if (audioOutput->inputChannel != previous_channel || audioOutput->getOutputRecordingFrom() != previous_source)
		request_peer_input_refresh();

	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

// if they're in session view and press a clip's pad, record from that output
ActionResult AudioInputSelector::padAction(int32_t x, int32_t y, int32_t on) {
	if (on && sdRoutineLock)
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	if (on && has_current_output() && getUIUpOneLevel() == &session_view_for_session()) {
		auto track = (&session_view_for_session())->getOutputFromPad(x, y);
		if (audioOutput->canRecordFrom(track)) {
			const bool changed = audioOutput->inputChannel != AudioInputChannel::SPECIFIC_OUTPUT
			                     || audioOutput->getOutputRecordingFrom() != track;
			audioOutput->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
			audioOutput->setOutputRecordingFrom(track);
			if (changed)
				request_peer_input_refresh();
			if (display->have7SEG()) {
				// OLED shows this persistently in renderOLED(); 7SEG still needs popup feedback.
				display->popupTextTemporary(track->name.get());
			}
			// sets scroll to the position of specific output
			scrollPos = static_cast<int32_t>(Value::TRACK);
			currentOption = scrollPos;
			renderUIsForOled();
		}
		else if (track && track == audioOutput) {
			display->popupTextTemporary("Can't record self!");
		}
		else if (track) {
			display->popupTextTemporary("Can't record MIDI or CV!");
		}

		return ActionResult::DEALT_WITH;
	}
	return ContextMenu::padAction(x, y, on);
}

void AudioInputSelector::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (!read_input_selection())
		return;
	ContextMenu::renderOLED(canvas);

	if (audioOutput->inputChannel != AudioInputChannel::SPECIFIC_OUTPUT) {
		return;
	}

	// Show the selected target in the context menu footer.
	Output* recordFrom = getRecordableOutputInSong(audioOutput, audioOutput->getOutputRecordingFrom());
	char const* trackName = recordFrom ? recordFrom->name.get() : "No track";

	int32_t windowHeight = 40;
	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int32_t textPixelY = windowMinY + 20 + kTextSpacingY;
	canvas.drawString(trackName, 22, textPixelY, kTextSpacingX, kTextSpacingY, 0, OLED_MAIN_WIDTH_PIXELS - 26);
}

} // namespace deluge::gui::context_menu
