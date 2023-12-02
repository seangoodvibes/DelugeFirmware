/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "gui/views/midi_session_view.h"
#include "definitions_cxx.hpp"
#include "dsp/master_compressor/master_compressor.h"
#include "extern.h"
#include "gui/colour.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge;
using namespace gui;

const char* MIDI_DEFAULTS_XML = "MIDIFollow.XML";
const char* MIDI_DEFAULTS_TAG = "defaults";
const char* MIDI_DEFAULTS_CC_TAG = "defaultCCMappings";

//grid sized arrays to assign automatable parameters to the grid

const uint32_t patchedParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Local::OSC_A_VOLUME, Param::Local::OSC_A_PITCH_ADJUST, kNoParamID, Param::Local::OSC_A_PHASE_WIDTH,
     kNoParamID, Param::Local::CARRIER_0_FEEDBACK, Param::Local::OSC_A_WAVE_INDEX, Param::Local::NOISE_VOLUME},
    {Param::Local::OSC_B_VOLUME, Param::Local::OSC_B_PITCH_ADJUST, kNoParamID, Param::Local::OSC_B_PHASE_WIDTH,
     kNoParamID, Param::Local::CARRIER_1_FEEDBACK, Param::Local::OSC_B_WAVE_INDEX, kNoParamID},
    {Param::Local::MODULATOR_0_VOLUME, Param::Local::MODULATOR_0_PITCH_ADJUST, kNoParamID, kNoParamID, kNoParamID,
     Param::Local::MODULATOR_0_FEEDBACK, kNoParamID, kNoParamID},
    {Param::Local::MODULATOR_1_VOLUME, Param::Local::MODULATOR_1_PITCH_ADJUST, kNoParamID, kNoParamID, kNoParamID,
     Param::Local::MODULATOR_1_FEEDBACK, kNoParamID, kNoParamID},
    {Param::Global::VOLUME_POST_FX, kNoParamID, Param::Local::PITCH_ADJUST, Param::Local::PAN, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Local::FOLD},
    {Param::Local::ENV_0_RELEASE, Param::Local::ENV_0_SUSTAIN, Param::Local::ENV_0_DECAY, Param::Local::ENV_0_ATTACK,
     Param::Local::LPF_MORPH, kNoParamID, Param::Local::LPF_RESONANCE, Param::Local::LPF_FREQ},
    {Param::Local::ENV_1_RELEASE, Param::Local::ENV_1_SUSTAIN, Param::Local::ENV_1_DECAY, Param::Local::ENV_1_ATTACK,
     Param::Local::HPF_MORPH, kNoParamID, Param::Local::HPF_RESONANCE, Param::Local::HPF_FREQ},
    {kNoParamID, kNoParamID, Param::Global::VOLUME_POST_REVERB_SEND, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {Param::Global::ARP_RATE, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Global::LFO_FREQ, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Global::MOD_FX_DEPTH,
     Param::Global::MOD_FX_RATE},
    {Param::Local::LFO_LOCAL_FREQ, kNoParamID, kNoParamID, Param::Global::REVERB_AMOUNT, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {Param::Global::DELAY_RATE, kNoParamID, kNoParamID, Param::Global::DELAY_FEEDBACK, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

const uint32_t unpatchedParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::SAMPLE_RATE_REDUCTION,
     Param::Unpatched::BITCRUSHING, kNoParamID},
    {Param::Unpatched::Sound::PORTAMENTO, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::COMPRESSOR_SHAPE, kNoParamID,
     Param::Unpatched::BASS, Param::Unpatched::BASS_FREQ},
    {kNoParamID, kNoParamID, Param::Unpatched::Sound::ARP_GATE, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::TREBLE, Param::Unpatched::TREBLE_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::MOD_FX_OFFSET, Param::Unpatched::MOD_FX_FEEDBACK,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

const uint32_t globalEffectableParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Unpatched::GlobalEffectable::VOLUME, kNoParamID, Param::Unpatched::GlobalEffectable::PITCH_ADJUST,
     Param::Unpatched::GlobalEffectable::PAN, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::LPF_RES, Param::Unpatched::GlobalEffectable::LPF_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::HPF_RES, Param::Unpatched::GlobalEffectable::HPF_FREQ},
    {kNoParamID, kNoParamID, Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
    {kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {Param::Unpatched::GlobalEffectable::DELAY_RATE, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

MidiSessionView midiSessionView{};

//initialize variables
MidiSessionView::MidiSessionView() {
	successfullyReadDefaultsFromFile = false;

	anyChangesToSave = false;
	onParamDisplay = false;
	showLearnedParams = false;

	initPadPress(lastPadPress);
	initParamToCC(paramToCC);
	initParamToCC(backupXMLParamToCC);
}

void MidiSessionView::initPadPress(MidiPadPress& padPress) {
	padPress.isActive = false;
	padPress.xDisplay = kNoSelection;
	padPress.yDisplay = kNoSelection;
	padPress.paramKind = Param::Kind::NONE;
	padPress.paramID = kNoSelection;
}

void MidiSessionView::initParamToCC(uint8_t mapping[kDisplayWidth][kDisplayHeight]) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			mapping[xDisplay][yDisplay] = kNoSelection;
		}
	}
}

bool MidiSessionView::opened() {
	focusRegained();

	return true;
}

void MidiSessionView::focusRegained() {
	currentSong->affectEntire = true;

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	if (!successfullyReadDefaultsFromFile) {
		readDefaultsFromFile();
	}

	setLedStates();

	updateMappingChangeStatus();

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void MidiSessionView::graphicsRoutine() {
	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	// Nothing to do here but clear since we don't render playhead
	memset(&tickSquares, 255, sizeof(tickSquares));
	memset(&colours, 255, sizeof(colours));
	PadLEDs::setTickSquares(tickSquares, colours);
}

ActionResult MidiSessionView::timerCallback() {
	return ActionResult::DEALT_WITH;
}

bool MidiSessionView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                     uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	PadLEDs::renderingLock = true;

	// erase current image as it will be refreshed
	memset(image, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth) * 3);

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	//render midi view
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];
		int32_t imageWidth = kDisplayWidth + kSideBarWidth;

		renderRow(&image[0][0][0] + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
	}

	PadLEDs::renderingLock = false;

	return true;
}

