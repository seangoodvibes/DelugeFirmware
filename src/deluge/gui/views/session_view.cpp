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

#include "gui/views/session_view.h"
#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/clip_settings/clip_settings.h"
#include "gui/context_menu/clip_settings/new_clip_type.h"
#include "gui/context_menu/context_menu.h"
#include "gui/context_menu/midi_learn_mode.h"
#include "gui/context_menu/stem_export/cancel_stem_export.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_follow.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/instrument.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/settings/runtime_feature_settings.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include "util/d_string.h"
#include "util/finally.h"
#include "util/functions.h"
#include "util/try.h"
#include <algorithm>
#include <cstdint>
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

namespace {
PLACE_SDRAM_BSS SessionView local_session_view{};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<SessionView> remote_session_view;
} // namespace

SessionView& session_view_for_session() {
	return remote_session_view.get(local_session_view);
}

namespace {
// Keyboard view's sidebar columns morph to/from the Session colours of the clip's row. The mute colour PadLEDs
// already has; hand it the section colour, unless the clip's row is off-screen and there is nothing to morph to.
void setUpKeyboardSidebarMorph(int32_t clipRow) {
	if (clipRow < 0 || clipRow >= kDisplayHeight) {
		return;
	}
	RGB sidebarRow[kDisplayWidth + kSideBarWidth]{};
	session_view_for_session().drawSectionSquare(clipRow, sidebarRow);
	PadLEDs::enableKeyboardSidebarMorph(sidebarRow[kDisplayWidth + 1]);
}
} // namespace

SessionView::SessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
	createClip = false;
}

bool SessionView::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
		switch (currentSong->session_layout_for_session()) {
		case SessionLayoutType::SessionLayoutTypeRows: {
			*cols = 0xFFFFFFFD;
			*rows = 0;
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				Clip* clip = getClipOnScreen(yDisplay);
				if (clip && !clip->armedForRecording) {
					*rows |= (1 << yDisplay);
				}
			}
			break;
		}
		case SessionLayoutType::SessionLayoutTypeGrid: {
			*cols = 0x03; // Only sidebar
			*rows = 0x0;
			break;
		}
		// explicit fallthrough cases
		case SessionLayoutType::SessionLayoutTypeMaxElement:;
		}

		return true;
	}
	else if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		*cols = 0b11;
		return true;
	}
	else {
		return false;
	}
}

bool SessionView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	focusRegained();

	return true;
}

void SessionView::focusRegained() {
	viewingRecordArmingActive = false;
	horizontalEncoderPressed = false;
	selectLayout(0); // Make sure we get a valid layout from the loaded file

	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);
	redrawClipsOnScreen(doingRender); // We want this here, not just in opened(), because after coming back from
	                                  // loadInstrumentPresetUI, need to at least redraw, and also really need to
	                                  // re-render stuff in case note-tails-being-allowed has changed

	// needs to be set before setActiveModControllableTimelineCounter so that midi follow mode can get
	// the right model stack with param (otherwise midi follow mode will think you're still in a clip)
	selectedClipYDisplay = 255;

	ClipNavigationTimelineView::focusRegained();
	view_for_session().focusRegained();
	// this could happen if you've just converted an instrument clip to an audio clip
	// using the clip settings menu in grid view and it sent you back to song view
	// and the mod controllable was set to the newly converted audio clip
	// if you're still holding that clip, don't change the active mod controllable
	if (currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		view_for_session().setActiveModControllableTimelineCounter(currentSong);
	}

	if (display->haveOLED()) {
		setCentralLEDStates();
	}
	else {
		redrawNumericDisplay();
	}

	indicator_leds::setLedState(IndicatorLED::BACK, false);

	setLedStates();

	currentSong->last_clip_instance_entered_start_pos_for_session() = -1;

	// initiate pulsing of selected clip if you're in grid view
	// but only if it's not already running (which is the case when coming back from clip settings menus)
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		if (!gridSelectedClipPulsing) {
			gridPulseSelectedClip();
		}
	}
}

ActionResult SessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (cancel_stale_session_hold()) {
		return ActionResult::DEALT_WITH;
	}
	using namespace deluge::hid::button;

	// when stem export process has started,
	// do not action anybutton presses except BACK to cancel the process
	if (b != BACK && stemExport.processStarted) {
		return ActionResult::DEALT_WITH;
	}

	OutputType newOutputType;

	if (currentUIMode == UI_MODE_CREATING_CLIP) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		return clipCreationButtonPressed(b, on, inCardRoutine);
	}

	// Clip-view button
	if (b == CLIP_VIEW) {
		bool isGridView = currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid;
		if (on
		    && ((currentUIMode == UI_MODE_NONE) || (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW && isGridView))
		    && playbackHandler.recording != RecordingMode::ARRANGEMENT) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			transitionToViewForClip(); // May fail if no currentClip
		}
	}

// Song-view button without shift

// Arranger view button, or if there isn't one then song view button
#ifdef arrangerViewButtonX
	else if (b == arranger_view_for_session()) {
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
				Buttons::state().recordButtonPressUsedUp = true;

				// Make sure we weren't already playing...
				if (!playbackHandler.playbackState) {

					Action* action =
					    actionLogger.getNewAction(ActionType::ARRANGEMENT_RECORD, ActionAddition::NOT_ALLOWED);

					arranger_view_for_session().xScrollWhenPlaybackStarted =
					    currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT];
					if (action) {
						action->posToClearArrangementFrom = arranger_view_for_session().xScrollWhenPlaybackStarted;
					}

					Error clear_error = currentSong->clearArrangementBeyondPos(
					    arranger_view_for_session().xScrollWhenPlaybackStarted,
					    action); // Want to do this before setting up playback or place new instances
					if (clear_error != Error::NONE) {
						actionLogger.deleteAllLogs();
						display->displayError(clear_error);
						return ActionResult::DEALT_WITH;
					}
					Error error = currentSong->placeFirstInstancesOfActiveClips(
					    arranger_view_for_session().xScrollWhenPlaybackStarted);

					if (error != Error::NONE) {
						display->displayError(error);
						return ActionResult::DEALT_WITH;
					}
					playbackHandler.recording = RecordingMode::ARRANGEMENT;
					playbackHandler.setupPlaybackUsingInternalClock();

					arrangement.playbackStartedAtPos =
					    arranger_view_for_session()
					        .xScrollWhenPlaybackStarted; // Have to do this after setting up playback

					indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
					indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
					sessionButtonUsed = true;
				}
			}

			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
					return ActionResult::DEALT_WITH;
				}

				// Rows are not aligned in grid so we disabled this function, the code below also would need to be
				// aligned
				if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_IMPOSSIBLE_FROM_GRID));
					return ActionResult::DEALT_WITH;
				}

				actionLogger.deleteAllLogs();

				Clip* clip = getClipOnScreen(selectedClipYDisplay);
				Output* output = clip->output;
				int32_t instrumentIndex = currentSong->getOutputIndex(output);
				currentSong->arrangement_y_scroll_for_session() = instrumentIndex - selectedClipPressYDisplay;

				int32_t posPressed = arranger_view_for_session().getPosFromSquare(selectedClipPressXDisplay);
				int32_t proposedStartPos = posPressed;

				int32_t i = output->clipInstances.search(proposedStartPos, LESS);
				ClipInstance* otherInstance = output->clipInstances.getElement(i);
				if (otherInstance) {
					if (otherInstance->pos + otherInstance->length > proposedStartPos) {
moveAfterClipInstance:
						proposedStartPos = ((otherInstance->pos + otherInstance->length - 1)
						                        / currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT]
						                    + 1)
						                   * currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT];
					}
				}

				// Look at the next ClipInstance
				i++;
				otherInstance = output->clipInstances.getElement(i);
				if (otherInstance) {
					if (otherInstance->pos < proposedStartPos + clip->loopLength) {
						goto moveAfterClipInstance;
					}
				}

				// Make sure it won't be extending beyond numerical limit
				if (proposedStartPos > kMaxSequenceLength - clip->loopLength) {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_WOULD_BREACH_MAX_ARRANGEMENT_LENGTH));
					return ActionResult::DEALT_WITH;
				}

				// If we're here, we're ok!
				Error error = output->clipInstances.insertAtIndex(i);
				if (error != Error::NONE) {
					display->displayError(error);
					return ActionResult::DEALT_WITH;
				}

				ClipInstance* newInstance = output->clipInstances.getElement(i);
				newInstance->pos = proposedStartPos;
				newInstance->clip = clip;
				newInstance->length = clip->loopLength;
				arrangement.rowEdited(output, proposedStartPos, proposedStartPos + clip->loopLength, nullptr,
				                      newInstance);

				int32_t howMuchLater = proposedStartPos - posPressed;

				arranger_view_for_session().xPressed = selectedClipPressXDisplay;
				arranger_view_for_session().yPressedEffective = selectedClipPressYDisplay;
				arranger_view_for_session().yPressedActual = selectedClipPressYDisplay;
				arranger_view_for_session().actionOnDepress = false;
				arranger_view_for_session().desiredLength = clip->loopLength;
				arranger_view_for_session().originallyPressedClipActualLength = clip->loopLength;
				arranger_view_for_session().pressedClipInstanceIndex = i;
				arranger_view_for_session().pressedClipInstanceXScrollWhenLastInValidPosition =
				    currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT] + howMuchLater;
				arranger_view_for_session().pressedClipInstanceOutput = clip->output;
				arranger_view_for_session().pressedClipInstanceIsInValidPosition = true;

				currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW;

				arranger_view_for_session().repopulateOutputsOnScreen(false);
				arranger_view_for_session().putDraggedClipInstanceInNewPosition(output);
				sessionButtonActive = false;
				goToArrangementEditor();
			}
		}
		// Release without special mode
		else if (!on && currentUIMode == UI_MODE_NONE) {
			if (lastSessionButtonActiveState && !sessionButtonActive && !sessionButtonUsed && !gridFirstPadActive()) {
				if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					// Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					currentSong->resumeClipsClonedForArrangementRecording();
					playbackHandler.recording = RecordingMode::OFF;
					view_for_session().setModLedStates();
					playbackHandler.setLedStates();
				}
				else {
					goToArrangementEditor();
				}

				sessionButtonUsed = false;
			}
		}
	}

	// Affect-entire button
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			currentSong->affect_entire_for_session() = !currentSong->affect_entire_for_session();
			view_for_session().setActiveModControllableTimelineCounter(currentSong);
		}
	}

	// Record button - adds to what MatrixDriver does with it
	else if (b == RECORD) {
		if (on) {
			if (isNoUIModeActive()) {
				uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
				view_for_session().blinkOn = true;
			}
			// trigger stem export when pressing record while holding save
			else if (isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
				if (playbackHandler.isEitherClockActive() || playbackHandler.recording != RecordingMode::OFF) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EXPORT_STEMS));
				}
				else {
					if (inCardRoutine) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}
					stemExport.startStemExportProcess(StemExportType::CLIP);
					return ActionResult::DEALT_WITH;
				}
			}
			// try loop recording if holding clip in song view
			else if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
				playbackHandler.tryLoopCommand(FlashStorage::defaultLoopRecordingCommand);
			}
			else {
				goto notDealtWith;
			}
		}
		else {
			viewingRecordArmingActive = false;
			if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
				exitUIMode(UI_MODE_VIEWING_RECORD_ARMING);
				PadLEDs::reassessGreyout(false);
				requestRendering(this, 0, 0xFFFFFFFF);
			}
			else {
				goto notDealtWith;
			}
		}
		return ActionResult::NOT_DEALT_WITH; // Make the MatrixDriver do its normal thing with it too
	}

	// cancel stem export process
	else if (b == BACK && isUIModeActive(UI_MODE_STEM_EXPORT)) {
		if (on) {
			bool available = context_menu::cancel_stem_export_for_session().setupAndCheckAvailability();

			if (available) {
				display->setNextTransitionDirection(1);
				openUI(&context_menu::cancel_stem_export_for_session());
			}
		}
	}

	// Overwrite to allow not showing zoom level in grid
	else if (b == X_ENC) {
		horizontalEncoderPressed = on;
		if (on) {
			// Show current zoom level
			if (isNoUIModeActive()
			    && (currentSong->session_layout_for_session() != SessionLayoutType::SessionLayoutTypeGrid)) {
				displayZoomLevel();
			}

			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}

		else {
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
				if (currentSong->session_layout_for_session() != SessionLayoutType::SessionLayoutTypeGrid) {
					display->cancelPopup();
				}
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}
	}

	// If save / delete button pressed, delete the Clip!
	else if (b == SAVE && (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW || gridFirstPadActive())) {
		if (on) {

			if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				performActionOnPadRelease = false;
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			Clip* clip = getClipForLayout();

			if (clip != nullptr) {
				actionLogger.deleteAllLogs();
				clipPressEnded();
				removeClip(clip);
			}
		}
	}

	// Select encoder button
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_HOLDING_SECTION_PAD) {
				if (performActionOnSectionPadRelease) {
					beginEditingSectionRepeatsNum();
				}
				else {
					currentSong->sections[sectionPressed].numRepetitions = 0;
					drawSectionRepeatNumber();
				}
			}
			else if (currentUIMode == UI_MODE_HOLDING_STATUS_PAD) {
				context_menu::clip_settings::clip_settings_for_session().setupAndCheckAvailability();
				openUI(&context_menu::clip_settings::clip_settings_for_session());
			}
			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				actionLogger.deleteAllLogs();
				performActionOnPadRelease = false;

				Clip* clip = getClipForLayout();
				if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
					requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
					if (clip != nullptr) {
						context_menu::clip_settings::clip_settings_for_session().clip = clip;
						context_menu::clip_settings::clip_settings_for_session().setupAndCheckAvailability();
						openUI(&context_menu::clip_settings::clip_settings_for_session());
					}
				}
				else if (clip != nullptr) {
					replaceInstrumentClipWithAudioClip(clip);
				}
			}
			else if (currentUIMode == UI_MODE_NONE) {
				if (session.hasPlaybackActive()) {
					if (session.launchEventAtSwungTickCount) {
						session.cancelAllArming();
						session.cancelAllLaunchScheduling();
						session.lastSectionArmed = 255;
						if (display->haveOLED()) {
							renderUIsForOled();
						}
						else {
							redrawNumericDisplay();
						}
						requestRendering(this, 0, 0xFFFFFFFF);
					}
				}
				// open Song FX menu
				display->setNextTransitionDirection(1);
				sound_editor_for_session().setup();
				openUI(&sound_editor_for_session());
			}
		}
	}

	// Which-instrument-type buttons
	else if (b == SYNTH) {
		newOutputType = OutputType::SYNTH;

changeOutputType:
		if (on && currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW && !Buttons::isShiftButtonPressed()) {

			performActionOnPadRelease = false;

			if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			Clip* clip = getClipForLayout();

			if (clip != nullptr) {
				// Don't allow converting audio clip to instrument clip
				if (clip->type == ClipType::AUDIO) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_CONVERT_TYPE));
				}
				else {

					InstrumentClip* instrumentClip = (InstrumentClip*)clip;
					Instrument* instrument = (Instrument*)instrumentClip->output;

					// don't allow clip type change if clip is not empty
					// only impose this restriction if switching to/from kit clip
					if (((instrument->type == OutputType::KIT) || (newOutputType == OutputType::KIT))
					    && (!clip->isEmpty() || !clip->output->isEmpty())) {
						return ActionResult::DEALT_WITH;
					}

					// If load button held, go into LoadInstrumentPresetUI
					if (Buttons::isButtonPressed(deluge::hid::button::LOAD)) {

						// Can't do that for MIDI or CV Clips though
						if (newOutputType == OutputType::MIDI_OUT || newOutputType == OutputType::CV) {
							goto doActualSimpleChange;
						}

						actionLogger.deleteAllLogs();

						currentUIMode = UI_MODE_NONE;
						selectedClipYDisplay = 255;

						load_instrument_preset_ui_for_session().setupLoadInstrument(newOutputType, instrument, nullptr);
						openUI(&load_instrument_preset_ui_for_session());
					}

					// Otherwise, just change the instrument type
					else {
doActualSimpleChange:

						switch (currentSong->session_layout_for_session()) {
						case SessionLayoutType::SessionLayoutTypeRows: {
							char modelStackMemory[MODEL_STACK_MAX_SIZE];
							ModelStackWithTimelineCounter* modelStack =
							    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClip);

							view_for_session().changeOutputType(newOutputType, modelStack, true);
							break;
						}
						case SessionLayoutType::SessionLayoutTypeGrid: {
							// Mostly taken from ArrangerView::changeOutputType
							if (instrument->type != newOutputType) {
								Instrument* newInstrument = currentSong->changeOutputType(instrument, newOutputType);
								if (newInstrument) {
									view_for_session().displayOutputName(newInstrument);
									view_for_session().setActiveModControllableTimelineCounter(
									    newInstrument->getActiveClip());
								}
							}
							break;
						}
						// explicit fallthrough cases
						case SessionLayoutType::SessionLayoutTypeMaxElement:;
						}
					}
				}

				requestRendering(this, 1 << selectedClipYDisplay, 0);
			}
		}
	}
	else if (b == KIT) {
		newOutputType = OutputType::KIT;
		goto changeOutputType;
	}
	else if (b == MIDI) {
		newOutputType = OutputType::MIDI_OUT;
		goto changeOutputType;
	}
	else if (b == CV) {
		newOutputType = OutputType::CV;
		goto changeOutputType;
	}
	else if (b == KEYBOARD) {
		if (on && (currentUIMode == UI_MODE_NONE)) {
			performance_view_for_session().timeKeyboardShortcutPress = AudioEngine::audioSampleTimer;
			changeRootUI(&performance_view_for_session());
		}
	}
	else if (b == Y_ENC) {
		if (on && !Buttons::isShiftButtonPressed()) {
			UI* currentUI = getCurrentUI();
			bool isOLEDSessionView =
			    display->haveOLED()
			    && (currentUI == &session_view_for_session() || currentUI == &arranger_view_for_session());
			// only display pop-up if we're using 7SEG or we're not currently in Song / Arranger View
			if (!isOLEDSessionView) {
				currentSong->displayCurrentRootNoteAndScaleName();
			}
		}
	}
	else {
notDealtWith:
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

void SessionView::goToArrangementEditor() {
	currentSong->x_zoom_for_return_to_song_view_for_session() = currentSong->x_zoom_for_session()[NAVIGATION_CLIP];
	currentSong->x_scroll_for_return_to_song_view_for_session() = currentSong->x_scroll_for_session()[NAVIGATION_CLIP];
	changeRootUI(&arranger_view_for_session());
}

void SessionView::beginEditingSectionRepeatsNum() {
	performActionOnSectionPadRelease = false;
	drawSectionRepeatNumber();
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
}

ActionResult SessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	if (cancel_stale_session_hold()) {
		return ActionResult::DEALT_WITH;
	}
	// don't interact with sidebar if VU Meter is displayed
	// and you're in the volume/pan mod knob mode (0)
	if (xDisplay >= kDisplayWidth && view_for_session().displayVUMeter && (view_for_session().getModKnobMode() == 0)) {
		return ActionResult::DEALT_WITH;
	}

	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridHandlePads(xDisplay, yDisplay, on);
	}

	Clip* clip = getClipOnScreen(yDisplay);
	int32_t clipIndex = yDisplay + currentSong->song_view_y_scroll_for_session();

	// If we tapped on a Clip's main pads...
	if (xDisplay < kDisplayWidth) {

		// Press down
		if (on) {

			Buttons::state().recordButtonPressUsedUp = true;

			if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
				goto holdingRecord;
			}

			// If no Clip previously pressed...
			if (currentUIMode == UI_MODE_NONE) {

				// If they're holding down the record button...
				if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {

holdingRecord:
					// If doing recording stuff, create a "pending overdub".
					// We may or may not be doing a tempoless record and need to finish that up.
					if (playbackHandler.playbackState && currentPlaybackMode == &session) {

						Clip* sourceClip = getClipOnScreen(yDisplay + 1);

						if (!sourceClip) {
							return ActionResult::DEALT_WITH;
						}

						// If already has a pending overdub, get out
						if (currentSong->getPendingOverdubWithOutput(sourceClip->output)) {
							return ActionResult::DEALT_WITH;
						}

						if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
							display->displayPopup(
							    deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
							return ActionResult::DEALT_WITH;
						}

						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}

						int32_t clipIndex = yDisplay + currentSong->song_view_y_scroll_for_session() + 1;

						// If source clip currently recording, arm it to stop (but not if tempoless recording)
						if (playbackHandler.isEitherClockActive() && sourceClip->getCurrentlyRecordingLinearly()
						    && sourceClip->armState == ArmState::OFF) {
							session.toggleClipStatus(sourceClip, &clipIndex, false, kInternalButtonPressLatency);
						}

						OverDubType newOverdubNature =
						    (xDisplay < kDisplayWidth) ? OverDubType::Normal : OverDubType::ContinuousLayering;
						Clip* overdub =
						    currentSong->createPendingNextOverdubBelowClip(sourceClip, clipIndex, newOverdubNature);
						if (overdub) {

							session.scheduleOverdubToStartRecording(overdub, sourceClip);

							if (playbackHandler.recording == RecordingMode::OFF) {
								playbackHandler.recording = RecordingMode::NORMAL;
								playbackHandler.setLedStates();
							}

							// Since that was all effective, let's exit out of UI_MODE_VIEWING_RECORD_ARMING too
							if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
								uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
								currentUIMode = UI_MODE_NONE;
								PadLEDs::reassessGreyout(false);
								requestRendering(this, 0, 0xFFFFFFFF);
							}

							// If we were doing a tempoless record, now's the time to stop that and restart playback
							if (!playbackHandler.isEitherClockActive()) {
								playbackHandler.finishTempolessRecording(true, kInternalButtonPressLatency, false);
							}
						}
						else if (currentSong->anyClipsSoloing) {
							display->displayPopup(deluge::l10n::get(
							    deluge::l10n::String::STRING_FOR_CANT_CREATE_OVERDUB_WHILE_CLIPS_SOLOING));
						}
					}
				}

				// If Clip present here...
				else if (clip) {

					// If holding down tempo knob...
					if (Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC)) {
						playbackHandler.grabTempoFromClip(clip);
					}

					// If it's a pending overdub, delete it
					else if (clip->isPendingOverdub) {
removePendingOverdub:
						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Possibly not quite necessary...
						}

						removeClip(getClipOnScreen(yDisplay));
						session.justAbortedSomeLinearRecording();
					}

					// Or, normal action - select the pressed Clip
					else {

						selectedClipYDisplay = yDisplay;
						// This is only interresting for changing colour
						clipWasSelectedWithShift = Buttons::isShiftButtonPressed();
startHoldingDown:
						selectedClipPressYDisplay = yDisplay;
						// we've either created or selected a clip, so set it to be current
						currentSong->setCurrentClip(clip);
						currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
						session_hold_revision =
						    deluge::gui::ui_session::navigation.active().structural_refresh.revision();
						selectedClipPressXDisplay = xDisplay;
						performActionOnPadRelease = true;
						selectedClipTimePressed = AudioEngine::audioSampleTimer;
						view_for_session().setActiveModControllableTimelineCounter(clip);
						view_for_session().displayOutputName(clip->output, true, clip);
					}
				}

				// Otherwise, try and create one
				else {

					if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
						return ActionResult::DEALT_WITH;
					}
					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					// if (possiblyCreatePendingNextOverdub(clipIndex, OverdubType::EXTENDING)) return
					// ActionResult::DEALT_WITH;

					OutputType toCreate;
					if (FlashStorage::defaultUseLastClipType && lastTypeCreated != OutputType::NONE) {
						toCreate = lastTypeCreated;
					}
					else {
						toCreate = FlashStorage::defaultNewClipType;
					}

					// we can't create CV or Audio Clip's first because audio clips can't be subsequently converted
					// to other clip types (yet) and CV clips will block creating any other clips after two CV clips are
					// created
					if (toCreate == OutputType::NONE || toCreate == OutputType::CV || toCreate == OutputType::AUDIO) {
						toCreate = OutputType::SYNTH;
					}
					clip = createNewInstrumentClip(toCreate, yDisplay);
					if (!clip) {
						return ActionResult::DEALT_WITH;
					}

					lastTypeCreated = clip->output->type;
					createClip = true;

					int32_t numClips = currentSong->sessionClips.getNumElements();
					if (clipIndex < 0) {
						clipIndex = 0;
					}
					else if (clipIndex >= numClips) {
						clipIndex = numClips - 1;
					}

					// This is only interresting for changing colour
					clipWasSelectedWithShift = Buttons::isShiftButtonPressed();
					selectedClipYDisplay = clipIndex - currentSong->song_view_y_scroll_for_session();
					requestRendering(this, 0, 1 << selectedClipYDisplay);

					if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeRows) {
						goto startHoldingDown;
					}
				}
			}

			// If Clip previously already pressed, clone it to newly-pressed row
			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				if (selectedClipYDisplay != yDisplay && performActionOnPadRelease) {

					if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
						display->displayPopup(
						    deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
						return ActionResult::DEALT_WITH;
					}

					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					actionLogger.deleteAllLogs();
					cloneClip(selectedClipYDisplay, yDisplay);
					goto justEndClipPress;
				}
			}

			else if (currentUIMode == UI_MODE_MIDI_LEARN) {
				if (clip) {

					// AudioClip
					if (clip->type == ClipType::AUDIO) {
						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}
						if (getCurrentUI() != &deluge::gui::context_menu::midi_learn_mode_for_session()) {
							view_for_session().endMIDILearn();
						}
						gui::context_menu::audio_input_selector_for_session().audioOutput = (AudioOutput*)clip->output;
						gui::context_menu::audio_input_selector_for_session().setupAndCheckAvailability();
						openUI(&gui::context_menu::audio_input_selector_for_session());
					}

					// InstrumentClip
					else {
midiLearnMelodicInstrumentAction:

						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}
						view_for_session().instrumentMidiLearnPadPressed(on, (Instrument*)clip->output);
					}
				}
			}
		}

		// Release
		else {
			// If Clip was pressed before...
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {

				// Stop stuttering if we are
				if (isUIModeActive(UI_MODE_STUTTERING)) {
					((ModControllableAudio*)view_for_session().activeModControllableModelStack.modControllable)
					    ->endStutter(
					        (ParamManagerForTimeline*)view_for_session().activeModControllableModelStack.paramManager);
				}

				if (performActionOnPadRelease && xDisplay == selectedClipPressXDisplay
				    && AudioEngine::audioSampleTimer - selectedClipTimePressed < kShortPressTime) {

					// Not allowed if recording arrangement
					if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
						display->displayPopup(
						    deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
						goto justEndClipPress;
					}

					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					// Enter Clip
					Clip* clip = getClipOnScreen(selectedClipYDisplay);
					transitionToViewForClip(clip);
					createClip = false;
				}

				// If doing nothing, at least exit the submode - if this was that initial press
				else {
					if (yDisplay == selectedClipPressYDisplay && xDisplay == selectedClipPressXDisplay) {
justEndClipPress:
						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // If in card routine, might mean
							                                                     // it's still loading an Instrument
							                                                     // they selected,
						}

						// check if we just created a clip and whether we changed the clip type before releasing the
						// press
						if (createClip) {
							OutputType thisType = getClipForLayout()->output->type;
							if (thisType != lastTypeCreated) {
								lastTypeCreated = thisType;
							}
							createClip = false;
						}

						// and we don't want the loading animation or anything to get stuck onscreen
						clipPressEnded();
					}
				}
			}

			else if (isUIModeActive(UI_MODE_MIDI_LEARN)) {
				if (clip && clip->type == ClipType::INSTRUMENT) {
					requestRendering(this, 1 << yDisplay, 0);
					goto midiLearnMelodicInstrumentAction;
				}
			}

			// In all other cases, then if also inside card routine, do get it to remind us after. Especially
			// important because it could be that the user has actually pressed down on a pad, that's caused a new
			// clip to be created and preset to load, which is still loading right now, but the uiMode hasn't been
			// set to "holding down" yet and control hasn't been released back to the user, and this is the user
			// releasing their press, so we definitely want to be reminded of this later after the above has
			// happened.
			else {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
			}
		}
	}

	// Or, status or section (aka audition) pads
	else {

		if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
			if (currentUIMode == UI_MODE_NONE) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				playbackHandler.switchToSession();
			}
		}

		else {

			if (clip && clip->isPendingOverdub) {
				if (on && !currentUIMode) {
					goto removePendingOverdub;
				}
			}

			// Status pad
			if (xDisplay == kDisplayWidth) {

				// If Clip is present here
				if (clip) {

					return view_for_session().clipStatusPadAction(clip, on, yDisplay);
				}
			}

			// Section pad
			else if (xDisplay == kDisplayWidth + 1) {

				if (on && Buttons::isButtonPressed(deluge::hid::button::RECORD)
				    && (!currentUIMode || currentUIMode == UI_MODE_VIEWING_RECORD_ARMING)) {
					Buttons::state().recordButtonPressUsedUp = true;
					goto holdingRecord;
				}

				// If Clip is present here
				if (clip) {

					switch (currentUIMode) {
					case UI_MODE_MIDI_LEARN:
						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}
						view_for_session().sectionMidiLearnPadPressed(on, clip->section);
						break;

					case UI_MODE_NONE:
					case UI_MODE_CLIP_PRESSED_IN_SONG_VIEW:
					case UI_MODE_STUTTERING:
						performActionOnPadRelease = false;
						// No break
					case UI_MODE_HOLDING_SECTION_PAD:
						sectionPadAction(yDisplay, on);
						break;
					}
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

bool SessionView::cancel_stale_session_hold() {
	// Combined modes such as stutter need engine-target cleanup before cancellation.
	const bool section_hold = currentUIMode == UI_MODE_HOLDING_SECTION_PAD;
	const bool clip_hold = currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
	const bool grid_gesture = currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid
	                          && currentUIMode == UI_MODE_NONE && gridFirstPadActive();
	if ((!section_hold && !clip_hold && !grid_gesture)
	    || session_hold_revision == deluge::gui::ui_session::navigation.active().structural_refresh.revision()) {
		return false;
	}
	performActionOnPadRelease = false;
	performActionOnSectionPadRelease = false;
	gridResetPresses();
	gridStopSelectedClipPulsing();
	selectedClipYDisplay = 255;
	clipWasSelectedWithShift = false;
	currentUIMode = UI_MODE_NONE;
	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	view_for_session().setActiveModControllableTimelineCounter(currentSong, false);
	uiNeedsRendering(this);
	if (display->haveOLED()) {
		if (section_hold || currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid)
			deluge::hid::display::OLED::removePopup();
		renderUIsForOled();
	}
	else
		redrawNumericDisplay();
	return true;
}

void SessionView::clipPressEnded() {
	// End stuttering since this can also end selection
	if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) && isUIModeActive(UI_MODE_STUTTERING)) {
		((ModControllableAudio*)view_for_session().activeModControllableModelStack.modControllable)
		    ->endStutter((ParamManagerForTimeline*)view_for_session().activeModControllableModelStack.paramManager);
	}

	if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
		exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
		if (display->haveOLED()) {
			deluge::hid::display::OLED::removePopup();
		}
		else {
			redrawNumericDisplay();
		}
	}

	if (currentUIMode == UI_MODE_EXPLODE_ANIMATION) {
		return;
	}
	// needs to be set before setActiveModControllableTimelineCounter so that midi follow mode can get
	// the right model stack with param (otherwise midi follow mode will think you're still in a clip)
	selectedClipYDisplay = 255;
	clipWasSelectedWithShift = false;
	// Cancel copying clip
	if (gridFirstPressedX != -1 && gridFirstPressedY != -1 && gridSecondPressedX != -1 && gridSecondPressedY != -1) {
		display->popupTextTemporary("COPY CANCELED");
	}
	gridResetPresses();

	currentUIMode = UI_MODE_NONE;
	view_for_session().setActiveModControllableTimelineCounter(currentSong);
	if (display->haveOLED()) {
		renderUIsForOled();
		// check UI in case this code is called from performance view
		if (getCurrentUI() == &session_view_for_session()) {
			setCentralLEDStates();
		}
	}
	else {
		redrawNumericDisplay();
	}
}

