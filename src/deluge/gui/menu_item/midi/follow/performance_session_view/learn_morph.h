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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/performance_session_view.h"
#include "io/midi/midi_engine.h"

namespace deluge::gui::menu_item::midi {

class LearnMorph final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(midiEngine.midiFollowPerformanceViewMorphModeCCNumber); }
	void writeCurrentValue() override { midiEngine.midiFollowPerformanceViewMorphModeCCNumber = this->getValue(); }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMIDIValue; }
	bool allowsLearnMode() override { return true; }

	void learnCC(MIDIDevice* device, int32_t channel, int32_t ccNumber, int32_t value) {
		this->setValue(ccNumber);
		midiEngine.midiFollowPerformanceViewMorphModeCCNumber = ccNumber;

		if (soundEditor.getCurrentMenuItem() == this) {
			renderDisplay();
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
		}
	}

	void unlearnAction() {
		this->setValue(MIDI_CC_NONE);
		midiEngine.midiFollowPerformanceViewMorphModeCCNumber = MIDI_CC_NONE;
		if (soundEditor.getCurrentMenuItem() == this) {
			renderDisplay();
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNLEARNED));
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (this->getValue() == MIDI_CC_NONE) {
			if (offset > 0) {
				this->setValue(0);
			}
			else if (offset < 0) {
				this->setValue(kMaxMIDIValue);
			}
		}
		else {
			this->setValue(this->getValue() + offset);
			if ((this->getValue() > kMaxMIDIValue) || (this->getValue() < 0)) {
				this->setValue(MIDI_CC_NONE);
				midiEngine.midiFollowPerformanceViewMorphModeCCNumber = MIDI_CC_NONE;
				renderDisplay();
				return;
			}
		}
		Number::selectEncoderAction(offset);
	}

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
		char buffer[12];
		char const* text;
		if (this->getValue() == MIDI_CC_NONE) {
			text = l10n::get(l10n::String::STRING_FOR_NONE);
		}
		else {
			intToString(this->getValue(), buffer, 1);
			text = buffer;
		}
		deluge::hid::display::OLED::drawStringCentred(text, yPixel + OLED_MAIN_TOPMOST_PIXEL,
		                                              deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, textWidth, textHeight);
	}

	void drawValue() override {
		if (this->getValue() == MIDI_CC_NONE) {
			display->setText(l10n::get(l10n::String::STRING_FOR_NONE));
		}
		else {
			display->setTextAsNumber(this->getValue());
		}
	}

	void renderDisplay() {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
};
} // namespace deluge::gui::menu_item::midi
