/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "model/action/action_logger.h"
#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_clip_state.h"
#include "model/action/reversible_prefix.h"
#include "model/action/reversion_guard.h"
#include "model/action/session_history.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/consequence/consequence_clip_begin_linear_record.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_param_change.h"
#include "model/consequence/consequence_performance_view_press.h"
#include "model/consequence/consequence_swing_change.h"
#include "model/consequence/consequence_tempo_change.h"
#include "model/instrument/kit.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <new>
#include <string.h>

ActionLogger actionLogger{};

ActionLogger::ActionLogger() {
	firstAction[BEFORE] = nullptr;
	firstAction[AFTER] = nullptr;
}

void ActionLogger::deleteLastActionIfEmpty() {
	if (currentSong && firstAction[BEFORE] && firstAction[BEFORE]->captured_song == currentSong) {

		// There are probably more cases where we might want to do this, but I've only done it for recording so far
		// Paul: reinstating the original for now because it seems there are broken pointers in this list which lead to
		// crashes, we need to fix after release while (!firstAction[BEFORE]->firstConsequence) {
		if (firstAction[BEFORE]->type == ActionType::RECORD && !firstAction[BEFORE]->firstConsequence) {

			deleteLastAction();
		}
	}
}

void ActionLogger::deleteLastAction() {
	// Destruction can yield. Preserve an outer reversion guard when cleanup is nested.
	ReversionGuard cleanup(reversion_in_progress);
	Action* toDelete = firstAction[BEFORE];

	firstAction[BEFORE] = firstAction[BEFORE]->nextAction;

	toDelete->prepareForDestruction(BEFORE, currentSong);
	toDelete->~Action();
	delugeDealloc(toDelete);
}

Action* ActionLogger::getNewAction(ActionType newActionType, ActionAddition addToExistingIfPossible) {

	if (!currentSong) {
		return nullptr;
	}
	Song* const requested_song = currentSong;
	UI* const requested_ui = getCurrentUI();
	Clip* const requested_clip = currentSong->getCurrentClip();
	Output* const requested_output = requested_clip ? requested_clip->output : nullptr;
	const auto requested_owner = deluge::gui::ui_session::current();
	const auto requested_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
	auto request_context_unchanged = [&]() {
		return currentSong == requested_song && deluge::gui::ui_session::current() == requested_owner
		       && getCurrentUI() == requested_ui
		       && deluge::gui::ui_session::navigation.active().structural_refresh.revision() == requested_revision
		       && currentSong->getCurrentClip() == requested_clip
		       && (!requested_clip || requested_clip->output == requested_output);
	};
	deleteLog(AFTER);
	if (!request_context_unchanged()) {
		return nullptr;
	}

	// If not on a View, not allowed!
	// Exception for sound editor note editor UI which can edit notes on the grid
	// Exception for sound editor note row editor UI which can edit note rows on the grid
	// Exception for loadPatternUI which does edit note rows on the grid
	if ((getCurrentUI() != getRootUI())
	    && (!(getCurrentUI() == &sound_editor_for_session()
	          && (sound_editor_for_session().inNoteEditor() || sound_editor_for_session().inNoteRowEditor())))
	    && (getCurrentUI() != &load_pattern_ui_for_session())) {
		return nullptr;
	}

	Action* newAction;

	// If recording arrangement...
	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		return nullptr;

		// If there's no action for that, we're really screwed, we'd better get out
		if (!firstAction[BEFORE] || firstAction[BEFORE]->type != ActionType::ARRANGEMENT_RECORD) {
			return nullptr;
		}

		// Only a couple of kinds of new actions are allowed to add to that action
		if (newActionType == ActionType::SWING_CHANGE || newActionType == ActionType::TEMPO_CHANGE) {
			newAction = firstAction[BEFORE];
		}

		// Otherwise, not allowed
		else {
			return nullptr;
		}
	}

	// See if we can add to an existing action...
	else if (addToExistingIfPossible != ActionAddition::NOT_ALLOWED && firstAction[BEFORE]
	         && firstAction[BEFORE]->captured_song == requested_song
	         && firstAction[BEFORE]->navigation_owner == deluge::gui::ui_session::current()
	         && firstAction[BEFORE]->openForAdditions && firstAction[BEFORE]->type == newActionType
	         && firstAction[BEFORE]->view == getCurrentUI() && firstAction[BEFORE]->currentClip == requested_clip
	         && firstAction[BEFORE]->captured_output == requested_output
	         && (addToExistingIfPossible == ActionAddition::ALLOWED
	             || firstAction[BEFORE]->creationTime == AudioEngine::audioSampleTimer)) {
		newAction = firstAction[BEFORE];
	}

	// If we can't do that...
	else {

		deleteLastActionIfEmpty();
		if (!request_context_unchanged()) {
			return nullptr;
		}

		// Make sure we close off any existing action
		if (firstAction[BEFORE]) {
			firstAction[BEFORE]->openForAdditions = false;
		}

		// And make a new one
		Song* const song_before_allocation = currentSong;
		Action* const history_before_allocation = firstAction[BEFORE];
		const auto revision_before_allocation =
		    deluge::gui::ui_session::navigation.active().structural_refresh.revision();
		auto allocation_context_unchanged = [&]() {
			return request_context_unchanged() && currentSong == song_before_allocation
			       && firstAction[BEFORE] == history_before_allocation
			       && deluge::gui::ui_session::navigation.active().structural_refresh.revision()
			              == revision_before_allocation;
		};
		void* actionMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Action));

		if (!actionMemory) {
			D_PRINTLN("no ram to create new Action");
			return nullptr;
		}
		if (!allocation_context_unchanged()) {
			delugeDealloc(actionMemory);
			return nullptr;
		}

		// Store states of every Clip in existence
		int32_t numClips =
		    currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements();

		ActionClipState* clipStates = nullptr;
		if (numClips) {
			clipStates =
			    (ActionClipState*)GeneralMemoryAllocator::get().allocLowSpeed(numClips * sizeof(ActionClipState));
		}

		if (numClips && !clipStates) {
			delugeDealloc(actionMemory);
			return nullptr;
		}
		if (!allocation_context_unchanged()
		    || numClips
		           != currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements()) {
			if (clipStates) {
				delugeDealloc(clipStates);
			}
			delugeDealloc(actionMemory);
			return nullptr;
		}

		newAction = new (actionMemory) Action(newActionType);
		newAction->captured_song = requested_song;
		newAction->clipStates = clipStates;

		int32_t i = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			auto* state = new (&newAction->clipStates[i++]) ActionClipState{};
			state->grabFromClip(clip);
		}

		newAction->numClipStates = numClips;

		// Only now put the new action into the list of undo actions - because in the above steps, we may have decided
		// to delete it and get out (if we ran out of RAM while creating the ActionClipStates)
		newAction->nextAction = firstAction[BEFORE];
		firstAction[BEFORE] = newAction;

		// And fill out all the snapshot stuff that the Action captures at a song-wide level
		newAction->yScrollSongView[BEFORE] = currentSong->getYScrollSongViewWithoutPendingOverdubs();
		newAction->xScrollClip[BEFORE] = currentSong->x_scroll_for_session()[NAVIGATION_CLIP];
		newAction->xZoomClip[BEFORE] = currentSong->x_zoom_for_session()[NAVIGATION_CLIP];

		newAction->yScrollArranger[BEFORE] = currentSong->arrangement_y_scroll_for_session();
		newAction->xScrollArranger[BEFORE] = currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT];
		newAction->xZoomArranger[BEFORE] = currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT];

		newAction->modeNotes[BEFORE] = currentSong->key.modeNotes;

		newAction->tripletsOn = currentSong->triplets_on_for_session();
		newAction->tripletsLevel = currentSong->triplets_level_for_session();
		// newAction->modKnobModeSongView = currentSong->modKnobMode;
		newAction->affectEntireSongView = currentSong->affect_entire_for_session();

		newAction->view = getCurrentUI();
		newAction->currentClip = getCurrentClip();
		newAction->captured_output = requested_output;
	}

	updateAction(newAction);

	return newAction;
}