void SessionView::sectionPadAction(uint8_t y, bool on) {

	Clip* clip = getClipOnScreen(y);

	if (!clip) {
		return;
	}

	if (on) {

		if (isNoUIModeActive()) {
			// If user wanting to change Clip's section
			if (Buttons::isShiftButtonPressed()) {

				// Not allowed if recording arrangement
				if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
					return;
				}

				actionLogger.deleteAllLogs();

				uint8_t oldSection = clip->section;

				clip->section = 255;

				bool sectionUsed[kMaxNumSections];
				memset(sectionUsed, 0, sizeof(sectionUsed));

				for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
					Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

					if (thisClip->section < kMaxNumSections) {
						sectionUsed[thisClip->section] = true;
					}
				}

				// Mark first unused section as available
				for (int32_t i = 0; i < kMaxNumSections; i++) {
					if (!sectionUsed[i]) {
						sectionUsed[i] = true;
						break;
					}
				}

				do {
					oldSection = (oldSection + 1) % kMaxNumSections;
				} while (!sectionUsed[oldSection]);

				clip->section = oldSection;

				// use root UI in case this is called from performanceView
				requestRendering(getRootUI(), 0, 1 << y);
			}

			else {
				enterUIMode(UI_MODE_HOLDING_SECTION_PAD);
				session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
				performActionOnSectionPadRelease = true;
				sectionPressed = clip->section;
				uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 300);
			}
		}
	}

	// Or, triggering actual section play, with de-press
	else {

		if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
			if (!Buttons::isShiftButtonPressed() && performActionOnSectionPadRelease) {
				session.armSection(sectionPressed, kInternalButtonPressLatency);
			}
			exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
			if (display->haveOLED()) {
				deluge::hid::display::OLED::removePopup();
			}
			else {
				redrawNumericDisplay();
			}
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		}

		else if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
			session.armSection(clip->section, kInternalButtonPressLatency);
		}
	}
}

ActionResult SessionView::timerCallback() {
	if (cancel_stale_session_hold()) {
		return ActionResult::DEALT_WITH;
	}
	switch (currentUIMode) {

	case UI_MODE_HOLDING_SECTION_PAD:
		beginEditingSectionRepeatsNum();
		break;

	case UI_MODE_NONE:
		if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			if (currentSong->session_layout_for_session() != SessionLayoutType::SessionLayoutTypeGrid
			    || (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid
			        && gridModeActive == SessionGridModeLaunch)) {
				enterUIMode(UI_MODE_VIEWING_RECORD_ARMING);
				viewingRecordArmingActive = true;
				PadLEDs::reassessGreyout(false);
			}
		}
		break;
	}

	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING || viewingRecordArmingActive) {
		requestRendering(this, 0, 0xFFFFFFFF);
		view_for_session().blinkOn = !view_for_session().blinkOn;
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kFastFlashTime);
	}

	return ActionResult::DEALT_WITH;
}

void SessionView::drawSectionRepeatNumber() {
	int32_t number = currentSong->sections[sectionPressed].numRepetitions;
	char const* outputText;
	if (display->haveOLED()) {
		char buffer[21];
		if (number == -2) {
			outputText = "Launch \nexclusively"; // Need line break to match format of next label.
		}
		else if (number == -1) {
			outputText = "Launch non-\nexclusively"; // Need line break cos line splitter doesn't deal with hyphens.
		}
		else {
			outputText = buffer;
			strcpy(buffer, "Repeats: ");
			if (number == 0) {
				strcpy(&buffer[9], "infinite");
			}
			else {
				intToString(number, &buffer[9]);
			}
		}

		if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
			display->popupText(outputText);
		}
		else {
			display->popupTextTemporary(outputText);
		}
	}
	else {
		char buffer[5];
		if (number == -1) {
			outputText = "SHAR";
		}
		else if (number == 0) {
			outputText = "INFI";
		}
		else {
			intToString(number, buffer);
			outputText = buffer;
		}
		display->setText(outputText, true, 255, true);
	}
}

void SessionView::commandChangeSectionRepeats(int8_t offset) {
	if (performActionOnSectionPadRelease) {
		beginEditingSectionRepeatsNum();
	}
	else {
		int16_t* numRepetitions = &currentSong->sections[sectionPressed].numRepetitions;
		*numRepetitions += offset;
		if (*numRepetitions > 9999) {
			*numRepetitions = 9999;
		}
		else if (*numRepetitions < -2) {
			*numRepetitions = -2;
		}
		drawSectionRepeatNumber();
	}
}

void SessionView::commandChangeClipPreset(int8_t offset) {
	performActionOnPadRelease = false;

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
		return;
	}

	Clip* clip = getClipForLayout();

	if (clip == nullptr) {
		return;
	}

	if (clip->type == ClipType::INSTRUMENT) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

		switch (currentSong->session_layout_for_session()) {
		case SessionLayoutType::SessionLayoutTypeRows: {
			view_for_session().navigateThroughPresetsForInstrumentClip(offset, modelStack, true);
			break;
		}
		case SessionLayoutType::SessionLayoutTypeGrid: {
			Output* oldOutput = clip->output;
			Output* newOutput = currentSong->navigateThroughPresetsForInstrument(oldOutput, offset);
			if (oldOutput != newOutput) {
				view_for_session().setActiveModControllableTimelineCounter(newOutput->getActiveClip());
				requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
			}
			break;
		}
		// explicit fallthrough cases
		case SessionLayoutType::SessionLayoutTypeMaxElement:;
		}
	}
	else {
		auto ao = (AudioOutput*)clip->output;
		ao->scrollAudioOutputMode(offset);
	}
}

void SessionView::commandChangeCurrentSectionRepeats(int8_t offset) {
	if (session.hasPlaybackActive()) {
		if (session.launchEventAtSwungTickCount) {
			editNumRepeatsTilLaunch(offset);
		}
		else if (offset > 0) {
			session.userWantsToArmNextSection(1);
		}
	}
}

void SessionView::commandChangeLayout(int8_t offset) {
	return selectLayout(offset);
}

void SessionView::selectEncoderAction(int8_t offset) {
	if (cancel_stale_session_hold()) {
		return;
	}
	switch (currentUIMode) {
	case UI_MODE_HOLDING_SECTION_PAD:
		return commandChangeSectionRepeats(offset);
	case UI_MODE_CLIP_PRESSED_IN_SONG_VIEW:
		return commandChangeClipPreset(offset);
	case UI_MODE_NONE:
		if (sessionButtonActive) {
			// TODO: this "held button consumed so no action on release" -logic really needs
			// to be abstracted somehow. Maybe something like Button::consumeButtonPress() which
			// removes it from buttonState[] and causes the button-up not to trigger an action?
			sessionButtonUsed = true;
			return commandChangeLayout(offset);
		}
		else {
			return commandChangeCurrentSectionRepeats(offset);
		}
	}
}

void SessionView::editNumRepeatsTilLaunch(int32_t offset) {
	session.numRepeatsTilLaunch += offset;
	if (session.numRepeatsTilLaunch < 1) {
		session.numRepeatsTilLaunch = 1;
	}
	else if (session.numRepeatsTilLaunch > 9999) {
		session.numRepeatsTilLaunch = 9999;
	}
	else {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			redrawNumericDisplay();
		}
	}
}

