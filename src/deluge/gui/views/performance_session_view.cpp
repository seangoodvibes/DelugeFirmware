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

#include "gui/views/performance_session_view.h"
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
//using namespace deluge::gui::menu_item;

const ParamsForPerformance songParamsForPerformance[kDisplayWidth] = {
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_FREQ, 8, 7},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_RES, 8, 6},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_FREQ, 9, 7},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_RES, 9, 6},
    {Param::Kind::UNPATCHED, Param::Unpatched::BASS, 10, 6},
    {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE, 11, 6},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT, 13, 3},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_AMOUNT, 14, 3},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_RATE, 14, 0},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_RATE, 12, 7},
    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH, 12, 6},
    {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_FEEDBACK, 12, 5},
    {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_OFFSET, 12, 4},
    {Param::Kind::UNPATCHED, Param::Unpatched::SAMPLE_RATE_REDUCTION, 6, 5},
    {Param::Kind::UNPATCHED, Param::Unpatched::BITCRUSHING, 6, 6},
    {Param::Kind::UNPATCHED, Param::Unpatched::STUTTER_RATE, 5, 7},
};

//colours for the performance mode

const uint8_t rowColour[kDisplayWidth][3] = {

    {255, 0, 0},    //LPF Cutoff
    {255, 0, 0},    //LPF Resonance
    {221, 72, 13},  //HPF Cutoff
    {221, 72, 13},  //HPF Resonance
    {170, 182, 0},  //EQ Bass
    {170, 182, 0},  //EQ Treble
    {85, 182, 72},  //Reverb Amount
    {51, 109, 145}, //Delay Amount
    {51, 109, 145}, //Delay Rate
    {144, 72, 91},  //Mod FX Offset
    {144, 72, 91},  //Mod FX Feedback
    {144, 72, 91},  //Mod FX Depth
    {144, 72, 91},  //Mod FX Rate
    {128, 0, 128},  //Decimation
    {128, 0, 128},  //Bitcrush
    {0, 0, 255}};   //Stutter

const uint8_t rowTailColour[kDisplayWidth][3] = {

    {53, 2, 2},   //LPF Cutoff
    {53, 2, 2},   //LPF Resonance
    {46, 16, 2},  //HPF Cutoff
    {46, 16, 2},  //HPF Resonance
    {36, 38, 2},  //EQ Bass
    {36, 38, 2},  //EQ Treble
    {19, 38, 16}, //Reverb Amount
    {12, 23, 31}, //Delay Amount
    {12, 23, 31}, //Delay Rate
    {37, 15, 37}, //Mod FX Offset
    {37, 15, 37}, //Mod FX Feedback
    {37, 15, 37}, //Mod FX Depth
    {37, 15, 37}, //Mod FX Rate
    {53, 0, 53},  //Decimation
    {53, 0, 53},  //Bitcrush
    {2, 2, 53}};  //Stutter

PerformanceSessionView performanceSessionView{};

PerformanceSessionView::PerformanceSessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
	successfullyReadDefaultsFromFile = false;
	anyChangesToSave = false;
	defaultEditingMode = false;
	lastPadPress.isActive = false;
	lastPadPress.xDisplay = kNoSelection;
	lastPadPress.yDisplay = kNoSelection;
	lastPadPress.paramKind = Param::Kind::NONE;
	lastPadPress.paramID = kNoSelection;

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		previousKnobPosition[xDisplay] = kNoSelection;
		currentKnobPosition[xDisplay] = kNoSelection;
		previousPadPressYDisplay[xDisplay] = kNoSelection;
		timeLastPadPress[xDisplay] = 0;
		padPressHeld[xDisplay] = false;

		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			int32_t defaultFXValue = calculateKnobPosForSinglePadPress(yDisplay);
			defaultFXValues[xDisplay][yDisplay] = defaultFXValue;
		}
	}
}

bool PerformanceSessionView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	if (!successfullyReadDefaultsFromFile) {
		readDefaultsFromFile();
	}

	focusRegained();

	return true;
}