void ActionLogger::updateAction(Action* newAction) {
	if (!newAction || !currentSong || newAction->captured_song != currentSong) {
		return;
	}
	// A shared edit may be finalized by another panel or a service callback.
	// Its navigation snapshot still belongs to the panel that created it.
	deluge::gui::ui_session::Scope snapshot_owner(newAction->navigation_owner);
	// Update ActionClipStates for each Clip
	if (newAction->numClipStates) {

		// If number of Clips has changed, discard
		if (newAction->numClipStates
		    != currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements()) {
			ActionClipState* states_to_discard = newAction->clipStates;
			newAction->numClipStates = 0;
			newAction->clipStates = nullptr;
			delugeDealloc(states_to_discard);
			D_PRINTLN("discarded clip states");
		}

		else {
			// NOTE: i ranges over all clips, not just instrument clips
			int32_t i = 0;
			for (Clip* clip : AllClips::everywhere(currentSong)) {
				if (newAction->clipStates[i].matches(clip, clip->output) && clip->type == ClipType::INSTRUMENT) {
					newAction->clipStates[i].yScrollSessionView[AFTER] =
					    ((InstrumentClip*)clip)->y_scroll_for_session();
				}
				i++;
			}
		}
	}

	newAction->yScrollSongView[AFTER] = currentSong->getYScrollSongViewWithoutPendingOverdubs();
	newAction->xScrollClip[AFTER] = currentSong->x_scroll_for_session()[NAVIGATION_CLIP];
	newAction->xZoomClip[AFTER] = currentSong->x_zoom_for_session()[NAVIGATION_CLIP];

	newAction->yScrollArranger[AFTER] = currentSong->arrangement_y_scroll_for_session();
	newAction->xScrollArranger[AFTER] = currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT];
	newAction->xZoomArranger[AFTER] = currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT];

	newAction->modeNotes[AFTER] = currentSong->key.modeNotes;
}

void ActionLogger::recordUnautomatedParamChange(ModelStackWithAutoParam const* modelStack, ActionType actionType) {

	// If this is a param where you should not record automated param changes (e.g. tempo) then exit
	if (!modelStack->paramCollection->shouldRecordUnautomatedParamChange(modelStack)) {
		return;
	}

	Action* action = getNewAction(actionType, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
}

void ActionLogger::recordSwingChange(int8_t swingBefore, int8_t swingAfter) {

	Action* action = getNewAction(ActionType::SWING_CHANGE, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	// See if there's a previous one we can update
	if (action->firstConsequence) {
		ConsequenceSwingChange* consequence = (ConsequenceSwingChange*)action->firstConsequence;
		consequence->swing[AFTER] = swingAfter;
	}
	else {
		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceSwingChange));

		if (consMemory) {
			ConsequenceSwingChange* newConsequence = new (consMemory) ConsequenceSwingChange(swingBefore, swingAfter);
			action->addConsequence(newConsequence);
		}
	}
}