ActionResult SessionView::horizontalEncoderAction(int32_t offset) {
	if (cancel_stale_session_hold()) {
		return ActionResult::DEALT_WITH;
	}
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridHandleScroll(offset, 0);
	}

	// So long as we're not in a submode...
	if (isNoUIModeActive()) {

		// Or, if the shift key is pressed
		if (Buttons::isShiftButtonPressed()) {
			// Tell the user why they can't resize
			indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW);
			return ActionResult::DEALT_WITH;
		}
	}

	return ClipNavigationTimelineView::horizontalEncoderAction(offset);
}

ActionResult SessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (cancel_stale_session_hold()) {
		return ActionResult::DEALT_WITH;
	}

	if (currentUIMode == UI_MODE_NONE && Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
		currentSong->commandTranspose(offset);
	}
	else if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	         || currentUIMode == UI_MODE_VIEWING_RECORD_ARMING
	         || getCurrentUI() == &deluge::gui::context_menu::midi_learn_mode_for_session()) {

		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
		}

		// Change row colour by pressing row & shift - same shortcut as in clip view.
		if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
		    && (Buttons::isShiftButtonPressed() || clipWasSelectedWithShift)) {

			Clip* clip = getClipOnScreen(selectedClipYDisplay);
			if (!clip)
				return ActionResult::NOT_DEALT_WITH;

			clip->colourOffset += offset;

			requestRendering(this, 1 << selectedClipYDisplay, 0);

			return ActionResult::DEALT_WITH;
		}

		if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
			// For safety, is used in verticalScrollOneSquare on clip copy
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			return gridHandleScroll(0, offset);
		}

		return verticalScrollOneSquare(offset);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SessionView::verticalScrollOneSquare(int32_t direction) {

	if (direction == 1) {
		if (currentSong->song_view_y_scroll_for_session() >= currentSong->sessionClips.getNumElements() - 1) {
			return ActionResult::DEALT_WITH;
		}
	}
	else {
		if (currentSong->song_view_y_scroll_for_session() <= 1 - kDisplayHeight) {
			return ActionResult::DEALT_WITH;
		}
	}

	// Drag Clip along with scroll if one is selected
	if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {

		performActionOnPadRelease = false;

		// Not allowed if recording arrangement
		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return ActionResult::DEALT_WITH;
		}

		int32_t oldIndex = selectedClipYDisplay + currentSong->song_view_y_scroll_for_session();

		if (direction == 1) {
			if (oldIndex >= currentSong->sessionClips.getNumElements() - 1) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (oldIndex <= 0) {
				return ActionResult::DEALT_WITH;
			}
		}

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		actionLogger.deleteAllLogs();

		int32_t newIndex = oldIndex + direction;
		currentSong->sessionClips.swapElements(newIndex, oldIndex);
		currentSong->notify_peer_clips_swapped(newIndex, oldIndex);
	}

	currentSong->song_view_y_scroll_for_session() += direction;
	redrawClipsOnScreen();

	if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
		PadLEDs::reassessGreyout(true);
	}

	return ActionResult::DEALT_WITH;
}

bool SessionView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (view_for_session().potentiallyRenderVUMeter(image)) {
		return true;
	}

	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridRenderSidebar(whichRows, image, occupancyMask);
	}

	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (whichRows & (1 << i)) {
			drawStatusSquare(i, image[i]);
			drawSectionSquare(i, image[i]);
		}
	}

	return true;
}

void SessionView::drawStatusSquare(uint8_t yDisplay, RGB thisImage[]) {
	RGB& thisColour = thisImage[kDisplayWidth];

	Clip* clip = getClipOnScreen(yDisplay);

	// If no Clip, black
	if (!clip) {
		thisColour = colours::black;
	}
	else {
		thisColour = view_for_session().getClipMuteSquareColour(clip, thisColour);
	}
}

void SessionView::drawSectionSquare(uint8_t yDisplay, RGB thisImage[]) {
	RGB& thisColour = thisImage[kDisplayWidth + 1];

	Clip* clip = getClipOnScreen(yDisplay);

	// If no Clip, black
	if (!clip) {
		thisColour = colours::black;
	}
	else {
		if (view_for_session().midiLearnFlashOn
		    && currentSong->sections[clip->section].launchMIDICommand.containsSomething()) {
			thisColour = colours::midi_command;
		}

		else {
			thisColour = defaultClipSectionColours[clip->section];

			// If user assigning MIDI controls and has this section selected, flash to half brightness
			if (view_for_session().midiLearnFlashOn && currentSong
			    && view_for_session().learnedThing == &currentSong->sections[clip->section].launchMIDICommand) {
				thisColour = thisColour.dim();
			}
		}
	}
}

// Will now look in subfolders too if need be.
Error setPresetOrNextUnlaunchedOne(InstrumentClip* clip, OutputType outputType, bool* instrumentAlreadyInSong,
                                   bool copyDrumsFromClip = true) {
	Error error = Browser::current_dir_for_session().set(getInstrumentFolder(outputType));
	if (error != Error::NONE) {
		return error;
	}

	FileItem* fileItem =
	    D_TRY_CATCH(load_instrument_preset_ui_for_session().findAnUnlaunchedPresetIncludingWithinSubfolders(
	                    currentSong, outputType, Availability::INSTRUMENT_UNUSED),
	                error, { return error; });

	Instrument* newInstrument = fileItem->instrument;
	bool isHibernating = newInstrument && !fileItem->instrumentAlreadyInSong;
	*instrumentAlreadyInSong = newInstrument && fileItem->instrumentAlreadyInSong;

	if (!newInstrument) {
		String newPresetName;
		fileItem->getFilenameWithoutExtension(&newPresetName);
		error = StorageManager::loadInstrumentFromFile(currentSong, nullptr, outputType, false, &newInstrument,
		                                               &fileItem->filePointer, &newPresetName,
		                                               &Browser::current_dir_for_session());
	}

	Browser::emptyFileItems();

	if (error != Error::NONE) {
		return error;
	}

	if (isHibernating) {
		currentSong->removeInstrumentFromHibernationList(newInstrument);
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::displayWorkingAnimation("Loading");
	}
	else {
		display->displayLoadingAnimation();
	}

	newInstrument->loadAllAudioFiles(true);

	display->removeWorkingAnimation();

	if (copyDrumsFromClip) {
		error = clip->setAudioInstrument(newInstrument, currentSong, true, nullptr); // Does a setupPatching()
		if (error != Error::NONE) {
			// setAudioInstrument failed (most likely INSUFFICIENT_RAM while loading the kit's samples). Release the
			// Instrument we just brought in so it doesn't leak - delete it, or return it to the hibernation list if
			// it's been edited. One that was already an active Output in the song is left untouched.
			if (!*instrumentAlreadyInSong) {
				currentSong->deleteOrAddToHibernationListOutput(newInstrument);
			}
			return error;
		}

		if (outputType == OutputType::KIT) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);

			clip->setupAsNewKitClipIfNecessary(modelStack);
		}
	}
	else {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);
		Error error = clip->changeInstrument(modelStack, newInstrument, nullptr, InstrumentRemoval::NONE);
		if (error != Error::NONE) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_SWITCHING_TO_TRACK_FAILED));
		}

		if (newInstrument->type == OutputType::KIT) {
			clip->y_scroll_for_session() = 0;
		}
	}

	return Error::NONE;
}

constexpr float colourStep = 22.5882352941;
static float lastColour = 192 - colourStep + 1;

Clip* SessionView::createNewClip(OutputType outputType, int32_t yDisplay) {
	Clip* clip = nullptr;
	switch (outputType) {
	case OutputType::AUDIO:
		if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
			clip = gridCreateAudioClipWithNewTrack();
		}
		else {
			clip = createNewAudioClip(yDisplay);
		}
		break;
	default:
		if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
			clip = gridCreateInstrumentClipWithNewTrack(outputType);
		}
		else {
			clip = createNewInstrumentClip(outputType, yDisplay);
		}
		break;
	}

	return clip;
}

Clip* SessionView::createNewAudioClip(int32_t yDisplay) {
	actionLogger.deleteAllLogs();

	// Allocate memory for audio clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
	if (clipMemory == nullptr) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	// Create the audio clip and ParamManager
	AudioClip* newClip = new (clipMemory) AudioClip();

	// suss output
	if (!createNewTrackForAudioClip(newClip)) {
		newClip->~AudioClip();
		delugeDealloc(clipMemory);
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	// Give the new clip its stuff
	setupNewClip(newClip);

	// Insert and Resync New Clip
	if (!insertAndResyncNewClip(newClip, yDisplay)) {
		newClip->~AudioClip();
		delugeDealloc(clipMemory);
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	return newClip;
}

Clip* SessionView::createNewInstrumentClip(OutputType outputType, int32_t yDisplay) {
	actionLogger.deleteAllLogs();

	// Allocate memory for instrument clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (clipMemory == nullptr) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	// create the instrument clip and param manager
	InstrumentClip* newClip = new (clipMemory) InstrumentClip(currentSong);

	// suss output
	if (!createNewTrackForInstrumentClip(outputType, newClip, true)) {
		newClip->~InstrumentClip();
		delugeDealloc(clipMemory);
		return nullptr;
	}

	// Give the new clip its stuff
	setupNewClip(newClip);

	// Insert and Resync New Clip
	if (!insertAndResyncNewClip(newClip, yDisplay)) {
		newClip->~InstrumentClip();
		delugeDealloc(clipMemory);
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	return newClip;
}

bool SessionView::insertAndResyncNewClip(Clip* newClip, int32_t yDisplay) {
	// insert clip at index
	int32_t index = yDisplay + currentSong->song_view_y_scroll_for_session();
	if (index <= 0) {
		index = 0;
		newClip->section = currentSong->sessionClips.getClipAtIndex(0)->section;
		currentSong->song_view_y_scroll_for_session()++;
	}
	else if (index >= currentSong->sessionClips.getNumElements()) {
		index = currentSong->sessionClips.getNumElements();
		newClip->section =
		    currentSong->sessionClips.getClipAtIndex(currentSong->sessionClips.getNumElements() - 1)->section;
	}
	if (currentSong->sessionClips.insertClipAtIndex(newClip, index) != Error::NONE) {
		return false;
	}

	currentSong->notify_peer_clip_inserted(index);

	// resync new clip play pos
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newClip);

	resyncNewClip(newClip, modelStackWithTimelineCounter);

	return true;
}

void SessionView::resyncNewClip(Clip* newClip, ModelStackWithTimelineCounter* modelStackWithTimelineCounter) {
	// Figure out the play pos for the new Clip if we're currently playing
	if (session.hasPlaybackActive() && playbackHandler.isEitherClockActive() && currentSong->isClipActive(newClip)) {
		session.reSyncClip(modelStackWithTimelineCounter, true);
	}
}

void SessionView::replaceInstrumentClipWithAudioClip(Clip* clip) {
	if (!clip || clip->type != ClipType::INSTRUMENT) {
		return;
	}

	// don't convert a track to audio if it has clip instances in arranger or more than one clip in session view
	if (clip->output->clipHasInstance(clip) || currentSong->getClipWithOutput(clip->output, false, clip)) {
		display->displayPopup(deluge::l10n::get(
		    deluge::l10n::String::STRING_FOR_INSTRUMENTS_WITH_CLIPS_CANT_BE_TURNED_INTO_AUDIO_TRACKS));
		return;
	}

	// don't allow clip type change if clip is not empty
	InstrumentClip* instrumentClip = (InstrumentClip*)clip;
	if (!instrumentClip->isEmpty()) {
		return;
	}

	int32_t clipIndex = currentSong->sessionClips.getIndexForClip(clip);
	Clip* newClip = currentSong->replaceInstrumentClipWithAudioClip(clip, clipIndex);

	if (!newClip) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}

	currentSong
	    ->arrangement_y_scroll_for_session()--; // Is our best bet to avoid the scroll appearing to change visually

	view_for_session().setActiveModControllableTimelineCounter(newClip);
	view_for_session().displayOutputName(newClip->output, true, newClip);

	// If Clip was in keyboard view, need to redraw that
	requestRendering(this, 1 << selectedClipYDisplay, 1 << selectedClipYDisplay);
}

void SessionView::removeClip(Clip* clip) {
	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager(
	    "PM17", "PM18"); // was E373 / H373. Trying to narrow down PM16 (was H067) that Leo got, below.

	if (!clip) {
		return;
	}

	int32_t clipIndex = currentSong->sessionClips.getIndexForClip(clip);

	// If last session Clip left, just don't allow. Easiest
	if (currentSong->sessionClips.getNumElements() == 1) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_REMOVE_FINAL_CLIP));
		return;
	}

	// If this Clip is the inputTickScaleClip
	if (clip == currentSong->getSyncScalingClip()) {
		// Don't let the user do it
		indicator_leds::indicateAlertOnLed(IndicatorLED::SYNC_SCALING);
		return;
	}

	clip->stopAllNotesPlaying(currentSong); // Stops any MIDI-controlled auditioning / stuck notes

	midiFollow.removeClip(clip);
	currentSong->removeSessionClip(clip, clipIndex);

	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == &session) {
		session.launchSchedulingMightNeedCancelling();
	}

	redrawClipsOnScreen();

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("PM15",
	                                                                 "PM16"); // was E067 / H067. Leo got a H067!!!!
}

Clip* SessionView::getClipOnScreen(int32_t yDisplay) {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		if (gridFirstPadActive()) {
			return gridClipFromCoords(gridFirstPressedX, gridFirstPressedY);
		}

		return nullptr;
	}

	int32_t index = yDisplay + currentSong->song_view_y_scroll_for_session();

	if (index < 0 || index >= currentSong->sessionClips.getNumElements()) {
		return nullptr;
	}

	return currentSong->sessionClips.getClipAtIndex(index);
}

void SessionView::redrawClipsOnScreen(bool doRender) {
	if (doRender) {
		// use root UI in case this is called from performance view
		requestRendering(getRootUI());
	}
	view_for_session().flashPlayEnable();
}

void SessionView::setLedStates() {

	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	view_for_session().setLedStates();

#ifdef currentClipStatusButtonX
	view_for_session().switchOffCurrentClipPad();
#endif
}

extern char loopsRemainingText[];

void SessionView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (stemExport.processStarted) {
		stemExport.displayStemExportProgressOLED(StemExportType::CLIP);
		return;
	}

	UI* currentUI = getCurrentUI();
	if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		view_for_session().displayOutputName(getCurrentClip()->output, true, getCurrentClip());
	}
	else if (currentUI != &performance_view_for_session()) {
		renderViewDisplay();
	}

	if (playbackHandler.isEitherClockActive()) {
		// Session playback
		if (currentPlaybackMode == &session) {
			if (session.launchEventAtSwungTickCount) {
				intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
				deluge::hid::display::OLED::clearMainImage();
				deluge::hid::display::OLED::drawPermanentPopupLookingText(loopsRemainingText);
			}
		}

		else { // Arrangement playback
			if (playbackHandler.stopOutputRecordingAtLoopEnd) {
				deluge::hid::display::OLED::clearMainImage();
				deluge::hid::display::OLED::drawPermanentPopupLookingText("Resampling will end...");
			}
		}
	}
}

void SessionView::redrawNumericDisplay() {
	if ((currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW || stemExport.processStarted)) {
		return;
	}

	UI* currentUI = getCurrentUI();

	bool isPerformanceView = (currentUI == &performance_view_for_session());

	bool isSessionView =
	    ((currentUI == &session_view_for_session())
	     || (isPerformanceView && currentSong->last_clip_instance_entered_start_pos_for_session() == -1));

	bool isArrangerView =
	    ((currentUI == &arranger_view_for_session())
	     || (isPerformanceView && currentSong->last_clip_instance_entered_start_pos_for_session() != -1));

	// If playback on...
	if (playbackHandler.isEitherClockActive()) {

		// Session playback
		if (currentPlaybackMode == &session) {
			if (!session.launchEventAtSwungTickCount) {
				goto nothingToDisplay;
			}

			if (load_song_ui_for_session().isLoadingSong()) {
				if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
					displayRepeatsTilLaunch();
				}
			}

			else if (isArrangerView) {
				if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW
				    || currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
					if (session.switchToArrangementAtLaunchEvent) {
						displayRepeatsTilLaunch();
					}
					else {
						clearNumericDisplay();
					}
				}
			}

			else if (isSessionView) {
				if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
					displayRepeatsTilLaunch();
				}
			}
		}

		else { // Arrangement playback
			if (isArrangerView) {

				if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) {
					if (playbackHandler.stopOutputRecordingAtLoopEnd) {
						display->setText("1", true, 255, true, NULL, false, true);
					}
					else {
						clearNumericDisplay();
					}
				}
			}
			else if (isSessionView) {
				clearNumericDisplay();
			}
		}
	}

	// Or if no playback active...
	else {
nothingToDisplay:
		if ((isSessionView || isArrangerView)) {
			if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
				clearNumericDisplay();
			}
		}
	}

	// don't override LED states set by performance view
	if (!isPerformanceView) {
		setCentralLEDStates();
	}
}

void SessionView::clearNumericDisplay() {
	if (getCurrentUI() == &performance_view_for_session()) {
		performance_view_for_session().renderViewDisplay();
	}
	else {
		display->setText("");
	}
}

void SessionView::displayRepeatsTilLaunch() {
	char buffer[5];
	intToString(session.numRepeatsTilLaunch, buffer);
	display->setText(buffer, true, 255, true, NULL, false, true);
}

/// render session view display on opening
void SessionView::renderViewDisplay() {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main_for_session();
	hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

	DEF_STACK_STRING_BUF(tempoBPM, 10);
	lastDisplayedTempo = playbackHandler.calculateBPM(playbackHandler.getTimePerInternalTickFloat());
	playbackHandler.getTempoStringForOLED(lastDisplayedTempo, tempoBPM);
	displayTempoBPM(canvas, tempoBPM, false);

#if OLED_MAIN_HEIGHT_PIXELS == 64
	yPos = OLED_MAIN_TOPMOST_PIXEL + 30;
#else
	yPos = OLED_MAIN_TOPMOST_PIXEL + 17;
#endif

	char const* name;
	if (currentSong->name.isEmpty()) {
		name = "UNSAVED";
	}
	else {
		name = currentSong->name.get();
	}

	int32_t stringLengthPixels = canvas.getStringWidthInPixels(name, kTextTitleSizeY);

	if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
		canvas.drawStringCentred(name, yPos, kTextTitleSpacingX, kTextTitleSizeY);
	}
	else {
		canvas.drawString(name, 0, yPos, kTextTitleSpacingX, kTextTitleSizeY);
		deluge::hid::display::OLED::setupSideScroller(0, name, 0, OLED_MAIN_WIDTH_PIXELS, yPos, yPos + kTextTitleSizeY,
		                                              kTextTitleSpacingX, kTextTitleSizeY, false);
	}

	yPos = OLED_MAIN_TOPMOST_PIXEL + 32;

	DEF_STACK_STRING_BUF(rootNoteAndScaleName, 40);
	currentSong->getCurrentRootNoteAndScaleName(rootNoteAndScaleName);
	displayCurrentRootNoteAndScaleName(canvas, rootNoteAndScaleName, false);

	deluge::hid::display::OLED::markChanged();
}

