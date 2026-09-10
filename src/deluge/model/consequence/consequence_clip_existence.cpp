/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_clip_existence.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_navigation_state.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_array.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "util/misc.h"

ConsequenceClipExistence::ConsequenceClipExistence(Clip* newClip, ClipArray* newClipArray,
                                                   ExistenceChangeType newType) {
	clip = newClip;
	clipArray = newClipArray;
	type = newType;
}

void ConsequenceClipExistence::prepareForDestruction(int32_t whichQueueActionIn, Song* song) {
	if (owns_detached_clip) {
		// Membership is authoritative even when a failed callback path did not
		// reconcile the consequence's cached ownership before cleanup.
		if (song && song->contains_clip_for_undo(clip)) {
			owns_detached_clip = false;
			return;
		}
		owns_detached_clip = false;
		song->deleteBackedUpParamManagersForClip(clip);

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO) {
			if (((AudioClip*)clip)->recorder) {
				FREEZE_WITH_ERROR("i002"); // Trying to diversify Qui's E278
			}
		}
#endif

		clip->~Clip();
		delugeDealloc(clip);
	}
}

bool ConsequenceClipExistence::can_recreate(Song* song) {
	if (!song || !owns_detached_clip || !clip
	    || (clipArray != &song->sessionClips && clipArray != &song->arrangementOnlyClips))
		return false;
	if (song->contains_clip_for_undo(clip)) {
		// The song owns this clip again. Failure cleanup must not destroy it.
		owns_detached_clip = false;
		return false;
	}
	return clipIndex >= 0 && clipIndex <= clipArray->getNumElements();
}

Error ConsequenceClipExistence::reserve_for_recreation(Song* song) {
	if (song != currentSong || !can_recreate(song))
		return Error::BUG;
	Clip* const target = clip;
	ClipArray* const array = clipArray;
	const int32_t index = clipIndex;
	using namespace deluge::gui::ui_session;
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	bool reserved = array->ensureEnoughSpaceAllocated(1);
	// Reconcile ownership before returning an allocation failure: a callback
	// may have returned the clip to the song even when reservation failed.
	if (currentSong != song || !can_recreate(song) || clip != target || clipArray != array || clipIndex != index
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision)
		return Error::BUG;
	return reserved ? Error::NONE : Error::INSUFFICIENT_RAM;
}

Error ConsequenceClipExistence::reattach_for_recreation(ModelStackWithTimelineCounter* modelStack) {
	if (!modelStack || modelStack->song != currentSong || !can_recreate(modelStack->song)
	    || modelStack->getTimelineCounterAllowNull() != clip)
		return Error::BUG;
	Song* const song = modelStack->song;
	Clip* const target = clip;
	ClipArray* const array = clipArray;
	const int32_t index = clipIndex;
	using namespace deluge::gui::ui_session;
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	Error error = target->undoDetachmentFromOutput(modelStack);
	// Reconcile ownership even on failure before history cleanup can destroy a
	// clip returned to the song by reattachment callbacks.
	if (currentSong != song || !can_recreate(song) || clip != target || clipArray != array || clipIndex != index
	    || modelStack->song != song || modelStack->getTimelineCounterAllowNull() != target
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision)
		return Error::BUG;
	return error;
}

Error ConsequenceClipExistence::commit_recreation(Song* song) {
	if (song != currentSong || !can_recreate(song))
		return Error::BUG;
	// Reservation happened before parameter reattachment. If that work consumed
	// the capacity, fail without allocating or relinquishing detached ownership.
	Error error = clipArray->insert_at_index_without_allocation(clipIndex);
	if (error != Error::NONE)
		return error;
	clipArray->setPointerAtIndex(clip, clipIndex);
	owns_detached_clip = false;
	return Error::NONE;
}