void ActionLogger::recordTempoChange(uint64_t timePerBigBefore, uint64_t timePerBigAfter) {

	Action* action = getNewAction(ActionType::TEMPO_CHANGE, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	// See if there's a previous one we can update
	if (action->firstConsequence) {
		ConsequenceTempoChange* consequence = (ConsequenceTempoChange*)action->firstConsequence;
		consequence->timePerBig[AFTER] = timePerBigAfter;
	}
	else {

		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceTempoChange));

		if (consMemory) {
			ConsequenceTempoChange* newConsequence =
			    new (consMemory) ConsequenceTempoChange(timePerBigBefore, timePerBigAfter);
			action->addConsequence(newConsequence);
		}
	}
}

/// Record Performance View Hold Press
void ActionLogger::recordPerformanceViewPress(FXColumnPress fxPressBefore[kDisplayWidth],
                                              FXColumnPress fxPressAfter[kDisplayWidth], int32_t xDisplay) {

	Action* action = getNewAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE, ActionAddition::ALLOWED);

	if (!action) {
		return;
	}

	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequencePerformanceViewPress));

	if (consMemory) {
		ConsequencePerformanceViewPress* newConsequence =
		    new (consMemory) ConsequencePerformanceViewPress(fxPressBefore, fxPressAfter, xDisplay);
		action->addConsequence(newConsequence);
	}
}

// Returns whether anything was reverted.
// doNavigation and updateVisually are only false when doing one of those undo-Clip-resize things as part of another
// Clip resize. You must not call this during the card routine - though I've lost track of the exact reason why not - is
// it just because we could then be in the middle of executing whichever function accessed the card and we don't know if
// things will break?
bool ActionLogger::revert(TimeType time, bool updateVisually, bool doNavigation) {
	if (!currentSong) {
		return false;
	}
	ReversionGuard reversion(reversion_in_progress);
	if (!reversion) {
		return false;
	}
	D_PRINTLN("ActionLogger::revert");

	Song* const song_before_cleanup = currentSong;
	UI* const ui_before_cleanup = getCurrentUI();
	const auto owner_before_cleanup = deluge::gui::ui_session::current();
	const auto revision_before_cleanup = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
	deleteLastActionIfEmpty();
	if (currentSong != song_before_cleanup || deluge::gui::ui_session::current() != owner_before_cleanup
	    || getCurrentUI() != ui_before_cleanup
	    || deluge::gui::ui_session::navigation.active().structural_refresh.revision() != revision_before_cleanup) {
		return false;
	}

	if (firstAction[time]) {
		Action* toRevert = firstAction[time];
		if (!currentSong || toRevert->captured_song != currentSong) {
			return false;
		}

		// If we're in a UI mode, and reverting this Action would mean changing UI, we have to disallow that.
		if (toRevert->view != getCurrentUI() && !isNoUIModeActive()) {
			return false;
		}
		// A navigated undo/redo ends the gesture. Implicit resize reversion keeps its
		// existing grouping semantics by passing doNavigation=false.
		if (doNavigation) {
			toRevert->openForAdditions = false;
		}

		firstAction[time] = firstAction[time]->nextAction;

		Error error = revertAction(toRevert, updateVisually, doNavigation, time);

		if (error != Error::NONE) {
			// A partially applied action has no valid undo/redo direction. Clip
			// consequences track their actual ownership independently of this queue.
			toRevert->prepareForDestruction(time, song_before_cleanup);
			toRevert->~Action();
			delugeDealloc(toRevert);
			changeRootUI(&session_view_for_session());
			return false;
		}
		toRevert->nextAction = firstAction[1 - time];
		firstAction[1 - time] = toRevert;
		return true;
	}

	return false;
}

enum class Animation {
	NONE,
	SCROLL,
	ZOOM,
	CLIP_MINDER_TO_SESSION,
	SESSION_TO_CLIP_MINDER,
	ENTER_KEYBOARD_VIEW,
	EXIT_KEYBOARD_VIEW,
	CHANGE_CLIP,
	CLIP_MINDER_TO_ARRANGEMENT,
	ARRANGEMENT_TO_CLIP_MINDER,
	SESSION_TO_ARRANGEMENT,
	ARRANGEMENT_TO_SESSION,
	ENTER_AUTOMATION_VIEW,
	EXIT_AUTOMATION_VIEW,
};

