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

#include "gui/views/automation/layout.h"
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

const uint32_t auditionPadActionUIModes[] = {UI_MODE_NOTES_PRESSED,
                                             UI_MODE_AUDITIONING,
                                             UI_MODE_HORIZONTAL_SCROLL,
                                             UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             0};

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, 0};

const uint32_t verticalScrollUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN, 0};

constexpr int32_t kNumNonGlobalParamsForAutomation = 70;
constexpr int32_t kNumGlobalParamsForAutomation = 26;
constexpr int32_t kParamNodeWidth = 3;

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
    // OSC 2 Volume, Pitch, Pulse Width, Carrier Feedback, Wave Index
    {params::Kind::PATCHED, params::LOCAL_OSC_A_WAVE_INDEX},
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
    // LFO 1 Freq
    {params::Kind::PATCHED, params::GLOBAL_LFO_FREQ},
    // LFO 2 Freq
    {params::Kind::PATCHED, params::LOCAL_LFO_LOCAL_FREQ},
    // Mod FX Offset, Feedback, Depth, Rate
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_OFFSET},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_MOD_FX_FEEDBACK},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_DEPTH},
    {params::Kind::PATCHED, params::GLOBAL_MOD_FX_RATE},
    // Arp Rate, Gate, Rhythm, Chord Polyphony, Sequence Length, Ratchet Amount, Note Prob, Bass Prob, Chord Prob,
    // Ratchet Prob, Spread Gate, Spread Octave, Spread Velocity
    {params::Kind::PATCHED, params::GLOBAL_ARP_RATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RHYTHM},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_POLYPHONY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SEQUENCE_LENGTH},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_NOTE_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_BASS_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_CHORD_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_GATE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_ARP_SPREAD_OCTAVE},
    {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SPREAD_VELOCITY},
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
}};

// VU meter style colours for the automation editor

const RGB rowColour[kDisplayHeight] = {{0, 255, 0},   {36, 219, 0}, {73, 182, 0}, {109, 146, 0},
                                       {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const RGB rowTailColour[kDisplayHeight] = {{2, 53, 2},  {9, 46, 2},  {17, 38, 2}, {24, 31, 2},
                                           {31, 24, 2}, {38, 17, 2}, {46, 9, 2},  {53, 2, 2}};

const RGB rowBlurColour[kDisplayHeight] = {{71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57},
                                           {76, 70, 53},  {77, 60, 48},  {78, 49, 44}, {79, 39, 39}};

const RGB rowBipolarDownColour[kDisplayHeight / 2] = {{255, 0, 0}, {182, 73, 0}, {73, 182, 0}, {0, 255, 0}};

const RGB rowBipolarDownTailColour[kDisplayHeight / 2] = {{53, 2, 2}, {38, 17, 2}, {17, 38, 2}, {2, 53, 2}};

const RGB rowBipolarDownBlurColour[kDisplayHeight / 2] = {{79, 39, 39}, {77, 60, 48}, {73, 90, 62}, {71, 111, 71}};

// colours for the velocity editor

const RGB velocityRowColour[kDisplayHeight] = {{0, 0, 255},   {36, 0, 219}, {73, 0, 182}, {109, 0, 146},
                                               {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}};

const RGB velocityRowTailColour[kDisplayHeight] = {{2, 2, 53},  {9, 2, 46},  {17, 2, 38}, {24, 2, 31},
                                                   {31, 2, 24}, {38, 2, 17}, {46, 2, 9},  {53, 2, 2}};

const RGB velocityRowBlurColour[kDisplayHeight] = {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80},
                                                   {76, 53, 70},  {77, 48, 60},  {78, 44, 49}, {79, 39, 39}};

// lookup tables for the values that are set when you press the pads in each row of the grid
const int32_t nonPatchCablePadPressValues[kDisplayHeight] = {0, 18, 37, 55, 73, 91, 110, 128};
const int32_t patchCablePadPressValues[kDisplayHeight] = {-128, -90, -60, -30, 30, 60, 90, 128};

// lookup tables for the min value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMinPadDisplayValues[kDisplayHeight] = {0, 17, 33, 49, 65, 81, 97, 113};
const int32_t patchCableMinPadDisplayValues[kDisplayHeight] = {-128, -96, -64, -32, 1, 33, 65, 97};

// lookup tables for the max value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMaxPadDisplayValues[kDisplayHeight] = {16, 32, 48, 64, 80, 96, 112, 128};
const int32_t patchCableMaxPadDisplayValues[kDisplayHeight] = {-97, -65, -33, -1, 32, 64, 96, 128};

// summary of pad ranges and press values (format: MIN < PRESS < MAX)
// patch cable:
// y = 7 ::   97 <  128 < 128
// y = 6 ::   65 <   90 <  96
// y = 5 ::   33 <   60 <  64
// y = 4 ::    1 <   30 <  32
// y = 3 ::  -32 <  -30 <  -1
// y = 2 ::  -64 <  -60 < -33
// y = 1 ::  -96 <  -90 < -65
// y = 0 :: -128 < -128 < -97

// non-patch cable:
// y = 7 :: 113 < 128 < 128
// y = 6 ::  97 < 110 < 112
// y = 5 ::  81 <  91 <  96
// y = 4 ::  65 <  73 <  80
// y = 3 ::  49 <  55 <  64
// y = 2 ::  33 <  37 <  48
// y = 1 ::  17 <  18 <  32
// y = 0 ::  0  <   0 <  16

AutomationLayout::AutomationLayout() {

	instrumentClipView.numEditPadPresses = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		instrumentClipView.editPadPresses[i].isActive = false;
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay] = 0;
		instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
		instrumentClipView.auditionPadIsPressed[yDisplay] = 0;
	}

	instrumentClipView.auditioningSilently = false;
	instrumentClipView.timeLastEditPadPress = 0;

	// initialize automation view specific variables
	automationLayoutEditor.interpolation = true;
	automationLayoutEditor.interpolationBefore = false;
	automationLayoutEditor.interpolationAfter = false;
	// used to set parameter shortcut blinking
	parameterShortcutBlinking = false;
	// used to set interpolation shortcut blinking
	interpolationShortcutBlinking = false;
	// used to set pad selection shortcut blinking
	padSelectionShortcutBlinking = false;
	// used to enter pad selection mode
	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	leftPadSelectedX = kNoSelection;
	leftPadSelectedY = kNoSelection;
	rightPadSelectedX = kNoSelection;
	rightPadSelectedY = kNoSelection;
	lastPadSelectedKnobPos = kNoSelection;
	numNotesSelected = 0;
	selectedPadPressed = 0;
	navSysId = NAVIGATION_CLIP;

	initMIDICCShortcutsForAutomation();
	midiCCShortcutsLoaded = false;

	probabilityChanged = false;
	timeSelectKnobLastReleased = 0;
}

void AutomationLayout::initMIDICCShortcutsForAutomation() {
	for (int x = 0; x < kDisplayWidth; x++) {
		for (int y = 0; y < kDisplayHeight; y++) {
			int32_t ccNumber = midiFollow.paramToCC[x][y];
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

// called everytime you open up the automation view
bool AutomationLayout::opened() {
	initialize();

	openedInBackground();

	focusRegained();

	return true;
}

void AutomationLayout::initialize() {
	navSysId = getNavSysId();

	if (!midiCCShortcutsLoaded) {
		initMIDICCShortcutsForAutomation();
		midiCCShortcutsLoaded = true;
	}

	// grab the default setting for interpolation
	automationLayoutEditor.interpolation = FlashStorage::automationInterpolate;

	// re-initialize pad selection mode (so you start with the default automation editor)
	initPadSelection();

	// let the view know if we're dealing with an automation parameter or a note parameter
	setAutomationParamType();

	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	if (!automationView.onArrangerView) {
		// only applies to instrument clips (not audio)
		if (clip) {
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

	// if we're in the note editor and we're in a kit,
	// check that the lastAuditionedYDisplay is in sync with the selected drum
	if (inNoteEditor()) {
		automationLayoutEditor.potentiallyVerticalScrollToSelectedDrum(clip, output);
	}
}

// Initializes some stuff to begin a new editing session
void AutomationLayout::focusRegained() {
	if (automationView.onArrangerView) {
		indicator_leds::setLedState(IndicatorLED::BACK, false);
		indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
		currentSong->affectEntire = true;
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(currentSong);
	}
	else {
		automationView.ClipView::focusRegained();

		Clip* clip = getCurrentClip();
		if (clip->type == ClipType::AUDIO) {
			indicator_leds::setLedState(IndicatorLED::BACK, false);
			indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, true);
			view.focusRegained();
			view.setActiveModControllableTimelineCounter(clip);
		}
		else {
			// check if patch cable previously selected is still valid
			// if not we'll reset parameter selection and go back to overview
			if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
				bool patchCableExists = false;
				ParamManagerForTimeline* paramManager = clip->getCurrentParamManager();
				if (paramManager) {
					PatchCableSet* set = paramManager->getPatchCableSetAllowJibberish();
					// make sure it's not jiberish
					if (set) {
						PatchSource s;
						ParamDescriptor destinationParamDescriptor;
						set->dissectParamId(clip->lastSelectedParamID, &destinationParamDescriptor, &s);
						if (set->getPatchCableIndex(s, destinationParamDescriptor) != kNoSelection) {
							patchCableExists = true;
						}
					}
				}
				if (!patchCableExists) {
					initParameterSelection();
				}
			}
			instrumentClipView.auditioningSilently = false; // Necessary?
			automationView.InstrumentClipMinder::focusRegained();
			instrumentClipView.setLedStates();
		}
	}

	// don't reset shortcut blinking if were still in the menu
	if (getCurrentUI()->getUIType() == UIType::AUTOMATION) {
		// blink timer got reset by view.focusRegained() above
		parameterShortcutBlinking = false;
		interpolationShortcutBlinking = false;
		padSelectionShortcutBlinking = false;
		instrumentClipView.noteRowBlinking = false;
		// remove patch cable blink frequencies
		soundEditor.resetSourceBlinks();
		// possibly restablish parameter shortcut blinking (if parameter is selected)
		blinkShortcuts();
	}
}

void AutomationLayout::openedInBackground() {
	Clip* clip = getCurrentClip();

	if (!automationView.onArrangerView) {
		// used when you're in song view / arranger view / keyboard view
		//(so it knows to come back to automation view)
		clip->onAutomationClipView = true;

		if (clip->type == ClipType::INSTRUMENT) {
			((InstrumentClip*)clip)->onKeyboardScreen = false;

			instrumentClipView.recalculateColours();
		}
	}

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationLayout::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		if (automationView.onArrangerView) {
			arrangerView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                           &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
		else {
			clip->renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                    &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
	}
	else {
		uiNeedsRendering(getRootUI());
	}

	// setup interpolation shortcut blinking when entering automation view from menu
	if (automationView.onMenuView && automationLayoutEditor.interpolation) {
		blinkInterpolationShortcut();
	}
}

// used for the play cursor
void AutomationLayout::graphicsRoutine() {
	if (automationView.onArrangerView) {
		arrangerView.graphicsRoutine();
	}
	else {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			audioClipView.graphicsRoutine();
		}
		else {
			instrumentClipView.graphicsRoutine();
		}
	}
	// if we changed probability, then a pop-up may be currently stuck on display
	// if more than half a second has past since last knob turn, cancel the pop-up
	if (probabilityChanged
	    && ((uint32_t)(AudioEngine::audioSampleTimer - timeSelectKnobLastReleased) >= (kSampleRate / 2))) {
		display->cancelPopup();
		probabilityChanged = false;
	}
}

// rendering
bool AutomationLayout::possiblyRefreshAutomationEditorGrid(Clip* clip, params::Kind paramKind, int32_t paramID) {
	bool doRefreshGrid = false;
	if (clip && !automationView.onArrangerView) {
		if ((clip->lastSelectedParamID == paramID) && (clip->lastSelectedParamKind == paramKind)) {
			doRefreshGrid = true;
		}
	}
	else if (automationView.onArrangerView) {
		if ((currentSong->lastSelectedParamID == paramID) && (currentSong->lastSelectedParamKind == paramKind)) {
			doRefreshGrid = true;
		}
	}
	if (doRefreshGrid) {
		uiNeedsRendering(getRootUI());
		return true;
	}
	return false;
}

// called whenever you call uiNeedsRendering(getRootUI()) somewhere else
// used to render automation overview, automation editor
// used to setup the shortcut blinking
bool AutomationLayout::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	PadLEDs::renderingLock = true;

	Clip* clip = getCurrentClip();
	if (!automationView.onArrangerView && clip->type == ClipType::INSTRUMENT) {
		instrumentClipView.recalculateColours();
	}

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	performActualRender(image, occupancyMask, currentSong->xScroll[navSysId], currentSong->xZoom[navSysId],
	                    kDisplayWidth, kDisplayWidth + kSideBarWidth, drawUndefinedArea);

	PadLEDs::renderingLock = false;

	return true;
}

// determines whether you should render the automation editor, automation overview or just render some love <3
void AutomationLayout::performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                           uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
                                           bool drawUndefinedArea) {

	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	ModelStackWithNoteRow* modelStackWithNoteRow = nullptr;
	int32_t effectiveLength = 0;
	SquareInfo rowSquareInfo[kDisplayWidth];

	if (automationView.onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		if (inNoteEditor()) {
			modelStackWithNoteRow = ((InstrumentClip*)clip)
			                            ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
			                                                 modelStackWithTimelineCounter); // don't create
			effectiveLength = modelStackWithNoteRow->getLoopLength();
			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
				noteRow->getRowSquareInfo(effectiveLength, rowSquareInfo);
			}
		}
	}

	if (!inNoteEditor()) {
		effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);
	}

	params::Kind kind = params::Kind::NONE;
	bool isBipolar = false;

	// if we have a valid model stack with param
	// get the param Kind and param bipolar status
	// so that it can be passed through the automation editor rendering
	// calls below
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		kind = modelStackWithParam->paramCollection->getParamKind();
		isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);
	}

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		// only render if:
		// you're on arranger view
		// you're not in a CV clip type
		// you're not in a kit where you haven't selected a drum and you haven't selected affect entire either
		// you're not in a kit where no sound drum has been selected and you're not editing velocity
		// you're in a kit where midi or CV sound drum has been selected and you're editing velocity
		if (automationView.onArrangerView
		    || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
			bool isMIDICVDrum = false;
			if (outputType == OutputType::KIT && !getAffectEntire()) {
				isMIDICVDrum = (((Kit*)output)->selectedDrum
				                && ((((Kit*)output)->selectedDrum->type == DrumType::MIDI)
				                    || (((Kit*)output)->selectedDrum->type == DrumType::GATE)));
			}

			// if parameter has been selected, show Automation Editor
			if (inAutomationEditor() && !isMIDICVDrum) {
				automationLayoutEditor.renderAutomationEditor(modelStackWithParam, clip, image, occupancyMask,
				                                              renderWidth, xScroll, xZoom, effectiveLength, xDisplay,
				                                              drawUndefinedArea, kind, isBipolar);
			}

			// if note parameter has been selected, show Note Editor
			else if (inNoteEditor()) {
				automationLayoutEditor.renderNoteEditor(modelStackWithNoteRow, (InstrumentClip*)clip, image,
				                                        occupancyMask, renderWidth, xScroll, xZoom, effectiveLength,
				                                        xDisplay, drawUndefinedArea, rowSquareInfo[xDisplay]);
			}

			// if not editing a parameter, show Automation Overview
			else {
				automationLayoutOverview.renderMainPads(modelStackWithTimelineCounter, modelStackWithThreeMainThings,
				                                        clip, outputType, image, occupancyMask, xDisplay, isMIDICVDrum);
			}
		}
		else {
			PadLEDs::clearColumnWithoutSending(xDisplay);
		}
	}
}