void SessionView::displayTempoBPM(deluge::hid::display::oled_canvas::Canvas& canvas, StringBuf& tempoBPM,
                                  bool clearArea) {
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;

	int32_t metronomeIconSpacingX = 7 + 3;

	if (clearArea) {
		canvas.clearAreaExact(OLED_MAIN_WIDTH_PIXELS - (kTextSpacingX * 6) - metronomeIconSpacingX,
		                      OLED_MAIN_TOPMOST_PIXEL, OLED_MAIN_WIDTH_PIXELS - 1, yPos + kTextSpacingY);
	}

	canvas.drawStringAlignRight(tempoBPM.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	int32_t stringLength = tempoBPM.size();
	int32_t metronomeIconStartX = OLED_MAIN_WIDTH_PIXELS - (kTextSpacingX * stringLength) - metronomeIconSpacingX;
	canvas.drawGraphicMultiLine(deluge::hid::display::OLED::metronomeIcon, metronomeIconStartX, yPos, 7);
}

void SessionView::displayCurrentRootNoteAndScaleName(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                     StringBuf& rootNoteAndScaleName, bool clearArea) {

	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 32;

	if (clearArea) {
		canvas.clearAreaExact(0, yPos, OLED_MAIN_WIDTH_PIXELS - 1, yPos + kTextSpacingY);
	}

	canvas.drawString(rootNoteAndScaleName.c_str(), 0, yPos, kTextSpacingX, kTextSpacingY);
}

// This gets called by redrawNumericDisplay() - or, if OLED, it gets called instead, because this still needs to
// happen.
void SessionView::setCentralLEDStates() {
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

uint32_t SessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

void SessionView::cloneClip(uint8_t yDisplayFrom, uint8_t yDisplayTo) {
	Clip* clipToClone = getClipOnScreen(yDisplayFrom);
	if (!clipToClone) {
		return;
	}

	// Just don't allow cloning of Clips which are linearly recording
	if (clipToClone->getCurrentlyRecordingLinearly()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_IN_PROGRESS));
		return;
	}

	bool enoughSpace = currentSong->sessionClips.ensureEnoughSpaceAllocated(1);
	if (!enoughSpace) {
ramError:
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clipToClone);

	Error error = clipToClone->clone(modelStack);
	if (error != Error::NONE) {
		goto ramError;
	}

	Clip* newClip = (Clip*)modelStack->getTimelineCounter();

	newClip->section = (uint8_t)(newClip->section + 1) % kMaxNumSections;
	copyClipName(clipToClone, newClip, clipToClone->output);

	int32_t newIndex = yDisplayTo + currentSong->song_view_y_scroll_for_session();

	if (yDisplayTo < yDisplayFrom) {
		currentSong->song_view_y_scroll_for_session()++;
		newIndex++;
	}

	if (newIndex < 0) {
		newIndex = 0;
	}
	else if (newIndex > currentSong->sessionClips.getNumElements()) {
		newIndex = currentSong->sessionClips.getNumElements();
	}

	currentSong->sessionClips.insertClipAtIndex(newClip,
	                                            newIndex); // Can't fail - we ensured enough space in advance
	currentSong->notify_peer_clip_inserted(newIndex);

	redrawClipsOnScreen();
}

void SessionView::graphicsRoutine() {
	potentiallyUpdateCompressorLEDs();

	if (view_for_session().potentiallyRenderVUMeter(PadLEDs::image_for_session())) {
		PadLEDs::sendOutSidebarColours();
	}

	if (display->haveOLED()) {
		displayPotentialTempoChange(this);
	}

	bool reallyNoTickSquare = (!playbackHandler.isEitherClockActive() || currentUIMode == UI_MODE_EXPLODE_ANIMATION
	                           || currentUIMode == UI_MODE_IMPLODE_ANIMATION || !session.launchEventAtSwungTickCount);

	int32_t sixteenthNotesRemaining = 0;

	// display bars / notes remaining until launch event
	if (!reallyNoTickSquare) {
		sixteenthNotesRemaining = displayLoopsRemainingPopup();
	}

	// in grid view, the only playhead we potentially render a playhead that displays
	// when the next clip launch event is expected occur (e.g. when clips will start or end)
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		potentiallyRenderClipLaunchPlayhead(reallyNoTickSquare, sixteenthNotesRemaining);

		return;
	}

	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];
	int32_t newTickSquare;

	bool anyLinearRecordingOnThisScreen = false;
	bool anyLinearRecordingOnNextScreen = false;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t newTickSquare;

		Clip* clip = getClipOnScreen(yDisplay);

		if (!playbackHandler.playbackState || !clip || !currentSong->isClipActive(clip)
		    || playbackHandler.ticksLeftInCountIn || currentUIMode == UI_MODE_HORIZONTAL_ZOOM
		    || (currentUIMode == UI_MODE_HORIZONTAL_SCROLL
		        && PadLEDs::transition_taking_place_on_row_for_session()[yDisplay])) {
			newTickSquare = 255;
		}

		// Tempoless recording
		else if (!playbackHandler.isEitherClockActive()) {
			newTickSquare = kDisplayWidth - 1;

			if (clip->getCurrentlyRecordingLinearly()) { // This would have to be true if we got here, I think?
				if (clip->type == ClipType::AUDIO) {
					((AudioClip*)clip)->renderData.xScroll = -1; // Make sure values are recalculated

					rowNeedsRenderingDependingOnSubMode(yDisplay);
				}
				colours[yDisplay] = 2;
			}
		}
		else {
			int32_t localScroll = getClipLocalScroll(clip, currentSong->x_scroll_for_session()[NAVIGATION_CLIP],
			                                         currentSong->x_zoom_for_session()[NAVIGATION_CLIP]);
			Clip* clipToRecordTo = clip->getClipToRecordTo();
			int32_t livePos = clipToRecordTo->getLivePos();

			// If we are recording to another Clip, we have to use its position.
			if (clipToRecordTo != clip) {
				int32_t whichRepeat = (uint32_t)livePos / (uint32_t)clip->loopLength;
				livePos -= whichRepeat * clip->loopLength;

				// But if it's currently reversing, we have to re-apply that here.
				if (clip->sequenceDirectionMode == SequenceDirection::REVERSE
				    || (clip->sequenceDirectionMode == SequenceDirection::PINGPONG && (whichRepeat & 1))) {
					livePos = -livePos;
					if (livePos < 0) {
						livePos += clip->loopLength;
					}
				}
			}

			newTickSquare = getSquareFromPos(livePos, nullptr, localScroll);

			// Linearly recording
			if (clip->getCurrentlyRecordingLinearly()) {
				if (clip->type == ClipType::AUDIO) {
					if (currentUIMode != UI_MODE_HORIZONTAL_SCROLL && currentUIMode != UI_MODE_HORIZONTAL_ZOOM) {
						rowNeedsRenderingDependingOnSubMode(yDisplay);
					}
				}

				if (newTickSquare >= 0
				    && (clip->armState == ArmState::OFF
				        || xScrollBeforeFollowingAutoExtendingLinearRecording
				               != -1)) { // Only if it's auto extending, or it was before
					if (newTickSquare < kDisplayWidth) {
						anyLinearRecordingOnThisScreen = true;
					}
					else if (newTickSquare == kDisplayWidth) {
						anyLinearRecordingOnNextScreen = true;
					}
				}

				colours[yDisplay] = 2;
			}

			// Not linearly recording
			else {
				colours[yDisplay] = 0;
			}

			if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}

		tickSquares[yDisplay] = newTickSquare;
	}

	// Auto scrolling for linear recording --------

	// If no linear recording onscreen now...
	if (!anyLinearRecordingOnThisScreen && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {

		// If there's some on the next screen to the right, go there
		if (anyLinearRecordingOnNextScreen) {

			if (!currentUIMode && getCurrentUI() == this) {

				if (xScrollBeforeFollowingAutoExtendingLinearRecording == -1) {
					xScrollBeforeFollowingAutoExtendingLinearRecording =
					    currentSong->x_scroll_for_session()[NAVIGATION_CLIP];
				}

				int32_t newXScroll = currentSong->x_scroll_for_session()[NAVIGATION_CLIP]
				                     + currentSong->x_zoom_for_session()[NAVIGATION_CLIP] * kDisplayWidth;
				horizontalScrollForLinearRecording(newXScroll);
			}
		}

		// Or if not, cancel following scrolling along, and go back to where we started
		else {
			if (xScrollBeforeFollowingAutoExtendingLinearRecording != -1) {
				int32_t newXScroll = xScrollBeforeFollowingAutoExtendingLinearRecording;
				xScrollBeforeFollowingAutoExtendingLinearRecording = -1;

				if (newXScroll != currentSong->x_zoom_for_session()[NAVIGATION_CLIP]) {
					horizontalScrollForLinearRecording(newXScroll);
				}
			}
		}
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

void SessionView::potentiallyUpdateCompressorLEDs() {
	static deluge::gui::ui_session::State<int> counters;
	auto& counter = counters.active();
	if (currentUIMode == UI_MODE_NONE) {
		int32_t modKnobMode = -1;
		bool editingComp = false;
		if (view_for_session().activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer =
			    view_for_session().activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;
				editingComp = view_for_session().activeModControllableModelStack.modControllable->isEditingComp();
			}
		}
		if (modKnobMode == 4 && editingComp) { // upper
			counter = (counter + 1) % 5;
			if (counter == 0) {
				uint8_t gr = currentSong->globalEffectable.compressor.gainReduction;

				indicator_leds::setMeterLevel(1, gr); // Gain Reduction LED
			}
		}
	}
}

// checks if tempo has changed since it was last rendered on the display and updates it if required
void SessionView::displayPotentialTempoChange(UI* ui) {
	// check UI in case graphics routine is called while we're in another UI (e.g. menu)
	if (getCurrentUI() == ui) {
		float tempo = playbackHandler.calculateBPMForDisplay();
		float diff = std::abs(tempo - lastDisplayedTempo);
		// always catch manual adjustments, limit rate of others
		if (diff > 0.5) {
			DEF_STACK_STRING_BUF(tempoBPM, 10);
			playbackHandler.getTempoStringForOLED(tempo, tempoBPM);
			displayTempoBPM(deluge::hid::display::OLED::main_for_session(), tempoBPM, true);
			deluge::hid::display::OLED::markChanged();
			lastDisplayedTempo = tempo;
		}
	}
}

/// display number of bars or quarter notes remaining until a launch event
int32_t SessionView::displayLoopsRemainingPopup(bool ephemeral) {
	int32_t sixteenthNotesRemaining = session.getNumSixteenthNotesRemainingTilLaunch();
	// only show pop-up if you're not in any other UI mode
	if (currentUIMode == UI_MODE_NONE) {
		if (sixteenthNotesRemaining > 0) {
			DEF_STACK_STRING_BUF(popupMsg, 40);
			if (sixteenthNotesRemaining > 16) {
				int32_t barsRemaining = ((sixteenthNotesRemaining - 1) / 16) + 1;
				if (display->haveOLED()) {
					popupMsg.append("Bars Remaining: ");
				}
				popupMsg.appendInt(barsRemaining);
			}
			else {
				int32_t quarterNotesRemaining = ((sixteenthNotesRemaining - 1) / 4) + 1;
				if (display->haveOLED()) {
					popupMsg.append("Beats Remaining: ");
				}
				popupMsg.appendInt(quarterNotesRemaining);
			}
			if (display->haveOLED() && !ephemeral) {
				deluge::hid::display::OLED::clearMainImage();
				deluge::hid::display::OLED::drawPermanentPopupLookingText(popupMsg.c_str());
				deluge::hid::display::OLED::sendMainImage();
			}
			else {
				display->displayPopup(popupMsg.c_str(), 1, true);
			}
		}
	}
	return sixteenthNotesRemaining;
}

uint8_t launchTickSquares[kDisplayHeight] = {255, 255, 255, 255, 255, 255, 255, 255};
const uint8_t launchTickColours[kDisplayHeight] = {0, 0, 0, 0, 0, 0, 0, 0};

// potentially render a playhead in top row of main grid that displays
// when the next clip launch event is expected occur (e.g. when clips will start or end)
void SessionView::potentiallyRenderClipLaunchPlayhead(bool reallyNoTickSquare, int32_t sixteenthNotesRemaining) {
	int32_t newTickSquare;
	const uint8_t* colours = launchTickColours;

	bool renderPlayhead = !reallyNoTickSquare
	                      && runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableLaunchEventPlayhead)
	                             == RuntimeFeatureStateToggle::On;

	// render last 16 notes
	if (renderPlayhead && (sixteenthNotesRemaining > 0 && sixteenthNotesRemaining <= kDisplayWidth)) {
		newTickSquare = kDisplayWidth - sixteenthNotesRemaining;

		launchTickSquares[kDisplayHeight - 1] = newTickSquare;
	}
	// don't render playhead
	else {
		launchTickSquares[kDisplayHeight - 1] = 255;
	}

	PadLEDs::setTickSquares(launchTickSquares, launchTickColours);
}

void SessionView::requestRendering(UI* ui, uint32_t whichMainRows, uint32_t whichSideRows) {
	if (ui == &performance_view_for_session()) {
		// don't re-render main pads in performance view
		uiNeedsRendering(ui, 0, whichSideRows);
	}
	else if (ui == &session_view_for_session()) {
		if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
			// Just redrawing should be faster than evaluating every cell in every row
			uiNeedsRendering(ui, 0xFFFFFFFF, 0xFFFFFFFF);
		}
		else {
			uiNeedsRendering(ui, whichMainRows, whichSideRows);
		}
	}
}

void SessionView::rowNeedsRenderingDependingOnSubMode(int32_t yDisplay) {

	switch (currentUIMode) {
	case UI_MODE_HORIZONTAL_SCROLL:
	case UI_MODE_HORIZONTAL_ZOOM:
	case UI_MODE_AUDIO_CLIP_EXPANDING:
	case UI_MODE_AUDIO_CLIP_COLLAPSING:
	case UI_MODE_INSTRUMENT_CLIP_EXPANDING:
	case UI_MODE_INSTRUMENT_CLIP_COLLAPSING:
	case UI_MODE_ANIMATION_FADE:
	case UI_MODE_EXPLODE_ANIMATION:
	case UI_MODE_IMPLODE_ANIMATION:
		break;

	default:
		requestRendering(this, 1 << yDisplay, 0);
	}
}

bool SessionView::calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom) {

	bool anyToDo = false;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		Clip* clip = getClipOnScreen(yDisplay);

		if (clip && clip->currentlyScrollableAndZoomable()) {
			int32_t oldLocal = getClipLocalScroll(clip, oldScroll, oldZoom);
			int32_t newLocal = getClipLocalScroll(clip, newScroll, newZoom);

			PadLEDs::zoom_pin_square_for_session()[yDisplay] =
			    ((int64_t)(int32_t)(oldLocal - newLocal) << 16) / (int32_t)(newZoom - oldZoom);
			PadLEDs::transition_taking_place_on_row_for_session()[yDisplay] = true;
			anyToDo = true;
		}
		else {
			PadLEDs::transition_taking_place_on_row_for_session()[yDisplay] = false;
		}
	}

	return anyToDo;
}

int32_t SessionView::getClipPlaceOnScreen(Clip* clip) {
	return currentSong->sessionClips.getIndexForClip(clip) - currentSong->song_view_y_scroll_for_session();
}

uint32_t SessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

bool SessionView::setupScroll(uint32_t oldScroll) {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		return false;
	}
	// Ok I'm sorta pretending that this is definitely previously false, though only one caller of this function
	// actually checks for that. Should be ok-ish though...
	deluge::gui::ui_session::navigation.active().rendering = true;

	uint32_t xZoom = currentSong->x_zoom_for_session()[NAVIGATION_CLIP];

	bool anyMoved = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		Clip* clip = getClipOnScreen(yDisplay);

		if (clip && clip->currentlyScrollableAndZoomable()) {

			uint32_t newLocalPos =
			    getClipLocalScroll(clip, currentSong->x_scroll_for_session()[NAVIGATION_CLIP], xZoom);
			uint32_t oldLocalPos = getClipLocalScroll(clip, oldScroll, xZoom);
			bool moved = (newLocalPos != oldLocalPos);
			if (moved) {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

				clip->renderAsSingleRow(modelStackWithTimelineCounter, this, newLocalPos, xZoom,
				                        PadLEDs::image_store_for_session()[yDisplay],
				                        PadLEDs::occupancy_mask_store_for_session()[yDisplay]);
				anyMoved = true;
			}
			PadLEDs::transition_taking_place_on_row_for_session()[yDisplay] = moved;
		}
		else {
noTransition:
			PadLEDs::transition_taking_place_on_row_for_session()[yDisplay] = false;
		}
	}

	deluge::gui::ui_session::navigation.active().rendering = false;

	return anyMoved;
}

uint32_t SessionView::getClipLocalScroll(Clip* clip, uint32_t overviewScroll, uint32_t xZoom) {
	return std::min((clip->loopLength - 1) / (xZoom * kDisplayWidth) * xZoom * kDisplayWidth, overviewScroll);
}

void SessionView::flashPlayRoutine() {
	switch (currentSong->session_layout_for_session()) {
	case SessionLayoutType::SessionLayoutTypeRows: {
		uint32_t whichRowsNeedReRendering = 0;
		bool any = false;
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			Clip* clip = getClipOnScreen(yDisplay);
			if ((clip != nullptr) && clip->armState != ArmState::OFF) {
				whichRowsNeedReRendering |= (1 << yDisplay);
			}
		}

		if (whichRowsNeedReRendering) {
			view_for_session().flashPlayEnable();
			// use root UI in case this is called from performanceView
			requestRendering(getRootUI(), 0, whichRowsNeedReRendering);
		}
		break;
	}
	case SessionLayoutType::SessionLayoutTypeGrid: {
		bool renderFlashing = false;
		for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip->armState != ArmState::OFF) {
				renderFlashing = true;
				break;
			}
		}

		// view.clipArmFlashOn needs to be off so the pad is finally rendered after flashing
		if (renderFlashing || view_for_session().clipArmFlashOn) {
			if (currentUIMode != UI_MODE_EXPLODE_ANIMATION && currentUIMode != UI_MODE_IMPLODE_ANIMATION) {
				requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
				view_for_session().flashPlayEnable();
			}
		}
		break;
	}
	// explicit fallthrough cases
	case SessionLayoutType::SessionLayoutTypeMaxElement:;
	}
}

void SessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	UI::modEncoderButtonAction(whichModEncoder, on);
	performActionOnPadRelease = false;
}

void SessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
	performActionOnPadRelease = false;
}

void SessionView::noteRowChanged(InstrumentClip* instrumentClip, NoteRow* noteRow) {

	if (currentUIMode == UI_MODE_HORIZONTAL_SCROLL) {
		return; // Is this 100% correct? What if that one Clip isn't visually scrolling?
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip == instrumentClip) {
			requestRendering(this, 1 << yDisplay, 0);
			return;
		}
	}
}

uint32_t SessionView::getGreyedOutRowsNotRepresentingOutput(Output* output) {
	uint32_t rows = 0xFFFFFFFF;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->output == output) {
			rows &= ~(1 << yDisplay);
		}
	}
	return rows;
}

bool SessionView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridRenderMainPads(whichRows, image, occupancyMask, drawUndefinedArea);
	}

	uint32_t whichRowsCouldntBeRendered = 0;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	PadLEDs::rendering_lock_for_session() = true;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (whichRows & (1 << yDisplay)) {
			bool success = renderRow(modelStack, yDisplay, image[yDisplay], occupancyMask[yDisplay], drawUndefinedArea);
			if (!success) {
				whichRowsCouldntBeRendered |= (1 << yDisplay);
			}
		}
	}
	PadLEDs::rendering_lock_for_session() = false;

	if (whichRowsCouldntBeRendered && image == PadLEDs::image_for_session()) {
		requestRendering(this, whichRowsCouldntBeRendered, 0);
	}

	return true;
}