// doNavigation and updateVisually are only false when doing one of those undo-Clip-resize things as part of another
// Clip resize
Error ActionLogger::revertAction(Action* action, bool updateVisually, bool doNavigation, TimeType time) {
	const bool foreign_navigation = action->navigation_owner != deluge::gui::ui_session::current();
	// Scale restoration is shared model state even when the UI snapshot belongs
	// to another panel. Preserve the existing doNavigation=false resize behavior.
	const bool restore_scale = doNavigation;
	if (foreign_navigation) {
		doNavigation = false;
		updateVisually = false;
	}

	currentSong->deletePendingOverdubs();

	Animation whichAnimation = Animation::NONE;
	uint32_t songZoomBeforeTransition = currentSong->x_zoom_for_session()[NAVIGATION_CLIP];
	uint32_t arrangerZoomBeforeTransition = currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT];

	if (doNavigation) {

		// If it's an arrangement record action...
		if (action->type == ActionType::ARRANGEMENT_RECORD) {

			// If user is in song view or arranger view, just stay in that UI.
			if (getCurrentUI() == &arranger_view_for_session() || getCurrentUI() == &session_view_for_session()) {
				action->view = getCurrentUI();

				// If in arranger view, don't go scrolling anywhere - that'd just visually confuse things
				if (getCurrentUI() == &arranger_view_for_session()) {
					action->xScrollArranger[time] = currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT];
				}
			}
		}

		// We only want to display one animation

		if (updateVisually) {

			// Switching between session / arranger
			if (action->view == &session_view_for_session() && getCurrentUI() == &arranger_view_for_session()) {
				whichAnimation = Animation::ARRANGEMENT_TO_SESSION;
			}
			else if (action->view == &arranger_view_for_session() && getCurrentUI() == &session_view_for_session()) {
				whichAnimation = Animation::SESSION_TO_ARRANGEMENT;
			}

			// Switching between session and clip view
			else if (action->view == &session_view_for_session() && getCurrentUI()->toClipMinder()) {
				whichAnimation = Animation::CLIP_MINDER_TO_SESSION;
			}
			else if (action->view->toClipMinder() && getCurrentUI() == &session_view_for_session()) {
				whichAnimation = Animation::SESSION_TO_CLIP_MINDER;
			}

			// Entering / exiting arranger
			else if (action->view == &arranger_view_for_session() && getCurrentUI()->toClipMinder()) {
				whichAnimation = Animation::CLIP_MINDER_TO_ARRANGEMENT;
			}
			else if (action->view->toClipMinder() && getCurrentUI() == &arranger_view_for_session()) {
				whichAnimation = Animation::ARRANGEMENT_TO_CLIP_MINDER;
			}

			// Then entering or exiting keyboard view
			else if (action->view == &keyboard_screen_for_session()
			         && getCurrentUI() != &keyboard_screen_for_session()) {
				whichAnimation = Animation::ENTER_KEYBOARD_VIEW;
			}
			else if (action->view != &keyboard_screen_for_session()
			         && getCurrentUI() == &keyboard_screen_for_session()) {
				whichAnimation = Animation::EXIT_KEYBOARD_VIEW;
			}

			// Then entering or exiting automation view
			else if (action->view == &automation_view_for_session()
			         && getCurrentUI() != &automation_view_for_session()) {
				whichAnimation = Animation::ENTER_AUTOMATION_VIEW;
			}
			else if (action->view != &automation_view_for_session()
			         && getCurrentUI() == &automation_view_for_session()) {
				whichAnimation = Animation::EXIT_AUTOMATION_VIEW;
			}

			// Or if we've changed Clip but ended up back in the same view...
			else if (getCurrentUI()->toClipMinder() && getCurrentClip() != action->currentClip) {
				whichAnimation = Animation::CHANGE_CLIP;
			}

			// Or if none of those is happening, we might like to do a horizontal zoom or scroll - only if [vertical
			// scroll isn't changed], and we're not on keyboard view
			else {
				if (getCurrentUI() != &keyboard_screen_for_session()) {

					if (getCurrentUI() == &arranger_view_for_session()) {
						if (currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT] != action->xZoomArranger[time]) {
							whichAnimation = Animation::ZOOM;
						}

						else if (currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT]
						         != action->xScrollArranger[time]) {
							whichAnimation = Animation::SCROLL;
						}
					}

					else {
						if (currentSong->x_zoom_for_session()[NAVIGATION_CLIP] != action->xZoomClip[time]) {
							whichAnimation = Animation::ZOOM;
						}

						else if (currentSong->x_scroll_for_session()[NAVIGATION_CLIP] != action->xScrollClip[time]) {
							whichAnimation = Animation::SCROLL;
						}
					}
				}
			}
		}

		// Change some stuff that'll need to get changed in any case
		currentSong->x_zoom_for_session()[NAVIGATION_CLIP] = action->xZoomClip[time];
		currentSong->x_zoom_for_session()[NAVIGATION_ARRANGEMENT] = action->xZoomArranger[time];

		// Restore states of each Clip
		if (action->numClipStates) {
			int32_t totalNumClips =
			    currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements();
			if (action->numClipStates == totalNumClips) {

				// NOTE: i ranges over all clips, not just instrument clips
				int32_t i = 0;

				for (Clip* clip : AllClips::everywhere(currentSong)) {
					if (!action->clipStates[i].matches(clip, clip->output)) {
						++i;
						continue;
					}
					// clip->modKnobMode = action->clipStates[i].modKnobMode;
					if (clip->type == ClipType::INSTRUMENT) {
						InstrumentClip* instrumentClip = (InstrumentClip*)clip;
						instrumentClip->y_scroll_for_session() = action->clipStates[i].yScrollSessionView[time];
						instrumentClip->affect_entire_for_session() = action->clipStates[i].affectEntire;
						instrumentClip->wrap_editing_for_session() = action->clipStates[i].wrapEditing;
						instrumentClip->wrap_edit_level_for_session() = action->clipStates[i].wrapEditLevel;

						if (clip->output->type == OutputType::KIT) {
							Kit* kit = (Kit*)clip->output;
							Drum* currentSelectedDrum = kit->selected_drum_for_session();
							Drum* saved_drum = action->clipStates[i].selected_drum_identity;
							kit->selected_drum_for_session() =
							    saved_drum && kit->getDrumIndex(saved_drum) >= 0 ? saved_drum : nullptr;
							// if affect entire is disabled and we've updated drum selection
							// need to update the mod controllable context that the gold knobs are editing
							if (!instrumentClip->affect_entire_for_session()
							    && currentSelectedDrum != kit->selected_drum_for_session()) {
								view_for_session().setActiveModControllableTimelineCounter(instrumentClip);
							}
						}
					}

					i++;
				}
			}
			else {
				D_PRINTLN("clip states wrong number so not restoring");
			}
		}

		// Vertical scroll
		currentSong->song_view_y_scroll_for_session() = action->yScrollSongView[time];
		currentSong->arrangement_y_scroll_for_session() = action->yScrollArranger[time];

		// Restore the scale before same-panel navigation animations, as before.
		currentSong->key.modeNotes = action->modeNotes[time];

		// Other stuff
		// currentSong->modKnobMode = action->modKnobModeSongView;
		currentSong->affect_entire_for_session() = action->affectEntireSongView;
		currentSong->triplets_on_for_session() = action->tripletsOn;
		currentSong->triplets_level_for_session() = action->tripletsLevel;

		// Now do the animation we decided on - for animations which we prefer to set up before reverting the actual
		// action
		if (whichAnimation == Animation::SCROLL && getCurrentUI() == &arranger_view_for_session()) {
			bool worthDoingAnimation = arranger_view_for_session().initiateXScroll(action->xScrollArranger[time]);
			if (!worthDoingAnimation) {
				whichAnimation = Animation::NONE;
				goto otherOption;
			}
		}
		else {
otherOption:
			if (getCurrentUI() != &arranger_view_for_session() || whichAnimation != Animation::ZOOM) {
				currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT] =
				    action->xScrollArranger[time]; // Have to do this if we didn't do the actual scroll animation yet
				                                   // some scrolling happened
			}
		}

		if (whichAnimation == Animation::SCROLL && getCurrentUI() != &arranger_view_for_session()) {
			((TimelineView*)getCurrentUI())->initiateXScroll(action->xScrollClip[time]);
		}
		else if (getCurrentUI() == &arranger_view_for_session() || whichAnimation != Animation::ZOOM) {
			currentSong->x_scroll_for_session()[NAVIGATION_CLIP] =
			    action->xScrollClip[time]; // Have to do this if we didn't do the actual scroll animation yet some
			                               // scrolling happened
		}

		if (whichAnimation == Animation::ZOOM) {
			if (getCurrentUI() == &arranger_view_for_session()) {
				arranger_view_for_session().initiateXZoom(
				    howMuchMoreMagnitude(action->xZoomArranger[time], arrangerZoomBeforeTransition),
				    action->xScrollArranger[time], arrangerZoomBeforeTransition);
			}
			else {
				((TimelineView*)getCurrentUI())
				    ->initiateXZoom(howMuchMoreMagnitude(action->xZoomClip[time], songZoomBeforeTransition),
				                    action->xScrollClip[time], songZoomBeforeTransition);
			}
		}

		else if (whichAnimation == Animation::CLIP_MINDER_TO_SESSION) {
			session_view_for_session().transitionToSessionView();
		}

		else if (whichAnimation == Animation::SESSION_TO_CLIP_MINDER) {
			session_view_for_session().transitionToViewForClip(action->currentClip);
			goto currentClipSwitchedOver; // Skip the below - our call to transitionToViewForClip will switch it over
			                              // for us
		}

		// Swap currentClip over. Can only do this after calling transitionToSessionView(). Previously, we did this much
		// earlier, causing a crash. Hopefully moving it later here is ok...
		if (action->currentClip) { // If song just loaded and we hadn't been into ClipMinder yet, this would be NULL,
			                       // and we don't want to set currentSong->currentClip back to this
			currentSong->setCurrentClip(action->currentClip);
		}
	}