void PerformanceSessionView::focusRegained() {
	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);

	currentSong->affectEntire = true;
	currentSong->performanceView = true;

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	setCentralLEDStates();

	indicator_leds::setLedState(IndicatorLED::BACK, false);

	setLedStates();

	if (anyChangesToSave) {
		indicator_leds::blinkLed(IndicatorLED::SAVE);
	}

	currentSong->lastClipInstanceEnteredStartPos = -1;

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void PerformanceSessionView::graphicsRoutine() {
	static int counter = 0;
	if (currentUIMode == UI_MODE_NONE) {
		int32_t modKnobMode = -1;
		bool editingComp = false;
		if (view.activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;
				editingComp = view.activeModControllableModelStack.modControllable->isEditingComp();
			}
		}
		if (modKnobMode == 4 && editingComp) { //upper
			counter = (counter + 1) % 5;
			if (counter == 0) {
				uint8_t gr = AudioEngine::mastercompressor.gainReduction;

				indicator_leds::setMeterLevel(1, gr); //Gain Reduction LED
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	// Nothing to do here but clear since we don't render playhead
	memset(&tickSquares, 255, sizeof(tickSquares));
	memset(&colours, 255, sizeof(colours));
	PadLEDs::setTickSquares(tickSquares, colours);
}

ActionResult PerformanceSessionView::timerCallback() {
	return ActionResult::DEALT_WITH;
}

bool PerformanceSessionView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                            bool drawUndefinedArea) {
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

	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);

	PadLEDs::renderingLock = false;

	return true;
}

//render performance mode
void PerformanceSessionView::performActualRender(uint32_t whichRows, uint8_t* image,
                                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                 int32_t xScroll, uint32_t xZoom, int32_t renderWidth,
                                                 int32_t imageWidth, bool drawUndefinedArea) {

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

		renderRow(image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
	}
}

//this function started off as a copy of the renderRow function from the NoteRow class - I replaced "notes" with "nodes"
//it worked for the most part, but there was bugs so I removed the buggy code and inserted my alternative rendering method
//which always works. hoping to bring back the other code once I've worked out the bugs.
void PerformanceSessionView::renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		uint8_t* pixel = image + (xDisplay * 3);

		if ((currentKnobPosition[xDisplay] != kNoSelection) && (padPressHeld[xDisplay] == false)) {
			memcpy(pixel, &rowColour[xDisplay], 3);
		}
		else {
			memcpy(pixel, &rowTailColour[xDisplay], 3);
		}

		if ((currentKnobPosition[xDisplay] == defaultFXValues[xDisplay][yDisplay])
		    && (previousPadPressYDisplay[xDisplay] == yDisplay)) {
			pixel[0] = 130;
			pixel[1] = 120;
			pixel[2] = 130;
		}

		occupancyMask[xDisplay] = 64;
	}
}

bool PerformanceSessionView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	return true;
}

//render performance view display on opening
void PerformanceSessionView::renderViewDisplay() {
	if (defaultEditingMode) {
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORMANCE), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			yPos = yPos + 24;

			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_EDITOR), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			deluge::hid::display::OLED::sendMainImage();
		}
		else {
			display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORM_EDITOR));
		}
	}
	else {
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

			yPos = yPos + 12;

			deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORMANCE), yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			deluge::hid::display::OLED::sendMainImage();
		}
		else {
			display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORMANCE));
		}
	}
}

//Render Parameter Name and Value set when using Performance Pads
void PerformanceSessionView::renderFXDisplay(Param::Kind paramKind, int32_t paramID, int32_t knobPos) {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

		//display parameter name
		char parameterName[30];
		if (paramKind == Param::Kind::UNPATCHED) {
			strncpy(parameterName, getUnpatchedParamDisplayName(paramID), 29);
		}
		else if (paramKind == Param::Kind::GLOBAL_EFFECTABLE) {
			strncpy(parameterName, getGlobalEffectableParamDisplayName(paramID), 29);
		}

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
		deluge::hid::display::OLED::drawStringCentred(parameterName, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		//display parameter value
		yPos = yPos + 24;

		char buffer[5];
		intToString(knobPos, buffer);
		deluge::hid::display::OLED::drawStringCentred(buffer, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();
	}
	//7Seg Display
	else {
		char buffer[5];
		intToString(knobPos, buffer);
		display->displayPopup(buffer);
	}
}

void PerformanceSessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	renderViewDisplay();
	return;
}

void PerformanceSessionView::redrawNumericDisplay() {
	renderViewDisplay();
	return;
}