// Returns false if can't because in card routine
bool SessionView::renderRow(ModelStack* modelStack, uint8_t yDisplay, RGB thisImage[kDisplayWidth + kSideBarWidth],
                            uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	Clip* clip = getClipOnScreen(yDisplay);

	if (clip) {

		// If user assigning MIDI controls and this Clip has a command assigned, flash pink
		if (view_for_session().midiLearnFlashOn && ((Instrument*)clip->output)->midiInput.containsSomething()) {

			for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
				// We halve the intensity of the brightness in this case, because a lot of pads will be lit, it
				// looks mental, and I think one user was having it cause his Deluge to freeze due to underpowering.
				thisImage[xDisplay] = colours::midi_command.dim();
			}
		}

		else {

			bool success = true;

			if (clip->isPendingOverdub) {
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
					thisImage[xDisplay][0] = 30;
					thisImage[xDisplay][1] = 0;
					thisImage[xDisplay][2] = 0;
				}
			}
			else {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

				success = clip->renderAsSingleRow(
				    modelStackWithTimelineCounter, this,
				    getClipLocalScroll(clip, currentSong->x_scroll_for_session()[NAVIGATION_CLIP],
				                       currentSong->x_zoom_for_session()[NAVIGATION_CLIP]),
				    currentSong->x_zoom_for_session()[NAVIGATION_CLIP], thisImage, thisOccupancyMask,
				    drawUndefinedArea);
			}

			if (view_for_session().thingPressedForMidiLearn == MidiLearn::INSTRUMENT_INPUT
			    && view_for_session().midiLearnFlashOn
			    // fine even if output isn't an Instrument - will just compare as false
			    && view_for_session().learnedThing == &((Instrument*)clip->output)->midiInput) {

				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
					thisImage[xDisplay] = thisImage[xDisplay].dim();
				}
			}

			return success;
		}
	}
	else {
		memset(thisImage, 0, kDisplayWidth * sizeof(RGB));
		// Occupancy mask doesn't need to be cleared in this case
	}

	return true;
}

void SessionView::transitionToViewForClip(Clip* clip) {
	// If no Clip, just go back into the previous one we were in
	if (!clip) {
		clip = getCurrentClip();

		// If there was no previous one (e.g. because we just loaded the Song), do nothing.
		if (!clip || clip->section == 255) {
			return;
		}
	}
	// it should already be this clip, but if it ever isn't it would be a disaster
	currentSong->setCurrentClip(clip);

	int32_t clipPlaceOnScreen = std::clamp(getClipPlaceOnScreen(clip), -1_i32, kDisplayHeight);

	currentSong->x_scroll_for_session()[NAVIGATION_CLIP] = getClipLocalScroll(
	    clip, currentSong->x_scroll_for_session()[NAVIGATION_CLIP], currentSong->x_zoom_for_session()[NAVIGATION_CLIP]);

	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		gridTransitionToViewForClip(clip);
		return;
	}

	bool onKeyboardScreen =
	    ((clip->type == ClipType::INSTRUMENT) && ((InstrumentClip*)clip)->on_keyboard_screen_for_session());

	// when transitioning back to clip, if keyboard view is enabled, it takes precedent
	// over automation and instrument clip views.
	if (clip->on_automation_clip_view_for_session() && !onKeyboardScreen) {
		currentUIMode = UI_MODE_INSTRUMENT_CLIP_EXPANDING;

		// Transition pre-render can happen before AutomationView::opened(). Force clip context so we don't
		// accidentally render stale arranger automation state into the animation store.
		automation_view_for_session().onArrangerView = false;
		automation_view_for_session().navSysId = automation_view_for_session().getNavSysId();
		automation_view_for_session().setAutomationParamType();

		// Store rows 1..kDisplayHeight hold the visible clip rows; rows 0 and kDisplayHeight + 1 are reserved for
		// offscreen rows so sidebar pads can animate the full height.
		automation_view_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
		                                             &PadLEDs::occupancy_mask_store_for_session()[1], false);
		clip->renderSidebar(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
		                    &PadLEDs::occupancy_mask_store_for_session()[1]);
		if (clip->type == ClipType::INSTRUMENT) {
			instrument_clip_view_for_session().fillOffScreenImageStores();
		}
		else {
			PadLEDs::clearTransitionStoreOffScreenRows();
		}

		PadLEDs::num_animated_rows_for_session() = kDisplayHeight + 2;
		for (int32_t y = 0; y < PadLEDs::num_animated_rows_for_session(); y++) {
			PadLEDs::animated_row_going_to_for_session()[y] = clipPlaceOnScreen;
			PadLEDs::animated_row_going_from_for_session()[y] = y - 1;
		}

		PadLEDs::setupInstrumentClipCollapseAnimation(true);

		// Preparing the stores can take a while (fillOffScreenImageStores loads clusters), so only start the clock
		// once we're ready to draw the first frame.
		PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
		PadLEDs::renderClipExpandOrCollapse();

		if (clip->type == ClipType::INSTRUMENT) {
			// Hook point for specificMidiDevice
			iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_SESSION_VIEW);
		}
	}

	// InstrumentClips
	else if (clip->type == ClipType::INSTRUMENT) {

		currentUIMode = UI_MODE_INSTRUMENT_CLIP_EXPANDING;

		if (onKeyboardScreen) {

			// Keyboard view uses only its visible rows for this animation, so it starts at store row 0.
			keyboard_screen_for_session().renderMainPads(0xFFFFFFFF, PadLEDs::image_store_for_session(),
			                                             PadLEDs::occupancy_mask_store_for_session());
			keyboard_screen_for_session().renderSidebar(0xFFFFFFFF, PadLEDs::image_store_for_session(),
			                                            PadLEDs::occupancy_mask_store_for_session());

			PadLEDs::num_animated_rows_for_session() = kDisplayHeight;
			for (int32_t y = 0; y < PadLEDs::num_animated_rows_for_session(); y++) {
				PadLEDs::animated_row_going_to_for_session()[y] = clipPlaceOnScreen;
				PadLEDs::animated_row_going_from_for_session()[y] = y;
			}
		}

		else {

			// Won't have happened automatically because we haven't begun the "session"
			instrument_clip_view_for_session().recalculateColours();

			// Non-keyboard clip views include one offscreen row above and below the visible rows.
			instrument_clip_view_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
			                                                  &PadLEDs::occupancy_mask_store_for_session()[1], false);
			instrument_clip_view_for_session().renderSidebar(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
			                                                 &PadLEDs::occupancy_mask_store_for_session()[1]);

			// Important that this is done after currentSong->x_scroll_for_session() is changed, above
			instrument_clip_view_for_session().fillOffScreenImageStores();

			PadLEDs::num_animated_rows_for_session() = kDisplayHeight + 2;
			for (int32_t y = 0; y < PadLEDs::num_animated_rows_for_session(); y++) {
				PadLEDs::animated_row_going_to_for_session()[y] = clipPlaceOnScreen;
				PadLEDs::animated_row_going_from_for_session()[y] = y - 1;
			}
		}

		PadLEDs::setupInstrumentClipCollapseAnimation(true);
		if (onKeyboardScreen) {
			setUpKeyboardSidebarMorph(clipPlaceOnScreen);
		}

		// As above: don't let the store setup eat into the animation's 200ms.
		PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
		PadLEDs::renderClipExpandOrCollapse();

		// Hook point for specificMidiDevice
		iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_SESSION_VIEW);
	}

	// AudioClips
	else {
		AudioClip* clip = getCurrentAudioClip();

		Sample* sample = (Sample*)clip->sampleHolder.audioFile;

		if (sample) {

			currentUIMode = UI_MODE_AUDIO_CLIP_EXPANDING;

			waveform_renderer_for_session().collapseAnimationToWhichRow = clipPlaceOnScreen;

			PadLEDs::setupAudioClipCollapseOrExplodeAnimation(clip);

			PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
			PadLEDs::renderAudioClipExpandOrCollapse();

			PadLEDs::clearSideBar(); // Sends "now"
		}

		// If no sample, just skip directly there
		else {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audio_clip_view_for_session());
		}
	}
}

void SessionView::transitionToSessionView() {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		gridTransitionToSessionView();
		return;
	}

	if (getCurrentClip()->type == ClipType::AUDIO && getCurrentUI() != &automation_view_for_session()) {
		AudioClip* clip = getCurrentAudioClip();
		// !clip probably couldn't happen, but just in case...
		if (!clip || !clip->sampleHolder.audioFile) {
			memcpy(PadLEDs::image_store_for_session(), PadLEDs::image_for_session(),
			       sizeof(PadLEDs::image_for_session()));
			finishedTransitioningHere();
		}
		else {
			currentUIMode = UI_MODE_AUDIO_CLIP_COLLAPSING;
			waveform_renderer_for_session().collapseAnimationToWhichRow = getClipPlaceOnScreen(getCurrentClip());

			PadLEDs::setupAudioClipCollapseOrExplodeAnimation(clip);

			PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
			PadLEDs::renderAudioClipExpandOrCollapse();
		}
	}
	else {
		int32_t transitioningToRow = getClipPlaceOnScreen(getCurrentClip());
		bool transitioningFromKeyboardScreen = false;
		if (getCurrentUI() == &automation_view_for_session()) {
			// Automation collapse follows the same store layout as instrument clip view: offscreen row, visible rows,
			// offscreen row.
			automation_view_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
			                                             &PadLEDs::occupancy_mask_store_for_session()[1], false);
			getCurrentClip()->renderSidebar(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
			                                &PadLEDs::occupancy_mask_store_for_session()[1]);
			if (getCurrentClip()->type == ClipType::INSTRUMENT) {
				instrument_clip_view_for_session().fillOffScreenImageStores();
			}
			else {
				PadLEDs::clearTransitionStoreOffScreenRows();
			}

			// I didn't see a difference but the + 2 seems intentional
			PadLEDs::num_animated_rows_for_session() = kDisplayHeight + 2;
			for (int32_t y = 0; y < PadLEDs::num_animated_rows_for_session(); y++) {
				PadLEDs::animated_row_going_to_for_session()[y] = transitioningToRow;
				PadLEDs::animated_row_going_from_for_session()[y] = y - 1;
			}
		}
		else {
			InstrumentClip* instrumentClip = getCurrentInstrumentClip();
			if (instrumentClip->on_keyboard_screen_for_session()) {
				transitioningFromKeyboardScreen = true;
				// Start keyboard collapse from the exact frame currently on the LEDs. Re-rendering here can change
				// transient sidebar colours before the first animation frame and reads as a blink.
				memcpy(PadLEDs::image_store_for_session(), PadLEDs::image_for_session(),
				       sizeof(PadLEDs::image_for_session()));
				memcpy(PadLEDs::occupancy_mask_store_for_session(), PadLEDs::occupancy_mask_for_session(),
				       sizeof(PadLEDs::occupancy_mask_for_session()));

				PadLEDs::num_animated_rows_for_session() = kDisplayHeight;
				for (int32_t y = 0; y < kDisplayHeight; y++) {
					PadLEDs::animated_row_going_to_for_session()[y] = transitioningToRow;
					PadLEDs::animated_row_going_from_for_session()[y] = y;
				}
			}
			else {
				// Instrument clip collapse renders visible rows into the middle of the transition store.
				instrument_clip_view_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
				                                                  &PadLEDs::occupancy_mask_store_for_session()[1],
				                                                  false);
				instrument_clip_view_for_session().renderSidebar(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
				                                                 &PadLEDs::occupancy_mask_store_for_session()[1]);

				// I didn't see a difference but the + 2 seems intentional
				PadLEDs::num_animated_rows_for_session() = kDisplayHeight + 2;
				for (int32_t y = 0; y < PadLEDs::num_animated_rows_for_session(); y++) {
					PadLEDs::animated_row_going_to_for_session()[y] = transitioningToRow;
					PadLEDs::animated_row_going_from_for_session()[y] = y - 1;
				}
			}
		}

		// Must set this after above render calls, or else they'll see it and not render
		currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;

		// The sidebar occupancy the renderSidebar() calls above produced is authoritative: a black sidebar pad is
		// empty, and forcing it to occupied would make drawSquare() marginalise the destination pad it collapses into.

		PadLEDs::setupInstrumentClipCollapseAnimation(true);
		if (transitioningFromKeyboardScreen) {
			setUpKeyboardSidebarMorph(transitioningToRow);
		}

		if (getCurrentUI() == &instrument_clip_view_for_session()) {
			instrument_clip_view_for_session().fillOffScreenImageStores();
		}
		PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
		PadLEDs::renderClipExpandOrCollapse();
	}

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_SESSION_VIEW);
}

// Might be called during card routine! So renders might fail. Not too likely
void SessionView::finishedTransitioningHere() {
	AudioEngine::routineWithClusterLoading();

	currentUIMode = UI_MODE_ANIMATION_FADE;
	PadLEDs::recordTransitionBegin(kFadeSpeed);
	changeRootUI(this);
	renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[kDisplayHeight],
	               &PadLEDs::occupancy_mask_store_for_session()[kDisplayHeight], true);
	renderSidebar(0xFFFFFFFF, &PadLEDs::image_store_for_session()[kDisplayHeight],
	              &PadLEDs::occupancy_mask_store_for_session()[kDisplayHeight]);
	PadLEDs::timerRoutine(); // What... why? This would normally get called from that...
}

void SessionView::playbackEnded() {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	uint32_t whichRowsToReRender = 0;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->type == ClipType::AUDIO) {
			AudioClip* audioClip = (AudioClip*)clip;

			if (!audioClip->sampleHolder.audioFile) {
				whichRowsToReRender |= (1 << yDisplay);
			}
		}
	}

	if (whichRowsToReRender) {
		requestRendering(this, whichRowsToReRender, 0);
	}
}

void SessionView::clipNeedsReRendering(Clip* clip) {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	int32_t bottomIndex = currentSong->song_view_y_scroll_for_session();
	int32_t topIndex = bottomIndex + kDisplayHeight;

	bottomIndex = std::max(bottomIndex, 0_i32);
	topIndex = std::min(topIndex, currentSong->sessionClips.getNumElements());

	for (int32_t c = bottomIndex; c < topIndex; c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
		if (thisClip == clip) {
			int32_t yDisplay = c - currentSong->song_view_y_scroll_for_session();
			requestRendering(this, (1 << yDisplay), 0);
			break;
		}
	}
}

void SessionView::sampleNeedsReRendering(Sample* sample) {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	int32_t bottomIndex = currentSong->song_view_y_scroll_for_session();
	int32_t topIndex = bottomIndex + kDisplayHeight;

	bottomIndex = std::max(bottomIndex, 0_i32);
	topIndex = std::min(topIndex, currentSong->sessionClips.getNumElements());

	for (int32_t c = bottomIndex; c < topIndex; c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
		if (thisClip->type == ClipType::AUDIO && ((AudioClip*)thisClip)->sampleHolder.audioFile == sample) {
			int32_t yDisplay = c - currentSong->song_view_y_scroll_for_session();
			requestRendering(this, (1 << yDisplay), 0);
		}
	}
}

void SessionView::midiLearnFlash() {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	uint32_t mainRowsToRender = 0;
	uint32_t sideRowsToRender = 0;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip) {

			if (clip->muteMIDICommand.containsSomething()
			    || (view_for_session().thingPressedForMidiLearn == MidiLearn::CLIP
			        && &clip->muteMIDICommand == view_for_session().learnedThing)
			    || currentSong->sections[clip->section].launchMIDICommand.containsSomething()
			    || (view_for_session().thingPressedForMidiLearn == MidiLearn::SECTION
			        && view_for_session().learnedThing == &currentSong->sections[clip->section].launchMIDICommand)) {
				sideRowsToRender |= (1 << yDisplay);
			}

			if (clip->output->type != OutputType::AUDIO && clip->output->type != OutputType::NONE) {

				if (((Instrument*)clip->output)->midiInput.containsSomething()
				    || (view_for_session().thingPressedForMidiLearn == MidiLearn::INSTRUMENT_INPUT
				        && view_for_session().learnedThing
				               == &((MelodicInstrument*)clip->output)
				                       ->midiInput)) { // Should be fine even if output isn't a MelodicInstrument

					mainRowsToRender |= (1 << yDisplay);
				}
			}
		}
	}

	requestRendering(this, mainRowsToRender, sideRowsToRender);
}

void SessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (cancel_stale_session_hold()) {
		return;
	}
	performActionOnPadRelease = false;

	if (getCurrentUI() == this) { // This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}

Clip* SessionView::getClipForLayout() {
	switch (currentSong->session_layout_for_session()) {
	case SessionLayoutType::SessionLayoutTypeGrid: {
		return gridClipFromCoords(gridFirstPressedX, gridFirstPressedY);
		break;
	}
	case SessionLayoutType::SessionLayoutTypeRows:
	default: {
		return getClipOnScreen(selectedClipYDisplay);
	}
	}
}

int32_t SessionView::getClipIndexForLayout() {
	switch (currentSong->session_layout_for_session()) {
	case SessionLayoutType::SessionLayoutTypeGrid: {
		return gridClipIndexFromCoords(gridFirstPressedX, gridFirstPressedY);
		break;
	}
	case SessionLayoutType::SessionLayoutTypeRows:
	default: {
		return (session_view_for_session().selectedClipPressYDisplay + currentSong->song_view_y_scroll_for_session());
	}
	}
}

void SessionView::selectLayout(int8_t offset) {
	gridSetDefaultMode();
	// only reset first pad if it's not still held
	bool keepFirst = matrixDriver.isPadPressed(gridFirstPressedX, gridFirstPressedY);
	gridResetPresses(!keepFirst);
	gridModeActive = gridModeSelected;
	if (matrixDriver.isPadPressed(kDisplayWidth + 1, GREEN)) {
		gridModeActive = SessionGridMode::SessionGridModeLaunch;
	}
	else if (matrixDriver.isPadPressed(kDisplayWidth + 1, BLUE)) {
		gridModeActive = SessionGridMode::SessionGridModeEdit;
	}
	// Layout change
	if (offset != 0) {
		switch (currentSong->session_layout_for_session()) {
		case SessionLayoutType::SessionLayoutTypeRows: {
			currentSong->session_layout_for_session() = SessionLayoutType::SessionLayoutTypeGrid;
			break;
		}
		case SessionLayoutType::SessionLayoutTypeGrid: {
			currentSong->session_layout_for_session() = SessionLayoutType::SessionLayoutTypeRows;
			break;
		}
		// explicit fallthrough cases
		case SessionLayoutType::SessionLayoutTypeMaxElement:;
		}
		renderLayoutChange();
	}
}

void SessionView::renderLayoutChange(bool displayPopup) {
	// After change
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeRows) {
		if (displayPopup) {
			display->displayPopup("Rows");
		}
		selectedClipYDisplay = 255;
		currentSong->song_view_y_scroll_for_session() = (currentSong->sessionClips.getNumElements() - kDisplayHeight);
	}
	else if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		if (displayPopup) {
			display->displayPopup("Grid");
		}
		currentSong->song_grid_scroll_x_for_session() = 0;
		currentSong->song_grid_scroll_y_for_session() = 0;
	}

	requestRendering(&session_view_for_session(), 0xFFFFFFFF, 0xFFFFFFFF);
	view_for_session().flashPlayEnable();
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		if (!gridSelectedClipPulsing) {
			gridPulseSelectedClip();
		}
	}
}

void SessionView::selectSpecificLayout(SessionLayoutType layout) {
	gridSetDefaultMode();
	gridResetPresses();
	gridModeActive = gridModeSelected;

	if (currentSong->session_layout_for_session() != layout) {
		currentSong->session_layout_for_session() = layout;
		renderLayoutChange(false);
	}
	else {
		requestRendering(&session_view_for_session(), 0xFFFFFFFF, 0xFFFFFFFF);
		view_for_session().flashPlayEnable();
	}
}