// defers to arranger, audio clip or instrument clip sidebar render functions
// depending on the active clip
bool AutomationLayout::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                     uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (automationView.onArrangerView) {
		return arrangerView.renderSidebar(whichRows, image, occupancyMask);
	}
	else {
		return getCurrentClip()->renderSidebar(whichRows, image, occupancyMask);
	}
}

/*render's what is displayed on OLED or 7SEG screens when in Automation View

On Automation Overview:

- on OLED it renders "Automation Overview" (or "Can't Automate CV" if you're on a CV clip)
- on 7Seg it renders AUTO (or CANT if you're on a CV clip)

On Automation Editor:

- on OLED it renders Parameter Name, Automation Status and Parameter Value (for selected Pad or the
current value for the Parameter for the last selected Mod Position)
- on 7SEG it renders Parameter name if no pad is selected or mod encoder is turned. If selecting pad it
displays the pads value for as long as you hold the pad. if turning mod encoder, it displays value while
turning mod encoder. after value displaying is finished, it displays scrolling parameter name again.

This function replaces the two functions that were previously called:

DisplayParameterValue
DisplayParameterName */

void AutomationLayout::renderDisplay(int32_t knobPosLeft, int32_t knobPosRight, bool modEncoderAction) {
	// don't refresh display if we're not current in the automation view UI
	// (e.g. if you're editing automation while in the menu)
	if (getCurrentUI()->getUIType() != UIType::AUTOMATION) {
		return;
	}

	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're not in a MIDI instrument clip, convert the knobPos to the same range as the menu (0-50)
	if (inAutomationEditor() && (automationView.onArrangerView || outputType != OutputType::MIDI_OUT)) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		if (automationView.onArrangerView) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
		}
		if (knobPosLeft != kNoSelection) {
			knobPosLeft = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, knobPosLeft);
		}
		if (knobPosRight != kNoSelection) {
			knobPosRight = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID, knobPosRight);
		}
	}

	// OLED Display
	if (display->haveOLED()) {
		renderDisplayOLED(clip, output, outputType, knobPosLeft, knobPosRight);
	}
	// 7SEG Display
	else {
		renderDisplay7SEG(clip, output, outputType, knobPosLeft, modEncoderAction);
	}
}

void AutomationLayout::renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft,
                                         int32_t knobPosRight) {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	hid::display::OLED::clearMainImage();

	if (onAutomationOverview()) {
		automationLayoutOverview.renderDisplayOLED(canvas, output, outputType);
	}
	else {
		if (inAutomationEditor()) {
			automationLayoutEditor.renderAutomationEditorDisplayOLED(canvas, clip, outputType, knobPosLeft,
			                                                         knobPosRight);
		}
		else {
			automationLayoutEditor.renderNoteEditorDisplayOLED(canvas, (InstrumentClip*)clip, outputType, knobPosLeft,
			                                                   knobPosRight);
		}
	}

	deluge::hid::display::OLED::markChanged();
}

void AutomationLayout::renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft,
                                         bool modEncoderAction) {
	// display OVERVIEW
	if (onAutomationOverview()) {
		automationLayoutOverview.renderDisplay7SEG(output, outputType);
	}
	else {
		if (inAutomationEditor()) {
			automationLayoutEditor.renderAutomationEditorDisplay7SEG(clip, outputType, knobPosLeft, modEncoderAction);
		}
		else {
			automationLayoutEditor.renderNoteEditorDisplay7SEG((InstrumentClip*)clip, outputType, knobPosLeft);
		}
	}
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationLayout::displayAutomation(bool padSelected, bool updateDisplay) {
	automationLayoutEditor.displayAutomation(padSelected, updateDisplay);
}

// button action

ActionResult AutomationLayout::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace hid::button;

	Clip* clip = getCurrentClip();
	bool isAudioClip = clip->type == ClipType::AUDIO;

	// these button actions are not used in the audio clip automation view
	if (isAudioClip || automationView.onArrangerView) {
		if (b == SCALE_MODE || b == KEYBOARD || b == KIT || b == SYNTH || b == MIDI || b == CV) {
			return ActionResult::DEALT_WITH;
		}
	}
	if (automationView.onArrangerView) {
		if (b == CLIP_VIEW) {
			return ActionResult::DEALT_WITH;
		}
	}

	OutputType outputType = clip->output->type;

	// Scale mode button
	if (b == SCALE_MODE) {
		return instrumentClipView.handleScaleButtonAction(on, inCardRoutine);
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		handleSessionButtonAction(clip, on);
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		handleKeyboardButtonAction(on);
	}

	// Clip button - exit mode
	// if you're holding shift or holding an audition pad while pressed clip, don't exit out of
	// automation view reset parameter selection and short blinking instead
	else if (b == CLIP_VIEW) {
		handleClipButtonAction(on, isAudioClip);
	}

	// Auto scrolling
	// Or Cross Screen Note Editing in Note Editor
	// Does not currently work for Automation
	else if (b == CROSS_SCREEN_EDIT) {
		// toggle auto scroll or cross screen editing
		if (automationView.onArrangerView || inNoteEditor()) {
			handleCrossScreenButtonAction(on);
		}
		// don't toggle for automation editing
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == KIT) {
		handleKitButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		handleSynthButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == MIDI) {
		handleMidiButtonAction(outputType, on);
	}

	// when switching clip type, reset parameter selection and shortcut blinking
	else if (b == CV) {
		handleCVButtonAction(outputType, on);
	}

	// Horizontal encoder button
	// Not relevant for arranger view
	else if (b == X_ENC) {
		if (handleHorizontalEncoderButtonAction(on, isAudioClip)) {
			goto passToOthers;
		}
	}

	// if holding horizontal encoder button down and pressing back clear automation
	// if you're on automation overview, clear all automation
	// if you're in the automation editor, clear the automation for the parameter in focus
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (handleBackAndHorizontalEncoderButtonComboAction(clip, on)) {
			goto passToOthers;
		}
	}

	// Vertical encoder button
	// Not relevant for audio clip
	else if (b == Y_ENC && !isAudioClip) {
		handleVerticalEncoderButtonAction(on);
	}

	// Select encoder
	// if you're not pressing shift and press down on the select encoder, enter sound menu
	else if (!Buttons::isShiftButtonPressed() && b == SELECT_ENC) {
		handleSelectEncoderButtonAction(on);
	}

	else {
passToOthers:
		// if you're entering settings menu
		if (on && (b == SELECT_ENC) && Buttons::isShiftButtonPressed()) {
			if (padSelectionOn) {
				initPadSelection();
			}
		}

		// if you just toggle playback off, re-render 7SEG display
		if (!on && (b == PLAY) && display->have7SEG() && inAutomationEditor() && !padSelectionOn
		    && !playbackHandler.isEitherClockActive()) {
			renderDisplay();
		}

		uiNeedsRendering(getRootUI());

		ActionResult result;
		if (automationView.onArrangerView) {
			result = automationView.TimelineView::buttonAction(b, on, inCardRoutine);
		}
		else if (isAudioClip) {
			result = automationView.ClipMinder::buttonAction(b, on);
		}
		else {
			result = automationView.InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		}
		if (result == ActionResult::NOT_DEALT_WITH) {
			result = automationView.ClipView::buttonAction(b, on, inCardRoutine);
		}

		// when you press affect entire, the parameter selection needs to reset
		// do this here because affect entire state may have just changed
		if (on && b == AFFECT_ENTIRE) {
			initParameterSelection();
			blinkShortcuts();
		}

		return result;
	}

	if (on && (b != KEYBOARD && b != CLIP_VIEW && b != SESSION_VIEW)) {
		uiNeedsRendering(getRootUI());
	}

	return ActionResult::DEALT_WITH;
}