/// render every column, one row at a time
void MidiSessionView::renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		uint8_t* pixel = image + (xDisplay * 3);

		if ((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
		    || (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
		    || (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {
			if (paramToCC[xDisplay][yDisplay] != kNoSelection) {
				if (showLearnedParams && (paramToCC[xDisplay][yDisplay] == currentCC)) {
					pixel[0] = 0;
					pixel[1] = 255;
					pixel[2] = 0;
				}
				else {
					pixel[0] = 130;
					pixel[1] = 120;
					pixel[2] = 130;
				}
			}
			else {
				pixel[0] = kUndefinedGreyShade;
				pixel[1] = kUndefinedGreyShade;
				pixel[2] = kUndefinedGreyShade;
			}

			occupancyMask[xDisplay] = 64;
		}
	}
}

/// nothing to render in sidebar (yet)
bool MidiSessionView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	return true;
}

/// render midi learning view display on opening
void MidiSessionView::renderViewDisplay() {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

		//Render "Midi Learning View" at the top of the OLED screen
		deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_MIDI_VIEW), yPos,
		                                              deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		yPos = yPos + 24;

		//Render Follow Mode Enabled Status at the bottom left of the OLED screen

		char followBuffer[20] = {0};
		strncat(followBuffer, l10n::get(l10n::String::STRING_FOR_MIDI_FOLLOW), 19);

		if (midiEngine.midiFollow) {
			strncat(followBuffer, l10n::get(l10n::String::STRING_FOR_ON), 4);
		}
		else {
			strncat(followBuffer, l10n::get(l10n::String::STRING_FOR_OFF), 4);
		}

		deluge::hid::display::OLED::drawString(followBuffer, 0, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		//Render Follow Mode Master Channel at the bottom right of the OLED screen

		char channelBuffer[10] = {0};
		strncat(channelBuffer, l10n::get(l10n::String::STRING_FOR_MIDI_CHANNEL), 9);

		char buffer[5];
		intToString(midiEngine.midiFollowChannel + 1, buffer);

		strncat(channelBuffer, buffer, 4);

		deluge::hid::display::OLED::drawStringAlignRight(channelBuffer, yPos,
		                                                 deluge::hid::display::OLED::oledMainImage[0],
		                                                 OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();
	}
	else {
		display->setScrollingText(l10n::get(l10n::String::STRING_FOR_MIDI_VIEW));
	}
	onParamDisplay = false;
}

/// Render Parameter Name and Learned Status with CC Set when holding param shortcut in Midi Learning View
void MidiSessionView::renderParamDisplay(Param::Kind paramKind, int32_t paramID, uint8_t ccNumber) {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

		//display parameter name
		char parameterName[30];
		strncpy(parameterName, getParamDisplayName(paramKind, paramID), 29);

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
		deluge::hid::display::OLED::drawStringCentred(parameterName, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		//display parameter value
		yPos = yPos + 24;

		if (ccNumber != kNoSelection) {
			char ccBuffer[20] = {0};
			strncat(ccBuffer, l10n::get(l10n::String::STRING_FOR_MIDI_LEARNED), 19);

			char buffer[5];
			intToString(ccNumber, buffer);

			strncat(ccBuffer, buffer, 4);

			deluge::hid::display::OLED::drawStringCentred(ccBuffer, yPos, deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
		}
		else {
			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_MIDI_NOT_LEARNED), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
		}

		deluge::hid::display::OLED::sendMainImage();
	}
	//7Seg Display
	else {
		if (ccNumber != kNoSelection) {
			char buffer[5];
			intToString(ccNumber, buffer);
			display->displayPopup(buffer, 3, true);
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_NONE), 3, true);
		}
	}
	onParamDisplay = true;
}

void MidiSessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	renderViewDisplay();
}

void MidiSessionView::redrawNumericDisplay() {
	renderViewDisplay();
}

void MidiSessionView::setLedStates() {
	setCentralLEDStates();  //inherited from session view
	view.setLedStates();    //inherited from session view
	view.setModLedStates(); //inherited from session view

	//performanceView specific LED settings
	indicator_leds::setLedState(IndicatorLED::MIDI, true);

	if (currentSong->lastClipInstanceEnteredStartPos != -1) {
		indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW);
	}
}

void MidiSessionView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);
}

ActionResult MidiSessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	//clear and reset learned params
	if (b == BACK && isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (on) {
			initPadPress(lastPadPress);
			initParamToCC(paramToCC);
			uiNeedsRendering(this);
		}
	}

	//save midi mappings
	else if (b == SAVE) {
		if (on) {
			saveMidiFollowMappings();
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_MIDI_DEFAULTS_SAVED));
		}
	}

	//load midi mappings
	else if (b == LOAD) {
		if (on) {
			loadMidiFollowMappings();
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_MIDI_DEFAULTS_LOADED));
		}
	}

	//enter "Midi Follow" soundEditor menu
	else if ((b == SELECT_ENC) && !Buttons::isShiftButtonPressed()) {
		if (on) {
			display->setNextTransitionDirection(1);
			soundEditor.setup();
			openUI(&soundEditor);
		}
	}

	//exit Midi View
	else if (b == MIDI) {
		if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
			if (on) {
				if (currentSong->lastClipInstanceEnteredStartPos != -1) {
					changeRootUI(&arrangerView);
				}
				else {
					changeRootUI(&sessionView);
				}
			}
		}
		else {
			currentCC = kNoSelection;

			if (on) {
				showLearnedParams = true;
			}
			else {
				showLearnedParams = false;
				uiNeedsRendering(this);
			}
		}
	}

	//enter exit Horizontal Encoder Button Press UI Mode
	else if (b == X_ENC) {
		if (on) {
			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
		else {
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}
	}	

	//disable button presses for Vertical encoder
	else if (b == Y_ENC) {
		return ActionResult::DEALT_WITH;
	}

	else {
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}
	return ActionResult::DEALT_WITH;
}

ActionResult MidiSessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	//if pad was pressed in main deluge grid (not sidebar)
	if (xDisplay < kDisplayWidth) {
		if (on) {
			//check if pad press corresponds to shortcut press
			//if yes, display parameter name and learned status
			potentialShortcutPadAction(xDisplay, yDisplay);
		}
		//let go of pad
		else {
			renderViewDisplay();
			initPadPress(lastPadPress);
		}
	}
	return ActionResult::DEALT_WITH;
}

