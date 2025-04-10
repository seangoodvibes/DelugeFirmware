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

#include "gui/views/automation/context/song.h"
#include "model/song/song.h"

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

constexpr int32_t kNumGlobalParamsForAutomation = 37;

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

PLACE_SDRAM_BSS AutomationViewSong automationViewSong{};

AutomationViewSong::AutomationViewSong() {
}

// rendering
bool AutomationViewSong::possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind,
                                                             int32_t paramID) {
	bool doRefreshGrid =
	    (currentSong->lastSelectedParamID == paramID) && (currentSong->lastSelectedParamKind == paramKind);

	if (doRefreshGrid) {
		uiNeedsRendering(getRootUI());
	}

	return doRefreshGrid;
}

// select encoder action

// used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
// once parameter is selected
void AutomationViewSong::selectEncoderAction(int8_t offset) {
	// don't allow switching to automation editor if you're holding the audition pad in arranger
	// automation view
	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return;
	}

	// 5x acceleration of select encoder when holding the shift button
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		offset = offset * 5;
	}

	selectParameterWithOffset(offset);
	uiNeedsRendering(getRootUI());
}

// used with SelectEncoderAction to get the next arranger affect entire parameter
void AutomationViewSong::selectParameterWithOffset(int32_t offset) {
	auto idx =
	    getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamID,
	                                      currentSong->lastSelectedParamArrayPosition, kNumGlobalParamsForAutomation);
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
			idx = getNextSelectedParamArrayPosition(offset, currentSong->lastSelectedParamID,
			                                        currentSong->lastSelectedParamArrayPosition,
			                                        kNumGlobalParamsForAutomation);
			id = globalParamsForAutomation[idx].second;
		}
	}
	currentSong->lastSelectedParamID = id;
	currentSong->lastSelectedParamKind = kind;
	currentSong->lastSelectedParamArrayPosition = idx;

	AutomationView::automationParamType = AutomationParamType::PER_SOUND;
}

// used with SelectEncoderAction to get the next parameter in the list of parameters
int32_t AutomationViewSong::getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamID,
                                                              int32_t lastSelectedParamArrayPosition,
                                                              int32_t numParams) {
	int32_t idx;
	// if you haven't selected a parameter yet, start at the beginning of the list
	if (lastSelectedParamID == kNoSelection) {
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

/// handles shortcut pad actions, including:
/// 1) toggle interpolation on / off
/// 2) select parameter on automation overview
/// 3) select parameter using shift + shortcut pad
/// 4) select parameter using audition + shortcut pad
bool AutomationViewSong::shortcutPadAction(int32_t xDisplay, int32_t yDisplay, int32_t velocity, int32_t xScroll,
                                           int32_t xZoom, int32_t effectiveLength) {
	AutomationLayout* currentAutomationLayout = getCurrentAutomationLayout();
	if (currentAutomationLayout == nullptr) {
		return true;
	}

	bool shortcutPress =
	    (velocity != 0)
	    && (Buttons::isShiftButtonPressed()
	        || (isUIModeActive(UI_MODE_AUDITIONING) && !FlashStorage::automationDisableAuditionPadShortcuts)
	        || currentAutomationLayout->onAutomationOverview());

	if (shortcutPress) {
		// only allow toggling of interpolation / pad selection mode when you've selected a parameter
		if (currentAutomationLayout->inAutomationEditor()) {
			// toggle interpolation on / off
			if (xDisplay == kInterpolationShortcutX && yDisplay == kInterpolationShortcutY) {
				return currentAutomationLayout->toggleAutomationInterpolation();
			}
			// toggle pad selection on / off
			else if (xDisplay == kPadSelectionShortcutX && yDisplay == kPadSelectionShortcutY) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStackMemory);
				return currentAutomationLayout->toggleAutomationPadSelectionMode(modelStackWithParam, effectiveLength,
				                                                                 xScroll, xZoom);
			}
		}
	}

	// don't change parameters this way if we're in the menu
	if (getCurrentUI() != getRootUI()) {
		return true;
	}

	// attempt to select parameter
	if (shortcutPress) {
		selectParameterWithShortcut(xDisplay, yDisplay);
		return true;
	}

	return false;
}

void AutomationViewSong::selectParameterWithShortcut(int32_t xDisplay, int32_t yDisplay) {
	// check if this is a valid shortcut
	if (unpatchedGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {

		params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
		int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];

		// don't allow automation of pitch adjust, sidechain or arp parameters in arranger
		if ((paramID == params::UNPATCHED_PITCH_ADJUST) || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
		    || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)
		    || (paramID >= params::UNPATCHED_FIRST_ARP_PARAM && paramID <= params::UNPATCHED_LAST_ARP_PARAM)
		    || (paramID == params::UNPATCHED_ARP_RATE)) {
			return; // no parameter selected, don't re-render grid;
		}

		currentSong->lastSelectedParamKind = paramKind;
		currentSong->lastSelectedParamID = paramID;

		getLastSelectedGlobalParamArrayPosition();
	}
}

void AutomationViewSong::getLastSelectedGlobalParamArrayPosition() {
	for (auto idx = 0; idx < kNumGlobalParamsForAutomation; idx++) {

		auto [kind, id] = globalParamsForAutomation[idx];

		if ((id == currentSong->lastSelectedParamID) && (kind == currentSong->lastSelectedParamKind)) {
			currentSong->lastSelectedParamArrayPosition = idx;
			break;
		}
	}
}

void AutomationViewSong::editPadAction(int32_t xDisplay, int32_t yDisplay, int32_t velocity, int32_t xScroll,
                                       int32_t xZoom, int32_t effectiveLength) {
	AutomationLayout* currentAutomationLayout = getCurrentAutomationLayout();
	if (currentAutomationLayout == nullptr) {
		return;
	}

	if (currentAutomationLayout->inAutomationEditor()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStackMemory);

		currentAutomationLayout->automationEditPadAction(xDisplay, yDisplay, velocity, xScroll, xZoom, effectiveLength,
		                                                 modelStackWithParam, nullptr);
	}
}

// get's the modelstack for the song parameters that are being edited
ModelStackWithAutoParam* AutomationViewSong::getModelStackWithParam(void* modelStackMemory) {
	if (currentSong->lastSelectedParamID == kNoSelection) {
		return nullptr;
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	return currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
}