// called by button action if b == SESSION_VIEW
void AutomationLayout::handleSessionButtonAction(Clip* clip, bool on) {
	// if shift is pressed, go back to automation overview
	if (on && Buttons::isShiftButtonPressed()) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(getRootUI());
	}
	// go back to song / arranger view
	else if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		if (automationView.onArrangerView) {
			automationView.onArrangerView = false;
			changeRootUI(&arrangerView);
		}
		else if (currentSong->lastClipInstanceEnteredStartPos != -1 || clip->isArrangementOnlyClip()) {
			bool success = arrangerView.transitionToArrangementEditor();
			if (!success) {
				goto doOther;
			}
		}
		else {
doOther:
			sessionView.transitionToSessionView();
		}
		resetShortcutBlinking();
	}
}

// called by button action if b == KEYBOARD
void AutomationLayout::handleKeyboardButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		changeRootUI(&keyboardScreen);
		// reset blinking if you're leaving automation view for keyboard view
		// blinking will be reset when you come back
		resetShortcutBlinking();
	}
}

// called by button action if b == CLIP_VIEW
void AutomationLayout::handleClipButtonAction(bool on, bool isAudioClip) {
	// if audition pad or shift is pressed, go back to automation overview
	if (on && (currentUIMode == UI_MODE_AUDITIONING || Buttons::isShiftButtonPressed())) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(getRootUI());
	}
	// go back to clip view
	else if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		if (padSelectionOn) {
			initPadSelection();
		}
		if (isAudioClip) {
			changeRootUI(&audioClipView);
		}
		else {
			changeRootUI(&instrumentClipView);
		}
		resetShortcutBlinking();
	}
}

// call by button action if b == CROSS_SCREEN_EDIT
void AutomationLayout::handleCrossScreenButtonAction(bool on) {
	if (!on && currentUIMode == UI_MODE_NONE) {
		// if another button wasn't pressed while cross screen was held
		if (Buttons::considerCrossScreenReleaseForCrossScreenMode) {
			if (automationView.onArrangerView) {
				currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

				if (currentSong->arrangerAutoScrollModeActive) {
					arrangerView.reassessWhetherDoingAutoScroll();
				}
				else {
					arrangerView.doingAutoScrollNow = false;
				}
			}
			else {
				InstrumentClip* clip = getCurrentInstrumentClip();
				if (clip) {
					if (clip->wrapEditing) {
						clip->wrapEditing = false;
					}
					else {
						clip->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;
						// Ensure that there are actually multiple screens to edit across
						if (clip->wrapEditLevel < clip->loopLength) {
							clip->wrapEditing = true;
						}
						// If in we're in the note editor, we can check if the note row has multiple screens
						else if (inNoteEditor()) {
							char modelStackMemory[MODEL_STACK_MAX_SIZE];
							ModelStackWithTimelineCounter* modelStack =
							    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
							ModelStackWithNoteRow* modelStackWithNoteRow =
							    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
							                             modelStack); // don't create
							if (clip->wrapEditLevel < modelStackWithNoteRow->getLoopLength()) {
								clip->wrapEditing = true;
							}
						}
					}

					automationView.setLedStates();
				}
			}
		}
	}
}

// called by button action if b == KIT
void AutomationLayout::handleKitButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to create a new instrument or change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		if (Buttons::isShiftButtonPressed()) {
			instrumentClipView.createNewInstrument(OutputType::KIT);
		}
		else {
			instrumentClipView.changeOutputType(OutputType::KIT);
		}
	}
}

// called by button action if b == SYNTH
void AutomationLayout::handleSynthButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to create a new instrument or change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		// this gets triggered when you change an existing clip to synth / create a new synth clip in
		// song mode
		if (Buttons::isShiftButtonPressed()) {
			instrumentClipView.createNewInstrument(OutputType::SYNTH);
		}
		// this gets triggered when you change clip type to synth from within inside clip view
		else {
			instrumentClipView.changeOutputType(OutputType::SYNTH);
		}
	}
}

// called by button action if b == MIDI
void AutomationLayout::handleMidiButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.changeOutputType(OutputType::MIDI_OUT);
	}
}

// called by button action if b == CV
void AutomationLayout::handleCVButtonAction(OutputType outputType, bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		// if you're going to change output type,
		// reset selection
		initParameterSelection();
		blinkShortcuts();

		instrumentClipView.changeOutputType(OutputType::CV);
	}
}
// called by button action if b == X_ENC
bool AutomationLayout::handleHorizontalEncoderButtonAction(bool on, bool isAudioClip) {
	// copy / paste automation (same shortcut used for notes)
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (inAutomationEditor()) {
			Clip* clip = getCurrentClip();
			OutputType outputType = clip->output->type;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (automationView.onArrangerView) {
				modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			else {
				modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}
			int32_t effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);

			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];

			if (Buttons::isShiftButtonPressed()) {
				// paste within Automation Editor
				automationLayoutEditor.pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
			}
			else {
				// copy within Automation Editor
				automationLayoutEditor.copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
			}
		}
		return false;
	}
	else if (automationView.onArrangerView) {
		return true;
	}
	else if (isAudioClip) {
		// removing time stretching by re-calculating clip length based on length of audio sample
		if (on && Buttons::isButtonPressed(deluge::hid::button::Y_ENC) && currentUIMode == UI_MODE_NONE) {
			audioClipView.setClipLengthEqualToSampleLength();
			return false;
		}
		// if shift is pressed then we're resizing the clip without time stretching
		else if (Buttons::isShiftButtonPressed()) {
			return false;
		}
		return true;
	}
	// If user wants to "multiply" Clip contents
	else if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
	         && !onAutomationOverview()) {
		if (isNoUIModeActive()) {
			// Zoom to max if we weren't already there...
			if (!automationView.zoomToMax()) {
				// Or if we didn't need to do that, double Clip length
				instrumentClipView.doubleClipLengthAction();
			}
			else {
				automationView.displayZoomLevel();
			}
		}
		// Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for
		// rotating individual NoteRow
		enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
	}

	// Otherwise...
	else {
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			if (!on) {
				instrumentClipView.timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
			}
		}
		return true;
	}
	return false;
}

// called by button action if b == back and UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON
bool AutomationLayout::handleBackAndHorizontalEncoderButtonComboAction(Clip* clip, bool on) {
	// only allow clearing of a clip if you're on the automation overview
	if (on && onAutomationOverview()) {
		return automationLayoutOverview.handleBackAndHorizontalEncoderButtonComboAction(clip, on);
	}
	else if (on && inAutomationEditor()) {
		// delete automation of current parameter selected

		char modelStackMemory[MODEL_STACK_MAX_SIZE];

		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (automationView.onArrangerView) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
		}

		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}
	else if (on && inNoteEditor()) {
		Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

		// don't create note row if it doesn't exist
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)clip)->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
			// don't clear automation, do clear notes and mpe
			noteRow->clear(action, modelStackWithNoteRow, false, true);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_CLEARED));
		}
	}
	return false;
}

// handle by button action if b == Y_ENC
void AutomationLayout::handleVerticalEncoderButtonAction(bool on) {
	if (on) {
		if (inNoteEditor()) {
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				// Just pop up number - don't do anything
				instrumentClipView.editNoteRepeat(0);
			}
			else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    ((InstrumentClip*)modelStack->getTimelineCounter())
				        ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay, modelStack);

				// Just pop up number - don't do anything
				instrumentClipView.editNumEuclideanEvents(modelStackWithNoteRow, 0,
				                                          instrumentClipView.lastAuditionedYDisplay);
			}
		}
	}
}

// called by button action if b == SELECT_ENC and shift button is not pressed
void AutomationLayout::handleSelectEncoderButtonAction(bool on) {
	if (on && (currentUIMode == UI_MODE_NONE || (currentUIMode == UI_MODE_NOTES_PRESSED && padSelectionOn))) {
		initParameterSelection();
		uiNeedsRendering(getRootUI());

		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return;
		}

		if ((getCurrentOutputType() == OutputType::KIT) && (getCurrentInstrumentClip()->affectEntire)) {
			soundEditor.setupKitGlobalFXMenu = true;
		}

		display->setNextTransitionDirection(1);
		Clip* clip = automationView.onArrangerView ? nullptr : getCurrentClip();
		if (soundEditor.setup(clip)) {
			openUI(&soundEditor);
		}
	}
}