void SessionView::enterMacrosConfigMode() {
	previousLayout = currentSong->session_layout_for_session();
	currentSong->session_layout_for_session() = SessionLayoutType::SessionLayoutTypeGrid;
	gridModeActive = SessionGridModeMacros;
	requestRendering(&session_view_for_session(), 0xFFFFFFFF, 0xFFFFFFFF);
	view_for_session().flashPlayEnable();
}

void SessionView::exitMacrosConfigMode() {
	selectSpecificLayout(previousLayout);
}

void SessionView::enterMidiLearnMode() {
	previousGridModeActive = gridModeActive;
	gridModeActive = SessionGridModeLaunch;
	requestRendering(&session_view_for_session(), 0xFFFFFFFF, 0xFFFFFFFF);
	view_for_session().startMIDILearn();
}

void SessionView::exitMidiLearnMode() {
	view_for_session().endMIDILearn();
	gridModeActive = previousGridModeActive;
	requestRendering(&session_view_for_session(), 0xFFFFFFFF, 0xFFFFFFFF);
}

bool SessionView::gridRenderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {

	// Section column
	uint32_t sectionColumnIndex = kDisplayWidth;
	for (int32_t y = (kGridHeight - 1); y >= 0; --y) {
		if (gridModeActive == SessionGridModeMacros) {
			view_for_session().renderMacros(sectionColumnIndex, y, selectedMacro, image, occupancyMask);
		}
		else {
			occupancyMask[y][sectionColumnIndex] = 64;

			auto section = gridSectionFromY(y);
			RGB& ptrSectionColour = image[y][sectionColumnIndex];

			ptrSectionColour = defaultClipSectionColours[gridSectionFromY(y)];

			if (view_for_session().midiLearnFlashOn && gridModeActive == SessionGridModeLaunch) {
				// MIDI colour if necessary
				if (currentSong->sections[section].launchMIDICommand.containsSomething()) {
					ptrSectionColour = colours::midi_command;
				}

				else {
					// If user assigning MIDI controls and has this section selected, flash to half brightness
					if (currentSong
					    && view_for_session().learnedThing == &currentSong->sections[section].launchMIDICommand) {
						ptrSectionColour = ptrSectionColour.dim();
					}
				}
			}
		}

		gridRenderActionModes(y, image, occupancyMask);
	}

	return true;
}

void SessionView::gridRenderActionModes(int32_t y, RGB image[][kDisplayWidth + kSideBarWidth],
                                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	// Action modes column
	uint32_t actionModeColumnIndex = kDisplayWidth + 1;
	bool modeExists = true;
	bool modeActive = false;
	RGB modeColour = colours::black;

	bool enableLoopPads =
	    runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableGridViewLoopPads) == RuntimeFeatureStateToggle::On;

	switch (y) {
	case GridMode::GREEN: {
		modeActive = (gridModeActive == SessionGridModeLaunch);
		modeColour = colours::green; // Green
		break;
	}
	case GridMode::BLUE: {
		modeActive = (gridModeActive == SessionGridModeEdit);
		modeColour = colours::blue; // Blue
		break;
	}
	case GridMode::RED: {
		if (enableLoopPads) {
			modeActive = matrixDriver.isPadPressed(kDisplayWidth + 1, RED);
			modeColour = colours::red; // Red
		}
		break;
	}
	case GridMode::MAGENTA: {
		if (enableLoopPads) {
			modeActive = matrixDriver.isPadPressed(kDisplayWidth + 1, MAGENTA);
			modeColour = colours::magenta; // Magenta
		}
		break;
	}

	default: {
		modeExists = false;
		break;
	}
	}
	occupancyMask[y][actionModeColumnIndex] = (modeExists ? 1 : 0);
	image[y][actionModeColumnIndex] = modeColour.adjust(255, (modeActive ? 1 : 8));
}

bool SessionView::gridRenderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                     uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	// Clear just the main pads
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			image[yDisplay][xDisplay] = {0, 0, 0};
			occupancyMask[yDisplay][xDisplay] = 0;
		}
	}

	// Iterate over all clips and render them where they are
	auto trackCount = gridTrackCount();

	PadLEDs::rendering_lock_for_session() = true;

	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		auto trackIndex = gridTrackIndexFromTrack(clip->output, trackCount);
		if (trackIndex < 0) {
			uartPrintln("Global output list mismatch");
			continue; // Should never happen but theoretically global output list can diverge from clip pointers
		}

		auto x = gridXFromTrack(trackIndex);
		auto y = gridYFromSection(clip->section);

		// Render colour for every valid clip
		// make sure the square hasn't already been written to - otherwise multiple clips in the same section will
		// overwrite each other. getClipFromPads uses the first in the list (grows from start, so furthest down in rows
		// mode), using the first clip here keeps it consistent
		if (x >= 0 && y >= 0 && occupancyMask[y][x] == 0) {
			occupancyMask[y][x] = 64;
			image[y][x] = gridRenderClipColor(clip, x, y);
		}
	}

	PadLEDs::rendering_lock_for_session() = false;

	return true;
}

RGB SessionView::gridRenderClipColor(Clip* clip, int32_t x, int32_t y, bool renderPulse) {
	// Greyout all clips during record button pressed or soloing, overwrite for clips that shouldn't be greyed out
	bool greyout = (viewingRecordArmingActive || currentSong->getAnyClipsSoloing());

	// Handle record button pressed
	if (viewingRecordArmingActive && clip->armedForRecording) {
		if (view_for_session().blinkOn) {
			bool shouldGoPurple = (clip->type == ClipType::AUDIO && ((AudioClip*)clip)->overdubsShouldCloneOutput);

			// Bright colour
			if (clip->wantsToBeginLinearRecording(currentSong)) {
				if (shouldGoPurple) {
					return colours::magenta;
				}
				return colours::red;
			}

			// Dull colour, cos can't actually begin linear recording despite being armed
			if (shouldGoPurple) {
				return colours::magenta_dull;
			}
			return colours::red_dull;
		}
	}

	// MIDI Learning
	if (view_for_session().midiLearnFlashOn) {
		if (getCurrentUI() == &deluge::gui::context_menu::midi_learn_mode_for_session()) {
			// Clip arm learned
			if (clip->muteMIDICommand.containsSomething()) {
				return colours::midi_command;
			}
			// Selected but unlearned
			if (view_for_session().learnedThing == &clip->muteMIDICommand) {
				return colours::black; // Flash black
			}
		}
		else {
			// Instrument learned
			OutputType type = clip->output->type;
			bool canLearn = (type != OutputType::AUDIO && type != OutputType::NONE);
			if (canLearn && ((MelodicInstrument*)clip->output)->midiInput.containsSomething()) {
				return colours::midi_command;
			}

			// Selected but unlearned
			if (view_for_session().thingPressedForMidiLearn == MidiLearn::INSTRUMENT_INPUT
			    && view_for_session().learnedThing == &((MelodicInstrument*)clip->output)->midiInput) {
				return colours::black; // Flash black
			}
		}
	}

	// Set a random colour if unset and convert to result colour
	if (clip->output->colour == 0) {
		lastColour = std::fmod(lastColour + colourStep + 192, 192);
		clip->output->colour = lastColour;
	}

	RGB resultColour = RGB::fromHue(clip->output->colour);

	// Black phase of arm flashing
	if (view_for_session().clipArmFlashOn && clip->armState != ArmState::OFF) {
		return colours::black;
	}

	// If we're currently pulsing this clip and we have the current pulse colour, use that instead
	if (!viewingRecordArmingActive && renderPulse && (clip == selectedClipForPulsing)) {
		return gridSelectedClipRenderedColour;
	}

	bool macroActive = false;
	if (gridModeActive == SessionGridModeMacros && selectedMacro >= 0) {
		auto& macro = currentSong->sessionMacros[selectedMacro];
		if (macro.kind == SessionMacroKind::CLIP_LAUNCH) {
			macroActive = (macro.clip == clip);
		}
		else if (macro.kind == SessionMacroKind::OUTPUT_CYCLE) {
			macroActive = (macro.output == clip->output);
		}
		else if (macro.kind == SessionMacroKind::SECTION) {
			macroActive = (macro.section == clip->section);
		}

		if (macroActive) {
			resultColour = RGB(255, 255, 255);
		}
		else {
			resultColour = RGB::fromHue(clip->output->colour);
		}
	}

	// If we are not in record arming mode make this clip full colour for being soloed
	if ((clip->soloingInSessionMode || clip->armState == ArmState::ON_TO_SOLO) && !viewingRecordArmingActive) {
		greyout = false;
	}

	// If clip is not active or grayed out - dim it
	else if (!clip->activeIfNoSolo) {
		resultColour =
		    resultColour.transform([macroActive](auto chan) { return ((float)chan / 255) * (macroActive ? 64 : 10); });
	}

	if (greyout) {
		return resultColour.greyOut(6500000);
	}

	return resultColour;
}

Clip* SessionView::gridCloneClip(Clip* sourceClip) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(sourceClip);

	Error error = sourceClip->clone(modelStack, false);
	if (error != Error::NONE) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	return (Clip*)modelStack->getTimelineCounter();
}

Clip* SessionView::gridCreateClipInTrack(Output* targetOutput) {
	Clip* sourceClip = nullptr;
	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == targetOutput) {
			sourceClip = clip;
			break;
		}
	}

	if (sourceClip == nullptr && targetOutput->getActiveClip() != nullptr) {
		sourceClip = targetOutput->getActiveClip();
	}

	if (sourceClip == nullptr) {
		return nullptr;
	}

	// New method is cloning full clip and emptying it
	Clip* newClip = gridCloneClip(sourceClip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, newClip);
	Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR);
	// clear everything
	bool clearAutomation = true;
	bool clearSequenceAndMPE = true;
	newClip->clear(action, modelStack, clearAutomation, clearSequenceAndMPE);
	actionLogger.deleteAllLogs();

	// For safety we set it up exactly as we want it
	setupNewClip(newClip);

	return newClip;
}

bool SessionView::createNewTrackForAudioClip(AudioClip* newClip) {
	// Suss output
	AudioOutput* newOutput = currentSong->createNewAudioOutput();
	if (!newOutput) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	newClip->setOutput(modelStack->addTimelineCounter(newClip), newOutput);

	return true;
}

bool SessionView::createNewTrackForInstrumentClip(OutputType type, InstrumentClip* clip, bool copyDrumsFromClip) {
	bool instrumentAlreadyInSong = false;
	if (type == OutputType::SYNTH || type == OutputType::KIT) {
		Error error = setPresetOrNextUnlaunchedOne(clip, type, &instrumentAlreadyInSong, copyDrumsFromClip);
		if (error != Error::NONE || instrumentAlreadyInSong) {
			if (error != Error::NONE) {
				display->displayError(error);
			}
			return false;
		}
	}
	else if (type == OutputType::MIDI_OUT || type == OutputType::CV) {
		clip->output = currentSong->getNonAudioInstrumentToSwitchTo(type, Availability::INSTRUMENT_UNUSED, 0, -1,
		                                                            &instrumentAlreadyInSong);
		if (clip->output == nullptr) {
			return false;
		}

		auto error = clip->setNonAudioInstrument((Instrument*)(clip->output), currentSong);
		if (error != Error::NONE) {
			display->displayError(error);
			return false;
		}
	}
	else {
		return false;
	}

	if (!instrumentAlreadyInSong) {
		currentSong->addOutput(clip->output);
	}

	if (!clip->output->getActiveClip()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		clip->output->setActiveClip(modelStackWithTimelineCounter);
	}

	return true;
}

AudioClip* SessionView::gridCreateAudioClipWithNewTrack() {
	// Allocate new clip
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
	if (!memory) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	AudioClip* newClip = new (memory) AudioClip();
	if (!createNewTrackForAudioClip(newClip)) {
		newClip->~AudioClip();
		delugeDealloc(memory);
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	// For safety we set it up exactly as we want it
	setupNewClip(newClip);

	return newClip;
}

InstrumentClip* SessionView::gridCreateInstrumentClipWithNewTrack(OutputType type) {
	// Allocate new clip
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (!memory) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	InstrumentClip* newClip = new (memory) InstrumentClip(currentSong);
	if (!createNewTrackForInstrumentClip(type, newClip, true)) {
		newClip->~InstrumentClip();
		delugeDealloc(memory);
		return nullptr;
	}

	// For safety we set it up exactly as we want it
	setupNewClip(newClip);

	return newClip;
}

// For safety we set it up the new clip exactly as we want it
void SessionView::setupNewClip(Clip* newClip) {
	newClip->colourOffset = random(72);
	newClip->soloingInSessionMode = false;
	newClip->wasActiveBefore = false;
	newClip->isPendingOverdub = false;
	newClip->isUnfinishedAutoOverdub = false;
	newClip->armState = ArmState::OFF;

	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		newClip->loopLength = currentSong->getBarLength();
		newClip->activeIfNoSolo = false;
	}
	else {
		uint32_t currentDisplayLength = currentSong->x_zoom_for_session()[NAVIGATION_CLIP] * kDisplayWidth;
		uint32_t oneBar = currentSong->getBarLength();

		// Default Clip length. Default to current zoom, minimum 1 bar
		int32_t newClipLength = std::max(currentDisplayLength, oneBar);

		newClip->loopLength = newClipLength;

		if (playbackHandler.playbackState
		    && (currentPlaybackMode == &arrangement || !playbackHandler.isEitherClockActive())) {
			newClip->activeIfNoSolo = false;
		}
	}

	if (newClip->type == ClipType::AUDIO) {
		if (currentSong->defaultAudioClipOverdubOutputCloning == -1) {
			currentSong->defaultAudioClipOverdubOutputCloning = 1;
			// For each Clip in session
			for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
				Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

				if (clip->type == ClipType::AUDIO && clip->armedForRecording) {
					currentSong->defaultAudioClipOverdubOutputCloning = ((AudioClip*)clip)->overdubsShouldCloneOutput;
					break;
				}
			}
		}
		newClip->overdubsShouldCloneOutput = currentSong->defaultAudioClipOverdubOutputCloning;
	}
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
void SessionView::copyClipName(Clip* source, Clip* target, Output* targetOutput) {
	if (source->name.isEmpty()) {
		return;
	}
	// Start from the original clip name. We have max 12 sections, so being able to append
	// a two digit number is enough.
	DEF_STACK_STRING_BUF(newName, source->name.getLength() + 2);
	newName.append(source->name.get());
	// If the name already exists on the target output, we'll append a number. We start from 2,
	// because "BRIDGE" & "BRIDGE2" make more sense than "BRIDGE" & "BRIDGE1".
	int32_t counter = 2;
	// If the original ends in a number, grab it instead.
	int32_t sourceEnd = newName.size();
	int32_t end = sourceEnd;
	while (end > 0 && isdigit(newName.data()[end - 1])) {
		end--;
	}
	if (end < sourceEnd) {
		counter = atoi(newName.data() + end);
	}
	// Keep trying until we have a name that's unique on the output.
	String newNameString;
	newNameString.set(newName.data());
	while (targetOutput->getClipFromName(newNameString.get()) != nullptr) {
		newName.truncate(end);
		newName.appendInt(counter++);
		newNameString.set(newName.data());
	}
	target->name.set(newName.data());
}
#pragma GCC diagnostic pop

Clip* SessionView::gridCreateClip(uint32_t targetSection, Output* targetOutput, Clip* sourceClip) {
	actionLogger.deleteAllLogs();

	Clip* newClip = nullptr;

	// From source
	if (sourceClip != nullptr) {
		// Can't convert between audio and non audio tracks
		if (targetOutput) {
			bool sourceIsAudio = (sourceClip->output->type == OutputType::AUDIO);
			bool targetIsAudio = (targetOutput->type == OutputType::AUDIO);
			if (sourceIsAudio != targetIsAudio) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_CONVERT_TYPE));
				return nullptr;
			}
		}

		// First we make an identical copy
		newClip = gridCloneClip(sourceClip);
		if (newClip == nullptr) {
			return nullptr;
		}
		// ...with a derived name
		copyClipName(sourceClip, newClip, targetOutput);
	}

	// Create new clip in existing track
	else if (targetOutput != nullptr) {
		newClip = gridCreateClipInTrack(targetOutput);
	}

	// Create new clip in new track
	else {
		// only create track and clip if you're pressing in an empty track column immediately to
		// to the right of another not-empty track column
		auto maxTrack = gridTrackCount();
		if ((gridFirstPressedX > 0) && (gridTrackFromX(gridFirstPressedX - 1, maxTrack))) {
			// This is the right position to add immediate type creation
			setupTrackCreation();
			createClip = true;

			// wait until you've chosen a type, by pressing a type button or releasing the pad
			yield([]() { return (currentUIMode != UI_MODE_CREATING_CLIP); });
			if (createClip) {
				OutputType toCreate = context_menu::clip_settings::new_clip_type_for_session().toCreate;
				newClip = createNewClip(toCreate, -1);
				if (newClip == nullptr) {
					clipPressEnded();
				}
				// if we made a clip, update last type created
				else {
					lastTypeCreated = toCreate;
				}
				createClip = false;
			}
		}
		else {
			clipPressEnded();
		}
	}

	if (newClip == nullptr) {
		return nullptr;
	}

	// Set new clip section and add it to the list
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(newClip);

	newClip->section = targetSection;
	if (newClip->type == ClipType::INSTRUMENT) {
		((InstrumentClip*)newClip)->on_keyboard_screen_for_session() = false;
	}

	if (currentSong->sessionClips.insertClipAtIndex(newClip, 0) != Error::NONE) {
		newClip->~Clip();
		delugeDealloc(newClip);
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	// If we copied from source and the clip should go in another track we need to move it after putting it in the
	// session Remember this assumes a non Audio clip
	if (sourceClip != nullptr) {
		if (sourceClip->type == ClipType::INSTRUMENT) {
			InstrumentClip* newInstrumentClip = (InstrumentClip*)newClip;
			// Create a new track for the clip
			if (targetOutput == nullptr) {
				if (!createNewTrackForInstrumentClip(sourceClip->output->type, newInstrumentClip, false)) {
					currentSong->sessionClips.deleteAtIndex(0);
					newClip->~Clip();
					delugeDealloc(newClip);
					return nullptr;
				}

				targetOutput = newInstrumentClip->output;
			}

			// Different instrument, switch the cloned clip to it
			else if (targetOutput != sourceClip->output) {
				Error error = newInstrumentClip->changeInstrument(modelStack, (Instrument*)targetOutput, nullptr,
				                                                  InstrumentRemoval::NONE);
				if (error != Error::NONE) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_SWITCHING_TO_TRACK_FAILED));
				}

				if (targetOutput->type == OutputType::KIT) {
					newInstrumentClip->y_scroll_for_session() = 0;
				}
			}
		}

		else if (sourceClip->type == ClipType::AUDIO) {
			AudioClip* newAudioClip = (AudioClip*)newClip;

			if (targetOutput == nullptr) {
				AudioOutput* newOutput = currentSong->createNewAudioOutput();
				if (!newOutput) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_SWITCHING_TO_TRACK_FAILED));
				}
				else {
					targetOutput = newOutput;
				}
			}

			if (targetOutput && targetOutput != sourceClip->output && targetOutput->type == OutputType::AUDIO) {
				((AudioOutput*)targetOutput)->cloneFrom((AudioOutput*)(sourceClip->output));
				newAudioClip->setOutput(modelStack, targetOutput);
			}
		}
	}

	currentSong->notify_peer_clip_inserted(0);

	// Figure out the play pos for the new Clip if we're currently playing
	resyncNewClip(newClip, modelStack);

	// Set to active for new tracks
	if (targetOutput == nullptr && !newClip->output->getActiveClip()) {
		newClip->output->setActiveClip(modelStack);
	}
	// set it active in the song
	gridSelectClipForPulsing(*newClip);
	currentSong->setCurrentClip(newClip);
	return newClip;
}