void PerformanceSessionView::setLedStates() {

	indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);

	view.setLedStates();
	view.setModLedStates();

#ifdef currentClipStatusButtonX
	view.switchOffCurrentClipPad();
#endif
}

void PerformanceSessionView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	if (getCurrentUI() == this) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

ActionResult PerformanceSessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	// Clip-view button
	if (b == CLIP_VIEW) {
		if (on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))
		    && playbackHandler.recording != RECORDING_ARRANGEMENT) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			releaseStutter(modelStack);
			sessionView.transitionToViewForClip(); // May fail if no currentClip
		}
	}

	// Song-view button without shift

	// Arranger view button, or if there isn't one then song view button
#ifdef arrangerViewButtonX
	else if (b == arrangerView) {
#else
	else if (b == SESSION_VIEW && !Buttons::isShiftButtonPressed()) {
#endif
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		bool lastSessionButtonActiveState = sessionButtonActive;
		sessionButtonActive = on;

		// Press with special modes
		if (on) {
			sessionButtonUsed = false;

			// If holding record button...
			if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
				Buttons::recordButtonPressUsedUp = true;

				// Make sure we weren't already playing...
				if (!playbackHandler.playbackState) {

					Action* action = actionLogger.getNewAction(ACTION_ARRANGEMENT_RECORD, false);

					arrangerView.xScrollWhenPlaybackStarted = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
					if (action) {
						action->posToClearArrangementFrom = arrangerView.xScrollWhenPlaybackStarted;
					}

					currentSong->clearArrangementBeyondPos(
					    arrangerView.xScrollWhenPlaybackStarted,
					    action); // Want to do this before setting up playback or place new instances
					int32_t error =
					    currentSong->placeFirstInstancesOfActiveClips(arrangerView.xScrollWhenPlaybackStarted);

					if (error) {
						display->displayError(error);
						return ActionResult::DEALT_WITH;
					}
					playbackHandler.recording = RECORDING_ARRANGEMENT;
					playbackHandler.setupPlaybackUsingInternalClock();

					arrangement.playbackStartedAtPos =
					    arrangerView.xScrollWhenPlaybackStarted; // Have to do this after setting up playback

					indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
					indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
					sessionButtonUsed = true;
				}
			}
		}
		// Release without special mode
		else if (!on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))) {
			if (lastSessionButtonActiveState && !sessionButtonActive && !sessionButtonUsed
			    && !sessionView.gridFirstPadActive()) {

				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					// Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					currentSong->resumeClipsClonedForArrangementRecording();
					playbackHandler.recording = RECORDING_OFF;
					view.setModLedStates();
					playbackHandler.setLedStates();
				}
				else {
					releaseStutter(modelStack);
					sessionView.goToArrangementEditor();
				}

				sessionButtonUsed = false;
			}
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			releaseStutter(modelStack);
			changeRootUI(&sessionView);
			indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
		}
	}

	//clear and reset held params
	else if (b == BACK && isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (on) {
			resetPerformanceView(modelStack);
		}
	}

	else if (b == SAVE) {
		if (on) {
			writeDefaultsToFile();
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_SAVED));
			indicator_leds::setLedState(IndicatorLED::SAVE, false);
		}
	}

	else if (b == LOAD) {
		if (on) {
			readDefaultsFromFile();
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_LOADED));
			indicator_leds::setLedState(IndicatorLED::SAVE, false);
		}
	}

	else if ((b == SELECT_ENC) && !Buttons::isShiftButtonPressed()) {
		if (on) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			display->setNextTransitionDirection(1);
			soundEditor.setup();
			openUI(&soundEditor);
		}
	}

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
	//disable back button press since undo doesn't work well in this view atm.
	else if ((b == Y_ENC) || (b == BACK)) {
		goto doNothing;
	}

	else {
notDealtWith:
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}

doNothing:
	return ActionResult::DEALT_WITH;
}