// pad action
// handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
// everything else is pretty much the same as instrument clip view
ActionResult AutomationLayout::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	Clip* clip = nullptr;
	Output* output = nullptr;
	OutputType outputType;

	if (!automationView.onArrangerView) {
		clip = getCurrentClip();
		if (clip && clip->output) {
			output = clip->output;
			outputType = output->type;
		}
		else {
			return ActionResult::DEALT_WITH;
		}
	}

	// Interacting with main pads
	if (x < kDisplayWidth) {
		return handleEditPadAction(clip, output, outputType, x, y, velocity);
	}
	// Interacting with side bar
	else {
		// arranger automation view
		if (automationView.onArrangerView) {
			// don't interact with sidebar if VU Meter is displayed
			if (view.displayVUMeter) {
				return ActionResult::DEALT_WITH;
			}
		}
		// clip automation view
		else {
			clip = getCurrentClip();
			// no sidebar to interact with in audio clips
			if (clip && (clip->type == ClipType::AUDIO)) {
				return ActionResult::DEALT_WITH;
			}
		}

		// mute / status pad action
		if (x == kDisplayWidth) {
			return handleMutePadAction((InstrumentClip*)clip, output, outputType, y, velocity);
		}
		// Audition pad action
		else {
			if (x == kDisplayWidth + 1) {
				return handleAuditionPadAction((InstrumentClip*)clip, output, outputType, y, velocity);
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the main grid (x < kDisplayWidth)
ActionResult AutomationLayout::handleEditPadAction(Clip* clip, Output* output, OutputType outputType, int32_t x,
                                                   int32_t y, int32_t velocity) {

	// if we're in arranger automation view and holding audition pad, ignore main pad press
	if (automationView.onArrangerView) {
		if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
			return ActionResult::DEALT_WITH;
		}
	}
	// if we're in a midi clip, with a midi cc selected
	// and we press the name shortcut while holding shift
	// then enter the rename midi cc UI
	else if (outputType == OutputType::MIDI_OUT && !onAutomationOverview()) {
		if (Buttons::isShiftButtonPressed() && x == 11 && y == 5) {
			openUI(&renameMidiCCUI);
			return ActionResult::DEALT_WITH;
		}
	}

	// this code can be moved to the editor

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithNoteRow* modelStackWithNoteRow = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t effectiveLength = 0;
	SquareInfo squareInfo;

	if (!automationView.onArrangerView) {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	}

	ModelStackWithAutoParam* modelStackWithParam =
	    getModelStackWithParam(modelStackMemory, modelStackWithTimelineCounter, clip);

	if (inNoteEditor()) {
		modelStackWithNoteRow = ((InstrumentClip*)clip)
		                            ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
		                                                 modelStackWithTimelineCounter); // don't create
		// does note row exist?
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
			// if you're in note editor and note row doesn't exist, create it
			// don't create note rows that don't exist in kits because those are empty kit rows
			if (outputType != OutputType::KIT) {
				modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(
				    modelStackWithTimelineCounter, instrumentClipView.lastAuditionedYDisplay);
			}
		}

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			effectiveLength = modelStackWithNoteRow->getLoopLength();
			noteRow = modelStackWithNoteRow->getNoteRow();
			noteRow->getSquareInfo(x, effectiveLength, squareInfo);
		}
	}
	else {
		effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);
	}

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// if the user wants to change the parameter they are editing using Shift + Pad shortcut
	// or change the parameter they are editing by press on a shortcut pad on automation overview
	// or they want to enable/disable interpolation
	// or they want to enable/disable pad selection mode
	if (shortcutPadAction(modelStackWithParam, clip, output, outputType, effectiveLength, x, y, velocity, xScroll,
	                      xZoom, squareInfo)) {
		return ActionResult::DEALT_WITH;
	}

	// regular automation / note editing action
	if (isUIModeWithinRange(editPadActionUIModes) && automationView.isSquareDefined(x, xScroll, xZoom)) {
		if (inAutomationEditor()) {
			automationLayoutEditor.automationEditPadAction(modelStackWithParam, clip, x, y, velocity, effectiveLength,
			                                               xScroll, xZoom);
		}
		else if (inNoteEditor()) {
			if (noteRow) {
				automationLayoutEditor.noteEditPadAction(modelStackWithNoteRow, noteRow, (InstrumentClip*)clip, x, y,
				                                         velocity, effectiveLength, squareInfo);
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

/// handles shortcut pad actions, including:
/// 1) toggle interpolation on / off
/// 2) select parameter on automation overview
/// 3) select parameter using shift + shortcut pad
/// 4) select parameter using audition + shortcut pad
bool AutomationLayout::shortcutPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, Output* output,
                                         OutputType outputType, int32_t effectiveLength, int32_t x, int32_t y,
                                         int32_t velocity, int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo) {
	if (velocity) {
		bool shortcutPress = false;
		if (Buttons::isShiftButtonPressed()
		    || (isUIModeActive(UI_MODE_AUDITIONING) && !FlashStorage::automationDisableAuditionPadShortcuts)) {

			if (inAutomationEditor()) {
				// toggle interpolation on / off
				if (x == kInterpolationShortcutX && y == kInterpolationShortcutY) {
					return automationLayoutEditor.toggleAutomationInterpolation();
				}
				// toggle pad selection on / off
				else if (x == kPadSelectionShortcutX && y == kPadSelectionShortcutY) {
					return automationLayoutEditor.toggleAutomationPadSelectionMode(modelStackWithParam, effectiveLength,
					                                                               xScroll, xZoom);
				}
			}

			shortcutPress = true;
		}
		// this means you are selecting a parameter
		if (shortcutPress || onAutomationOverview()) {
			// don't change parameters this way if we're in the menu
			if (getCurrentUI()->getUIType() == UIType::AUTOMATION) {
				// make sure the context is valid for selecting a parameter
				// can't select a parameter in a kit if you haven't selected a drum
				if (automationView.onArrangerView
				    || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)
				    || (outputType == OutputType::KIT && getAffectEntire())) {

					handleParameterSelection(clip, output, outputType, x, y);

					// if you're in not in note editor, turn led off if it's on
					if (((InstrumentClip*)clip)->wrapEditing) {
						indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, inNoteEditor());
					}
				}
			}

			return true;
		}
	}
	return false;
}

// called by shortcutPadAction when it is determined that you are selecting a parameter on automation
// overview or by using a grid shortcut combo
void AutomationLayout::handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
                                                int32_t yDisplay) {
	// PatchSource::Velocity shortcut
	// Enter Velocity Note Editor
	if (xDisplay == kVelocityShortcutX && yDisplay == kVelocityShortcutY) {
		if (clip->type == ClipType::INSTRUMENT) {
			// don't enter if we're in a kit with affect entire enabled
			if (!(outputType == OutputType::KIT && getAffectEntire())) {
				automationLayoutEditor.potentiallyVerticalScrollToSelectedDrum((InstrumentClip*)clip, output);
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
	else if (!automationView.onArrangerView
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
	else if ((automationView.onArrangerView || (outputType == OutputType::AUDIO)
	          || (outputType == OutputType::KIT && getAffectEntire()))
	         && (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID)) {

		params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
		int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

		// don't allow automation of pitch adjust, or sidechain in arranger
		if (automationView.onArrangerView
		    && ((paramID == params::UNPATCHED_PITCH_ADJUST) || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
		        || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME))) {
			return; // no parameter selected, don't re-render grid;
		}

		if (automationView.onArrangerView) {
			currentSong->lastSelectedParamKind = paramKind;
			currentSong->lastSelectedParamID = paramID;
		}
		else {
			clip->lastSelectedParamKind = paramKind;
			clip->lastSelectedParamID = paramID;
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
	if (automationView.onArrangerView) {
		currentSong->lastSelectedParamShortcutX = xDisplay;
		currentSong->lastSelectedParamShortcutY = yDisplay;
	}
	else {
		clip->lastSelectedParamShortcutX = xDisplay;
		clip->lastSelectedParamShortcutY = yDisplay;
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

// called by pad action when pressing a pad in the mute column (x = kDisplayWidth)
ActionResult AutomationLayout::handleMutePadAction(InstrumentClip* instrumentClip, Output* output,
                                                   OutputType outputType, int32_t y, int32_t velocity) {
	if (automationView.onArrangerView) {
		return arrangerView.handleStatusPadAction(y, velocity, getRootUI());
	}
	else {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (outputType != OutputType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = instrumentClip->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			if (inAutomationEditor()) {
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    instrumentClip->getNoteRowOnScreen(y, modelStackWithTimelineCounter);

				// if we're in a kit, and you press a mute pad
				// check if it's a mute pad corresponding to the current selected drum
				// if not, change the drum selection, refresh parameter selection and go back to automation overview
				if (outputType == OutputType::KIT) {
					if (modelStackWithNoteRow->getNoteRowAllowNull()) {
						Drum* drum = modelStackWithNoteRow->getNoteRow()->drum;
						if (((Kit*)output)->selectedDrum != drum) {
							if (!getAffectEntire()) {
								initParameterSelection();
							}
						}
					}
				}
			}

			instrumentClipView.mutePadPress(y);
		}
	}
	return ActionResult::DEALT_WITH;
}

// called by pad action when pressing a pad in the audition column (x = kDisplayWidth + 1)
ActionResult AutomationLayout::handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output,
                                                       OutputType outputType, int32_t y, int32_t velocity) {
	if (automationView.onArrangerView) {
		if (onAutomationOverview()) {
			return arrangerView.handleAuditionPadAction(y, velocity, getRootUI());
		}
	}
	else {
		// "Learning" to this audition pad:
		if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
			if (getCurrentUI()->getUIType() == UIType::AUTOMATION) {
				if (outputType == OutputType::KIT) {
					NoteRow* thisNoteRow = instrumentClip->getNoteRowOnScreen(y, currentSong);
					if (!thisNoteRow || !thisNoteRow->drum) {
						return ActionResult::DEALT_WITH;
					}
					view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum, (Kit*)output);
				}
				else {
					view.instrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)output);
				}
			}
		}

		// Actual basic audition pad press:
		else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
			/* 	special handling of audition pad action for note editing mode:

			    when we're in note editor mode, pressing audition pad changes note row selection
			    in this case, when pressing audition pad, only allow audition pad actions
			    if we're in pad selection mode as in pad selection mode we're only ever selecting
			    one column at a time, so this makes it easier to release the selection before
			    selecting a new note row
			*/

			int32_t previousY = instrumentClipView.lastAuditionedYDisplay;

			// are we in note editor mode and holding a note? don't process audition pad action
			if (previousY != y && inNoteEditor() && isUIModeActive(UI_MODE_NOTES_PRESSED)) {
				return ActionResult::DEALT_WITH;
			}

			// process audition pad action
			auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());

			// now that we've processed audition pad action, we may now have changed note row selection
			// if note row selection has changed, and we're in pad selection mode
			// we'll re-select the previous column selection by recording a pad press
			if (previousY != instrumentClipView.lastAuditionedYDisplay && inNoteEditor() && padSelectionOn
			    && leftPadSelectedX != kNoSelection) {
				automationLayoutEditor.recordNoteEditPadAction(leftPadSelectedX, 1);
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

// audition pad action
// not used with Audio Clip Automation View or Arranger Automation View
void AutomationLayout::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {
	if (instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan && !velocity) {
		// in case we were editing quantize/humanize
		actionLogger.closeAction(ActionType::NOTE_NUDGE);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument =
	    automationView.InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	bool isKit = (outputType == OutputType::KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    clip->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = nullptr;

	bool selectedDrumChanged = false;
	bool selectedRowChanged = false;
	bool drawNoteCode = false;

	// If Kit...
	if (isKit) {

		// if we're in a kit, and you press an audition pad
		// check if it's a audition pad corresponding to the current selected drum
		// also check that you're not in affect entire mode
		// if not, change the drum selection, refresh parameter selection and go back to automation
		// overview
		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
			Drum* selectedDrum = ((Kit*)output)->selectedDrum;
			if (selectedDrum != drum) {
				selectedDrumChanged = true;
			}
		}

		// If NoteRow doesn't exist here, don't try to create one
		else {
			return;
		}
	}

	// Or if synth
	else if (outputType == OutputType::SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
				                                              + ((SoundInstrument*)output)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(clip)) {

		// Note-on
		if (velocity) {

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see
			// noteMessageReceived() in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				if (isKit) {
					if (drum) {
						drum->recordNoteOnEarly(
						    (velocity == USE_DEFAULT_VELOCITY) ? ((Instrument*)output)->defaultVelocity : velocity,
						    clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					// NoteRow is allowed to be NULL in this case.
					int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)output)
					    ->earlyNotes.emplace(yNote, MelodicInstrument::EarlyNoteInfo{
					                                    ((Instrument*)output)->defaultVelocity,
					                                    clip->allowNoteTails(modelStackWithNoteRowOnCurrentClip),
					                                });
				}
			}

			else {

				// May need to create NoteRow if there wasn't one previously
				if (!modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

					modelStackWithNoteRowOnCurrentClip =
					    instrumentClipView.createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					clip->recordNoteOn(modelStackWithNoteRowOnCurrentClip, (velocity == USE_DEFAULT_VELOCITY)
					                                                           ? ((Instrument*)output)->defaultVelocity
					                                                           : velocity);
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				clip->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
			}
		}
	}

	{
		NoteRow* noteRowOnActiveClip;

		if (clipIsActiveOnInstrument) {
			noteRowOnActiveClip = modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull();
		}

		else {
			// Kit
			if (isKit) {
				noteRowOnActiveClip = ((InstrumentClip*)output->getActiveClip())->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int32_t yNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)output->getActiveClip())->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = ((Instrument*)output)->defaultVelocity;
			}

			// Yup, need to do this even if we're going to do a "silent" audition, so pad lights up etc.
			instrumentClipView.auditionPadIsPressed[yDisplay] = velocityToSound;

			if (noteRowOnActiveClip) {
				// Ensure our auditioning doesn't override a note playing in the sequence
				if (playbackHandler.isEitherClockActive() && noteRowOnActiveClip->sequenced) {
					goto doSilentAudition;
				}
			}

			// If won't be actually sounding Instrument...
			if (shiftButtonDown || Buttons::isButtonPressed(hid::button::Y_ENC)) {

				instrumentClipView.fileBrowserShouldNotPreview = true;
doSilentAudition:
				instrumentClipView.auditioningSilently = true;
				instrumentClipView.reassessAllAuditionStatus();
			}
			else {
				if (!instrumentClipView.auditioningSilently) {

					instrumentClipView.fileBrowserShouldNotPreview = false;

					instrumentClipView.sendAuditionNote(true, yDisplay, velocityToSound, 0);

					{ instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = velocityToSound; }
				}
			}

			// If wasn't already auditioning...
			if (!isUIModeActive(UI_MODE_AUDITIONING)) {
				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			drawNoteCode = true;

			if (!isKit && (instrumentClipView.lastAuditionedYDisplay != yDisplay)) {
				selectedRowChanged = true;
			}

			instrumentClipView.lastAuditionedYDisplay = yDisplay;

			// are we in a synth / midi / cv clip
			// and have we changed our note row selection
			if (selectedRowChanged) {
				instrumentClipView.potentiallyRefreshNoteRowMenu();
			}

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}

			if (isKit) {
				instrumentClipView.setSelectedDrum(drum);
			}
		}

		// Or if auditioning this NoteRow just finished...
		else {
			if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
				instrumentClipView.auditionPadIsPressed[yDisplay] = 0;
				instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;

				// Stop the note sounding - but only if a sequenced note isn't in fact being played here.
				// Or if it's drone note, end auditioning to transfer the note's sustain to the sequencer
				if (!noteRowOnActiveClip || !noteRowOnActiveClip->sequenced
				    || noteRowOnActiveClip->isDroning(modelStackWithNoteRowOnCurrentClip->getLoopLength())) {
					instrumentClipView.sendAuditionNote(false, yDisplay, 64, 0);
				}
			}
			display->cancelPopup();
			// don't recalculateLastAuditionedNoteOnScreen if we're in the note editor because it
			// messes up the note row selection	for velocity editing
			instrumentClipView.someAuditioningHasEnded(!inNoteEditor());
			actionLogger.closeAction(ActionType::EUCLIDEAN_NUM_EVENTS_EDIT);
			actionLogger.closeAction(ActionType::NOTEROW_ROTATE);
			if (display->have7SEG()) {
				renderDisplay();
			}
		}
	}

getOut:

	if (selectedRowChanged || (selectedDrumChanged && (!getAffectEntire() || inNoteEditor()))) {
		if (inNoteEditor()) {
			renderDisplay();
			instrumentClipView.resetSelectedNoteRowBlinking();
			instrumentClipView.blinkSelectedNoteRow(0xFFFFFFFF);
		}
		else if (selectedDrumChanged) {
			initParameterSelection();
			uiNeedsRendering(getRootUI());
		}
		else {
			renderingNeededRegardlessOfUI(0, 1 << yDisplay);
		}
	}
	else {
		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

	// draw note code on top of the automation view display which may have just been refreshed
	// don't draw if you're in note editor because note code is already on the display
	if (drawNoteCode && !inNoteEditor()) {
		instrumentClipView.drawNoteCode(yDisplay);
	}

	// This has to happen after instrumentClipView.setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

// horizontal encoder actions:
// scroll left / right
// zoom in / out
// adjust clip length
// shift automations left / right
// adjust velocity in note editor
ActionResult AutomationLayout::horizontalEncoderAction(int32_t offset) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
	}

	if (inAutomationEditor()) {
		// exit multi pad press selection but keep single pad press selection (if it's selected)
		multiPadPressSelected = false;
		rightPadSelectedX = kNoSelection;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (automationView.onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	}

	if (!onAutomationOverview()
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		if (inAutomationEditor()) {
			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];
			int32_t squareSize =
			    automationView.getPosFromSquare(1, xScroll, xZoom) - automationView.getPosFromSquare(0, xScroll, xZoom);
			int32_t shiftAmount = offset * squareSize;

			if (automationView.onArrangerView) {
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			else {
				Clip* clip = getCurrentClip();
				modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}

			int32_t effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);

			shiftAutomationHorizontally(modelStackWithParam, shiftAmount, effectiveLength);

			if (offset < 0) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_LEFT));
			}
			else if (offset > 0) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_SHIFT_RIGHT));
			}
		}
		else if (inNoteEditor()) {
			instrumentClipView.rotateNoteRowHorizontally(offset);
		}

		return ActionResult::DEALT_WITH;
	}

	// else if showing the Parameter selection grid menu, disable this action
	else if (onAutomationOverview()) {
		return automationLayoutOverview.horizontalEncoderAction(offset);
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		instrumentClipView.editNoteRowLength(offset);
		return ActionResult::DEALT_WITH;
	}

	// fine tune note velocity
	// If holding down notes and nothing else is held down, adjust velocity
	else if (inNoteEditor() && isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
		if (automationView.automationParamType == AutomationParamType::NOTE_VELOCITY) {
			if (!instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				instrumentClipView.adjustVelocity(offset);
				renderDisplay(getCurrentInstrument()->defaultVelocity);
				uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Shift and x pressed - edit length of audio clip without timestretching
	else if (getCurrentClip()->type == ClipType::AUDIO && isNoUIModeActive()
	         && Buttons::isButtonPressed(deluge::hid::button::X_ENC) && Buttons::isShiftButtonPressed()) {
		ActionResult result = audioClipView.editClipLengthWithoutTimestretching(offset);
		return result;
	}

	// Or, let parent deal with it
	else {
		return automationView.ClipView::horizontalEncoderAction(offset);
	}
}

// new function created for automation instrument clip view to shift automations of the selected
// parameter previously users only had the option to shift ALL automations together as part of community
// feature i disabled automation shifting in the regular instrument clip view
void AutomationLayout::shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
                                                   int32_t effectiveLength) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		modelStackWithParam->autoParam->shiftHorizontally(offset, effectiveLength);
	}

	uiNeedsRendering(getRootUI());
}