void SessionView::gridClonePad(uint32_t sourceX, uint32_t sourceY, uint32_t targetX, uint32_t targetY) {
	Clip* sourceClip = gridClipFromCoords(sourceX, sourceY);
	if (sourceClip == nullptr) {
		return;
	}

	// Don't allow copying recording clips
	if (sourceClip->getCurrentlyRecordingLinearly()) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_CLONE_AUDIO_IN_OTHER_TRACK));
		return;
	}

	Clip* targetClip = gridClipFromCoords(targetX, targetY);
	if (targetClip != nullptr) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_TARGET_FULL));
		return;
	}

	gridCreateClip(gridSectionFromY(targetY), gridTrackFromX(targetX, gridTrackCount()), sourceClip);
	display->popupTextTemporary("COPIED");
}

void SessionView::gridStartSection(uint32_t section, bool instant) {
	if (instant) {
		currentSong->turnSoloingIntoJustPlaying(currentSong->sections[section].numRepetitions > -1);

		for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);

			if ((clip->section == section && !clip->activeIfNoSolo)
			    || (clip->section != section && clip->activeIfNoSolo)) {
				gridToggleClipPlay(clip, instant);
			}
			else {
				clip->armState = ArmState::OFF;
			}
		}

		session.launchSchedulingMightNeedCancelling();
	}
	else {
		session.armSection(section, kInternalButtonPressLatency);
	}
}

void SessionView::gridToggleClipPlay(Clip* clip, bool instant) {
	session.toggleClipStatus(clip, nullptr, instant, kInternalButtonPressLatency);
}

ActionResult SessionView::gridHandlePads(int32_t x, int32_t y, int32_t on) {
	// Except for the path to sectionPadAction in the original function all paths contained this check. Can probably
	// be refactored
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_IMPLODE_ANIMATION
	    || load_song_ui_for_session().isLoadingSong()) {
		return ActionResult::DEALT_WITH;
	}

	// Right sidebar column - action modes
	if (x > kDisplayWidth) {

		if (on) {
			if (getCurrentUI() != &deluge::gui::context_menu::midi_learn_mode_for_session()) {
				clipPressEnded();
			}
			gridActiveModeUsed = false;
			bool enableLoopPads = runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableGridViewLoopPads)
			                      == RuntimeFeatureStateToggle::On;
			switch (y) {
			case GridMode::GREEN: {
				gridModeActive = SessionGridModeLaunch;
				break;
			}
			case GridMode::BLUE: {
				gridModeActive = SessionGridModeEdit;
				break;
			}
			case GridMode::RED: {
				if (enableLoopPads) {
					playbackHandler.tryLoopCommand(GlobalMIDICommand::LOOP);
				}
				break;
			}
			case GridMode::MAGENTA: {
				if (enableLoopPads) {
					playbackHandler.tryLoopCommand(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING);
				}
				break;
			}
			}
		}
		else {
			if (FlashStorage::defaultGridActiveMode == GridDefaultActiveModeSelection) {
				if (!gridActiveModeUsed) {
					gridModeSelected = gridModeActive;
				}
			}
			else {
				gridSetDefaultMode();
			}

			gridModeActive = gridModeSelected;
		}
	}
	else {
		gridActiveModeUsed = true;

		Clip* clip = gridClipFromCoords(x, y);
		ActionResult modeHandleResult = ActionResult::NOT_DEALT_WITH;
		switch (gridModeActive) {
		case SessionGridModeEdit: {
			modeHandleResult = gridHandlePadsEdit(x, y, on, clip);
			break;
		}
		case SessionGridModeLaunch: {
			modeHandleResult = gridHandlePadsLaunch(x, y, on, clip);
			break;
		}
		case SessionGridModeMacros: {
			modeHandleResult = gridHandlePadsMacros(x, y, on, clip);
			break;
		}
		// explicit fallthrough cases
		case SessionGridModeMaxElement:;
		}

		if (modeHandleResult == ActionResult::DEALT_WITH) {
			return ActionResult::DEALT_WITH;
		}
	}

	if (currentUIMode != UI_MODE_EXPLODE_ANIMATION && currentUIMode != UI_MODE_IMPLODE_ANIMATION) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		view_for_session().flashPlayEnable();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SessionView::gridHandlePadsEdit(int32_t x, int32_t y, int32_t on, Clip* clip) {
	// Left sidebar column (sections)
	if (x == kDisplayWidth) {
		// Get pressed section
		auto section = gridSectionFromY(y);
		if (section < 0) {
			return ActionResult::DEALT_WITH;
		}

		// Immediate release of the pad arms the section, holding allows changing repeats
		if (on) {
			enterUIMode(UI_MODE_HOLDING_SECTION_PAD);
			session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
			sectionPressed = section;
			beginEditingSectionRepeatsNum();
		}
		else {
			if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
				exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
				if (display->haveOLED()) {
					deluge::hid::display::OLED::removePopup();
				}
				else {
					redrawNumericDisplay();
				}
			}
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	// Learn MIDI for tracks
	if (currentUIMode == UI_MODE_MIDI_LEARN) {
		gridHandlePadsWithMidiLearnPressed(x, on, clip);

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	if (on) {
		// Only do this if no pad is pressed yet
		if (gridFirstPressedX == -1 && gridFirstPressedY == -1) {
			gridFirstPressedX = x;
			session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
			gridFirstPressedY = y;

			// Create new track on empty slots
			if (clip == nullptr) {
				uint32_t trackCount = gridTrackCount();
				auto trackIndex = gridTrackIndexFromX(x, trackCount);

				// Create clip if it does not exist
				if ((x + currentSong->song_grid_scroll_x_for_session()) <= trackCount) {
					Output* track = gridTrackFromX(x, trackCount);
					// Keep this checkpoint local: nested input can replace the gesture revision.
					Song* const song_before_creation = currentSong;
					const auto revision_before_creation =
					    deluge::gui::ui_session::navigation.active().structural_refresh.revision();
					clip = gridCreateClip(gridSectionFromY(y), track, nullptr);
					if (currentSong != song_before_creation
					    || revision_before_creation
					           != deluge::gui::ui_session::navigation.active().structural_refresh.revision()) {
						// The returned pointer may no longer be a valid target. Do not dereference it.
						if (currentSong == song_before_creation)
							cancel_stale_session_hold();
						return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
					}
					// Immediately start playing it for new tracks
					if (clip != nullptr && track == nullptr) {
						gridToggleClipPlay(clip, true);
					}
					// if the pad has already been released while we yielded just get out of here
					// don't update clip selection if we didn't create a clip
					if (clip != nullptr && (x != gridFirstPressedX || y != gridFirstPressedY)) {
						gridSelectClipForPulsing(*clip);
						currentSong->setCurrentClip(clip);
						transitionToViewForClip(clip);
						return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
					}
				}
			}

			if (clip == nullptr) {
				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}
			if (display->haveOLED()) {
				// removes potential stuck pop-up if you're previewing / entering a clip
				// while holding section pad and repeats popup is displayed
				deluge::hid::display::OLED::removePopup();
			}
			view_for_session().displayOutputName(clip->output, true, clip);

			// we've either created or selected a clip, so set it to be current
			gridSelectClipForPulsing(*clip);
			currentSong->setCurrentClip(clip);

			// Allow clip control (selection)
			currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
			performActionOnPadRelease = true;
			selectedClipTimePressed = AudioEngine::audioSampleTimer;
			view_for_session().setActiveModControllableTimelineCounter(clip);
		}
		// Remember the second press down if empty
		else if (gridSecondPressedX == -1 || gridSecondPressedY == -1) {
			performActionOnPadRelease = false;
			gridSecondPressedX = x;
			gridSecondPressedY = y;
			display->popupText("COPY CLIPS");
		}
	}
	// Release
	else {
		// First finger up
		if (gridFirstPressedX == x && gridFirstPressedY == y) {
			if (currentUIMode == UI_MODE_CREATING_CLIP) {
				// accept the current option on release. -1 for last
				exitTrackCreation(); // will fall back into making a clip
			}
			// Open clip if no other pad was previously pressed, timer has not run out and clip is pressed
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) && performActionOnPadRelease
			    && AudioEngine::audioSampleTimer - selectedClipTimePressed < kShortPressTime) {

				// Not allowed if recording arrangement
				if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				}
				else {
					if (clip != nullptr) {
						transitionToViewForClip(clip);
					}
					return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
				}
			}

			clipPressEnded();
		}

		// Second finger up, clone clip
		else if (gridSecondPressedX == x && gridSecondPressedY == y) {
			gridClonePad(gridFirstPressedX, gridFirstPressedY, gridSecondPressedX, gridSecondPressedY);
			gridResetPresses(false, true);
		}
	}

	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}
void SessionView::setupTrackCreation() const { // start clip creation, blink LED corresponding to last type created
	context_menu::clip_settings::new_clip_type_for_session().setupAndCheckAvailability();
	openUI(&context_menu::clip_settings::new_clip_type_for_session());
}

ActionResult SessionView::clipCreationButtonPressed(hid::Button i, bool on, bool routine) {
	using namespace deluge::hid::button;
	OutputType toCreate = buttonToOutputType(i);
	if (toCreate != OutputType::NONE) {
		context_menu::clip_settings::new_clip_type_for_session().toCreate = toCreate;
		exitTrackCreation();
		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}
	// exit on back, cancel creation
	if (i == BACK) {
		createClip = false;
		exitTrackCreation();
		clipPressEnded();
		return ActionResult::DEALT_WITH;
	}
	return ActionResult::NOT_DEALT_WITH;
}
void SessionView::exitTrackCreation() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false, false);
	indicator_leds::setLedState(IndicatorLED::CV, false, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false, false);
	exitUIMode(UI_MODE_CREATING_CLIP);
}

ActionResult SessionView::gridHandlePadsLaunch(int32_t x, int32_t y, int32_t on, Clip* clip) {
	if (on && playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		if (currentUIMode == UI_MODE_NONE) {
			playbackHandler.switchToSession();
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	// Left sidebar column (sections)
	if (x == kDisplayWidth) {
		// Get pressed section
		auto section = gridSectionFromY(y);
		if (section < 0) {
			return ActionResult::DEALT_WITH;
		}

		// MIDI learn section
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			view_for_session().sectionMidiLearnPadPressed(on, section);
			return ActionResult::DEALT_WITH;
		}

		if (on) {
			// Immediate launch if shift pressed
			if (Buttons::isShiftButtonPressed()) {
				gridStartSection(section, true);
			}
			// With green selection enabled, holding the section pad allows changing repeats (like blue mode),
			// while a short press still arms the section on release
			else if (FlashStorage::gridAllowGreenSelection) {
				enterUIMode(UI_MODE_HOLDING_SECTION_PAD);
				session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
				performActionOnSectionPadRelease = true;
				sectionPressed = section;
				uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 300);
			}
			else {
				gridStartSection(section, false);
			}
		}
		else {
			if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
				// A short press (timer not yet fired) arms the section; a hold just edits repeats
				if (performActionOnSectionPadRelease) {
					session.armSection(sectionPressed, kInternalButtonPressLatency);
				}
				exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
				if (display->haveOLED()) {
					deluge::hid::display::OLED::removePopup();
				}
				else {
					redrawNumericDisplay();
				}
				uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			}
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	if (clip == nullptr) {
		// press on an empty pad
		if (on) {
			auto maxTrack = gridTrackCount();
			Output* track = gridTrackFromX(x, maxTrack);

			// holding one clip, then pressing a second empty clip
			if ((gridFirstPressedX != -1 && gridFirstPressedY != -1)
			    && (gridSecondPressedX == -1 || gridSecondPressedY == -1)) {
				performActionOnPadRelease = false; // doing a copy paste, so don't launch clips
				gridSecondPressedX = x;
				gridSecondPressedY = y;
				display->popupText("COPY CLIPS");
			}
			// next disarm the track if that's the user prefernce and a track exists, unless we're recording and
			// createAndRecord is enabled
			else if (currentUIMode == UI_MODE_NONE && track && FlashStorage::gridEmptyPadsUnarm
			         && (playbackHandler.recording == RecordingMode::OFF || !FlashStorage::gridEmptyPadsCreateRec)) {
				auto maxTrack = gridTrackCount();
				Output* track = gridTrackFromX(x, maxTrack);
				if (track != nullptr) {
					for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
						Clip* sessionClip = currentSong->sessionClips.getClipAtIndex(idxClip);
						if (sessionClip->output == track) {
							if (sessionClip->activeIfNoSolo) {
								gridToggleClipPlay(sessionClip, Buttons::isShiftButtonPressed());
							}
							else {
								sessionClip->armState = ArmState::OFF;
							}
						}
					}

					return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
				}
			}
			// lastly create a new clip, either on an existing track or on a new one
			else if (currentUIMode == UI_MODE_NONE) {
				gridFirstPressedX = x;
				session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
				gridFirstPressedY = y;
				// will create the track if it doesn't exist
				// Keep this checkpoint local: nested input can replace the gesture revision.
				Song* const song_before_creation = currentSong;
				const auto revision_before_creation =
				    deluge::gui::ui_session::navigation.active().structural_refresh.revision();
				clip = gridCreateClip(gridSectionFromY(y), track, nullptr);
				if (currentSong != song_before_creation
				    || revision_before_creation
				           != deluge::gui::ui_session::navigation.active().structural_refresh.revision()) {
					// The returned pointer may no longer be a valid target. Do not dereference it.
					if (currentSong == song_before_creation)
						cancel_stale_session_hold();
					return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
				}
				// If playing and Rec enabled, selecting an empty clip creates a new clip and starts it playing
				// (depending on setting)
				if (clip != nullptr && playbackHandler.playbackState
				    && playbackHandler.recording == RecordingMode::NORMAL && FlashStorage::gridEmptyPadsCreateRec) {
					gridToggleClipPlay(clip, Buttons::isShiftButtonPressed());
				}

				// if you didn't create a clip, don't change clip selection, don't update display and mod controllable
				if (clip != nullptr) {
					gridSelectClipForPulsing(*clip);
					currentSong->setCurrentClip(clip);

					// Allow clip control (selection) if still holding it
					if (x == gridFirstPressedX && y == gridFirstPressedY) {
						currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
						view_for_session().displayOutputName(clip->output, true, clip);
						display->cancelPopup();

						// this needs to be called after the current clip is set in order to ensure that
						// if midi follow feedback is enabled, it sends feedback for the right clip
						view_for_session().setActiveModControllableTimelineCounter(clip);
					}
				}

				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}
		}
		// release on an empty pad
		else {
			// if creating a clip releasing the first pad makes it
			if (isUIModeActive(UI_MODE_CREATING_CLIP)) {
				if (x == gridFirstPressedX && y == gridFirstPressedY) {
					exitTrackCreation();
					clipPressEnded();
				}
			}
			// clone clip on release of second pad
			if (gridSecondPressedX == x && gridSecondPressedY == y) {
				gridClonePad(gridFirstPressedX, gridFirstPressedY, gridSecondPressedX, gridSecondPressedY);
				gridResetPresses(false, true);
				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}
		}

		return ActionResult::DEALT_WITH;
	}

	// Learn MIDI ARM
	if (currentUIMode == UI_MODE_MIDI_LEARN) {
		if (getCurrentUI() == &deluge::gui::context_menu::midi_learn_mode_for_session()) {
			view_for_session().clipStatusMidiLearnPadPressed(on, clip);
		}
		else {
			gridHandlePadsWithMidiLearnPressed(x, on, clip);
		}
		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	if (FlashStorage::gridAllowGreenSelection) {
		return gridHandlePadsLaunchWithSelection(x, y, on, clip);
	}
	else {
		return gridHandlePadsLaunchImmediate(x, y, on, clip);
	}
}

ActionResult SessionView::gridHandlePadsLaunchImmediate(int32_t x, int32_t y, int32_t on, Clip* clip) {
	// From here all actions only happen on press
	if (!on) {
		return ActionResult::DEALT_WITH;
	}

	gridHandlePadsLaunchToggleArming(clip, Buttons::isShiftButtonPressed());
	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

ActionResult SessionView::gridHandlePadsLaunchWithSelection(int32_t x, int32_t y, int32_t on, Clip* clip) {
	if (on) {
		// Immediate arming, immediate consumption
		if (Buttons::isShiftButtonPressed()) {
			gridHandlePadsLaunchToggleArming(clip, true);
			return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
		}

		if (gridFirstPressedX == -1 && gridFirstPressedY == -1) {
			gridFirstPressedX = x;
			session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
			gridFirstPressedY = y;

			// Allow clip control (selection)
			currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
			performActionOnPadRelease = true;
			selectedClipTimePressed = AudioEngine::audioSampleTimer;
			gridSelectClipForPulsing(*clip);
			currentSong->setCurrentClip(clip);
			view_for_session().displayOutputName(clip->output, true, clip);
			// this needs to be called after the current clip is set in order to ensure that
			// if midi follow feedback is enabled, it sends feedback for the right clip
			view_for_session().setActiveModControllableTimelineCounter(clip);
		}
		// Special case, if there are already selected pads we allow immediate arming all others
		else {
			return gridHandlePadsLaunchImmediate(x, y, on, clip);
		}
	}
	else {
		if (gridFirstPressedX == x && gridFirstPressedY == y) {
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) && performActionOnPadRelease
			    && AudioEngine::audioSampleTimer - selectedClipTimePressed < kShortPressTime) {

				gridHandlePadsLaunchToggleArming(clip, false);
			}

			clipPressEnded();
		}
	}

	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

void SessionView::gridHandlePadsLaunchToggleArming(Clip* clip, bool immediate) {
	if (immediate) {
		if (horizontalEncoderPressed) {
			session.soloClipAction(clip, immediate, kInternalButtonPressLatency);
		}
		else {
			gridToggleClipPlay(clip, true);
		}
	}
	else {
		if (horizontalEncoderPressed) {
			session.soloClipAction(clip, immediate, kInternalButtonPressLatency);
		}
		else if (viewingRecordArmingActive) {
			// Here I removed the overdubbing settings
			clip->armedForRecording = !clip->armedForRecording;
			PadLEDs::reassessGreyout(true);
		}
		else if (currentUIMode == UI_MODE_NONE && Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			clip->armedForRecording = !clip->armedForRecording;
			session_view_for_session().timerCallback();
		}
		else if ((currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
		          || currentUIMode == UI_MODE_STUTTERING)) {
			gridToggleClipPlay(clip, false);
		}
	}
}

void SessionView::gridHandlePadsWithMidiLearnPressed(int32_t x, int32_t on, Clip* clip) {
	if (clip != nullptr) {
		if (clip->type != ClipType::AUDIO) {
			// Learn + Holding pad = Learn MIDI channel
			Output* output = gridTrackFromX(x, gridTrackCount());
			if (output && (output->type != OutputType::AUDIO && output->type != OutputType::NONE)) {
				view_for_session().instrumentMidiLearnPadPressed(on, (Instrument*)output);
			}
		}
		else {
			if (getCurrentUI() != &deluge::gui::context_menu::midi_learn_mode_for_session()) {
				view_for_session().endMIDILearn();
			}
			gui::context_menu::audio_input_selector_for_session().audioOutput = (AudioOutput*)clip->output;
			gui::context_menu::audio_input_selector_for_session().setupAndCheckAvailability();
			openUI(&gui::context_menu::audio_input_selector_for_session());
		}
	}
}

ActionResult SessionView::gridHandlePadsMacros(int32_t x, int32_t y, int32_t on, Clip* clip) {
	if (x < kDisplayWidth) {
		if (selectedMacro == -1 || !on) {
			return ActionResult::DEALT_WITH;
		}
		auto& macro = currentSong->sessionMacros[selectedMacro];
		if (gridFirstPressedX != x || gridFirstPressedY != y) {
			if (clip == nullptr) {
				// TODO: be smart and assign output or section if can be determined
				gridFirstPressedX = -1;
				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}
			gridFirstPressedX = x;
			session_hold_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
			gridFirstPressedY = y;
			macro.kind = SessionMacroKind::CLIP_LAUNCH;
			macro.clip = clip;
			macro.output = clip->output;
			macro.section = clip->section;
		}
		else {
			int kindIndex = (int32_t)macro.kind + 1;
			if (kindIndex == SessionMacroKind::NUM_KINDS) {
				kindIndex = 0;
			}
			macro.kind = (SessionMacroKind)kindIndex;
		}

		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			const char* macroKind = getMacroKindString(macro.kind);
			display->displayPopup(macroKind);
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}
	else {
		if (on) {
			selectedMacro = (selectedMacro == y) ? -1 : y;
			gridFirstPressedX = -1;
		}
	}
	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

char const* SessionView::getMacroKindString(SessionMacroKind kind) {
	const char* macroKind;
	// display new macro type on the screen
	switch (kind) {
		using deluge::l10n::String;
	case SessionMacroKind::CLIP_LAUNCH:
		macroKind = get(String::STRING_FOR_SONG_MACRO_KIND_CLIP);
		break;
	case SessionMacroKind::OUTPUT_CYCLE:
		macroKind = get(String::STRING_FOR_SONG_MACRO_KIND_OUTPUT);
		break;
	case SessionMacroKind::SECTION:
		macroKind = get(String::STRING_FOR_SONG_MACRO_KIND_SECTION);
		break;
	default:
		macroKind = get(String::STRING_FOR_SONG_MACRO_KIND_NONE);
		break;
	}
	return macroKind;
}

ActionResult SessionView::gridHandleScroll(int32_t offsetX, int32_t offsetY) {
	if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW && offsetY != 0) {
		auto track = gridTrackFromX(gridFirstPressedX, gridTrackCount());
		if (track != nullptr) {
			if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
				track->colour += offsetY;
				if (track->colour == 0) {
					track->colour += offsetY;
				}
			}
			else {
				track->colour = static_cast<int16_t>(track->colour + (colourStep * offsetY) + 192) % 192;
			}
			requestRendering(this);
		}

		return ActionResult::DEALT_WITH;
	}

	if (getCurrentUI() != &deluge::gui::context_menu::midi_learn_mode_for_session()) {
		gridResetPresses();
		clipPressEnded();
	}

	// Fix the range
	currentSong->song_grid_scroll_y_for_session() =
	    std::clamp<int32_t>(currentSong->song_grid_scroll_y_for_session() - offsetY, 0, kMaxNumSections - kGridHeight);
	currentSong->song_grid_scroll_x_for_session() =
	    std::clamp<int32_t>(currentSong->song_grid_scroll_x_for_session() + offsetX, 0,
	                        std::max<int32_t>(0, (gridTrackCount() - kDisplayWidth) + 1));

	// This is the right place to add new features like moving clips or tracks :)

	// use root UI in case this is called from performance view
	requestRendering(getRootUI(), 0xFFFFFFFF, 0xFFFFFFFF);
	view_for_session().flashPlayEnable();
	return ActionResult::DEALT_WITH;
}