ActionResult PerformanceSessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	//if pad was pressed in main deluge grid (not sidebar)
	if (xDisplay < kDisplayWidth) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		//obtain Param::Kind, ParamID corresponding to the column pressed on performance grid
		Param::Kind lastSelectedParamKind = songParamsForPerformance[xDisplay].paramKind; //kind;
		int32_t lastSelectedParamID = songParamsForPerformance[xDisplay].paramID;

		//pressing a pad
		if (on) {
			//no need to pad press action if you've already processed it previously and pad was held
			if (previousPadPressYDisplay[xDisplay] != yDisplay) {
				padPressAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, yDisplay,
				               !defaultEditingMode);
			}
		}
		//releasing a pad
		else {
			//if releasing a pad with "held" status shortly after being given that status
			//or releasing a pad that was not in "held" status but was a longer press and release
			if ((padPressHeld[xDisplay]
			     && ((AudioEngine::audioSampleTimer - timeLastPadPress[xDisplay]) < kShortPressTime))
			    || ((previousKnobPosition[xDisplay] != kNoSelection) && (previousPadPressYDisplay[xDisplay] == yDisplay)
			        && ((AudioEngine::audioSampleTimer - timeLastPadPress[xDisplay]) >= kShortPressTime))) {

				padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, !defaultEditingMode);
			}
			//if releasing a pad that was quickly pressed, give it held status
			else if ((previousKnobPosition[xDisplay] != kNoSelection)
			         && (previousPadPressYDisplay[xDisplay] == yDisplay)
			         && ((AudioEngine::audioSampleTimer - timeLastPadPress[xDisplay]) < kShortPressTime)) {
				padPressHeld[xDisplay] = true;
			}
		}
		uiNeedsRendering(this); //re-render pads
	}
	return ActionResult::DEALT_WITH;
}

void PerformanceSessionView::padPressAction(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                            int32_t paramID, int32_t xDisplay, int32_t yDisplay, bool renderDisplay) {
	if (setParameterValue(modelStack, paramKind, paramID, xDisplay, defaultFXValues[xDisplay][yDisplay],
	                      renderDisplay)) {
		//if pressing a new pad in a column, reset held status
		padPressHeld[xDisplay] = false;

		//save row yDisplay of current pad press in column xDisplay
		previousPadPressYDisplay[xDisplay] = yDisplay;

		//save time of current pad press in column xDisplay
		timeLastPadPress[xDisplay] = AudioEngine::audioSampleTimer;

		//update current knob position
		currentKnobPosition[xDisplay] = defaultFXValues[xDisplay][yDisplay];

		//save xDisplay, yDisplay, paramKind and paramID currently being edited
		lastPadPress.isActive = true;
		lastPadPress.xDisplay = xDisplay;
		lastPadPress.yDisplay = yDisplay;
		lastPadPress.paramKind = paramKind;
		lastPadPress.paramID = paramID;
	}
}

void PerformanceSessionView::padReleaseAction(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                              int32_t paramID, int32_t xDisplay, bool renderDisplay) {
	if (setParameterValue(modelStack, paramKind, paramID, xDisplay, previousKnobPosition[xDisplay], renderDisplay)) {
		previousPadPressYDisplay[xDisplay] = kNoSelection;
		previousKnobPosition[xDisplay] = kNoSelection;
		currentKnobPosition[xDisplay] = kNoSelection;
		padPressHeld[xDisplay] = false;
		lastPadPress.isActive = false;
		lastPadPress.xDisplay = kNoSelection;
		lastPadPress.yDisplay = kNoSelection;
		lastPadPress.paramKind = Param::Kind::NONE;
		lastPadPress.paramID = kNoSelection;
	}
}

void PerformanceSessionView::resetPerformanceView(ModelStackWithThreeMainThings* modelStack) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if (padPressHeld[xDisplay]) {
			//obtain Param::Kind and ParamID corresponding to the column in focus (xDisplay)
			Param::Kind lastSelectedParamKind = songParamsForPerformance[xDisplay].paramKind; //kind;
			int32_t lastSelectedParamID = songParamsForPerformance[xDisplay].paramID;

			padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, false);
		}
	}
	renderViewDisplay();
	uiNeedsRendering(this);
}

void PerformanceSessionView::releaseStutter(ModelStackWithThreeMainThings* modelStack) {
	if (isUIModeActive(UI_MODE_STUTTERING)) {
		padReleaseAction(modelStack, Param::Kind::UNPATCHED, Param::Unpatched::STUTTER_RATE, kDisplayWidth - 1, false);
	}
}