// vertical encoder action
// no change compared to instrument clip view version
// not used with Audio Clip Automation View
ActionResult AutomationLayout::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (automationView.onArrangerView) {
		if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
			if (Buttons::isShiftButtonPressed()) {
				currentSong->adjustMasterTransposeInterval(offset);
			}
			else {
				currentSong->transpose(offset);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return ActionResult::DEALT_WITH;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	OutputType outputType = clip->output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If encoder button pressed
	if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
		if (inNoteEditor() && currentUIMode != UI_MODE_NONE) {
			// only allow editing note repeats when selecting a note
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				instrumentClipView.editNoteRepeat(offset);
			}
			// only allow euclidean while holding audition pad
			else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
				                             modelStack); // don't create
				if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
					if (clip->output->type != OutputType::KIT) {
						modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(
						    modelStack, instrumentClipView.lastAuditionedYDisplay);
					}
				}

				instrumentClipView.editNumEuclideanEvents(modelStackWithNoteRow, offset,
				                                          instrumentClipView.lastAuditionedYDisplay);
				instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;
				instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			}
		}
		// If user not wanting to move a noteCode, they want to transpose the key
		else if (!currentUIMode && outputType != OutputType::KIT) {
			actionLogger.deleteAllLogs();

			auto nudgeType = Buttons::isShiftButtonPressed() ? VerticalNudgeType::ROW : VerticalNudgeType::OCTAVE;
			clip->nudgeNotesVertically(offset, nudgeType, modelStack);

			instrumentClipView.recalculateColours();
			uiNeedsRendering(getRootUI(), 0, 0xFFFFFFFF);
			if (inNoteEditor()) {
				renderDisplay();
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (outputType != OutputType::KIT) {
					goto shiftAllColour;
				}

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (instrumentClipView.auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						// This is fine. If we were in Kit mode, we could only be auditioning if there
						// was a NoteRow already
						if (noteRow) {
							noteRow->colourOffset += offset;
							if (noteRow->colourOffset >= 72) {
								noteRow->colourOffset -= 72;
							}
							if (noteRow->colourOffset < 0) {
								noteRow->colourOffset += 72;
							}
							instrumentClipView.recalculateColour(yDisplay);
							whichRowsToRender |= (1 << yDisplay);
						}
					}
				}
			}
		}

		// Otherwise, adjust whole colour spectrum
		else if (currentUIMode == UI_MODE_NONE) {
shiftAllColour:
			clip->colourOffset += offset;
			instrumentClipView.recalculateColours();
			whichRowsToRender = 0xFFFFFFFF;
		}

		if (whichRowsToRender) {
			uiNeedsRendering(getRootUI(), whichRowsToRender, whichRowsToRender);
		}
	}

	// If neither button is pressed, we'll do vertical scrolling
	else {
		if (isUIModeWithinRange(verticalScrollUIModes)) {
			if ((!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			     || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING)))
			    && (!(isUIModeActive(UI_MODE_NOTES_PRESSED) && inNoteEditor() && !padSelectionOn))) {
				// if we're in the note editor pad selection mode and vertical scrolling,
				// we want to end any presses first (which will end any note auditioning as well)
				if (inNoteEditor() && padSelectionOn) {
					instrumentClipView.endAllEditPadPresses();
				}

				scrollVertical(offset);

				// if we're in note editor pad selection mode, scrolling vertically will change note selected
				// so we want to re-render the display to show the updated note
				if (inNoteEditor()) {
					// if we're in pad selection mode, we will have de-selected the pad presses above
					// and now we want to re-instate the pad press for the selected note row
					// so that we can re-audition the selected note
					if (padSelectionOn && leftPadSelectedX != kNoSelection) {
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    ((InstrumentClip*)clip)
						        ->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
						                             modelStack); // don't create
						if (modelStackWithNoteRow->getNoteRowAllowNull()) {
							NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
							int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();
							SquareInfo squareInfo;
							noteRow->getSquareInfo(leftPadSelectedX, effectiveLength, squareInfo);
							numNotesSelected = squareInfo.numNotes;

							if (numNotesSelected != 0) {
								// select note if there are notes in this square
								automationLayoutEditor.recordNoteEditPadAction(leftPadSelectedX, 1);
								instrumentClipView.dontDeleteNotesOnDepress();
							}
						}
					}
					renderDisplay();
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

// Not used with Audio Clip Automation View or Arranger Automation View
ActionResult AutomationLayout::scrollVertical(int32_t scrollAmount) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = outputType == OutputType::KIT;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(clip->yScroll + scrollAmount) > (int16_t)(clip->getNumNoteRows() - 1)) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (clip->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return ActionResult::DEALT_WITH;
			}
		}
		// if we're in the note editor we don't want to over-scroll so that selected row is not a valid note row
		if (inNoteEditor()) {
			int32_t lastAuditionedYDisplayScrolled = instrumentClipView.lastAuditionedYDisplay + scrollAmount;
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    clip->getNoteRowOnScreen(lastAuditionedYDisplayScrolled, modelStack);
			// over-scrolled, no valid note row, so return and don't do the actual scrolling
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				return ActionResult::DEALT_WITH;
			}
			// we have a valid note row, so let's set selected drum equal to previous auditioned y display
			else {
				NoteRow* noteRow = clip->getNoteRowOnScreen(lastAuditionedYDisplayScrolled, currentSong);
				if (noteRow) {
					instrumentClipView.setSelectedDrum(noteRow->drum, true);
				}
			}
		}
	}

	// Or if not a Kit...
	else {
		int32_t newYNote;
		if (scrollAmount > 0) {
			newYNote = clip->getYNoteFromYDisplay(kDisplayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = clip->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!clip->isScrollWithinRange(scrollAmount, newYNote)) {
			return ActionResult::DEALT_WITH;
		}
	}

	bool currentClipIsActive = currentSong->isClipActive(clip);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		instrumentClipView.sendAuditionNote(false, yDisplay, 127, 0);

		ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

		if (noteRow) {
			// If recording, record a note-off for this NoteRow, if one exists
			if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {
				clip->recordNoteOff(modelStackWithNoteRow);
			}
		}
	}

	// Do actual scroll
	clip->yScroll += scrollAmount;

	// Don't render - we'll do that after we've dealt with presses (potentially creating Notes)
	instrumentClipView.recalculateColours();

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on
	// before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// switch its audition back on
			//  Check NoteRow exists, incase we've got a Kit
			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

			if (!isKit || modelStackWithNoteRow->getNoteRowAllowNull()) {

				if (modelStackWithNoteRow->getNoteRowAllowNull() && modelStackWithNoteRow->getNoteRow()->sequenced) {}
				else {

					// Record note-on if we're recording
					if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {

						// If no NoteRow existed before, try creating one
						if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
							modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, yDisplay);
						}

						if (modelStackWithNoteRow->getNoteRowAllowNull()) {
							clip->recordNoteOn(modelStackWithNoteRow, ((Instrument*)output)->defaultVelocity);
						}
					}

					// Should this technically grab the note-length of the note if there is one?
					instrumentClipView.sendAuditionNote(true, yDisplay,
					                                    instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay], 0);
				}
			}
			else {
				instrumentClipView.auditionPadIsPressed[yDisplay] = false;
				instrumentClipView.lastAuditionedVelocityOnScreen[yDisplay] = 255;
				forceStoppedAnyAuditioning = true;
			}
			// If we're shiftingNoteRow, no need to re-draw the noteCode, because it'll be the same
			if (!drawnNoteCodeYet && instrumentClipView.auditionPadIsPressed[yDisplay]) {
				/* if you're in the note editor:
				    - don't draw note code because the note code is already on the display
				    - don't update selected drum as this was done above
				*/
				if (!inNoteEditor()) {
					instrumentClipView.drawNoteCode(yDisplay);

					if (isKit) {
						Drum* newSelectedDrum = nullptr;
						NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong);
						if (noteRow) {
							newSelectedDrum = noteRow->drum;
						}
						instrumentClipView.setSelectedDrum(newSelectedDrum, true);
					}
				}

				if (outputType == OutputType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(clip->getYNoteFromYDisplay(yDisplay, currentSong)
						                                              + ((SoundInstrument*)output)->transpose);
					}
				}

				drawnNoteCodeYet = true;
			}
		}
	}
	if (forceStoppedAnyAuditioning) {
		// don't recalculateLastAuditionedNoteOnScreen if we're in the note editor because it
		// messes up the note row selection	for velocity editing
		instrumentClipView.someAuditioningHasEnded(!inNoteEditor());
	}

	uiNeedsRendering(getRootUI());
	return ActionResult::DEALT_WITH;
}

