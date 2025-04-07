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
#include "gui/views/automation/parameter_selection.h"
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

PLACE_SDRAM_BSS AutomationParameterSelection automationParameterSelection{};

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
}

void AutomationLayout::initMIDICCShortcutsForAutomation() {
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

	automationParameterSelection.initialize();

	// grab the default setting for interpolation
	automationLayoutEditor.interpolation = FlashStorage::automationInterpolate;

	// re-initialize pad selection mode (so you start with the default automation editor)
	initPadSelection();

	InstrumentClip* clip = getCurrentInstrumentClip();
	Output* output = clip->output;

	// if we're in the note editor and we're in a kit,
	// check that the lastAuditionedYDisplay is in sync with the selected drum
	if (inNoteEditor()) {
		automationLayoutEditor.potentiallyVerticalScrollToSelectedDrum(clip, output);
	}
}

// Initializes some stuff to begin a new editing session
void AutomationLayout::focusRegained() {
	automationParameterSelection.focusRegained();

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
	Clip* clip = nullptr;

	bool isClipContext = rootUIIsClipMinderScreen();

	if (isClipContext) {
		clip = getCurrentClip();

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
		if (isClipContext) {
			clip->renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
			                    &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		}
		else {
			arrangerView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
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
	return automationLayoutEditor.possiblyRefreshAutomationEditorGrid(clip, paramKind, paramID);
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

	if (getRootUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) {
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
	bool isClipContext = rootUIIsClipMinderScreen();
	bool isSongContext = !isClipContext;

	if (isClipContext) {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam =
		    automationParameterSelection.getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
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
	else {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
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
		if (isSongContext || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
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
	if (rootUIIsClipMinderScreen()) {
		return getCurrentClip()->renderSidebar(whichRows, image, occupancyMask);
	}
	else {
		return arrangerView.renderSidebar(whichRows, image, occupancyMask);
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
	bool isClipContext = rootUIIsClipMinderScreen();
	bool isSongContext = !isClipContext;

	// if you're not in a MIDI instrument clip, convert the knobPos to the same range as the menu (0-50)
	if (inAutomationEditor() && (isSongContext || outputType != OutputType::MIDI_OUT)) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		if (isClipContext) {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
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

	bool isClipContext = rootUIIsClipMinderScreen();
	bool isSongContext = !isClipContext;
	bool isAudioClip = isClipContext && (clip->type == ClipType::AUDIO);

	OutputType outputType = isClipContext ? clip->output->type : OutputType::NONE;

	// Song view button
	if (b == SESSION_VIEW) {
		handleSessionButtonAction(clip, on);
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
		if (inNoteEditor()) {
			handleCrossScreenButtonAction(on);
		}
		// don't toggle for automation editing
		else {
			return ActionResult::DEALT_WITH;
		}
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
		// if you just toggle playback off, re-render 7SEG display
		if (!on && (b == PLAY) && display->have7SEG() && inAutomationEditor() && !padSelectionOn
		    && !playbackHandler.isEitherClockActive()) {
			renderDisplay();
		}

		uiNeedsRendering(getRootUI());

		ActionResult result;
		if (isSongContext) {
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
	else if (on && (currentUIMode == UI_MODE_NONE)) {
		if (getRootUI()->getUIContextType() == UIType::ARRANGER) {
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

// called by button action if b == CLIP_VIEW
void AutomationLayout::handleClipButtonAction(bool on, bool isAudioClip) {
	// if audition pad or shift is pressed, go back to automation overview
	if (on && (currentUIMode == UI_MODE_AUDITIONING || Buttons::isShiftButtonPressed())) {
		initParameterSelection();
		blinkShortcuts();
		uiNeedsRendering(getRootUI());
	}
	// go back to clip view
	else if (on && (currentUIMode == UI_MODE_NONE)) {
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
			if (rootUIIsClipMinderScreen()) {
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

// called by button action if b == X_ENC
bool AutomationLayout::handleHorizontalEncoderButtonAction(bool on, bool isAudioClip) {
	bool isClipContext = rootUIIsClipMinderScreen();
	bool isSongContext = !isClipContext;

	// copy / paste automation (same shortcut used for notes)
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (inAutomationEditor()) {
			Clip* clip = getCurrentClip();
			OutputType outputType = clip->output->type;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (isClipContext) {
				modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				modelStackWithParam =
				    automationParameterSelection.getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}
			else {
				modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
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
	else if (isSongContext) {
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

		if (rootUIIsClipMinderScreen()) {
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			modelStackWithParam = automationParameterSelection.getModelStackWithParamForClip(modelStack, clip);
		}
		else {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			modelStackWithParam =
			    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
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
	if (on && (currentUIMode == UI_MODE_NONE)) {
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
		Clip* clip = rootUIIsClipMinderScreen() ? getCurrentClip() : nullptr;
		if (soundEditor.setup(clip)) {
			openUI(&soundEditor);
		}
	}
}

// pad action
// handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
// everything else is pretty much the same as instrument clip view
ActionResult AutomationLayout::padAction(int32_t x, int32_t y, int32_t velocity) {
	Clip* clip = nullptr;
	Output* output = nullptr;
	OutputType outputType;

	bool isClipContext = rootUIIsClipMinderScreen();

	if (isClipContext) {
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
		// clip automation view
		if (isClipContext) {
			clip = getCurrentClip();
			// no sidebar to interact with in audio clips
			if (clip && (clip->type == ClipType::AUDIO)) {
				return ActionResult::DEALT_WITH;
			}
		}
		// arranger automation view
		else {
			// don't interact with sidebar if VU Meter is displayed
			if (view.displayVUMeter) {
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
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool isClipContext = rootUIIsClipMinderScreen();
	bool isSongContext = !isClipContext;

	// if we're in arranger automation view and holding audition pad, ignore main pad press
	if (isSongContext) {
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

	if (isClipContext) {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	}

	ModelStackWithAutoParam* modelStackWithParam =
	    automationParameterSelection.getModelStackWithParam(modelStackMemory, modelStackWithTimelineCounter, clip);

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
				if (!rootUIIsClipMinderScreen()
				    || !(outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)
				    || (outputType == OutputType::KIT && getAffectEntire())) {

					automationParameterSelection.handleParameterSelection(clip, output, outputType, x, y);

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

// called by pad action when pressing a pad in the mute column (x = kDisplayWidth)
ActionResult AutomationLayout::handleMutePadAction(InstrumentClip* instrumentClip, Output* output,
                                                   OutputType outputType, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	if (rootUIIsClipMinderScreen()) {
		if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
			if (inAutomationEditor()) {
				// if we're in a kit, and you press a mute pad
				// check if it's a mute pad corresponding to the current selected drum
				// if not, change the drum selection, refresh parameter selection and go back to automation overview
				if (outputType == OutputType::KIT) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];

					ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
					    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

					ModelStackWithNoteRow* modelStackWithNoteRow =
					    instrumentClip->getNoteRowOnScreen(y, modelStackWithTimelineCounter);

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
	if (rootUIIsClipMinderScreen()) {
		// "Learning" to this audition pad:
		if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) [[unlikely]] {
			if (getCurrentUI()->getUIType() == UIType::AUTOMATION) {
				instrumentClipView.commandLearnAuditionPad(instrumentClip, output, outputType, y, velocity);
			}
		}

		else if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON && velocity) [[unlikely]] {
			return instrumentClipView.commandSaveKitRow(instrumentClip, output, outputType, y);
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
			ActionResult result =
			    auditionPadAction(instrumentClip, output, outputType, velocity, y, Buttons::isShiftButtonPressed());

			if (result != ActionResult::DEALT_WITH) {
				return result;
			}

			// now that we've processed audition pad action, we may now have changed note row selection
			// if note row selection has changed, and we're in pad selection mode
			// we'll re-select the previous column selection by recording a pad press
			if (previousY != instrumentClipView.lastAuditionedYDisplay && inNoteEditor() && padSelectionOn
			    && leftPadSelectedX != kNoSelection) {
				automationLayoutEditor.recordNoteEditPadAction(leftPadSelectedX, 1);
			}
		}
	}
	else {
		if (onAutomationOverview()) {
			return arrangerView.handleAuditionPadAction(y, velocity, getRootUI());
		}
	}
	return ActionResult::DEALT_WITH;
}

// audition pad action
// not used with Audio Clip Automation View or Arranger Automation View
ActionResult AutomationLayout::auditionPadAction(InstrumentClip* clip, Output* output, OutputType outputType,
                                                 int32_t yDisplay, int32_t velocity, bool shiftButtonDown) {
	if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
	}

	if (instrumentClipView.editedAnyPerNoteRowStuffSinceAuditioningBegan && !velocity) {
		// in case we were editing quantize/humanize
		actionLogger.closeAction(ActionType::NOTE_NUDGE);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument =
	    automationView.InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

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
			return ActionResult::DEALT_WITH;
		}
	}

	// Or if synth
	else if (outputType == OutputType::SYNTH) {
		instrumentClipView.potentiallyUpdateMultiRangeMenu(velocity, yDisplay, (Instrument*)output);
	}

	instrumentClipView.potentiallyRecordAuditionPadAction(clipIsActiveOnInstrument, velocity, yDisplay,
	                                                      (Instrument*)output, isKit, modelStackWithTimelineCounter,
	                                                      modelStackWithNoteRowOnCurrentClip, drum);

	NoteRow* noteRowOnActiveClip = instrumentClipView.getNoteRowOnActiveClip(
	    yDisplay, (Instrument*)output, clipIsActiveOnInstrument, modelStackWithNoteRowOnCurrentClip, drum);

	bool doRender = true;

	// If note on...
	if (velocity != 0) {
		int32_t lastAuditionedYDisplay = instrumentClipView.lastAuditionedYDisplay;

		doRender = instrumentClipView.startAuditioningRow(velocity, yDisplay, shiftButtonDown, isKit,
		                                                  noteRowOnActiveClip, drum);

		drawNoteCode = true;

		if (!isKit && (instrumentClipView.lastAuditionedYDisplay != lastAuditionedYDisplay)) {
			selectedRowChanged = true;
		}
	}

	// Or if auditioning this NoteRow just finished...
	else {
		instrumentClipView.finishAuditioningRow(yDisplay, modelStackWithNoteRowOnCurrentClip, noteRowOnActiveClip);
		if (display->have7SEG()) {
			renderDisplay();
		}
	}

	if (selectedRowChanged || (selectedDrumChanged && (!getAffectEntire() || inNoteEditor()))) {
		if (inNoteEditor()) {
			renderDisplay();
			instrumentClipView.resetSelectedNoteRowBlinking();
			instrumentClipView.blinkSelectedNoteRow(0xFFFFFFFF);
			doRender = false;
		}
		else if (selectedDrumChanged) {
			initParameterSelection();
			uiNeedsRendering(getRootUI());
			doRender = false;
		}
	}

	if (doRender) {
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

	return ActionResult::DEALT_WITH;
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

	if (rootUIIsClipMinderScreen()) {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	}
	else {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	}

	if (!onAutomationOverview()
	    && ((isNoUIModeActive() && Buttons::isButtonPressed(hid::button::Y_ENC))
	        || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	            && Buttons::isButtonPressed(hid::button::CLIP_VIEW))
	        || (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)))) {

		return automationLayoutEditor.horizontalEncoderAction(offset);
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

// vertical encoder action
// no change compared to instrument clip view version
// not used with Audio Clip Automation View
ActionResult AutomationLayout::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	if (rootUIIsClipMinderScreen()) {
		Clip* clip = getCurrentClip();
		if (clip != nullptr && clip->type != ClipType::AUDIO) {
			// If encoder button pressed
			if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
				if (inNoteEditor() && currentUIMode != UI_MODE_NONE) {
					// only allow editing note repeats when selecting a note
					if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
						instrumentClipView.editNoteRepeat(offset);
					}
					// only allow euclidean while holding audition pad
					else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
						instrumentClipView.commandEuclidean(offset);
					}
				}
				// If user not wanting to move a noteCode, they want to transpose the key
				else if (!currentUIMode && clip->output->type != OutputType::KIT) {
					return instrumentClipView.commandTransposeKey(offset, inCardRoutine);
				}
			}

			// Or, if shift key is pressed
			else if (Buttons::isShiftButtonPressed()) {
				instrumentClipView.commandShiftColour(offset);
			}

			// If neither button is pressed, we'll do vertical scrolling
			else {
				commandVerticalScroll((InstrumentClip*)clip, offset);
			}
		}
	}
	else {
		if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
			currentSong->commandTranspose(offset);
		}
	}

	return ActionResult::DEALT_WITH;
}

void AutomationLayout::commandVerticalScroll(InstrumentClip* clip, int32_t scrollAmount) {
	if (isUIModeWithinRange(verticalScrollUIModes)) {
		if ((!instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
		     || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING)))
		    && (!(isUIModeActive(UI_MODE_NOTES_PRESSED) && inNoteEditor() && !padSelectionOn))) {
			// if we're in the note editor pad selection mode and vertical scrolling,
			// we want to end any presses first (which will end any note auditioning as well)
			if (inNoteEditor() && padSelectionOn) {
				instrumentClipView.endAllEditPadPresses();
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			scrollVertical(clip, modelStack, scrollAmount);

			// if we're in note editor pad selection mode, scrolling vertically will change note selected
			// so we want to re-render the display to show the updated note
			if (inNoteEditor()) {
				// if we're in pad selection mode, we will have de-selected the pad presses above
				// and now we want to re-instate the pad press for the selected note row
				// so that we can re-audition the selected note
				if (padSelectionOn && leftPadSelectedX != kNoSelection) {
					ModelStackWithNoteRow* modelStackWithNoteRow =
					    clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
					                             modelStack); // don't create
					if (modelStackWithNoteRow->getNoteRowAllowNull()) {
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
						int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();
						SquareInfo squareInfo;
						noteRow->getSquareInfo(leftPadSelectedX, effectiveLength, squareInfo);

						if (squareInfo.numNotes != 0) {
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

/// if we're entering note editor, we want the selected drum to be visible and in sync with lastAuditionedYDisplay
/// so we'll check if the yDisplay of the selectedDrum is in sync with the lastAuditionedYDisplay
/// if they're not in sync, we'll sync them up by performing a vertical scroll
void AutomationLayout::potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output) {
	return automationLayoutEditor.potentiallyVerticalScrollToSelectedDrum(clip, output);
}

// Not used with Audio Clip Automation View or Arranger Automation View
void AutomationLayout::scrollVertical(InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack,
                                      int32_t scrollAmount) {
	Output* output = clip->output;
	OutputType outputType = output->type;

	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = outputType == OutputType::KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(clip->yScroll + scrollAmount) > (int16_t)(clip->getNumNoteRows() - 1)) {
				return;
			}
		}
		else {
			if (clip->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return;
			}
		}
		// if we're in the note editor we don't want to over-scroll so that selected row is not a valid note row
		if (inNoteEditor()) {
			int32_t lastAuditionedYDisplayScrolled = instrumentClipView.lastAuditionedYDisplay + scrollAmount;
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    clip->getNoteRowOnScreen(lastAuditionedYDisplayScrolled, modelStack);
			// over-scrolled, no valid note row, so return and don't do the actual scrolling
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				return;
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
			return;
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
}

// mod encoder action

// used to change the value of a step when you press and hold a pad on the timeline
// used to record live automations in
void AutomationLayout::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (rootUIIsClipMinderScreen()) {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		Clip* clip = getCurrentClip();
		modelStackWithParam =
		    automationParameterSelection.getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}
	else {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
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

	if (rootUIIsClipMinderScreen()) {
		modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam =
		    automationParameterSelection.getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
	}
	else {
		modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
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

	Clip* clip = nullptr;
	Output* output = nullptr;
	OutputType outputType = OutputType::NONE;

	if (rootUIIsClipMinderScreen()) {
		clip = getCurrentClip();
		output = clip->output;
		outputType = output->type;
	}

	// try to select a parameter
	if (automationParameterSelection.selectEncoderAction(clip, output, outputType, offset)) {
		// update name on display, the LED mod indicators, and refresh the grid
		lastPadSelectedKnobPos = kNoSelection;
		if (multiPadPressSelected && padSelectionOn) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = nullptr;
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (rootUIIsClipMinderScreen()) {
				modelStackWithTimelineCounter = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				modelStackWithParam =
				    automationParameterSelection.getModelStackWithParamForClip(modelStackWithTimelineCounter, clip);
			}
			else {
				modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				modelStackWithParam = currentSong->getModelStackWithParam(modelStackWithThreeMainThings,
				                                                          currentSong->lastSelectedParamID);
			}
			int32_t effectiveLength = automationLayoutEditor.getEffectiveLength(modelStackWithTimelineCounter);
			int32_t xScroll = currentSong->xScroll[navSysId];
			int32_t xZoom = currentSong->xZoom[navSysId];
			automationLayoutEditor.renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength,
			                                                               xScroll, xZoom);
		}
		else {
			displayAutomation(true, !display->have7SEG());
		}
		resetParameterShortcutBlinking();
		blinkShortcuts();
		view.setModLedStates();
		uiNeedsRendering(getRootUI());
	}
}

// used with Select Encoder action to get the X, Y grid shortcut coordinates of the parameter selected
void AutomationLayout::getLastSelectedParamShortcut(Clip* clip) {
	automationParameterSelection.getLastSelectedParamShortcut(clip);
}

void AutomationLayout::getLastSelectedParamArrayPosition(Clip* clip) {
	automationParameterSelection.getLastSelectedParamArrayPosition(clip);
}

bool AutomationLayout::isMultiPadPressSelected() {
	return multiPadPressSelected;
}

// resets the Parameter Selection which sends you back to the Automation Overview screen
// these values are saved on a clip basis
void AutomationLayout::initParameterSelection(bool updateDisplay) {
	automationParameterSelection.initParameterSelection(updateDisplay);
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
}

int32_t AutomationLayout::getNavSysId() const {
	if (rootUIIsClipMinderScreen()) {
		return NAVIGATION_CLIP;
	}
	else {
		return NAVIGATION_ARRANGEMENT;
	}
}

void AutomationLayout::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
                                                        int32_t knobPosRight) {
	return automationLayoutEditor.setAutomationKnobIndicatorLevels(modelStack, knobPosLeft, knobPosRight);
}

// used to render automation overview
// used to handle pad actions on automation overview
// used to disable certain actions on the automation overview screen
// e.g. doubling clip length, editing clip length
bool AutomationLayout::onAutomationOverview() {
	return (!inAutomationEditor() && !inNoteEditor());
}

bool AutomationLayout::inAutomationEditor() {
	if (rootUIIsClipMinderScreen()) {
		if (getCurrentClip()->lastSelectedParamID == kNoSelection) {
			return false;
		}
	}
	else {
		if (currentSong->lastSelectedParamID == kNoSelection) {
			return false;
		}
	}

	return true;
}

// used to check if we're automating a note row specific param type
// e.g. velocity, probability, poly expression, etc.
bool AutomationLayout::inNoteEditor() {
	return (automationView.automationParamType != AutomationParamType::PER_SOUND);
}

// used to determine the affect entire context
bool AutomationLayout::getAffectEntire() {
	if (rootUIIsClipMinderScreen()) {
		// are you in the sound menu for a kit?
		if (getCurrentOutputType() == OutputType::KIT && getCurrentUI() == &soundEditor
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
	}
	// arranger view always uses affect entire
	else {
		return true;
	}
	// otherwise if you're not in the kit sound menu, use the clip affect entire state
	return getCurrentInstrumentClip()->affectEntire;
}

void AutomationLayout::blinkShortcuts() {
	if (getCurrentUI()->getUIType() == UIType::AUTOMATION) {
		int32_t lastSelectedParamShortcutX = kNoSelection;
		int32_t lastSelectedParamShortcutY = kNoSelection;
		if (rootUIIsClipMinderScreen()) {
			Clip* clip = getCurrentClip();
			lastSelectedParamShortcutX = clip->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = clip->lastSelectedParamShortcutY;
		}
		else {
			lastSelectedParamShortcutX = currentSong->lastSelectedParamShortcutX;
			lastSelectedParamShortcutY = currentSong->lastSelectedParamShortcutY;
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