bool PerformanceSessionView::setParameterValue(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind,
                                               int32_t paramID, int32_t xDisplay, int32_t knobPos, bool renderDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStack, paramID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			//if switching to a new pad in the stutter column and stuttering is already active
			//e.g. it means a pad was held before, end previous stutter before starting stutter again
			if ((paramKind == Param::Kind::UNPATCHED) && (paramID == Param::Unpatched::STUTTER_RATE)
			    && (isUIModeActive(UI_MODE_STUTTERING))) {
				((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
				    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
			}

			if (previousKnobPosition[xDisplay] == kNoSelection) {
				int32_t oldParameterValue =
				    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
				previousKnobPosition[xDisplay] =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(oldParameterValue, modelStackWithParam);
			}

			int32_t newParameterValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(knobPos, modelStackWithParam);

			modelStackWithParam->autoParam->setValuePossiblyForRegion(newParameterValue, modelStackWithParam,
			                                                          view.modPos, view.modLength);

			if ((paramKind == Param::Kind::UNPATCHED) && (paramID == Param::Unpatched::STUTTER_RATE)
			    && (previousKnobPosition[xDisplay] != knobPos)) {
				((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
				    ->beginStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
			}

			if (renderDisplay) {
				int32_t valueForDisplay = calculateKnobPosForDisplay(knobPos + kKnobPosOffset);
				renderFXDisplay(paramKind, paramID, valueForDisplay);
			}

			if (defaultEditingMode) {
				int32_t lastSelectedParamShortcutX = songParamsForPerformance[xDisplay].xDisplay;
				int32_t lastSelectedParamShortcutY = songParamsForPerformance[xDisplay].yDisplay;

				soundEditor.potentialShortcutPadAction(lastSelectedParamShortcutX, lastSelectedParamShortcutY, true);
			}

			return true;
		}
	}

	return false;
}

//get's the modelstack for the parameters that are being edited
ModelStackWithAutoParam* PerformanceSessionView::getModelStackWithParam(ModelStackWithThreeMainThings* modelStack,
                                                                        int32_t paramID) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (modelStack) {
		ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();

		if (summary) {
			ParamSet* paramSet = (ParamSet*)summary->paramCollection;
			modelStackWithParam = modelStack->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
		}
	}

	return modelStackWithParam;
}

//converts grid pad press yDisplay into a knobPosition value
//this will likely need to be customized based on the parameter to create some more param appropriate ranges
int32_t PerformanceSessionView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {
	int32_t newKnobPos = 0;

	//if you press bottom pad, value is 0, for all other pads except for the top pad, value = row Y * 18
	if (yDisplay < 7) {
		newKnobPos = yDisplay * kParamValueIncrementForAutomationSinglePadPress;
	}
	//if you are pressing the top pad, set the value to max (128)
	else {
		newKnobPos = kMaxKnobPos;
	}

	//in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

//convert deluge internal knobPos range to same range as used by menu's.
int32_t PerformanceSessionView::calculateKnobPosForDisplay(int32_t knobPos) {
	int32_t offset = 0;

	//convert knobPos from 0 - 128 to 0 - 50
	return (((((knobPos << 20) / kMaxKnobPos) * kMaxMenuValue) >> 20) - offset);
}

//Used to edit a pad's value in editing mode
void PerformanceSessionView::selectEncoderAction(int8_t offset) {
	if (defaultEditingMode && lastPadPress.isActive && (getCurrentUI() == &soundEditor)) {
		int32_t lastSelectedParamShortcutX = songParamsForPerformance[lastPadPress.xDisplay].xDisplay;
		int32_t lastSelectedParamShortcutY = songParamsForPerformance[lastPadPress.xDisplay].yDisplay;

		if (soundEditor.getCurrentMenuItem()
		    == paramShortcutsForSessionView[lastSelectedParamShortcutX][lastSelectedParamShortcutY]) {
			defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay] = calculateKnobPosForSelectEncoderTurn(
			    defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay], offset);

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			if (setParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay,
			                      defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay], false)) {
				anyChangesToSave = true;
				indicator_leds::blinkLed(IndicatorLED::SAVE);
			}
			goto exit;
		}
	}
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->selectEncoderAction(offset);
	}
exit:
	return;
}