currentClipSwitchedOver:

	if (foreign_navigation && restore_scale) {
		currentSong->key.modeNotes = action->modeNotes[time];
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	Error error = action->revert(time, modelStack);
	if (error != Error::NONE) {
		// Do not animate or dereference the requested destination after a failed
		// structural reversion. It may never have been restored to the song.
		deluge::gui::ui_session::navigation.active().structural_refresh.request();
		deluge::gui::ui_session::request_peer_structural_refresh();
		currentUIMode = 0;
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		setRootUILowLevel(&session_view_for_session());
		view_for_session().setActiveModControllableTimelineCounter(currentSong);
		display->displayError(error);
		deleteAllLogs();
		if (playbackHandler.isEitherClockActive()) {
			currentPlaybackMode->reversionDone();
		}
		return error;
	}

	if (foreign_navigation) {
		// Do not render retained editor targets immediately after shared deletion.
		deluge::gui::ui_session::navigation.active().structural_refresh.request();
		deluge::gui::ui_session::request_peer_structural_refresh();
	}

	// Some "animations", we prefer to do after we've reverted the action
	if (whichAnimation == Animation::ENTER_KEYBOARD_VIEW) {
		changeRootUI(&keyboard_screen_for_session());
	}

	else if (whichAnimation == Animation::EXIT_KEYBOARD_VIEW) {

		if (getCurrentClip()->on_automation_clip_view_for_session()) {
			changeRootUI(&automation_view_for_session());
		}
		else {
			changeRootUI(&instrument_clip_view_for_session());
		}
	}

	else if (whichAnimation == Animation::ENTER_AUTOMATION_VIEW) {
		changeRootUI(&automation_view_for_session());
	}

	else if (whichAnimation == Animation::EXIT_AUTOMATION_VIEW) {
		automation_view_for_session().resetShortcutBlinking();
		if (getCurrentClip()->type == ClipType::INSTRUMENT) {
			changeRootUI(&instrument_clip_view_for_session());
		}
		else {
			changeRootUI(&audio_clip_view_for_session());
		}
	}

	else if (whichAnimation == Animation::CHANGE_CLIP) {
		if (action->view != getCurrentUI()) {
			changeRootUI(action->view);
		}
		else {
			getCurrentUI()->focusRegained();
			renderingNeededRegardlessOfUI(); // Didn't have this til March 2020, and stuff didn't update. Guess this is
			                                 // just needed? Can't remember specifics just now
		}
	}

	else if (whichAnimation == Animation::CLIP_MINDER_TO_ARRANGEMENT) {
		changeRootUI(&arranger_view_for_session());
	}

	else if (whichAnimation == Animation::ARRANGEMENT_TO_CLIP_MINDER) {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			changeRootUI(&audio_clip_view_for_session());
		}
		else if (getCurrentInstrumentClip()->on_keyboard_screen_for_session()) {
			changeRootUI(&keyboard_screen_for_session());
		}
		else if (getCurrentClip()->on_automation_clip_view_for_session()) {
			changeRootUI(&automation_view_for_session());
		}
		else {
			changeRootUI(&instrument_clip_view_for_session());
		}
	}

	else if (whichAnimation == Animation::SESSION_TO_ARRANGEMENT) {
		changeRootUI(&arranger_view_for_session());
	}

	else if (whichAnimation == Animation::ARRANGEMENT_TO_SESSION) {
		changeRootUI(&session_view_for_session());
	}

	if (updateVisually) {
		UI* currentUI = getCurrentUI();

		if (currentUI == &instrument_clip_view_for_session()) {
			// If we're not animating away from this view (but something like scrolling sideways would be allowed)
			if (whichAnimation != Animation::CLIP_MINDER_TO_SESSION
			    && whichAnimation != Animation::CLIP_MINDER_TO_ARRANGEMENT) {
				instrument_clip_view_for_session().recalculateColours();
				if (whichAnimation == Animation::NONE) {
					uiNeedsRendering(currentUI);
				}
			}
		}
		else if (currentUI == &automation_view_for_session()) {
			// If we're not animating away from this view (but something like scrolling sideways would be allowed)
			if (whichAnimation != Animation::CLIP_MINDER_TO_SESSION
			    && whichAnimation != Animation::CLIP_MINDER_TO_ARRANGEMENT) {
				if (getCurrentClip()->type == ClipType::INSTRUMENT) {
					instrument_clip_view_for_session().recalculateColours();
				}
				if (whichAnimation == Animation::NONE) {
					uiNeedsRendering(currentUI);
				}
			}
		}
		else if (currentUI == &audio_clip_view_for_session()) {
			if (whichAnimation == Animation::NONE) {
				uiNeedsRendering(currentUI);
			}
		}
		else if (currentUI == &keyboard_screen_for_session()) {
			if (whichAnimation != Animation::ENTER_KEYBOARD_VIEW) {
				uiNeedsRendering(currentUI, 0xFFFFFFFF, 0);
			}
		}
		// Got to try this even if we're supposedly doing a horizontal scroll animation or something cos that may have
		// failed if the Clip wasn't long enough before we did the action->revert() ...
		else if (currentUI == &session_view_for_session()) {
			uiNeedsRendering(currentUI, 0xFFFFFFFF, 0xFFFFFFFF);
		}
		else if (currentUI == &arranger_view_for_session()) {
			arranger_view_for_session().repopulateOutputsOnScreen(whichAnimation == Animation::NONE);
		}

		// Usually need to re-display the mod LEDs etc, but not if either of these animations is happening, which means
		// that it'll happen anyway when the animation finishes - and also, if we just deleted the Clip which was the
		// activeModControllableClip, well that'll temporarily be pointing to invalid stuff. Check the actual UI mode
		// rather than the whichAnimation variable we've been using in this function, because under some circumstances
		// that'll bypass the actual animation / UI-mode. We would also put the "explode" animation for transitioning
		// *to* arranger here, but it just doesn't get used during reversion.
		if (!isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING) && !isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)
		    && !isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
			view_for_session().setKnobIndicatorLevels();
			view_for_session().setModLedStates();
		}

		// So long as we're not gonna animate to different UI...
		switch (whichAnimation) {
		case Animation::CLIP_MINDER_TO_SESSION:
		case Animation::SESSION_TO_CLIP_MINDER:
		case Animation::CLIP_MINDER_TO_ARRANGEMENT:
		case Animation::ARRANGEMENT_TO_CLIP_MINDER:
			break;

		default:
			ClipMinder* clipMinder = getCurrentUI()->toClipMinder();
			if (clipMinder) {
				if (getCurrentClip()->type == ClipType::INSTRUMENT) {
					((InstrumentClipMinder*)clipMinder)->setLedStates();
				}
			}
			else if (getCurrentUI() == &session_view_for_session()) {
				session_view_for_session().setLedStates();
			}
			if (auto* timelineView = getCurrentUI()->toTimelineView()) {
				timelineView->setTripletsLEDState();
			}
		}
	}

	if (playbackHandler.isEitherClockActive()) {
		currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
	}

	return error;
}

