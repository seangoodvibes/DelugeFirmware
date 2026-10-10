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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::arpeggiator {
class Mode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(sound_editor_for_session().currentArpSettings->mode); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ArpMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR
		    && sound_editor_for_session().editingKitRow()) {

			Kit* kit = getCurrentKit();

			// If was off, or is now becoming off...
			if (sound_editor_for_session().currentArpSettings->mode == ArpMode::OFF || current_value == ArpMode::OFF) {
				if (getCurrentClip()->isActiveOnOutput() && !sound_editor_for_session().editingKitAffectEntire()) {
					kit->cutAllSound();
				}
			}

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				thisDrum->arpSettings.mode = current_value;
				thisDrum->arpSettings.updatePresetFromCurrentSettings();
			}
		}
		// Or, the normal case of just one sound
		else {
			// If was off, or is now becoming off...
			if (sound_editor_for_session().currentArpSettings->mode == ArpMode::OFF || current_value == ArpMode::OFF) {
				if (getCurrentClip()->isActiveOnOutput() && !sound_editor_for_session().editingKitAffectEntire()) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithThreeMainThings* modelStack =
					    sound_editor_for_session().getCurrentModelStack(modelStackMemory);

					if (sound_editor_for_session().editingKit()) {
						// Drum
						Drum* currentDrum = ((Kit*)getCurrentClip()->output)->selected_drum_for_session();
						if (currentDrum != nullptr) {
							currentDrum->killAllVoices();
						}
					}
					else if (sound_editor_for_session().editingCVOrMIDIClip()) {
						getCurrentInstrumentClip()->stopAllNotesForMIDIOrCV(modelStack->toWithTimelineCounter());
					}
					else {
						ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
						sound_editor_for_session().currentSound->allNotesOff(
						    modelStackWithSoundFlags,
						    sound_editor_for_session()
						        .currentSound->getArp()); // Must switch off all notes when switching arp on / off
						sound_editor_for_session().currentSound->reassessRenderSkippingStatus(modelStackWithSoundFlags);
					}
				}
			}

			sound_editor_for_session().currentArpSettings->mode = current_value;
			sound_editor_for_session().currentArpSettings->updatePresetFromCurrentSettings();
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF), //<
		    l10n::getView(STRING_FOR_ON),  //<
		};
	}

	// flag this selection menu as a toggle menu so we can use a checkbox to toggle value
	bool isToggle() override { return true; }

	// don't enter menu on select button press
	bool shouldEnterSubmenu() override { return false; }
};
} // namespace deluge::gui::menu_item::arpeggiator