//check if pad press corresponds to shortcut press
void MidiSessionView::potentialShortcutPadAction(int32_t xDisplay, int32_t yDisplay) {
	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoSelection;

	if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::PATCHED;
		paramID = patchedParamShortcuts[xDisplay][yDisplay];
	}
	else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_SOUND;
		paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
	}
	else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
		paramKind = Param::Kind::UNPATCHED_GLOBAL;
		paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
	}
	if (paramKind != Param::Kind::NONE) {
		//if pressing a param shortcut while holding learn, unlearn midi CC from a specific param
		if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
			initParamToCC(paramToCC);
			updateMappingChangeStatus();
			uiNeedsRendering(this);
		}
		renderParamDisplay(paramKind, paramID, paramToCC[xDisplay][yDisplay]);
		lastPadPress.isActive = true;
		lastPadPress.xDisplay = xDisplay;
		lastPadPress.yDisplay = yDisplay;
		lastPadPress.paramKind = paramKind;
		lastPadPress.paramID = paramID;
	}
}

void MidiSessionView::selectEncoderAction(int8_t offset) {
	return;
}

ActionResult MidiSessionView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult MidiSessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

/// why do I need this? (code won't compile without it)
uint32_t MidiSessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

/// why do I need this? (code won't compile without it)
uint32_t MidiSessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

void MidiSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	return;
}

/// used to reset stutter if it's already active
void MidiSessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	return;
}

void MidiSessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}

ModelStackWithAutoParam* MidiSessionView::getModelStackWithParam(int32_t xDisplay, int32_t yDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	Param::Kind paramKind = Param::Kind::NONE;
	int32_t paramID = kNoParamID;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	if ((getRootUI() == &sessionView) || (getRootUI() == &arrangerView) || (getRootUI() == &performanceSessionView)) {
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::UNPATCHED_SOUND;
			paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			paramKind = Param::Kind::UNPATCHED_GLOBAL;
			paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
		}
		if (paramID != kNoParamID) {
			modelStackWithParam = performanceSessionView.getModelStackWithParam(modelStack, paramID);
		}
	}
	else if ((getRootUI() == &audioClipView) || (getRootUI() == &instrumentClipView)
	         || (getRootUI() == &automationInstrumentClipView)) {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		InstrumentClip* clip = (InstrumentClip*)currentSong->currentClip;
		Instrument* instrument = (Instrument*)clip->output;

		if (instrument->type == InstrumentType::SYNTH) {
			if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramKind = Param::Kind::PATCHED;
				paramID = patchedParamShortcuts[xDisplay][yDisplay];
			}
			else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramKind = Param::Kind::UNPATCHED_SOUND;
				paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
			}
		}
		else if (instrument->type == InstrumentType::KIT) {
			if (!instrumentClipView.getAffectEntire()) {
				if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::PATCHED;
					paramID = patchedParamShortcuts[xDisplay][yDisplay];
				}
				else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::UNPATCHED_SOUND;
					paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
				}
			}
			else {
				if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::UNPATCHED_SOUND;
					paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
				}
				else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					paramKind = Param::Kind::UNPATCHED_GLOBAL;
					paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
				}
			}
		}
		else if (instrument->type == InstrumentType::AUDIO) {
			if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramKind = Param::Kind::UNPATCHED_SOUND;
				paramID = unpatchedParamShortcuts[xDisplay][yDisplay];
			}
			else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramKind = Param::Kind::UNPATCHED_GLOBAL;
				paramID = globalEffectableParamShortcuts[xDisplay][yDisplay];
			}
		}
		if ((paramKind != Param::Kind::NONE) && (paramID != kNoParamID)) {
			modelStackWithParam =
			    automationInstrumentClipView.getModelStackWithParam(modelStack, clip, paramID, paramKind);
		}
	}
	return modelStackWithParam;
}

void MidiSessionView::updateMappingChangeStatus() {
	anyChangesToSave = false;

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (backupXMLParamToCC[xDisplay][yDisplay] != paramToCC[xDisplay][yDisplay]) {
				anyChangesToSave = true;
				break;
			}
		}
	}

	if (anyChangesToSave) {
		indicator_leds::blinkLed(IndicatorLED::SAVE);
	}
	else {
		indicator_leds::setLedState(IndicatorLED::SAVE, false);
	}
}

/// update saved paramToCC mapping and update saved changes status
void MidiSessionView::saveMidiFollowMappings() {
	writeDefaultsToFile();
	updateMappingChangeStatus();
}

