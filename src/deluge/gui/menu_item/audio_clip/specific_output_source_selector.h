/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/context_menu/audio_input_selector.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/ui_navigation_state.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "processing/audio_output.h"

namespace deluge::gui::menu_item::audio_clip {
class SpecificSourceOutputSelector final : public MenuItem {
public:
	using MenuItem::MenuItem;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		auto* const edited_output = edited_output_for_session();
		if (!edited_output)
			return;
		if (recordable_output_index(edited_output->getOutputRecordingFrom()) < 0) {
			// Resolve against the current song; another panel may have removed or reordered tracks.
			const int32_t first_index = next_recordable_output_index(-1, 1);
			set_recording_source(*edited_output,
			                     first_index < 0 ? nullptr : currentSong->getOutputFromIndex(first_index));
		}
		refresh_shared_value();
	}

	void selectEncoderAction(int32_t offset) override {
		auto* const edited_output = edited_output_for_session();
		if (!edited_output)
			return;
		const int32_t current_index = recordable_output_index(edited_output->getOutputRecordingFrom());
		const int32_t next_index = next_recordable_output_index(current_index, offset);
		if (next_index < 0)
			return;
		set_recording_source(*edited_output, currentSong->getOutputFromIndex(next_index));
		refresh_shared_value();
	}
	void refresh_shared_value() override {
		if (display->haveOLED())
			renderUIsForOled();
		else
			drawFor7seg();
	}

	void drawPixelsForOled() override {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main_for_session();

		// track
		Output* output = selected_output_for_session();
		if (!output) {
			canvas.drawStringCentred("No track", OLED_MAIN_TOPMOST_PIXEL + 21, kTextSpacingX, kTextSpacingY);
			return;
		}

		// track type
		OutputType outputType = output->type;

		// for midi instruments, get the channel
		int32_t channel = 0;
		if (outputType == OutputType::MIDI_OUT) {
			Instrument* instrument = (Instrument*)output;
			channel = ((NonAudioInstrument*)instrument)->getChannel();
		}

		char const* outputTypeText = getOutputTypeName(outputType, channel);

		// draw the track type
		canvas.drawStringCentred(outputTypeText, OLED_MAIN_TOPMOST_PIXEL + 14, kTextSpacingX, kTextSpacingY);

		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 28;

		// draw the track name
		char const* name = output->name.get();

		int32_t stringLengthPixels = canvas.getStringWidthInPixels(name, kTextTitleSizeY);

		if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
			canvas.drawStringCentred(name, yPos, kTextTitleSpacingX, kTextTitleSizeY);
		}
		else {
			canvas.drawString(name, 0, yPos, kTextTitleSpacingX, kTextTitleSizeY);
			deluge::hid::display::OLED::setupSideScroller(0, name, 0, OLED_MAIN_WIDTH_PIXELS, yPos,
			                                              yPos + kTextTitleSizeY, kTextTitleSpacingX, kTextTitleSizeY,
			                                              false);
		}
	}

	void drawFor7seg() {
		Output* output = selected_output_for_session();
		char const* text = output ? output->name.get() : "No track";
		display->setScrollingText(text, 0);
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		auto* output = edited_output_for_session();
		return output && output->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT;
	}

	bool shouldEnterSubmenu() override { return true; }

private:
	void set_recording_source(AudioOutput& edited_output, Output* source) {
		if (edited_output.getOutputRecordingFrom() == source)
			return;
		edited_output.setOutputRecordingFrom(source);
		const auto peer =
		    ui_session::current() == ui_session::Id::Local ? ui_session::Id::Remote : ui_session::Id::Local;
		ui_session::navigation.for_owner(peer).shared_model_refresh.request();
	}

	AudioOutput* edited_output_for_session() const {
		if (!currentSong || !currentSong->getCurrentClip())
			return nullptr;
		auto* output = getCurrentOutput();
		return output && output->type == OutputType::AUDIO ? static_cast<AudioOutput*>(output) : nullptr;
	}

	int32_t recordable_output_index(Output* output) const {
		auto* const edited_output = edited_output_for_session();
		if (!edited_output || !output)
			return -1;
		int32_t index = 0;
		for (Output* candidate = currentSong->firstOutput; candidate; candidate = candidate->next, ++index) {
			// Check membership before inspecting a retained recording-source pointer.
			if (candidate == output)
				return edited_output->canRecordFrom(candidate) ? index : -1;
		}
		return -1;
	}

	Output* selected_output_for_session() const {
		auto* const edited_output = edited_output_for_session();
		if (!edited_output)
			return nullptr;
		auto* const source = edited_output->getOutputRecordingFrom();
		return recordable_output_index(source) >= 0 ? source : nullptr;
	}

	int32_t next_recordable_output_index(int32_t start_index, int32_t offset) const {
		auto* const edited_output = edited_output_for_session();
		if (!edited_output)
			return -1;
		const int32_t direction = offset > 0 ? 1 : -1;
		// Widen before negation so even INT32_MIN is well-defined.
		const uint32_t steps = offset > 0 ? offset : -static_cast<int64_t>(offset);
		const int32_t output_count = currentSong->getNumOutputs();
		int32_t selected_index = start_index;
		for (uint32_t step = 0; step < steps; ++step) {
			int32_t candidate_index = selected_index;
			while (true) {
				candidate_index += direction;
				if (candidate_index < 0 || candidate_index >= output_count)
					return selected_index;
				if (edited_output->canRecordFrom(currentSong->getOutputFromIndex(candidate_index))) {
					selected_index = candidate_index;
					break;
				}
			}
		}
		return selected_index;
	}
};
} // namespace deluge::gui::menu_item::audio_clip