void ActionLogger::close_recording_action() {
	// Recording boundaries belong to shared playback, regardless of the initiating panel.
	if (currentSong && firstAction[BEFORE] && firstAction[BEFORE]->captured_song == currentSong
	    && firstAction[BEFORE]->type == ActionType::RECORD) {
		firstAction[BEFORE]->openForAdditions = false;
	}
}

void ActionLogger::closeAction(ActionType actionType) {
	auto* action = newest_action_for_panel(firstAction[BEFORE], deluge::gui::ui_session::current());
	if (currentSong && action && action->captured_song == currentSong && action->type == actionType) {
		action->openForAdditions = false;
	}
}

void ActionLogger::closeActionUnlessCreatedJustNow(ActionType actionType) {
	auto* action = newest_action_for_panel(firstAction[BEFORE], deluge::gui::ui_session::current());
	if (currentSong && action && action->captured_song == currentSong && action->type == actionType
	    && action->creationTime != AudioEngine::audioSampleTimer) {
		action->openForAdditions = false;
	}
}

void ActionLogger::deleteAllLogs() {
	ReversionGuard cleanup(reversion_in_progress);
	// Deferred undo/redo belongs to the history being discarded, including on song load.
	auto deferred_commands = playbackHandler.suspend_pending_global_m_id_i_commands();
	deleteLog(BEFORE);
	deleteLog(AFTER);
}