Error ConsequenceClipExistence::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !clip
	    || (clipArray != &modelStack->song->sessionClips && clipArray != &modelStack->song->arrangementOnlyClips))
		return Error::BUG;
	if (time == util::to_underlying(type) && modelStack->song->get_clip_index_for_undo(clipArray, clip) < 0)
		return Error::BUG;
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	if (time != util::to_underlying(type)) { // (Re-)create
		Error error = reserve_for_recreation(modelStack->song);
		if (error != Error::NONE)
			return error;

		error = reattach_for_recreation(modelStackWithTimelineCounter);
		if (error != Error::NONE) { // This shouldn't actually happen, but if it does...
			return error;           // Run away. This and the Clip(?) will get destructed, and everything should be ok!
		}

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO && !clip->paramManager.summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("PM27"); // was E419. Trying to diversify Leo's PM02 (was E410)
		}
#endif

		error = commit_recreation(modelStack->song);
		if (error != Error::NONE)
			return error;
		if (clipArray == &modelStackWithTimelineCounter->song->sessionClips) {
			modelStackWithTimelineCounter->song->notify_peer_clip_inserted(clipIndex);
		}

		clip->activeIfNoSolo = false;   // So we can toggle it back on, below
		clip->armState = ArmState::OFF; // In case was left on before

		if (shouldBeActiveWhileExistent && !(playbackHandler.playbackState && currentPlaybackMode == &arrangement)) {
			session.toggleClipStatus(clip, &clipIndex, true, 0);
			if (!clip->activeIfNoSolo) {
				D_PRINTLN("still not active!");
			}
		}

		if (!clip->output->getActiveClip()) {
			clip->output->setActiveClip(
			    modelStackWithTimelineCounter); // Must do this to avoid E170 error. If Instrument has no
			                                    // backedUpParamManager, it must have an activeClip
		}
	}

	else { // (Re-)delete

		// Make sure the currentClip isn't left pointing to this Clip. Most of the time, ActionLogger::revertAction()
		// reverts currentClip so we don't have to worry about it - but not if action->currentClip is NULL!
		modelStackWithTimelineCounter->song->invalidate_clip_selection(clip);

		clip->stopAllNotesPlaying(
		    modelStackWithTimelineCounter->song); // Stops any MIDI-controlled auditioning / stuck notes

		shouldBeActiveWhileExistent = session.deletingClipWhichCouldBeAbandonedOverdub(
		    clip); // But should we really be calling this without checking the Clip is a session one?

		clip->abortRecording();
		clip->armState = ArmState::OFF; // Not 100% sure if necessary... probably.

		clipIndex = modelStack->song->get_clip_index_for_undo(clipArray, clip);
		if (clipIndex == -1) {
			return Error::BUG;
		}

		if (clipArray == &modelStackWithTimelineCounter->song->sessionClips) {

			// Must unsolo the Clip before we delete it, in case its play-pos needs to be grabbed for another Clip - and
			// also so overall soloing may be cancelled if no others soloing
			if (clip->soloingInSessionMode) {
				session.unsoloClip(clip);
			}

			modelStackWithTimelineCounter->song->removeSessionClipLowLevel(clip, clipIndex);
			modelStackWithTimelineCounter->song->notify_peer_clip_removed(clipIndex);
		}
		else {
			clipArray->deleteAtIndex(clipIndex); // Deletes the array's pointer to the Clip - not the Clip itself.
		}

		owns_detached_clip = true;

		// This next call will back up all ParamManagers, including for Drums.
		// But it will (unusually) leave clip->output pointing to the Output, and the same for any NoteRows' Drums. So
		// that we can revert this stuff later.
		Output* oldOutput = clip->output;

		if (clip->isActiveOnOutput() && playbackHandler.isEitherClockActive()) {
			clip->expectNoFurtherTicks(modelStackWithTimelineCounter->song); // Still necessary? Probably.
		}

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO) {
			if (((AudioClip*)clip)->recorder) {
				FREEZE_WITH_ERROR("i003"); // Trying to diversify Qui's E278
			}
		}
#endif

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO && !clip->paramManager.summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("PM28"); // was E420. Trying to diversify Leo's PM02 (was E410)
		}
#endif

		clip->detachFromOutput(modelStackWithTimelineCounter, false, false, true);
		// modelStackWithTimelineCounter may not be used again after this!
		// ------------------------------------------------
		oldOutput->pickAnActiveClipIfPossible(modelStack); // Yup, we're required to call this after detachFromOutput().
	}

	return Error::NONE;
}