void SessionView::gridTransitionToSessionView() {
	Sample* sample;

	if (getCurrentClip()->type == ClipType::AUDIO && getCurrentUI() != &automation_view_for_session()) {
		// If no sample, just skip directly there
		if (!getCurrentAudioClip()->sampleHolder.audioFile) {
			changeRootUI(&session_view_for_session());
			memcpy(PadLEDs::image_store_for_session(), PadLEDs::image_for_session(),
			       sizeof(PadLEDs::image_for_session()));
			finishedTransitioningHere();
			return;
		}
	}

	currentUIMode = UI_MODE_IMPLODE_ANIMATION;

	memcpy(PadLEDs::image_store_for_session()[1], PadLEDs::image_for_session(),
	       (kDisplayWidth + kSideBarWidth) * kDisplayHeight * sizeof(RGB));
	memcpy(PadLEDs::occupancy_mask_store_for_session()[1], PadLEDs::occupancy_mask_for_session(),
	       (kDisplayWidth + kSideBarWidth) * kDisplayHeight);
	// Grid collapse uses the same offscreen instrument rows whether the current editor is notes or automation.
	if (getCurrentClip()->type == ClipType::INSTRUMENT
	    && (getCurrentUI() == &instrument_clip_view_for_session()
	        || getCurrentUI() == &automation_view_for_session())) {
		instrument_clip_view_for_session().fillOffScreenImageStores();
	}

	auto clipX = std::clamp<int32_t>(gridXFromTrack(gridTrackIndexFromTrack(getCurrentOutput(), gridTrackCount())), 0,
	                                 kDisplayWidth);
	auto clipY = std::clamp<int32_t>(gridYFromSection(getCurrentClip()->section), 0, kDisplayHeight);

	if (getCurrentClip()->type == ClipType::AUDIO && getCurrentUI() != &automation_view_for_session()) {
		waveform_renderer_for_session().collapseAnimationToWhichRow = clipY;

		PadLEDs::setupAudioClipCollapseOrExplodeAnimation(getCurrentAudioClip());
	}
	else {
		PadLEDs::explode_animation_y_origin_big_for_session() = clipY << 16;
	}

	PadLEDs::explode_animation_x_start_big_for_session() = clipX << 16;
	PadLEDs::explode_animation_x_width_big_for_session() = (1 << 16);

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
	PadLEDs::explode_animation_direction_for_session() = -1;

	// clear sidebar for instrumentClipView, automationClipView, and keyboardScreen
	if (getCurrentUI() != &audio_clip_view_for_session()) {
		PadLEDs::clearSideBar();
	}

	PadLEDs::explode_animation_target_ui_for_session() = this;
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 35);

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_SESSION_VIEW);
}

void SessionView::gridTransitionToViewForClip(Clip* clip) {
	currentUIMode = UI_MODE_EXPLODE_ANIMATION;

	auto clipX = std::clamp<int32_t>(gridXFromTrack(gridTrackIndexFromTrack(getCurrentOutput(), gridTrackCount())), 0,
	                                 kDisplayWidth);
	auto clipY = std::clamp<int32_t>(gridYFromSection(getCurrentClip()->section), 0, kDisplayHeight);

	bool onKeyboardScreen =
	    ((clip->type == ClipType::INSTRUMENT) && ((InstrumentClip*)clip)->on_keyboard_screen_for_session());

	// when transitioning back to clip, if keyboard view is enabled, it takes precedent
	// over automation and instrument clip views.
	if (clip->on_automation_clip_view_for_session() && !onKeyboardScreen) {
		PadLEDs::explode_animation_y_origin_big_for_session() = clipY << 16;

		// Transition pre-render can happen before AutomationView::opened(). Force clip context so we don't
		// accidentally render stale arranger automation state into the animation store.
		automation_view_for_session().onArrangerView = false;
		automation_view_for_session().navSysId = automation_view_for_session().getNavSysId();
		automation_view_for_session().setAutomationParamType();

		if (clip->type == ClipType::INSTRUMENT) {
			instrument_clip_view_for_session().recalculateColours();
			// Automation grid explode still needs the instrument rows above and below the visible display.
			instrument_clip_view_for_session().fillOffScreenImageStores();
		}
		else {
			PadLEDs::clearTransitionStoreOffScreenRows();
		}

		automation_view_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
		                                             &PadLEDs::occupancy_mask_store_for_session()[1], false);
	}
	else if (clip->type == ClipType::AUDIO) {
		// If no sample, just skip directly there
		if (!((AudioClip*)clip)->sampleHolder.audioFile) {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audio_clip_view_for_session());
			return;
		}
		else {
			waveform_renderer_for_session().collapseAnimationToWhichRow = clipY;

			int64_t xScrollSamples;
			int64_t xZoomSamples;

			((AudioClip*)clip)
			    ->getScrollAndZoomInSamples(currentSong->x_scroll_for_session()[NAVIGATION_CLIP],
			                                currentSong->x_zoom_for_session()[NAVIGATION_CLIP], &xScrollSamples,
			                                &xZoomSamples);

			waveform_renderer_for_session().findPeaksPerCol((Sample*)((AudioClip*)clip)->sampleHolder.audioFile,
			                                                xScrollSamples, xZoomSamples,
			                                                &((AudioClip*)clip)->renderData);

			PadLEDs::setupAudioClipCollapseOrExplodeAnimation((AudioClip*)clip);
		}
	}
	else {
		PadLEDs::explode_animation_y_origin_big_for_session() = clipY << 16;

		// If going to KeyboardView...
		if (onKeyboardScreen) {
			keyboard_screen_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
			                                             &PadLEDs::occupancy_mask_store_for_session()[1]);
			memset(PadLEDs::occupancy_mask_store_for_session()[0], 0, kDisplayWidth + kSideBarWidth);
			memset(PadLEDs::occupancy_mask_store_for_session()[kDisplayHeight + 1], 0, kDisplayWidth + kSideBarWidth);
		}

		// Or if just regular old InstrumentClipView
		else {
			instrument_clip_view_for_session().recalculateColours();
			instrument_clip_view_for_session().renderMainPads(0xFFFFFFFF, &PadLEDs::image_store_for_session()[1],
			                                                  &PadLEDs::occupancy_mask_store_for_session()[1], false);
			instrument_clip_view_for_session().fillOffScreenImageStores();
		}
	}

	int32_t start = instrument_clip_view_for_session().getPosFromSquare(0);
	int32_t end = instrument_clip_view_for_session().getPosFromSquare(kDisplayWidth);

	PadLEDs::explode_animation_x_start_big_for_session() = clipX << 16;
	PadLEDs::explode_animation_x_width_big_for_session() = 1 << 16;

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
	PadLEDs::explode_animation_direction_for_session() = 1;

	if (clip->type == ClipType::AUDIO) {
		PadLEDs::renderAudioClipExplodeAnimation(0);
	}
	else {
		PadLEDs::renderExplodeAnimation(0);
	}

	PadLEDs::sendOutSidebarColours(); // They'll have been cleared by the first explode render

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_CLIP_VIEW);
}

const size_t SessionView::gridTrackCount() const {
	size_t count = 0;
	Output* currentTrack = currentSong->firstOutput;
	while (currentTrack != nullptr) {
		if (currentTrack->getActiveClip() != nullptr) {
			++count;
		}
		currentTrack = currentTrack->next;
	}

	return count;
}

uint32_t SessionView::gridClipCountForTrack(Output* track) {
	uint32_t count = 0;
	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == track) {
			++count;
		}
	}

	return count;
}

uint32_t SessionView::gridTrackIndexFromTrack(Output* track, uint32_t maxTrack) {
	if (maxTrack <= 0) {
		return -1;
	}

	uint32_t reverseOutputIndex = 0;
	for (Output* ptrOutput = currentSong->firstOutput; ptrOutput; ptrOutput = ptrOutput->next) {
		if (ptrOutput == track) {
			return ((maxTrack - 1) - reverseOutputIndex);
		}
		if (ptrOutput->getActiveClip() != nullptr) {
			++reverseOutputIndex;
		}
	}
	return -1;
}

Output* SessionView::gridTrackFromIndex(uint32_t trackIndex, uint32_t maxTrack) {
	uint32_t count = 0;
	Output* currentTrack = currentSong->firstOutput;
	while (currentTrack != nullptr) {
		if (currentTrack->getActiveClip() != nullptr) {
			if (((maxTrack - 1) - count) == trackIndex) {
				return currentTrack;
			}

			++count;
		}
		currentTrack = currentTrack->next;
	}

	return nullptr;
}

int32_t SessionView::gridYFromSection(uint32_t section) {
	int32_t result = (kGridHeight - 1) - section + currentSong->song_grid_scroll_y_for_session();
	if (result >= kGridHeight) {
		return -1;
	}

	return result;
}

int32_t SessionView::gridSectionFromY(uint32_t y) {
	int32_t result = ((kGridHeight - 1) - y) + currentSong->song_grid_scroll_y_for_session();
	if (result >= kMaxNumSections) {
		return -1;
	}

	return result;
}

int32_t SessionView::gridXFromTrack(uint32_t trackIndex) {
	int32_t result = trackIndex - currentSong->song_grid_scroll_x_for_session();
	if (result >= kDisplayWidth) {
		return -1;
	}

	return result;
}

int32_t SessionView::gridTrackIndexFromX(uint32_t x, uint32_t maxTrack) {
	if (maxTrack <= 0) {
		return 0;
	}
	int32_t result = x + currentSong->song_grid_scroll_x_for_session();
	if (result >= maxTrack) {
		return -1;
	}

	return result;
}

Output* SessionView::gridTrackFromX(uint32_t x, uint32_t maxTrack) {
	auto trackIndex = gridTrackIndexFromX(x, maxTrack);

	if (trackIndex < 0) {
		return nullptr;
	}

	return gridTrackFromIndex(trackIndex, maxTrack);
}

Clip* SessionView::gridClipFromCoords(uint32_t x, uint32_t y) {
	auto maxTrack = gridTrackCount();
	Output* track = gridTrackFromX(x, maxTrack);

	if (track == nullptr) {
		return nullptr;
	}

	auto section = gridSectionFromY(y);
	if (section == -1) {
		return nullptr;
	}

	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == track && clip->section == section) {
			return clip;
		}
	}

	return nullptr;
}

int32_t SessionView::gridClipIndexFromCoords(uint32_t x, uint32_t y) {
	auto maxTrack = gridTrackCount();
	Output* track = gridTrackFromX(x, maxTrack);

	if (track == nullptr) {
		return -1;
	}

	auto section = gridSectionFromY(y);
	if (section == -1) {
		return -1;
	}

	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == track && clip->section == section) {
			return idxClip;
		}
	}

	return -1;
}

Output* SessionView::getOutputFromPad(int32_t x, int32_t y) {
	if (currentSong->session_layout_for_session() == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridTrackFromX(x, gridTrackCount());
	}
	else {
		return getClipOnScreen(y)->output;
	}
	return nullptr;
}

Cartesian SessionView::gridXYFromClip(Clip& clip) {
	Output& track = *clip.output;
	size_t maxTrack = gridTrackCount();
	int32_t trackIndex = gridTrackIndexFromTrack(&track, maxTrack);
	return {gridXFromTrack(trackIndex), gridYFromSection(clip.section)};
}

// stop pulsing selected clip
// called when exiting session view
void SessionView::gridStopSelectedClipPulsing() {
	uiTimerManager.unsetTimer(TimerName::SELECTED_CLIP_PULSE);
	gridSelectedClipPulsing = false;
	selectedClipForPulsing = nullptr;
	gridResetSelectedClipPulseProgress();
}

// reset blend position for pulse
void SessionView::gridResetSelectedClipPulseProgress() {
	progress = kMinProgress;
	blendDirection = 1;
	gridSelectedClipRenderedColour = RGB::monochrome(0);
}

// render pulse for selected clip
void SessionView::gridSelectClipForPulsing(Clip& clip) {
	// save the selected clip so that we don't override the pulsed colour when re-rendering grid
	gridResetSelectedClipPulseProgress();
	selectedClipForPulsing = &clip;
}

// check if we should stop pulsing
bool SessionView::gridCheckForPulseStop() {
	// stop pulsing if...
	return (getCurrentUI() != this) // we're in another view
	       || (currentSong->session_layout_for_session()
	           == SessionLayoutType::SessionLayoutTypeRows) // we're in row view
	       || (currentUIMode == UI_MODE_EXPLODE_ANIMATION); // we're transiting from session view to another view
}

static const uint32_t pulseUIModes[] = {UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, UI_MODE_CREATING_CLIP, 0};

void SessionView::gridPulseSelectedClip() {
	auto setupTimer = util::finally([] {
		// set pulse timer so that uiTimerManager comes back here to continue the pulsing
		uiTimerManager.setTimer(TimerName::SELECTED_CLIP_PULSE, kPulseRate);
	});

	if (!gridSelectedClipPulsing) {
		selectedClipForPulsing = nullptr;
		gridResetSelectedClipPulseProgress();
		gridSelectedClipPulsing = true;
		return;
	}

	// check if we should stop pulsing
	if (gridCheckForPulseStop()) {
		gridStopSelectedClipPulsing();
		setupTimer.disable();
		return;
	}

	// is UI mode valid for pulsing?
	if (!isUIModeWithinRange(pulseUIModes)) {
		return;
	}

	// Try getting the current clip. If there isn't one, exit
	Clip* clip = getCurrentClip();
	if (clip == nullptr || clip->output == nullptr) {
		return;
	}

	Output& output = *clip->output;

	// if you're holding record or the clip is armed and flashing
	// or if the track color has not been set yet, then exit
	if (!output.colour || viewingRecordArmingActive
	    || (view_for_session().clipArmFlashOn && clip->armState != ArmState::OFF)) {
		return;
	}

	// get the X, Y coordinates of the current clip
	Cartesian pad = gridXYFromClip(*clip);
	// don't render pulse if clip is scrolled out of view (coords = - 1)
	if (pad.x < 0 && pad.y < 0) {
		return;
	}

	// save the selected clip so that we don't override the pulsed colour when re-rendering grid
	selectedClipForPulsing = clip;

	// get the clip's colour, which can be different from the track colour
	RGB clipColour = gridRenderClipColor(selectedClipForPulsing, pad.x, pad.y, false);

	// get the track colour so we can compare the clip colour to it
	// to see if clip colour is dimmer or not
	RGB trackColour = RGB::fromHue(output.colour);

	// get the blur color that we will be pulsing towards (from the clip colour)
	RGB blurColour = trackColour.forBlur();

	// if colours are equal, then clip pad is not dim
	int32_t maxProgressForBlur = (clipColour == trackColour) ? kMaxProgressFull : kMaxProgressDim;
	int32_t blendOffsetForBlur = (clipColour == trackColour) ? kBlendOffsetFull : kBlendOffsetDim;

	// adjust blend offset for direction (are we pulsing towards blur or clip colour)
	blendOffsetForBlur = blendDirection ? blendOffsetForBlur : -blendOffsetForBlur;

	// dim the clip colour we're pulsing from to increase the pulse range
	// red needs a greater brightness range than other colours
	int32_t brightnessAdjustment = trackColour == colours::red ? 4 : 3;
	clipColour = clipColour.adjust(255, brightnessAdjustment);

	// calculate progress (position in pulse slider)
	// ensuring that we do not go outside min/max of the blend range
	progress = std::clamp(progress + blendOffsetForBlur, kMinProgress, maxProgressForBlur);

	// save the colour to be rendered as this will be used if grid view needs to refresh
	gridSelectedClipRenderedColour = RGB::blend(clipColour, blurColour, 65536 - progress);

	// we pulse back and forth between min and max of pulse range
	// so if we reach either end of the range, we need to reverse the direction
	// we do that by multiplying the blendOffset by a negative blend direction
	if (progress == maxProgressForBlur || progress == kMinProgress) {
		blendDirection = !blendDirection;
	}

	// update grid image with new colour for the pulsed clip pad
	PadLEDs::set(pad, gridSelectedClipRenderedColour);

	// action the colour change / render the pulse on the grid
	PadLEDs::sendOutMainPadColours();
}