void ActionLogger::deleteLog(int32_t time) {
	// Direct callers also need to exclude undo/redo during consequence destruction.
	ReversionGuard cleanup(reversion_in_progress);
	while (firstAction[time]) {
		Action* toDelete = firstAction[time];

		firstAction[time] = firstAction[time]->nextAction;

		toDelete->prepareForDestruction(time, currentSong);
		toDelete->~Action();
		delugeDealloc(toDelete);
	}
}

// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just
// because we could then be in the middle of executing whichever function accessed the card and we don't know if things
// will break?
void ActionLogger::undo() {
	if (reversion_in_progress || !currentSong) {
		return;
	}

	// Before we go and revert the most recent Action, there are a few recording-related states we first want to have a
	// go at cancelling out of. These are treated as special cases here rather than being Consequences because they're
	// never redoable: their "undoing" is a special case of cancellation.

	// But, this is to be done very sparingly! I used to have more of these which did things like deleting Clips for
	// which linear recording was ongoing. But then what if other Consequences, e.g. param automation, had been recorded
	// for those? Reverting those would call functions on invalid pointers. So instead, do just use regular Actions and
	// Consequences for everything possible. And definitely don't delete any Clips here.

	Song* const song_before_prelude = currentSong;
	UI* const ui_before_prelude = getCurrentUI();
	const auto owner_before_prelude = deluge::gui::ui_session::current();
	auto context_unchanged = [&]() {
		return currentSong == song_before_prelude && getCurrentUI() == ui_before_prelude
		       && deluge::gui::ui_session::current() == owner_before_prelude;
	};
	{
		ReversionGuard prelude(reversion_in_progress);
		// If currently recording an arrangement from session, we have to stop doing so first
		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			playbackHandler.recording = RecordingMode::OFF;
			currentSong->resumeClipsClonedForArrangementRecording();
			if (!context_unchanged()) {
				return;
			}

			view_for_session().setModLedStates(); // Set song LED back
			playbackHandler.setLedStates();
		}

		// Or if recording tempoless, gotta stop that
		else if (playbackHandler.playbackState && !playbackHandler.isEitherClockActive()) {
			playbackHandler.endPlayback();
			if (!context_unchanged()) {
				return;
			}
			goto displayUndoMessage;
		}

		// Or if recording linearly to arrangement, gotta exit that mode
		else if (playbackHandler.playbackState && playbackHandler.recording != RecordingMode::OFF
		         && currentPlaybackMode == &arrangement) {
			arrangement.endAnyLinearRecording();
		}
	}
	if (!context_unchanged()) {
		return;
	}

	// Ok, do the actual undo.
	if (revert(BEFORE)) {
displayUndoMessage:
#ifdef undoLedX
		indicator_leds::indicateAlertOnLed(undoLedX, undoLedY);
#else
		display->consoleText("Undo");
#endif
	}
}

// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just
// because we could then be in the middle of executing whichever function accessed the card and we don't know if things
// will break?
void ActionLogger::redo() {
	if (revert(AFTER)) {
#ifdef redoLedX
		indicator_leds::indicateAlertOnLed(redoLedX, redoLedY);
#else
		display->consoleText("Redo");
#endif
	}
}

const uint32_t reversionUIModes[] = {
    UI_MODE_AUDITIONING,
    UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
    UI_MODE_CLIP_PRESSED_IN_SONG_VIEW,
    UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
    0,
};

bool ActionLogger::allowedToDoReversion() {
	return (!reversion_in_progress && currentSong && getCurrentUI() == getRootUI()
	        && isUIModeWithinRange(reversionUIModes));
}

void ActionLogger::notifyClipRecordingAborted(Clip* clip) {

	// If there's an Action which only recorded the beginning of this Clip recording, we don't want it anymore.
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == ActionType::RECORD) {
		Consequence* firstConsequence = firstAction[BEFORE]->firstConsequence;
		if (!firstConsequence->next && firstConsequence->type == Consequence::CLIP_BEGIN_LINEAR_RECORD) {
			if (clip == ((ConsequenceClipBeginLinearRecord*)firstConsequence)->clip) {
				deleteLastAction();
			}
		}
	}
}