//used to calculate new knobPos when you turn the select encoder
int32_t PerformanceSessionView::calculateKnobPosForSelectEncoderTurn(int32_t knobPos, int32_t offset) {

	//adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		newKnobPos = knobPos;
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

	//in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

ActionResult PerformanceSessionView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult PerformanceSessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

uint32_t PerformanceSessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

uint32_t PerformanceSessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

void PerformanceSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}

void PerformanceSessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	UI::modEncoderButtonAction(whichModEncoder, on);
}

void PerformanceSessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}

void PerformanceSessionView::writeDefaultsToFile() {
	if (!anyChangesToSave) {
		return;
	}

	int32_t error = storageManager.createXMLFile(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_XML), true);
	if (error) {
		return;
	}

	storageManager.writeOpeningTagBeginning(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_TAG));
	storageManager.writeOpeningTagEnd();

	storageManager.writeOpeningTagBeginning(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG));
	storageManager.writeOpeningTagEnd();

	writeDefaultFXValuesToFile();

	storageManager.writeClosingTag(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG));

	storageManager.writeClosingTag(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_TAG));

	storageManager.closeFileAfterWriting();

	anyChangesToSave = false;
}

//creates "FX1 - FX16 tags"
//limiting # of FX to the # of columns on the grid (16 = kDisplayWidth)
//could expand # of FX in the future if we allow user to selected from a larger bank of FX / build their own FX
void PerformanceSessionView::writeDefaultFXValuesToFile() {
	char tagName[10];
	tagName[0] = 'F';
	tagName[1] = 'X';
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		intToString(xDisplay + 1, &tagName[2]);
		storageManager.writeOpeningTagBeginning(tagName);
		storageManager.writeOpeningTagEnd();
		writeDefaultFXRowValuesToFile(xDisplay);
		storageManager.writeClosingTag(tagName);
	}
}

//creates "8 - 1 row # tags within a "row" tag"
//limiting # of rows to the # of rows on the grid (8 = kDisplayHeight)
void PerformanceSessionView::writeDefaultFXRowValuesToFile(int32_t xDisplay) {
	storageManager.writeOpeningTagBeginning(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_ROW_TAG));
	storageManager.writeOpeningTagEnd();
	char rowNumber[5];
	//creates tags from row 8 down to row 1
	for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
		intToString(yDisplay + 1, rowNumber);
		storageManager.writeTag(rowNumber, defaultFXValues[xDisplay][yDisplay] + kKnobPosOffset);
	}
	storageManager.writeClosingTag(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_ROW_TAG));
}

void PerformanceSessionView::readDefaultsFromFile() {
	FilePointer fp;
	bool success = storageManager.fileExists(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_XML), &fp);
	if (!success) {
		return;
	}

	int32_t error = storageManager.openXMLFile(&fp, l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_TAG));
	if (error) {
		return;
	}

	char const* tagName;
	//step into the defaultFXValues tag
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_FXVALUES_TAG))) {
			readDefaultFXValuesFromFile();
		}
		storageManager.exitTag();
	}

	storageManager.closeFile();

	successfullyReadDefaultsFromFile = true;
}

void PerformanceSessionView::readDefaultFXValuesFromFile() {
	char const* tagName;
	char tagNameFX[5];
	tagNameFX[0] = 'F';
	tagNameFX[1] = 'X';

	//loop through all FX number tags
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//find the FX number that the tag corresponds to
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			intToString(xDisplay + 1, &tagNameFX[2]);

			if (!strcmp(tagName, tagNameFX)) {
				readDefaultFXRowValuesFromFile(xDisplay);
				break;
			}
		}
		storageManager.exitTag();
	}
}

void PerformanceSessionView::readDefaultFXRowValuesFromFile(int32_t xDisplay) {
	char const* tagName;
	//step into the row tag
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULTS_ROW_TAG))) {
			readDefaultFXRowNumberValuesFromFile(xDisplay);
		}
		storageManager.exitTag();
	}
}

void PerformanceSessionView::readDefaultFXRowNumberValuesFromFile(int32_t xDisplay) {
	char const* tagName;
	char rowNumber[5];
	//loop through all row number tags
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//find the row number that the tag corresponds to
		//reads from row 8 down to row 1
		for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
			intToString(yDisplay + 1, rowNumber);
			if (!strcmp(tagName, rowNumber)) {
				defaultFXValues[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt() - kKnobPosOffset;
				break;
			}
		}
		storageManager.exitTag();
	}
}