/// create default XML file and write defaults
/// I should check if file exists before creating one
void MidiSessionView::writeDefaultsToFile() {
	//MidiFollow.xml
	int32_t error = storageManager.createXMLFile(MIDI_DEFAULTS_XML, true);
	if (error) {
		return;
	}

	//<defaults>
	storageManager.writeOpeningTagBeginning(MIDI_DEFAULTS_TAG);
	storageManager.writeOpeningTagEnd();

	//<defaultCCMappings>
	storageManager.writeOpeningTagBeginning(MIDI_DEFAULTS_CC_TAG);
	storageManager.writeOpeningTagEnd();

	writeDefaultMappingsToFile();

	storageManager.writeClosingTag(MIDI_DEFAULTS_CC_TAG);

	storageManager.writeClosingTag(MIDI_DEFAULTS_TAG);

	storageManager.closeFileAfterWriting();

	anyChangesToSave = false;
}

/// convert paramID to a paramName to write to XML
void MidiSessionView::writeDefaultMappingsToFile() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			bool writeTag = false;
			char const* paramName;

			if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = ((Sound*)NULL)->Sound::paramToString(patchedParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;	
			}
			else if (unpatchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				if ((unpatchedParamShortcuts[xDisplay][yDisplay] == Param::Unpatched::Sound::ARP_GATE)
				|| (unpatchedParamShortcuts[xDisplay][yDisplay] == Param::Unpatched::Sound::PORTAMENTO)) {
					paramName = ((Sound*)NULL)->Sound::paramToString(Param::Unpatched::START + unpatchedParamShortcuts[xDisplay][yDisplay]);
				}
				else {
					paramName =
						ModControllableAudio::paramToString(Param::Unpatched::START + unpatchedParamShortcuts[xDisplay][yDisplay]);
				}
				writeTag = true;
			}
			else if (globalEffectableParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
				paramName = GlobalEffectable::paramToString(Param::Unpatched::START + globalEffectableParamShortcuts[xDisplay][yDisplay]);
				writeTag = true;			
			}

			if (writeTag) {
				char buffer[10];
				intToString(paramToCC[xDisplay][yDisplay], buffer);
				storageManager.writeTag(paramName, buffer);

				backupXMLParamToCC[xDisplay][yDisplay] = paramToCC[xDisplay][yDisplay];
			}			
		}
	}
}

/// load saved layout, update change status
void MidiSessionView::loadMidiFollowMappings() {
	initPadPress(lastPadPress);
	initParamToCC(paramToCC);
	if (successfullyReadDefaultsFromFile) {
		readDefaultsFromBackedUpFile();
	}
	else {
		readDefaultsFromFile();
	}
	updateMappingChangeStatus();
	uiNeedsRendering(this);
}

/// re-read defaults from backed up XML in memory in order to reduce SD Card IO
void MidiSessionView::readDefaultsFromBackedUpFile() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			paramToCC[xDisplay][yDisplay] = backupXMLParamToCC[xDisplay][yDisplay];
		}
	}
}

/// read defaults from XML
void MidiSessionView::readDefaultsFromFile() {
	//no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}

	FilePointer fp;
	//MIDIFollow.XML
	bool success = storageManager.fileExists(MIDI_DEFAULTS_XML, &fp);
	if (!success) {
		return;
	}

	//<defaults>
	int32_t error = storageManager.openXMLFile(&fp, MIDI_DEFAULTS_TAG);
	if (error) {
		return;
	}

	char const* tagName;
	//step into the <defaultCCMappings> tag
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_DEFAULTS_CC_TAG)) {
			readDefaultMappingsFromFile();
		}
		storageManager.exitTag();
	}

	storageManager.closeFile();

	successfullyReadDefaultsFromFile = true;
}

/// compares param name tag to the list of params available are midi controllable
/// if param is found, it loads the CC mapping info for that param into the view
void MidiSessionView::readDefaultMappingsFromFile() {
	char const* paramName;
	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				if (!strcmp(tagName, ((Sound*)NULL)->Sound::paramToString(patchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, ((Sound*)NULL)->Sound::paramToString(Param::Unpatched::START + unpatchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, ModControllableAudio::paramToString(Param::Unpatched::START + unpatchedParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, GlobalEffectable::paramToString(Param::Unpatched::START + globalEffectableParamShortcuts[xDisplay][yDisplay]))) {
					paramToCC[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt();
				}
				backupXMLParamToCC[xDisplay][yDisplay] = paramToCC[xDisplay][yDisplay];
			}
		}		
		storageManager.exitTag();
	}
}