// This function relies on Consequences having been sequentially added for each subsequent "mini action", so looking at
// the noteRowId of the most recent one, we can then know that all further Consequences until we see the same noteRowId
// again are part of the same "mini action". This will get called in some cases (Action types) were only one NoteRow,
// not many, could have had the editing done to it: that's fine too, and barely any time is really wasted here. Returns
// whether whole Action was reverted, which is the only case where visual updating / rendering, and also the calling of
// expectEvent(), would have taken place
PartialUndoResult ActionLogger::undoJustOneConsequencePerNoteRow(ModelStack* modelStack) {
	if (reversion_in_progress || !modelStack || modelStack->song != currentSong || !currentSong || !firstAction[BEFORE]
	    || firstAction[BEFORE]->captured_song != currentSong
	    || firstAction[BEFORE]->navigation_owner != deluge::gui::ui_session::current()) {
		return PartialUndoResult::FAILED;
	}
	Clip* selected_clip = currentSong->getCurrentClip();
	if (!selected_clip || selected_clip->type != ClipType::INSTRUMENT
	    || firstAction[BEFORE]->currentClip != selected_clip
	    || firstAction[BEFORE]->captured_output != selected_clip->output) {
		return PartialUndoResult::FAILED;
	}

	bool revertedWholeAction = false;

	Consequence* firstConsequence = firstAction[BEFORE]->firstConsequence;
	// This shortcut requires a note-array change at the head; other histories cannot
	// supply a note-row identity and must not be cast as one.
	if (!firstConsequence || firstConsequence->type != Consequence::NOTE_ARRAY_CHANGE) {
		return PartialUndoResult::FAILED;
	}
	// Validate note-array targets before reverting any part of this gesture. Compare
	// clip identity before dereferencing a consequence's retained clip pointer.
	for (auto* consequence = firstConsequence; consequence; consequence = consequence->next) {
		if (consequence->type == Consequence::NOTE_ARRAY_CHANGE) {
			auto* notes = static_cast<ConsequenceNoteArrayChange*>(consequence);
			if (notes->clip != selected_clip
			    || !static_cast<InstrumentClip*>(selected_clip)->getNoteRowFromId(notes->noteRowId)) {
				return PartialUndoResult::FAILED;
			}
		}
	}
	if (firstConsequence) { // Should always be true

		// Work out if multiple Consequences per NoteRow (see big comment above)
		int32_t firstNoteRowId = ((ConsequenceNoteArrayChange*)firstConsequence)->noteRowId;

		Consequence* thisConsequence = firstConsequence->next;
		while (thisConsequence) {
			if (thisConsequence->type == Consequence::NOTE_ARRAY_CHANGE
			    && ((ConsequenceNoteArrayChange*)thisConsequence)->noteRowId == firstNoteRowId) {
				goto gotMultipleConsequencesPerNoteRow;
			}

			thisConsequence = thisConsequence->next;
		}

		// If multiple Consequences per NoteRow, just revert most recent one per NoteRow
		if (false) {
gotMultipleConsequencesPerNoteRow:
			ReversionGuard reversion(reversion_in_progress);
			Song* const song_before_reversion = currentSong;
			Action* const action_before_reversion = firstAction[BEFORE];
			Output* const output_before_reversion = selected_clip->output;
			UI* const ui_before_reversion = getCurrentUI();
			const auto owner_before_reversion = deluge::gui::ui_session::current();
			const auto revision_before_reversion =
			    deluge::gui::ui_session::navigation.active().structural_refresh.revision();
			auto target_unchanged = [&]() {
				return currentSong == song_before_reversion && firstAction[BEFORE] == action_before_reversion
				       && deluge::gui::ui_session::current() == owner_before_reversion
				       && getCurrentUI() == ui_before_reversion
				       && deluge::gui::ui_session::navigation.active().structural_refresh.revision()
				              == revision_before_reversion
				       && currentSong->getCurrentClip() == selected_clip
				       && selected_clip->output == output_before_reversion;
			};
			// Only note-array swaps are safe in this allocation-free partial undo.
			// Mixed consequence prefixes can allocate or yield; leave them intact.
			for (auto* cons = firstConsequence; cons != thisConsequence; cons = cons->next) {
				if (!cons || cons->type != Consequence::NOTE_ARRAY_CHANGE) {
					return PartialUndoResult::FAILED;
				}
			}

			// All row identities were validated above. Note-array reversion only
			// swaps existing storage and cannot yield. Complete the entire prefix
			// before freeing any snapshots (destruction can service callbacks).
			Consequence* retired = nullptr;
			Error swap_error = Error::NONE;
			auto result = apply_reversible_prefix(
			    action_before_reversion->firstConsequence, thisConsequence, retired,
			    [&](Consequence& cons) {
				    swap_error = cons.revert(BEFORE, modelStack);
				    return swap_error == Error::NONE;
			    },
			    [&](Consequence& cons) {
				    Error rollback_error = cons.revert(AFTER, modelStack);
				    if (rollback_error != Error::NONE)
					    swap_error = rollback_error;
				    return rollback_error == Error::NONE;
			    });
			if (result != PrefixResult::APPLIED) {
				if (result == PrefixResult::ROLLBACK_FAILED)
					deleteAllLogs();
				display->displayError(swap_error);
				return PartialUndoResult::FAILED;
			}

			// The history no longer exposes anything being destroyed below.
			while (retired) {
				auto* toDelete = retired;
				retired = retired->next;
				toDelete->~Consequence();
				delugeDealloc(toDelete);
			}
			if (!target_unchanged()) {
				return PartialUndoResult::FAILED;
			}

			D_PRINTLN("did secret undo, just one Consequence");
		}

		// Or if only one Consequence (per NoteRow), revert whole Action
		else {
			revertedWholeAction = revert(BEFORE, true, false);
			if (!revertedWholeAction) {
				return PartialUndoResult::FAILED;
			}
			D_PRINTLN("did secret undo, whole Action");
		}

		deleteLog(AFTER);
	}

	return revertedWholeAction ? PartialUndoResult::WHOLE_ACTION : PartialUndoResult::PARTIAL;
}