// mod encoder action

// used to change the value of a step when you press and hold a pad on the timeline
// used to record live automations in
void AutomationLayout::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (automationView.onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = getCurrentClip();
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);

	// if user holding a node down, we'll adjust the value of the selected parameter being automated
	if (isUIModeActive(UI_MODE_NOTES_PRESSED) || padSelectionOn) {
		if (inAutomationEditor()
		    && ((instrumentClipView.numEditPadPresses > 0
		         && ((int32_t)(instrumentClipView.timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0))
		        || padSelectionOn)) {

			if (automationLayoutEditor.automationModEncoderActionForSelectedPad(modelStackWithParam, whichModEncoder,
			                                                                    offset, effectiveLength)) {
				return;
			}
		}
		else {
			goto followOnAction;
		}
	}
	// if playback is enabled and you are recording, you will be able to record in live automations for
	// the selected parameter this code is also executed if you're just changing the current value of
	// the parameter at the current mod position
	else {
		if (inAutomationEditor()) {
			automationLayoutEditor.automationModEncoderActionForUnselectedPad(modelStackWithParam, whichModEncoder,
			                                                                  offset, effectiveLength);
		}
		else {
			goto followOnAction;
		}
	}

	uiNeedsRendering(getRootUI());
	return;

followOnAction:
	return automationView.ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}

// used to copy paste automation or to delete automation of the current selected parameter
void AutomationLayout::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	Clip* clip = getCurrentClip();
	OutputType outputType = clip->output->type;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (automationView.onArrangerView) {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}
	int32_t effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);

	int32_t xScroll = currentSong->xScroll[navSysId];
	int32_t xZoom = currentSong->xZoom[navSysId];

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
		if (on) {
			if (Buttons::isShiftButtonPressed()) {
				// paste within Automation Editor
				if (inAutomationEditor()) {
					automationLayoutEditor.pasteAutomation(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				// paste on Automation Overview / Note Editor
				else {
					instrumentClipView.pasteAutomation(whichModEncoder, navSysId);
				}
			}
			else {
				// copy within Automation Editor
				if (inAutomationEditor()) {
					automationLayoutEditor.copyAutomation(modelStackWithParam, clip, xScroll, xZoom);
				}
				// copy on Automation Overview / Note Editor
				else {
					instrumentClipView.copyAutomation(whichModEncoder, navSysId);
				}
			}
		}
	}

	// delete automation of current parameter selected
	else if (Buttons::isShiftButtonPressed() && inAutomationEditor()) {
		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE);
			modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);

			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_DELETED));

			displayAutomation(padSelectionOn, !display->have7SEG());
		}
	}

	// if we're in automation overview or note editor
	// then we want to allow toggling with mod encoder buttons to change
	// mod encoder selections
	else if (!inAutomationEditor()) {
		goto followOnAction;
	}

	uiNeedsRendering(getRootUI());
	return;

followOnAction: // it will come here when you are on the automation overview / in note editor iscreen

	view.modEncoderButtonAction(whichModEncoder, on);
	uiNeedsRendering(getRootUI());
}

// select encoder action

// used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
// once parameter is selected
// used to fine tune the values of non-midi parameters
void AutomationLayout::selectEncoderAction(int8_t offset) {
	// 5x acceleration of select encoder when holding the shift button
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		offset = offset * 5;
	}

	// change midi CC or param ID
	Clip* clip = getCurrentClip();
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you've selected a mod encoder (e.g. by pressing modEncoderButton) and you're in Automation
	// Overview the currentUIMode will change to Selecting Midi CC. In this case, turning select encoder
	// should allow you to change the midi CC assignment to that modEncoder
	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		automationView.InstrumentClipMinder::selectEncoderAction(offset);
		return;
	}
	// don't allow switching to automation editor if you're holding the audition pad in arranger
	// automation view
	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return;
	}
	// edit row or note probability
	else if (inNoteEditor()) {
		// only allow adjusting probbaility while holding note
		if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			instrumentClipView.adjustNoteProbabilityWithOffset(offset);
			timeSelectKnobLastReleased = AudioEngine::audioSampleTimer;
			probabilityChanged = true;
		}
		// only allow adjusting row probability while holding audition
		else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			instrumentClipView.setNoteRowProbabilityWithOffset(offset);
			timeSelectKnobLastReleased = AudioEngine::audioSampleTimer;
			probabilityChanged = true;
		}
		return;
	}
	// if you're in a midi clip
	else if (outputType == OutputType::MIDI_OUT) {
		selectMIDICC(offset, clip);
		getLastSelectedParamShortcut(clip);
	}
	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	else if (automationView.onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (automationView.onArrangerView || (outputType == OutputType::AUDIO)
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
		if (clip->lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			getLastSelectedParamShortcut(clip);
		}
	}
	// if you're in a CV clip or function is called for some other reason, do nothing
	else {
		return;
	}

	// update name on display, the LED mod indicators, and refresh the grid
	lastPadSelectedKnobPos = kNoSelection;
	if (multiPadPressSelected && padSelectionOn) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
		ModelStackWithAutoParam* modelStackWithParam = nullptr;

		if (automationView.onArrangerView) {
			modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
		}
		else {
			modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
		}
		int32_t effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);
		int32_t xScroll = currentSong->xScroll[navSysId];
		int32_t xZoom = currentSong->xZoom[navSysId];
		renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
	}
	else {
		displayAutomation(true, !display->have7SEG());
	}
	resetParameterShortcutBlinking();
	blinkShortcuts();
	view.setModLedStates();
	uiNeedsRendering(getRootUI());
}

