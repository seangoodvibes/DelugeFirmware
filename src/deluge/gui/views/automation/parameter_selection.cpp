/*
 * Copyright (c) 2025 Sean Ditny
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

#include "gui/views/automation/parameter_selection.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/automation/layout/editor.h"
#include "gui/views/automation/layout/overview.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_follow.h"
#include "model/action/action_logger.h"
#include "model/instrument/midi_instrument.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/comparison.h"

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

using namespace deluge::gui;

constexpr int32_t kNumNonGlobalParamsForAutomation = 81;
constexpr int32_t kNumGlobalParamsForAutomation = 37;
constexpr uint8_t kVelocityShortcutX = 15;
constexpr uint8_t kVelocityShortcutY = 1;

// synth and kit rows FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<params::Kind, ParamType>, kNumNonGlobalParamsForAutomation> nonGlobalParamsForAutomation{{
    // Master Volume, Pitch, Pan
    {params::Kind::PATCHED, params::GLOBAL_VOLUME_POST_FX},
    {params::Kind::PATCHED, params::LOCAL_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_PAN},
    // LPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_LPF_FREQ},
    {params::Kind::PATCHED, params::LOCAL_LPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_LPF_MORPH},
    // HPF Cutoff, Resonance, Morph
    {params::Kind::PATCHED, params::LOCAL_HPF_FREQ},
    {params::Kind::PATCHED, params::LOCAL_HPF_RESONANCE},
    {params::Kind::PATCHED, params::LOCAL_HPF_MORPH},
    // Bass, Bass Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS_FREQ},
    // Treble, Treble Freq
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_TREBLE_FREQ},
    // Reverb Amount
    {params::Kind::PATCHED, params::GLOBAL_REVERB_AMOUNT},
    // Delay Rate, Amount
    {params::Kind::PATCHED, params::GLOBAL_DELAY_RATE},
    {params::Kind::PATCHED, params::GLOBAL_DELAY_FEEDBACK},
    // Sidechain Shape
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SIDECHAIN_SHAPE},
    // Decimation, Bitcrush, Wavefolder
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BITCRUSHING},
    {params::Kind::PATCHED, params::LOCAL_FOLD},
    // OSC 1 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_A_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_0_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_A_WAVE_INDEX},
    // OSC 2 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_B_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_PHASE_WIDTH},
    {params::Kind::PATCHED, params::LOCAL_CARRIER_1_FEEDBACK},
    {params::Kind::PATCHED, params::LOCAL_OSC_B_WAVE_INDEX},
    // FM Mod 1 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_FEEDBACK},
    // FM Mod 2 Volume, Pitch, Feedback
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_VOLUME},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_PITCH_ADJUST},
    {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_FEEDBACK},
    // Env 1 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_0_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_0_RELEASE},
    // Env 2 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_1_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_1_RELEASE},
    // Env 3 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_2_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_2_RELEASE},
    // Env 4 ADSR
    {params::Kind::PATCHED, params::LOCAL_ENV_3_ATTACK},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_DECAY},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_SUSTAIN},
    {params::Kind::PATCHED, params::LOCAL_ENV_3_RELEASE},
    // LFO 1
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ_1},
    // LFO 2
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ_1},
    // LFO 3
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ_2},
    // LFO 4
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ_2},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_DEPTH},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_RATE},
    // Arp Rate, Gate, Spread Gate, Spread Octave, Spread Velocity, Ratched Amount, Ratchet Probability
    // Chord Polyphony, Chord Probability, Note Probability, Bass Probability, Reverse Probability
    // Rhythm, Sequence Length
    {params::Kind::PATCHED, params::GLOBAL_ARP_RATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_OCTAVE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SPREAD_VELOCITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_POLYPHONY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_REVERSE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
    // Noise
    {params::Kind::PATCHED, params::LOCAL_NOISE_VOLUME},
    // Portamento
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO},
    // Stutter Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_STUTTER_RATE},
    // Compressor Threshold
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_COMPRESSOR_THRESHOLD},
    // Mono Expression: X - Pitch Bend
    {params::Kind::EXPRESSION, Expression::X_PITCH_BEND},
    // Mono Expression: Y - Mod Wheel
    {params::Kind::EXPRESSION, Expression::Y_SLIDE_TIMBRE},
    // Mono Expression: Z - Channel Pressure
    {params::Kind::EXPRESSION, Expression::Z_PRESSURE},
}};

// global FX - sorted in the order that Parameters are scrolled through on the display
// used with kit affect entire, audio clips, and arranger
const std::array<std::pair<params::Kind, ParamType>, kNumGlobalParamsForAutomation> globalParamsForAutomation{{
    // Master Volume, Pitch, Pan
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PITCH_ADJUST},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_PAN},
    // LPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_MORPH},
    // HPF Cutoff, Resonance
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_FREQ},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_RES},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_HPF_MORPH},
    // Bass, Bass Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS_FREQ},
    // Treble, Treble Freq
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_TREBLE_FREQ},
    // Reverb Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERB_SEND_AMOUNT},
    // Delay Rate, Amount
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_DELAY_AMOUNT},
    // Sidechain Send, Shape
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_VOLUME},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SIDECHAIN_SHAPE},
    // Decimation, Bitcrush
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BITCRUSHING},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_DEPTH},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_MOD_FX_RATE},
    // Stutter Rate
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_STUTTER_RATE},
    // Compressor Threshold
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_COMPRESSOR_THRESHOLD},
    // Arp Rate, Gate, Rhythm, Chord Polyphony, Sequence Length, Ratchet Amount, Note Prob, Bass Prob, Chord Prob,
    // Ratchet Prob, Spread Gate, Spread Octave, Spread Velocity
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_SPREAD_VELOCITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_REVERSE_PROBABILITY},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
}};

AutomationParameterSelection::AutomationParameterSelection() {
}

void AutomationParameterSelection::initialize() {
	if (!midiCCShortcutsLoaded) {
		initMIDICCShortcutsForAutomation();
		midiCCShortcutsLoaded = true;
	}

	// let the view know if we're dealing with an automation parameter or a note parameter
	setAutomationParamType();

	if (rootUIIsClipMinderScreen()) {
		InstrumentClip* clip = getCurrentInstrumentClip();

		// only applies to instrument clips (not audio)
		if (clip != nullptr) {
			Output* output = clip->output;
			OutputType outputType = output->type;

			// check if we for some reason, left the automation view, then switched clip types, then came back in
			// if you did that...reset the parameter selection and save the current parameter type selection
			// so we can check this again next time it happens
			if (outputType != clip->lastSelectedOutputType) {
				if (inAutomationEditor()) {
					initParameterSelection();
				}

				clip->lastSelectedOutputType = outputType;
			}

			// if we're in a kit, we want to make sure the param selected is valid for current context
			// e.g. only UNPATCHED_GLOBAL param kind's can be used with Kit Affect Entire enabled
			if ((outputType == OutputType::KIT) && (clip->lastSelectedParamKind != params::Kind::NONE)) {
				if (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL) {
					clip->affectEntire = true;
				}
				else {
					clip->affectEntire = false;
				}
			}

			// if you're not in note editor, turn led off if it's on
			if (clip->wrapEditing) {
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, inNoteEditor());
			}
		}
	}
}

void AutomationParameterSelection::initMIDICCShortcutsForAutomation() {
	for (int x = 0; x < kDisplayWidth; x++) {
		for (int y = 0; y < kDisplayHeight; y++) {
			uint8_t ccNumber = MIDI_CC_NONE;
			uint32_t paramId = patchedParamShortcuts[x][y];
			if (paramId != kNoParamID) {
				ccNumber = midiFollow.soundParamToCC[paramId];
				if (ccNumber == MIDI_CC_NONE) {
					ccNumber = midiFollow.globalParamToCC[paramId];
				}
			}
			if (ccNumber == MIDI_CC_NONE) {
				paramId = unpatchedNonGlobalParamShortcuts[x][y];
				if (paramId != kNoParamID) {
					ccNumber = midiFollow.soundParamToCC[paramId + params::UNPATCHED_START];
					if (ccNumber == MIDI_CC_NONE) {
						ccNumber = midiFollow.globalParamToCC[paramId];
					}
				}
			}
			if (ccNumber != MIDI_CC_NONE) {
				midiCCShortcutsForAutomation[x][y] = ccNumber;
			}
			else {
				midiCCShortcutsForAutomation[x][y] = kNoParamID;
			}
		}
	}

	midiCCShortcutsForAutomation[14][7] = CC_NUMBER_PITCH_BEND;
	midiCCShortcutsForAutomation[15][0] = CC_NUMBER_AFTERTOUCH;
	midiCCShortcutsForAutomation[15][7] = CC_NUMBER_Y_AXIS;
}

void AutomationParameterSelection::setAutomationParamType() {
	automationView.automationParamType = AutomationParamType::PER_SOUND;
	if (!inAutomationEditor()) {
		Clip* clip = getCurrentClip();
		if ((clip->lastSelectedParamShortcutX == kVelocityShortcutX)
		    && (clip->lastSelectedParamShortcutY == kVelocityShortcutY)) {
			automationView.automationParamType = AutomationParamType::NOTE_VELOCITY;
		}
	}
}

// called by shortcutPadAction when it is determined that you are selecting a parameter on automation
// overview or by using a grid shortcut combo
void AutomationParameterSelection::handleParameterSelection(Clip* clip, Output* output, OutputType outputType,
                                                            int32_t xDisplay, int32_t yDisplay) {
	bool isClipContext = rootUIIsClipMinderScreen();
	bool isSongContext = !isClipContext;

	// PatchSource::Velocity shortcut
	// Enter Velocity Note Editor
	if (xDisplay == kVelocityShortcutX && yDisplay == kVelocityShortcutY) {
		if (clip->type == ClipType::INSTRUMENT) {
			// don't enter if we're in a kit with affect entire enabled
			if (!(outputType == OutputType::KIT && getAffectEntire())) {
				//	potentiallyVerticalScrollToSelectedDrum((InstrumentClip*)clip, output);
				initParameterSelection(false);
				automationView.automationParamType = AutomationParamType::NOTE_VELOCITY;
				clip->lastSelectedParamShortcutX = xDisplay;
				clip->lastSelectedParamShortcutY = yDisplay;
				blinkShortcuts();
				renderDisplay();
				uiNeedsRendering(getRootUI());
				// if you're in note editor, turn led on
				if (((InstrumentClip*)clip)->wrapEditing) {
					indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
				}
			}
			return;
		}
	}
	// potentially select a regular automatable parameter
	else if (isClipContext
	         && (outputType == OutputType::SYNTH
	             || (outputType == OutputType::KIT && !getAffectEntire() && ((Kit*)output)->selectedDrum
	                 && ((Kit*)output)->selectedDrum->type == DrumType::SOUND))
	         && ((patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	             || (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)
	             || params::isPatchCableShortcut(xDisplay, yDisplay))) {
		// don't allow automation of portamento in kit's
		if ((outputType == OutputType::KIT)
		    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
			return; // no parameter selected, don't re-render grid;
		}

		// if you are in a synth or a kit instrumentClip and the shortcut is valid, set current selected
		// ParamID
		if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::PATCHED;
			clip->lastSelectedParamID = patchedParamShortcuts[xDisplay][yDisplay];
		}
		else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
			clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
			clip->lastSelectedParamID = unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay];
		}
		else if (params::isPatchCableShortcut(xDisplay, yDisplay)) {
			ParamDescriptor paramDescriptor;
			params::getPatchCableFromShortcut(xDisplay, yDisplay, &paramDescriptor);
			clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
			clip->lastSelectedParamID = paramDescriptor.data;
			clip->lastSelectedPatchSource = paramDescriptor.getBottomLevelSource();
		}

		if (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}

	// if you are in arranger, an audio clip, or a kit clip with affect entire enabled
	else if ((isSongContext || (outputType == OutputType::AUDIO)
	          || (outputType == OutputType::KIT && getAffectEntire()))
	         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {

		params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
		int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

		// don't allow automation of pitch adjust, sidechain or arp parameters in arranger
		if ((getRootUI()->getUIContextType() == UIType::ARRANGER)
		    && ((paramID == params::UNPATCHED_PITCH_ADJUST) || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
		        || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)
		        || (paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		        || (paramID == params::UNPATCHED_ARP_RATE))) {
			return; // no parameter selected, don't re-render grid;
		}
		// don't allow automation of arp params in audio clips
		else if (outputType == OutputType::AUDIO
		         && ((paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		             || paramID == params::UNPATCHED_ARP_RATE)) {
			return; // no parameter selected, don't re-render grid;
		}

		if (isClipContext) {
			clip->lastSelectedParamKind = paramKind;
			clip->lastSelectedParamID = paramID;
		}
		else {
			currentSong->lastSelectedParamKind = paramKind;
			currentSong->lastSelectedParamID = paramID;
		}

		getLastSelectedGlobalParamArrayPosition(clip);
	}

	else if (outputType == OutputType::MIDI_OUT && midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {

		// if you are in a midi clip and the shortcut is valid, set the current selected ParamID
		clip->lastSelectedParamID = midiCCShortcutsForAutomation[xDisplay][yDisplay];
	}
	// expression params, so sounds or midi/cv, or a single drum
	else if ((util::one_of(outputType, {OutputType::MIDI_OUT, OutputType::CV, OutputType::SYNTH})
	          // selected a single sound drum
	          || ((outputType == OutputType::KIT && !getAffectEntire() && ((Kit*)output)->selectedDrum
	               && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)))
	         && params::expressionParamFromShortcut(xDisplay, yDisplay) != kNoParamID) {
		clip->lastSelectedParamID = params::expressionParamFromShortcut(xDisplay, yDisplay);
		clip->lastSelectedParamKind = params::Kind::EXPRESSION;
	}

	else {
		return; // no parameter selected, don't re-render grid;
	}

	// save the selected parameter ID's shortcut pad x,y coords so that you can setup the shortcut blink
	if (isClipContext) {
		clip->lastSelectedParamShortcutX = xDisplay;
		clip->lastSelectedParamShortcutY = yDisplay;
	}
	else {
		currentSong->lastSelectedParamShortcutX = xDisplay;
		currentSong->lastSelectedParamShortcutY = yDisplay;
	}

	resetParameterShortcutBlinking();
	if (inNoteEditor()) {
		automationView.automationParamType = AutomationParamType::PER_SOUND;
		instrumentClipView.resetSelectedNoteRowBlinking();
		if (padSelectionOn) {
			initPadSelection();
		}
	}
	blinkShortcuts();
	if (display->have7SEG()) {
		renderDisplay(); // always display parameter name first, if there's automation it will show after
	}
	displayAutomation(true);
	view.setModLedStates();
	uiNeedsRendering(getRootUI());
	// turn off cross screen LED in automation editor
	if (clip && clip->type == ClipType::INSTRUMENT && ((InstrumentClip*)clip)->wrapEditing) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

// select encoder action

// used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
// once parameter is selected
bool AutomationParameterSelection::selectEncoderAction(Clip* clip, Output* output, OutputType outputType,
                                                       int8_t offset) {
	bool isClipContext = clip != nullptr;
	bool isSongContext = !isClipContext;

	// if you're in a midi clip
	if (outputType == OutputType::MIDI_OUT) {
		selectMIDICC(offset, clip);
		getLastSelectedParamShortcut(clip);
	}
	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	else if (isSongContext || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (isSongContext || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			selectGlobalParam(offset, clip);
		}
		// if you're a synth or a kit (with affect entire off and a sound drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			selectNonGlobalParam(offset, clip);
		}
		// don't have patch cable blinking logic figured out yet
		if (clip != nullptr && clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			getLastSelectedParamShortcut(clip);
		}
	}
	// if you're in a CV clip or function is called for some other reason, do nothing
	else {
		return false;
	}
	return true;
}

// used with SelectEncoderAction to get the next arranger / audio clip / kit affect entire parameter
void AutomationParameterSelection::selectGlobalParam(int32_t offset, Clip* clip) {
	if (clip != nullptr) {
		// don't allow automation of arp parameters in audio clips
		if (clip->output->type == OutputType::AUDIO) {
			auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
			                                             kNumGlobalParamsForAutomation);
			auto [kind, id] = globalParamsForAutomation[idx];
			{
				while ((id >= params::UNPATCHED_FIRST_ARP_PARAM && id <= params::UNPATCHED_LAST_ARP_PARAM)
				       || id == params::UNPATCHED_ARP_RATE) {

					if (offset < 0) {
						offset -= 1;
					}
					else if (offset > 0) {
						offset += 1;
					}
					idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
					                                        kNumGlobalParamsForAutomation);
					id = globalParamsForAutomation[idx].second;
				}
			}
			clip->lastSelectedParamID = id;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedParamArrayPosition = idx;
		}
		else {
			auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
			                                             kNumGlobalParamsForAutomation);
			auto [kind, id] = globalParamsForAutomation[idx];
			clip->lastSelectedParamID = id;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedParamArrayPosition = idx;
		}
	}
	else {
		auto idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			// don't allow automation of pitch adjust, sidechain or arp parameters in arranger
			while ((id == params::UNPATCHED_PITCH_ADJUST || id == params::UNPATCHED_SIDECHAIN_SHAPE
			        || id == params::UNPATCHED_SIDECHAIN_VOLUME || id == params::UNPATCHED_COMPRESSOR_THRESHOLD
			        || (id >= params::UNPATCHED_FIRST_ARP_PARAM && id <= params::UNPATCHED_LAST_ARP_PARAM)
			        || id == params::UNPATCHED_ARP_RATE)) {

				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
				                                        kNumGlobalParamsForAutomation);
				id = globalParamsForAutomation[idx].second;
			}
		}
		currentSong->lastSelectedParamID = id;
		currentSong->lastSelectedParamKind = kind;
		currentSong->lastSelectedParamArrayPosition = idx;
	}
	automationView.automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next synth or kit non-affect entire param
void AutomationParameterSelection::selectNonGlobalParam(int32_t offset, Clip* clip) {
	bool foundPatchCable = false;
	// if we previously selected a patch cable, we'll see if there are any more to scroll through
	if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
		foundPatchCable = selectPatchCable(offset, clip);
		// did we find another patch cable?
		if (!foundPatchCable) {
			// if we haven't found a patch cable, it means we reached beginning or end of patch cable
			// list if we're scrolling right, we'll resume with selecting a regular param from beg of
			// list if we're scrolling left, we'll resume with selecting a regular param from end of
			// list to do so we re-set the last selected param array position

			// scrolling right
			if (offset > 0) {
				clip->lastSelectedParamArrayPosition = kNumNonGlobalParamsForAutomation - 1;
			}
			// scrolling left
			else if (offset < 0) {
				clip->lastSelectedParamArrayPosition = 0;
			}
		}
	}
	// if we didn't find anymore patch cables, then we'll select a regular param from the list
	if (!foundPatchCable) {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumNonGlobalParamsForAutomation);
		{
			auto [kind, id] = nonGlobalParamsForAutomation[idx];
			if ((clip->output->type == OutputType::KIT) && (kind == params::Kind::UNPATCHED_SOUND)
			    && (id == params::UNPATCHED_PORTAMENTO)) {
				if (offset < 0) {
					offset -= 1;
				}
				else if (offset > 0) {
					offset += 1;
				}
				idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
				                                        kNumNonGlobalParamsForAutomation);
			}
		}

		// did we reach beginning or end of list?
		// if yes, then let's scroll through patch cables
		// but only if we haven't already scrolled through patch cables already above
		if ((clip->lastSelectedParamKind != params::Kind::PATCH_CABLE)
		    && (((offset > 0) && (idx < clip->lastSelectedParamArrayPosition))
		        || ((offset < 0) && (idx > clip->lastSelectedParamArrayPosition)))) {
			foundPatchCable = selectPatchCable(offset, clip);
		}

		// if we didn't find a patch cable, then we'll resume with scrolling the non-patch cable list
		if (!foundPatchCable) {
			auto [kind, id] = nonGlobalParamsForAutomation[idx];
			clip->lastSelectedParamID = id;
			clip->lastSelectedParamKind = kind;
			clip->lastSelectedParamArrayPosition = idx;
		}
	}
	automationView.automationParamType = AutomationParamType::PER_SOUND;
}

// iterate through the patch cable list to select the previous or next patch cable
// actual selecting of the patch cable is done in the selectPatchCableAtIndex function
bool AutomationParameterSelection::selectPatchCable(int32_t offset, Clip* clip) {
	ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
	if (paramManager) {
		PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
		// make sure it's not jiberish
		if (set) {
			// do we have any patch cables?
			if (set->numPatchCables > 0) {
				bool foundCurrentPatchCable = false;
				// scrolling right
				if (offset > 0) {
					// loop from beginning to end of patch cable list
					for (int i = 0; i < set->numPatchCables; i++) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
				// scrolling left
				else if (offset < 0) {
					// loop from end to beginning of patch cable list
					for (int i = set->numPatchCables - 1; i >= 0; i--) {
						// loop through patch cables until we've found a new one and select it
						// adjacent to current found patch cable (if we previously selected one)
						if (selectPatchCableAtIndex(clip, set, i, foundCurrentPatchCable)) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

// this function does the actual selecting of a patch cable
// see if the patch cable selected is different from current one selected (or not selected)
// if we havent already selected a patch cable, we'll select this one
// if we selected one previously, we'll see if this one is adjacent to the previous one selected
// if it's adjacent to the previous one selected, we'll select this one
bool AutomationParameterSelection::selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex,
                                                           bool& foundCurrentPatchCable) {
	PatchCable* cable = &set->patchCables[patchCableIndex];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	// need to add patch cable source to the descriptor so that we can get the paramId from it
	desc.addSource(cable->from);

	// if we've previously selected a patch cable, we want to start scrolling from that patch cable
	// note: the reason why we can't save the patchCableIndex to make finding the previous patch
	// cable selected easier is because the patch cable array gets re-indexed as patch cables get
	// added or removed or values change. Thus you need to search for the previous patch cable to get
	// the updated index and then you can find the adjacent patch cable in the list.
	if (desc.data == clip->lastSelectedParamID) {
		foundCurrentPatchCable = true;
	}
	// if we found the patch cable we previously selected and we found another one
	// or we hadn't selected a patch cable previously and found a patch cable
	// select the one we found
	else if ((foundCurrentPatchCable || (clip->lastSelectedParamKind != params::Kind::PATCH_CABLE))
	         && (desc.data != clip->lastSelectedParamID)) {
		clip->lastSelectedPatchSource = cable->from;
		clip->lastSelectedParamID = desc.data;
		clip->lastSelectedParamKind = params::Kind::PATCH_CABLE;
		return true;
	}
	return false;
}

// used with SelectEncoderAction to get the next midi CC
void AutomationParameterSelection::selectMIDICC(int32_t offset, Clip* clip) {
	if (onAutomationOverview()) {
		clip->lastSelectedParamID = CC_NUMBER_NONE;
	}
	auto newCC = clip->lastSelectedParamID;
	newCC += offset;
	if (newCC < 0) {
		newCC = CC_NUMBER_Y_AXIS;
	}
	else if (newCC >= kNumCCExpression) {
		newCC = 0;
	}
	if (newCC == CC_EXTERNAL_MOD_WHEEL) {
		// mod wheel is actually CC_NUMBER_Y_AXIS (122) internally
		newCC += offset;
	}
	clip->lastSelectedParamID = newCC;
	automationView.automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next parameter in the list of parameters
int32_t AutomationParameterSelection::getNextSelectedParamArrayPosition(int32_t offset,
                                                                        int32_t lastSelectedParamArrayPosition,
                                                                        int32_t numParams) {
	int32_t idx;
	// if you haven't selected a parameter yet, start at the beginning of the list
	if (onAutomationOverview()) {
		idx = 0;
	}
	// if you are scrolling left and are at the beginning of the list, go to the end of the list
	else if ((lastSelectedParamArrayPosition + offset) < 0) {
		idx = numParams + offset;
	}
	// if you are scrolling right and are at the end of the list, go to the beginning of the list
	else if ((lastSelectedParamArrayPosition + offset) > (numParams - 1)) {
		idx = 0;
	}
	// otherwise scrolling left/right within the list
	else {
		idx = lastSelectedParamArrayPosition + offset;
	}
	return idx;
}

// used with Select Encoder action to get the X, Y grid shortcut coordinates of the parameter selected
void AutomationParameterSelection::getLastSelectedParamShortcut(Clip* clip) {
	bool isClipContext = clip != nullptr;
	bool paramShortcutFound = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (isClipContext) {
				if (clip->output->type == OutputType::MIDI_OUT) {
					if (midiCCShortcutsForAutomation[x][y] == clip->lastSelectedParamID) {
						clip->lastSelectedParamShortcutX = x;
						clip->lastSelectedParamShortcutY = y;
						paramShortcutFound = true;
						break;
					}
				}
				else {
					if ((clip->lastSelectedParamKind == params::Kind::PATCHED
					     && patchedParamShortcuts[x][y] == clip->lastSelectedParamID)
					    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_SOUND
					        && unpatchedNonGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
					    || (clip->lastSelectedParamKind == params::Kind::UNPATCHED_GLOBAL
					        && unpatchedGlobalParamShortcuts[x][y] == clip->lastSelectedParamID)
					    || (clip->lastSelectedParamKind == params::Kind::EXPRESSION
					        && params::expressionParamFromShortcut(x, y) == clip->lastSelectedParamID)) {
						clip->lastSelectedParamShortcutX = x;
						clip->lastSelectedParamShortcutY = y;
						paramShortcutFound = true;
						break;
					}
				}
			}
			else {
				if (unpatchedGlobalParamShortcuts[x][y] == currentSong->lastSelectedParamID) {
					currentSong->lastSelectedParamShortcutX = x;
					currentSong->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
		}
		if (paramShortcutFound) {
			break;
		}
	}
	if (!paramShortcutFound) {
		if (isClipContext) {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			currentSong->lastSelectedParamShortcutX = kNoSelection;
			currentSong->lastSelectedParamShortcutY = kNoSelection;
		}
	}
}

void AutomationParameterSelection::getLastSelectedParamArrayPosition(Clip* clip) {
	bool isClipContext = clip != nullptr;
	bool isSongContext = !isClipContext;

	// if you're in a non-midi, non-cv clip (e.g. audio, synth, kit)
	if (isClipContext) {
		Output* output = clip->output;
		OutputType outputType = output->type;

		if ((outputType == OutputType::AUDIO) || (outputType == OutputType::KIT && getAffectEntire())) {
			getLastSelectedGlobalParamArrayPosition(clip);
		}
		// if you're a synth or a kit (with affect entire off and a drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}
	// if you're in arranger view
	else {
		getLastSelectedGlobalParamArrayPosition(nullptr);
	}
}

void AutomationParameterSelection::getLastSelectedNonGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumNonGlobalParamsForAutomation; idx++) {

		auto [kind, id] = nonGlobalParamsForAutomation[idx];

		if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
			clip->lastSelectedParamArrayPosition = idx;
			break;
		}
	}
}

void AutomationParameterSelection::getLastSelectedGlobalParamArrayPosition(Clip* clip) {
	bool isClipContext = clip != nullptr;

	for (auto idx = 0; idx < kNumGlobalParamsForAutomation; idx++) {

		auto [kind, id] = globalParamsForAutomation[idx];

		if (isClipContext) {
			if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
				clip->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
		else {
			if ((id == currentSong->lastSelectedParamID) && (kind == currentSong->lastSelectedParamKind)) {
				currentSong->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
	}
}

// resets the Parameter Selection which sends you back to the Automation Overview screen
// these values are saved on a clip basis
void AutomationParameterSelection::initParameterSelection(bool updateDisplay) {
	resetShortcutBlinking();
	initPadSelection();

	if (rootUIIsClipMinderScreen()) {
		Clip* clip = getCurrentClip();
		clip->lastSelectedParamID = kNoSelection;
		clip->lastSelectedParamKind = params::Kind::NONE;
		clip->lastSelectedParamShortcutX = kNoSelection;
		clip->lastSelectedParamShortcutY = kNoSelection;
		clip->lastSelectedPatchSource = PatchSource::NONE;
		clip->lastSelectedParamArrayPosition = 0;

		// if you're on automation overview, turn led off if it's on
		if (clip->type == ClipType::INSTRUMENT && ((InstrumentClip*)clip)->wrapEditing) {
			indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		}
	}
	else {
		currentSong->lastSelectedParamID = kNoSelection;
		currentSong->lastSelectedParamKind = params::Kind::NONE;
		currentSong->lastSelectedParamShortcutX = kNoSelection;
		currentSong->lastSelectedParamShortcutY = kNoSelection;
		currentSong->lastSelectedParamArrayPosition = 0;
	}

	automationView.automationParamType = AutomationParamType::PER_SOUND;

	// if we're going back to the Automation Overview, set the display to show "Automation Overview"
	// and update the knob indicator levels to match the master FX button selected
	display->cancelPopup();
	view.setKnobIndicatorLevels();
	view.setModLedStates();
	if (updateDisplay) {
		renderDisplay();
	}
}

// get's the modelstack for the parameters that are being edited
// the model stack differs for SONG, SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam* AutomationParameterSelection::getModelStackWithParam(void* modelStackMemory,
                                                                              ModelStackWithTimelineCounter* modelStack,
                                                                              Clip* clip) {
	if (rootUIIsClipMinderScreen()) {
		return getModelStackWithParamForClip(modelStack, clip);
	}
	else {
		return getModelStackWithParamForSong(modelStackMemory);
	}

	return nullptr;
}

// get's the modelstack for the song parameters that are being edited
ModelStackWithAutoParam* AutomationParameterSelection::getModelStackWithParamForSong(void* modelStackMemory) {
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	return currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
}

// get's the modelstack for the clip parameters that are being edited
// the model stack differs for SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam*
AutomationParameterSelection::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                            int32_t paramID, params::Kind paramKind) {
	if (modelStack && clip && clip->output) {
		if (paramID == kNoParamID) {
			paramID = clip->lastSelectedParamID;
			paramKind = clip->lastSelectedParamKind;
		}

		// check if we're in the sound menu and not the settings menu
		// because in the settings menu, the menu mod controllable's aren't setup, so we don't want to use those
		bool inSoundMenu = getCurrentUI() == &soundEditor && !soundEditor.inSettingsMenu();

		return clip->output->getModelStackWithParam(modelStack, clip, paramID, paramKind, getAffectEntire(),
		                                            inSoundMenu);
	}
	return nullptr;
}