// used with SelectEncoderAction to get the next arranger / audio clip / kit affect entire parameter
void AutomationLayout::selectGlobalParam(int32_t offset, Clip* clip) {
	if (automationView.onArrangerView) {
		auto idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		{
			while ((id == params::UNPATCHED_PITCH_ADJUST || id == params::UNPATCHED_SIDECHAIN_SHAPE
			        || id == params::UNPATCHED_SIDECHAIN_VOLUME || id == params::UNPATCHED_COMPRESSOR_THRESHOLD)) {

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
	else {
		auto idx = getNextSelectedParamArrayPosition(offset, clip->lastSelectedParamArrayPosition,
		                                             kNumGlobalParamsForAutomation);
		auto [kind, id] = globalParamsForAutomation[idx];
		clip->lastSelectedParamID = id;
		clip->lastSelectedParamKind = kind;
		clip->lastSelectedParamArrayPosition = idx;
	}
	automationView.automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next synth or kit non-affect entire param
void AutomationLayout::selectNonGlobalParam(int32_t offset, Clip* clip) {
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
bool AutomationLayout::selectPatchCable(int32_t offset, Clip* clip) {
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
bool AutomationLayout::selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex,
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
void AutomationLayout::selectMIDICC(int32_t offset, Clip* clip) {
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
int32_t AutomationLayout::getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
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
void AutomationLayout::getLastSelectedParamShortcut(Clip* clip) {
	bool paramShortcutFound = false;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (automationView.onArrangerView) {
				if (unpatchedGlobalParamShortcuts[x][y] == currentSong->lastSelectedParamID) {
					currentSong->lastSelectedParamShortcutX = x;
					currentSong->lastSelectedParamShortcutY = y;
					paramShortcutFound = true;
					break;
				}
			}
			else if (clip->output->type == OutputType::MIDI_OUT) {
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
		if (paramShortcutFound) {
			break;
		}
	}
	if (!paramShortcutFound) {
		if (automationView.onArrangerView) {
			currentSong->lastSelectedParamShortcutX = kNoSelection;
			currentSong->lastSelectedParamShortcutY = kNoSelection;
		}
		else {
			clip->lastSelectedParamShortcutX = kNoSelection;
			clip->lastSelectedParamShortcutY = kNoSelection;
		}
	}
}

void AutomationLayout::getLastSelectedParamArrayPosition(Clip* clip) {
	Output* output = clip->output;
	OutputType outputType = output->type;

	// if you're in arranger view or in a non-midi, non-cv clip (e.g. audio, synth, kit)
	if (automationView.onArrangerView || outputType != OutputType::CV) {
		// if you're in a audio clip, a kit with affect entire enabled, or in arranger view
		if (automationView.onArrangerView || (outputType == OutputType::AUDIO)
		    || (outputType == OutputType::KIT && getAffectEntire())) {
			getLastSelectedGlobalParamArrayPosition(clip);
		}
		// if you're a synth or a kit (with affect entire off and a drum selected)
		else if (outputType == OutputType::SYNTH
		         || (outputType == OutputType::KIT && ((Kit*)output)->selectedDrum
		             && ((Kit*)output)->selectedDrum->type == DrumType::SOUND)) {
			getLastSelectedNonGlobalParamArrayPosition(clip);
		}
	}
}

void AutomationLayout::getLastSelectedNonGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumNonGlobalParamsForAutomation; idx++) {

		auto [kind, id] = nonGlobalParamsForAutomation[idx];

		if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
			clip->lastSelectedParamArrayPosition = idx;
			break;
		}
	}
}

void AutomationLayout::getLastSelectedGlobalParamArrayPosition(Clip* clip) {
	for (auto idx = 0; idx < kNumGlobalParamsForAutomation; idx++) {

		auto [kind, id] = globalParamsForAutomation[idx];

		if (automationView.onArrangerView) {
			if ((id == currentSong->lastSelectedParamID) && (kind == currentSong->lastSelectedParamKind)) {
				currentSong->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
		else {
			if ((id == clip->lastSelectedParamID) && (kind == clip->lastSelectedParamKind)) {
				clip->lastSelectedParamArrayPosition = idx;
				break;
			}
		}
	}
}

bool AutomationLayout::isMultiPadPressSelected() {
	return multiPadPressSelected;
}

// called by melodic_instrument.cpp or kit.cpp
void AutomationLayout::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {
	instrumentClipView.noteRowChanged(clip, noteRow);
}

// called by playback_handler.cpp
void AutomationLayout::notifyPlaybackBegun() {
	if (!automationView.onArrangerView && getCurrentClip()->type != ClipType::AUDIO) {
		instrumentClipView.reassessAllAuditionStatus();
	}
}

// resets the Parameter Selection which sends you back to the Automation Overview screen
// these values are saved on a clip basis
void AutomationLayout::initParameterSelection(bool updateDisplay) {
	resetShortcutBlinking();
	initPadSelection();

	if (automationView.onArrangerView) {
		currentSong->lastSelectedParamID = kNoSelection;
		currentSong->lastSelectedParamKind = params::Kind::NONE;
		currentSong->lastSelectedParamShortcutX = kNoSelection;
		currentSong->lastSelectedParamShortcutY = kNoSelection;
		currentSong->lastSelectedParamArrayPosition = 0;
	}
	else {
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

// exit pad selection mode, reset pad press statuses
void AutomationLayout::initPadSelection() {
	padSelectionOn = false;
	multiPadPressSelected = false;
	multiPadPressActive = false;
	middlePadPressSelected = false;
	leftPadSelectedX = kNoSelection;
	rightPadSelectedX = kNoSelection;
	lastPadSelectedKnobPos = kNoSelection;

	resetPadSelectionShortcutBlinking();

	numNotesSelected = 0;
	selectedPadPressed = 0;

	// make sure no active presses remain when exiting pad selection mode
	if (inNoteEditor() && isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		instrumentClipView.endAllEditPadPresses();
	}

	resetPadSelectionShortcutBlinking();
}

// get's the modelstack for the parameters that are being edited
// the model stack differs for SONG, SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam* AutomationLayout::getModelStackWithParam(void* modelStackMemory,
                                                                  ModelStackWithTimelineCounter* modelStack,
                                                                  Clip* clip) {

	if (automationView.onArrangerView) {
		return getModelStackWithParamForSong(modelStackMemory);
	}
	else {
		return getModelStackWithParamForClip(modelStack, clip);
	}

	return nullptr;
}

// get's the modelstack for the song parameters that are being edited
ModelStackWithAutoParam* AutomationLayout::getModelStackWithParamForSong(void* modelStackMemory) {
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	return currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
}

// get's the modelstack for the clip parameters that are being edited
// the model stack differs for SYNTH's, KIT's, MIDI, and Audio clip's
ModelStackWithAutoParam* AutomationLayout::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack,
                                                                         Clip* clip, int32_t paramID,
                                                                         params::Kind paramKind) {
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

uint32_t AutomationLayout::getMaxLength() {
	if (automationView.onArrangerView) {
		return arrangerView.getMaxLength();
	}
	else {
		return getCurrentClip()->getMaxLength();
	}
}

uint32_t AutomationLayout::getMaxZoom() {
	if (automationView.onArrangerView) {
		return arrangerView.getMaxZoom();
	}
	else {
		return getCurrentClip()->getMaxZoom();
	}
}

int32_t AutomationLayout::getNavSysId() const {
	if (automationView.onArrangerView) {
		return NAVIGATION_ARRANGEMENT;
	}
	else {
		return NAVIGATION_CLIP;
	}
}

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationLayout::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
                                                        int32_t knobPosRight) {
	params::Kind kind = modelStack->paramCollection->getParamKind();
	bool isBipolar = isParamBipolar(kind, modelStack->paramId);

	// if you're dealing with a patch cable which has a -128 to +128 range
	// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
	if (kind == params::Kind::PATCH_CABLE) {
		knobPosLeft = view.convertPatchCableKnobPosToIndicatorLevel(knobPosLeft);
		knobPosRight = view.convertPatchCableKnobPosToIndicatorLevel(knobPosRight);
	}

	bool isBlinking = indicator_leds::isKnobIndicatorBlinking(0) || indicator_leds::isKnobIndicatorBlinking(1);

	if (!isBlinking) {
		indicator_leds::setKnobIndicatorLevel(0, knobPosLeft, isBipolar);
		indicator_leds::setKnobIndicatorLevel(1, knobPosRight, isBipolar);
	}
}

// updates the position that the active mod controllable stack is pointing to
// this sets the current value for the active parameter so that it can be auditioned
void AutomationLayout::updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
                                                   bool updateDisplay, bool updateIndicatorLevels) {

	if (!playbackHandler.isEitherClockActive() || padSelectionOn) {
		if (modelStack && modelStack->autoParam) {
			if (modelStack->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
				    squareStart, &view.activeModControllableModelStack);

				int32_t knobPos =
				    automationLayoutEditor.getAutomationParameterKnobPos(modelStack, squareStart) + kKnobPosOffset;

				if (updateDisplay) {
					renderDisplay(knobPos);
				}

				if (updateIndicatorLevels) {
					setAutomationKnobIndicatorLevels(modelStack, knobPos, knobPos);
				}
			}
		}
	}
}

// takes care of setting the automation value for the single pad that was pressed
void AutomationLayout::handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                      int32_t xDisplay, int32_t yDisplay, int32_t effectiveLength,
                                                      int32_t xScroll, int32_t xZoom) {

	Output* output = clip->output;
	OutputType outputType = output->type;

	// this means you are editing a parameter's value
	if (inAutomationEditor()) {
		handleAutomationParameterChange(modelStackWithParam, clip, outputType, xDisplay, yDisplay, effectiveLength,
		                                xScroll, xZoom);
	}

	uiNeedsRendering(getRootUI());
}

// called by handle single pad press when it is determined that you are editing parameter automation
// using the grid
void AutomationLayout::handleAutomationParameterChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                       OutputType outputType, int32_t xDisplay, int32_t yDisplay,
                                                       int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (padSelectionOn) {
		// display pad's value
		uint32_t squareStart = 0;

		// if a long press is selected and you're checking value of start or end pad
		// display value at very first or very last node
		if (multiPadPressSelected && ((leftPadSelectedX == xDisplay) || (rightPadSelectedX == xDisplay))) {
			if (leftPadSelectedX == xDisplay) {
				squareStart = automationView.getPosFromSquare(xDisplay, xScroll, xZoom);
			}
			else {
				int32_t squareRightEdge = automationView.getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);
				squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
			}
		}
		// display pad's middle value
		else {
			squareStart = automationLayoutEditor.getMiddlePosFromSquare(xDisplay, effectiveLength, xScroll, xZoom);
		}

		updateAutomationModPosition(modelStackWithParam, squareStart);

		if (!multiPadPressSelected) {
			leftPadSelectedX = xDisplay;
		}
	}

	else if (modelStackWithParam && modelStackWithParam->autoParam) {

		uint32_t squareStart = automationView.getPosFromSquare(xDisplay, xScroll, xZoom);

		if (squareStart < effectiveLength) {
			// use default interpolation settings
			automationLayoutEditor.initInterpolation();

			int32_t newKnobPos = calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, yDisplay);
			automationLayoutEditor.setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay,
			                                                   effectiveLength, xScroll, xZoom);
		}
	}
}

int32_t AutomationLayout::calculateAutomationKnobPosForPadPress(ModelStackWithAutoParam* modelStackWithParam,
                                                                OutputType outputType, int32_t yDisplay) {

	int32_t newKnobPos = 0;
	params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

	if (middlePadPressSelected) {
		newKnobPos = calculateAutomationKnobPosForMiddlePadPress(kind, yDisplay);
	}
	else {
		newKnobPos = calculateAutomationKnobPosForSinglePadPress(kind, yDisplay);
	}

	// for Midi Clips, maxKnobPos = 127
	if (outputType == OutputType::MIDI_OUT && newKnobPos == kMaxKnobPos) {
		newKnobPos -= 1; // 128 - 1 = 127
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a second pad in the same column
// middle value is calculated by taking average of min and max value of the range for the two pad
// presses
int32_t AutomationLayout::calculateAutomationKnobPosForMiddlePadPress(params::Kind kind, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	int32_t yMin = yDisplay < leftPadSelectedY ? yDisplay : leftPadSelectedY;
	int32_t yMax = yDisplay > leftPadSelectedY ? yDisplay : leftPadSelectedY;
	int32_t minKnobPos = 0;
	int32_t maxKnobPos = 0;

	if (kind == params::Kind::PATCH_CABLE) {
		minKnobPos = patchCableMinPadDisplayValues[yMin];
		maxKnobPos = patchCableMaxPadDisplayValues[yMax];
	}
	else {
		minKnobPos = nonPatchCableMinPadDisplayValues[yMin];
		maxKnobPos = nonPatchCableMaxPadDisplayValues[yMax];
	}

	newKnobPos = (minKnobPos + maxKnobPos) >> 1;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a single pad
int32_t AutomationLayout::calculateAutomationKnobPosForSinglePadPress(params::Kind kind, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	// patch cable
	if (kind == params::Kind::PATCH_CABLE) {
		newKnobPos = patchCablePadPressValues[yDisplay];
	}
	// non patch cable
	else {
		newKnobPos = nonPatchCablePadPressValues[yDisplay];
	}

	return newKnobPos;
}

// takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationLayout::handleAutomationMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                     int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
                                                     int32_t secondPadY, int32_t effectiveLength, int32_t xScroll,
                                                     int32_t xZoom, bool modEncoderAction) {

	int32_t secondPadLeftEdge = automationView.getPosFromSquare(secondPadX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = automationView.getPosFromSquare(firstPadX, xScroll, xZoom);
		int32_t secondPadRightEdge = automationView.getPosFromSquare(secondPadX + 1, xScroll, xZoom);

		int32_t firstPadValue = 0;
		int32_t secondPadValue = 0;

		// if we're updating the long press values via mod encoder action, then get current values of
		// pads pressed and re-interpolate
		if (modEncoderAction) {
			firstPadValue = automationLayoutEditor.getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge)
			                + kKnobPosOffset;

			uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;

			secondPadValue =
			    automationLayoutEditor.getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
		}

		// otherwise if it's a regular long press, calculate values from the y position of the pads
		// pressed
		else {
			OutputType outputType = clip->output->type;
			firstPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, firstPadY) + kKnobPosOffset;
			secondPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, secondPadY) + kKnobPosOffset;
		}

		// clear existing nodes from long press range

		// reset interpolation settings to default
		automationLayoutEditor.initInterpolation();

		// set value for beginning pad press at the very first node position within that pad
		automationLayoutEditor.setAutomationParameterValue(modelStackWithParam, firstPadValue - kKnobPosOffset,
		                                                   firstPadLeftEdge, firstPadX, effectiveLength, xScroll,
		                                                   xZoom);

		// set value for ending pad press at the very last node position within that pad
		int32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		automationLayoutEditor.setAutomationParameterValue(modelStackWithParam, secondPadValue - kKnobPosOffset,
		                                                   squareStart, secondPadX, effectiveLength, xScroll, xZoom);

		// converting variables to float for more accurate interpolation calculation
		float firstPadValueFloat = static_cast<float>(firstPadValue);
		float firstPadXFloat = static_cast<float>(firstPadLeftEdge);
		float secondPadValueFloat = static_cast<float>(secondPadValue);
		float secondPadXFloat = static_cast<float>(squareStart);

		// loop from first pad to last pad, setting values for nodes in between
		// these values will serve as "key frames" for the interpolation to flow through
		for (int32_t x = firstPadX; x <= secondPadX; x++) {

			int32_t newKnobPos = 0;
			uint32_t squareWidth = 0;

			// we've already set the value for the very first node corresponding to the first pad above
			// now we will set the value for the remaining nodes within the first pad
			if (x == firstPadX) {
				squareStart = automationView.getPosFromSquare(x, xScroll, xZoom) + kParamNodeWidth;
				squareWidth =
				    automationLayoutEditor.getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// we've already set the value for the very last node corresponding to the second pad above
			// now we will set the value for the remaining nodes within the second pad
			else if (x == secondPadX) {
				squareStart = automationView.getPosFromSquare(x, xScroll, xZoom);
				squareWidth =
				    automationLayoutEditor.getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// now we will set the values for the nodes between the first and second pad's pressed
			else {
				squareStart = automationView.getPosFromSquare(x, xScroll, xZoom);
				squareWidth = automationLayoutEditor.getSquareWidth(x, effectiveLength, xScroll, xZoom);
			}

			// linear interpolation formula to calculate the value of the pads
			// f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
			float newKnobPosFloat = std::round(firstPadValueFloat
			                                   + (((squareStart - firstPadXFloat) / kParamNodeWidth)
			                                      * ((secondPadValueFloat - firstPadValueFloat)
			                                         / ((secondPadXFloat - firstPadXFloat) / kParamNodeWidth))));

			newKnobPos = static_cast<int32_t>(newKnobPosFloat);
			newKnobPos = newKnobPos - kKnobPosOffset;

			// if interpolation is off, values for nodes in between first and second pad will not be set
			// in a staggered/step fashion
			if (automationLayoutEditor.interpolation) {
				automationLayoutEditor.interpolationBefore = true;
				automationLayoutEditor.interpolationAfter = true;
			}

			// set value for pads in between
			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);

			if (!automationView.onArrangerView) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}
		}

		// reset interpolation settings to off
		automationLayoutEditor.initInterpolation();

		// render the multi pad press
		uiNeedsRendering(getRootUI());
	}
}

// new function to render display when a long press is active
// on OLED this will display the left and right position in a long press on the screen
// on 7SEG this will display the position of the last selected pad
// also updates LED indicators. bottom LED indicator = left pad, top LED indicator = right pad
void AutomationLayout::renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                               int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
                                                               int32_t xDisplay, bool modEncoderAction) {

	int32_t secondPadLeftEdge = automationView.getPosFromSquare(rightPadSelectedX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = automationView.getPosFromSquare(leftPadSelectedX, xScroll, xZoom);
		int32_t secondPadRightEdge = automationView.getPosFromSquare(rightPadSelectedX + 1, xScroll, xZoom);

		int32_t knobPosLeft =
		    automationLayoutEditor.getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge)
		    + kKnobPosOffset;

		uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		int32_t knobPosRight =
		    automationLayoutEditor.getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

		if (xDisplay != kNoSelection) {
			if (leftPadSelectedX == xDisplay) {
				squareStart = firstPadLeftEdge;
				lastPadSelectedKnobPos = knobPosLeft;
			}
			else {
				lastPadSelectedKnobPos = knobPosRight;
			}
		}

		if (display->haveOLED()) {
			renderDisplay(knobPosLeft, knobPosRight);
		}
		// display pad value of second pad pressed
		else {
			if (modEncoderAction) {
				renderDisplay(lastPadSelectedKnobPos);
			}
			else {
				renderDisplay();
			}
		}

		setAutomationKnobIndicatorLevels(modelStackWithParam, knobPosLeft, knobPosRight);

		// update position of mod controllable stack
		updateAutomationModPosition(modelStackWithParam, squareStart, false, false);
	}
}

// used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationLayout::calculateAutomationKnobPosForModEncoderTurn(ModelStackWithAutoParam* modelStackWithParam,
                                                                      int32_t knobPos, int32_t offset) {

	// adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		if (kind == params::Kind::PATCH_CABLE) {
			if ((knobPos + offset) >= -kMaxKnobPos) {
				newKnobPos = knobPos + offset;
			}
			else if ((knobPos + offset) < -kMaxKnobPos) {
				newKnobPos = -kMaxKnobPos;
			}
			else {
				newKnobPos = knobPos;
			}
		}
		else {
			newKnobPos = knobPos;
		}
	}
	else if ((knobPos + offset) <= kMaxKnobPos) {
		newKnobPos = knobPos + offset;
	}
	else if ((knobPos + offset) > kMaxKnobPos) {
		newKnobPos = kMaxKnobPos;
	}
	else {
		newKnobPos = knobPos;
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// used to render automation overview
// used to handle pad actions on automation overview
// used to disable certain actions on the automation overview screen
// e.g. doubling clip length, editing clip length
bool AutomationLayout::onAutomationOverview() {
	return (!inAutomationEditor() && !inNoteEditor());
}

bool AutomationLayout::inAutomationEditor() {
	if (automationView.onArrangerView) {
		if (currentSong->lastSelectedParamID == kNoSelection) {
			return false;
		}
	}
	else if (getCurrentClip()->lastSelectedParamID == kNoSelection) {
		return false;
	}

	return true;
}

void AutomationLayout::setAutomationParamType() {
	automationView.automationParamType = AutomationParamType::PER_SOUND;
	if (!inAutomationEditor()) {
		Clip* clip = getCurrentClip();
		if ((clip->lastSelectedParamShortcutX == kVelocityShortcutX)
		    && (clip->lastSelectedParamShortcutY == kVelocityShortcutY)) {
			automationView.automationParamType = AutomationParamType::NOTE_VELOCITY;
		}
	}
}

// used to check if we're automating a note row specific param type
// e.g. velocity, probability, poly expression, etc.
bool AutomationLayout::inNoteEditor() {
	return (automationView.automationParamType != AutomationParamType::PER_SOUND);
}

// used to determine the affect entire context
bool AutomationLayout::getAffectEntire() {
	// arranger view always uses affect entire
	if (automationView.onArrangerView) {
		return true;
	}
	// are you in the sound menu for a kit?
	else if (getCurrentOutputType() == OutputType::KIT && getCurrentUI() == &soundEditor
	         && !soundEditor.inSettingsMenu()) {
		// if you're in the kit global FX menu, the menu context is the same as if affect entire is enabled
		if (soundEditor.setupKitGlobalFXMenu) {
			return true;
		}
		// otherwise you're in the kit row context which is the same as if affect entire is disabled
		else {
			return false;
		}
	}
	// otherwise if you're not in the kit sound menu, use the clip affect entire state
	return getCurrentInstrumentClip()->affectEntire;
}

void AutomationLayout::blinkShortcuts() {
	if (getCurrentUI()->getUIType() == UIType::AUTOMATION) {
		int32_t lastSelectedParamShortcutX = kNoSelection;
		int32_t lastSelectedParamShortcutY = kNoSelection;
		if (automationView.onArrangerView) {
			lastSelectedParamShortcutX = currentSong->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = currentSong->lastSelectedParamShortcutY;
		}
		else {
			Clip* clip = getCurrentClip();
			lastSelectedParamShortcutX = clip->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = clip->lastSelectedParamShortcutY;
		}
		// if a Param has been selected for editing, blink its shortcut pad
		if (lastSelectedParamShortcutX != kNoSelection) {
			if (!parameterShortcutBlinking) {
				soundEditor.setupShortcutBlink(lastSelectedParamShortcutX, lastSelectedParamShortcutY, 10);
				soundEditor.blinkShortcut();

				parameterShortcutBlinking = true;
			}
		}
		// unset previously set blink timers if not editing a parameter
		else {
			resetParameterShortcutBlinking();
		}
	}
	// only blink interpolation shortcut while in automation editor
	if (automationLayoutEditor.interpolation && inAutomationEditor()) {
		if (!interpolationShortcutBlinking) {
			blinkInterpolationShortcut();
		}
	}
	else {
		resetInterpolationShortcutBlinking();
	}
	if (padSelectionOn) {
		blinkPadSelectionShortcut();
	}
	else {
		resetPadSelectionShortcutBlinking();
	}
	if (inNoteEditor()) {
		if (!instrumentClipView.noteRowBlinking) {
			instrumentClipView.blinkSelectedNoteRow();
		}
	}
	else {
		instrumentClipView.resetSelectedNoteRowBlinking();
	}
}

void AutomationLayout::resetShortcutBlinking() {
	soundEditor.resetSourceBlinks();
	resetParameterShortcutBlinking();
	resetInterpolationShortcutBlinking();
	resetPadSelectionShortcutBlinking();
	instrumentClipView.resetSelectedNoteRowBlinking();
}

// created this function to undo any existing parameter shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when a parameter is
// deselected or when you enter/exit automation view
void AutomationLayout::resetParameterShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	parameterShortcutBlinking = false;
}

// created this function to undo any existing interpolation shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when interpolation is
// turned off or when you enter/exit automation view
void AutomationLayout::resetInterpolationShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::INTERPOLATION_SHORTCUT_BLINK);
	interpolationShortcutBlinking = false;
}

void AutomationLayout::blinkInterpolationShortcut() {
	PadLEDs::flashMainPad(kInterpolationShortcutX, kInterpolationShortcutY);
	uiTimerManager.setTimer(TimerName::INTERPOLATION_SHORTCUT_BLINK, 3000);
	interpolationShortcutBlinking = true;
}

// used to blink waveform shortcut when in pad selection mode
void AutomationLayout::resetPadSelectionShortcutBlinking() {
	uiTimerManager.unsetTimer(TimerName::PAD_SELECTION_SHORTCUT_BLINK);
	padSelectionShortcutBlinking = false;
}

void AutomationLayout::blinkPadSelectionShortcut() {
	PadLEDs::flashMainPad(kPadSelectionShortcutX, kPadSelectionShortcutY);
	uiTimerManager.setTimer(TimerName::PAD_SELECTION_SHORTCUT_BLINK, 3000);
	padSelectionShortcutBlinking = true;
}

bool AutomationLayout::interpolationBefore() {
	return automationLayoutEditor.interpolationBefore;
}

bool AutomationLayout::interpolationAfter() {
	return automationLayoutEditor.interpolationAfter;
}
