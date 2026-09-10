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

#include "playback/mode/arrangement.h"
#include "definitions_cxx.hpp"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/views/arranger_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"

extern "C" {}

Arrangement arrangement{};

Arrangement::Arrangement() {
}

// Call this *before* resetPlayPos
void Arrangement::setupPlayback() {

	currentSong->setParamsInAutomationMode(true);

	// Rohan: It seems strange that I ever put this here. It's now also in PlaybackHandler::setupPlayback,
	// so maybe extra unneeded here - but that's not the only place that calls us, so have left it in for now
	playbackHandler.swungTicksTilNextEvent = 0;

	for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		clip->wasActiveBefore = clip->activeIfNoSolo;

		clip->activeIfNoSolo = false;
		clip->soloingInSessionMode = false;
	}

	currentSong->anyClipsSoloing = false; // Got to set this, since we just set them all to not-soloing, above

	// Got to deactiveate all arrangement-only Clips too! Especially cos some of them might be muted or something (this
	// made stuff look very buggy!) For each Clip in arrangement
	for (int32_t c = 0; c < currentSong->arrangementOnlyClips.getNumElements(); c++) {
		Clip* clip = currentSong->arrangementOnlyClips.getClipAtIndex(c);
		clip->activeIfNoSolo = false;
	}
}

// Returns whether to do an instant song swap
bool Arrangement::endPlayback() {
	uint32_t timeRemainder;
	int32_t actualPos = getLivePos(&timeRemainder);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	currentSong->paramManager.expectNoFurtherTicks(modelStack);

	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (!currentSong->isOutputActiveInArrangement(output)) {
			continue;
		}

		output->endArrangementPlayback(currentSong, actualPos, timeRemainder);
	}

	currentSong->restoreClipStatesBeforeArrangementPlay();

	// Some Clips might have been reset to "disabled", so need their mute-square redrawn if we're actually viewing the
	// session view

	// use root UI in case this is called from performance view
	session_view_for_session().requestRendering(getRootUI(), 0, 0xFFFFFFFF);

	// Work-around. Our caller, PlaybackHandler::endPlayback(), sets this next anyway, and can't do it earlier,
	// but we need it before reassessing sessionView's greyout.
	playbackHandler.playbackState = 0;

	if (getCurrentUI() == &session_view_for_session()) {
		PadLEDs::reassessGreyout();
	}

	return false; // No song swap
}

void Arrangement::doTickForward(int32_t posIncrement) {

	bool anyChangeToSessionClipsPlaying = false;

	lastProcessedPos += posIncrement;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool songParamManagerMightContainAutomation = currentSong->paramManager.mightContainAutomation();

	if (songParamManagerMightContainAutomation) {
		ModelStackWithThreeMainThings* modelStackStackWithThreeMainThing =
		    modelStack->addTimelineCounter(modelStack->song)
		        ->addOtherTwoThingsButNoNoteRow(&modelStack->song->globalEffectable, &modelStack->song->paramManager);

		currentSong->paramManager.processCurrentPos(modelStackStackWithThreeMainThing, posIncrement, false);
		currentSong->updateBPMFromAutomation();
	}

	int32_t nearestArpTickTime = 2147483647;

	for (uint8_t iPass = 0; iPass < 2; iPass++) {
		// Go through all Outputs
		for (Output* output = currentSong->firstOutput; output; output = output->next) {

			if (output->needsEarlyPlayback() == (iPass == 1)) {
				continue; // First time through, skip anything but priority clips, which must take effect first
				          // 2nd time through, skip the priority clips already actioned.
			}

			// If recording, we only stop when we hit the next ClipInstance
			if (output->recordingInArrangement) {
				int32_t searchPos = lastProcessedPos;
				if (!posIncrement) {
					searchPos++; // If first, 0-length tick, don't look at the one that starts here.
				}
				int32_t nextI = output->clipInstances.search(searchPos, GREATER_OR_EQUAL);

				ClipInstance* nextClipInstance = output->clipInstances.getElement(nextI);
				if (nextClipInstance) {
					int32_t ticksTilStart = nextClipInstance->pos - lastProcessedPos;

					// If it starts right now...
					if (!ticksTilStart) {
						output->endAnyArrangementRecording(currentSong, lastProcessedPos, 0);
						goto notRecording;
					}

					// Or if it starts later...
					else {
						playbackHandler.swungTicksTilNextEvent =
						    std::min(playbackHandler.swungTicksTilNextEvent, ticksTilStart);
					}
				}

				// Tick forward the Clip being recorded to
				output->getActiveClip()->lastProcessedPos += posIncrement;
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
				    modelStack->addTimelineCounter(output->getActiveClip());
				output->getActiveClip()->processCurrentPos(modelStackWithTimelineCounter, posIncrement);
			}

			else {
notRecording:
				// Or if inactive (and not recording), just check when final ClipInstance ends
				if (!currentSong->isOutputActiveInArrangement(output)) {

					ClipInstance* lastClipInstance =
					    output->clipInstances.getElement(output->clipInstances.getNumElements() - 1);
					if (lastClipInstance) {
						int32_t endPos = lastClipInstance->pos + lastClipInstance->length;

						if (lastProcessedPos < endPos) {
							int32_t ticksTilEnd = endPos - lastProcessedPos;
							if (ticksTilEnd > 0) {
								playbackHandler.swungTicksTilNextEvent =
								    std::min(playbackHandler.swungTicksTilNextEvent, ticksTilEnd);
							}
						}
					}
				}

				else {
					// See if a ClipInstance was already playing
					int32_t i = output->clipInstances.search(lastProcessedPos, LESS);
					if (i >= 0 && i < output->clipInstances.getNumElements()) {

						ClipInstance* clipInstance = output->clipInstances.getElement(i);
						Clip* thisClip = clipInstance->clip;

						if (thisClip) {

							int32_t endPos = clipInstance->pos + clipInstance->length;

							// If it's ended right now...
							if (endPos == lastProcessedPos) {

								if (posIncrement) { // Don't deactivate any Clips on the first, 0-length tick, or else!
									thisClip->expectNoFurtherTicks(currentSong);
									thisClip->activeIfNoSolo = false;

									if (!thisClip->isArrangementOnlyClip()) {
										anyChangeToSessionClipsPlaying = true;
									}
								}
							}

							// Or if it's still going...
							else if (endPos > lastProcessedPos) {

								ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
								    modelStack->addTimelineCounter(thisClip);

								// Tick it forward and process it
								thisClip->incrementPos(modelStackWithTimelineCounter, posIncrement);
								thisClip->processCurrentPos(modelStackWithTimelineCounter, posIncrement);

								// Make sure we come back here when the clipInstance ends
								int32_t ticksTilEnd = endPos - lastProcessedPos;
								playbackHandler.swungTicksTilNextEvent =
								    std::min(playbackHandler.swungTicksTilNextEvent, ticksTilEnd);

								goto justDoArp; // No need to think about the next ClipInstance yet
							}
						}
					}

					// Look to the next ClipInstance that has a Clip, if there is one...
					ClipInstance* nextClipInstance;
					Clip* thisClip;

					do {
						i++;
						if (i >= output->clipInstances.getNumElements()) {
							goto justDoArp;
						}
						nextClipInstance = output->clipInstances.getElement(i);
						thisClip = nextClipInstance->clip;
					} while (!thisClip);

					// And just see when it starts
					int32_t ticksTilStart = nextClipInstance->pos - lastProcessedPos;

					// Maybe it starts right now!
					if (!ticksTilStart) {

						AudioEngine::bypassCulling = true;

						ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
						    modelStack->addTimelineCounter(thisClip);

						if (posIncrement) { // If posIncrement is 0, it means this is the very first tick of playback,
							                // in which case this has just been set up already. But otherwise...
							thisClip->activeIfNoSolo = true;
							thisClip->setPos(modelStackWithTimelineCounter, 0);
							// Rohan: used to call assertActiveness(), but that's actually
							// unnecessary - because we're playing in arrangement,
							// setActiveClip() is actually the only relevant bit
							bool activeClipChanged = output->setActiveClip(modelStackWithTimelineCounter);
							if (activeClipChanged) {
								// the play cursor has selected a new active clip for the current output
								// send updated feedback so that midi controller has the latest values for
								// the current clip selected for midi follow control
								view_for_session().sendMidiFollowFeedback();
							}
						}

						thisClip->processCurrentPos(modelStackWithTimelineCounter, 0);

						if (!thisClip->isArrangementOnlyClip()) {
							anyChangeToSessionClipsPlaying = true;
						}

						if (getCurrentUI() == &arranger_view_for_session()) {
							arranger_view_for_session().notifyActiveClipChangedOnOutput(output);
						}

						// Make sure we come back here when the clipInstance ends
						playbackHandler.swungTicksTilNextEvent =
						    std::min(playbackHandler.swungTicksTilNextEvent, nextClipInstance->length);
					}

					// Or if it starts later...
					else {
						playbackHandler.swungTicksTilNextEvent =
						    std::min(playbackHandler.swungTicksTilNextEvent, ticksTilStart);
					}
				}
			}

justDoArp:
			int32_t posForArp;
			if (output->getActiveClip() && output->getActiveClip()->activeIfNoSolo) {
				posForArp = output->getActiveClip()->lastProcessedPos;
			}
			else {
				posForArp = lastProcessedPos;
			}

			int32_t ticksTilNextArpEvent = output->doTickForwardForArp(modelStack, posForArp);
			nearestArpTickTime = std::min(ticksTilNextArpEvent, nearestArpTickTime);
		}
	}

	if (anyChangeToSessionClipsPlaying) {
		// use root UI in case this is called from performance view
		session_view_for_session().requestRendering(getRootUI(), 0, 0xFFFFFFFF);
	}

	// If nothing further in the arrangement, we usually just stop playing
	if (playbackHandler.swungTicksTilNextEvent == 2147483647
	    && playbackHandler.isInternalClockActive()
	    // Only do this if not recording MIDI - but override that and do do it if we're "resampling"
	    && (playbackHandler.recording == RecordingMode::OFF
	        || audio_recorder_for_session().recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION)) {

		if (playbackHandler.stopOutputRecordingAtLoopEnd && audio_recorder_for_session().isCurrentlyResampling()) {
			audio_recorder_for_session().endRecordingSoon();
		}

		playbackHandler.endPlayback();
	}

	// Make sure we come back at right time for any song-level param automation. Must only do these after checking above
	// for (playbackHandler.swungTicksTilNextEvent == 2147483647)
	playbackHandler.swungTicksTilNextEvent = std::min(playbackHandler.swungTicksTilNextEvent, nearestArpTickTime);
	if (songParamManagerMightContainAutomation) {
		playbackHandler.swungTicksTilNextEvent =
		    std::min(playbackHandler.swungTicksTilNextEvent, currentSong->paramManager.ticksTilNextEvent);
		// Yes we could only do that if songParamManagerMightContainAutomation, which means that we did call
		// processCurrentPos() on that paramManager above. Because otherwise, its ticksTilNextEvent would be an invalid
		// value - often 0, which causes a freeze / infinite loop.
	}
}

void Arrangement::resetPlayPos(int32_t newPos, bool doingComplete, int32_t buttonPressLatency) {

	AudioEngine::bypassCulling = true;

	playbackStartedAtPos = newPos;
	lastProcessedPos = newPos;
	arranger_view_for_session().xScrollWhenPlaybackStarted =
	    currentSong->x_scroll_for_session()[NAVIGATION_ARRANGEMENT];
	// if you were holding a clip pad and doing a reset,
	// it can be easy to accidentally delete or enter the clip
	// setting this to false prevents that
	arranger_view_for_session().actionOnDepress = false;

	if (currentSong->paramManager.mightContainAutomation()) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		currentSong->paramManager.setPlayPos(newPos, modelStack, false);
	}

	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (!currentSong->isOutputActiveInArrangement(output)) {
			continue;
		}

		int32_t i = output->clipInstances.search(lastProcessedPos + 1, LESS);
		ClipInstance* clipInstance = output->clipInstances.getElement(i);

		// If there's a ClipInstance...
		if (clipInstance && clipInstance->pos + clipInstance->length > lastProcessedPos) {
			resumeClipInstancePlayback(clipInstance, doingComplete);
		}

		// Or if no ClipInstance, shall we maybe make one and do a spot of linear recording?
		else {
			if (doingComplete && playbackHandler.recording != RecordingMode::OFF
			    && output->wantsToBeginArrangementRecording()) {

				Error error = output->possiblyBeginArrangementRecording(currentSong, newPos);
				if (error != Error::NONE) {
					display->displayError(error);
				}
			}
		}
	}
}

// After unmuting, or creating, lengthening or changing the Clip of a ClipInstance
void Arrangement::resumeClipInstancePlayback(ClipInstance* clipInstance, bool doingComplete,
                                             bool mayActuallyResumeClip) {
	Clip* thisClip = clipInstance->clip;

	if (thisClip) {
		int32_t clipPos = lastProcessedPos - clipInstance->pos; // Use just the currentPos, not the "actual" pos,
		                                                        // because a multi-tick-forward is probably coming

		// Must set this before calling setPos, otherwise, ParamManagers won't know to expectEvent()
		thisClip->activeIfNoSolo = true;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, thisClip);

		thisClip->setPos(modelStack, clipPos, true);
		currentSong->assertActiveness(modelStack); // Why exactly did I do this rather than just setActiveClip()?

		if (doingComplete && mayActuallyResumeClip) {
			// Use thisClip->currentPos, not clipPos, cos it's got wrapped in setPos()
			// Do this even if the current pos is 0, otherwise AudioClips can fail to sound because
			// that non-"actual" pos can remain 0 for the whole thing
			thisClip->resumePlayback(modelStack);
		}
	}
}

// Virtual function. Called after Clip length changed, which could have big effects if Clip repeats multiple times
// within an Instance. Don't have to yet know whether play-head is actually inside an associated ClipInstance. Don't
// worry about mustSetPosToSomething - the effects of that are only needed in Session. Check
// playbackHandler.isEitherClockActive() before calling this.
void Arrangement::reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething,
                             bool mayResumeClip) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	Output* output = clip->output;

	if (!modelStack->song->isOutputActiveInArrangement(output)) {
		return;
	}

	int32_t actualPos = getLivePos();

	int32_t i = output->clipInstances.search(actualPos + 1, LESS);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (clipInstance && clipInstance->clip == clip && clipInstance->pos + clipInstance->length > actualPos + 1) {
		resumeClipInstancePlayback(clipInstance, true, mayResumeClip);
	}
}

void Arrangement::resyncToSongTicks(Song* song) {
}

// This is a little bit un-ideal, but after an undo or redo, this will be called, and it will tell every active Clip
// to potentially expect a note or automation event - and to re-get all current automation values.
// I wish we could easily just do this to the Clips that need it, but we don't store an easy list of just the Clips
// affected by each Action. Check playbackHandler.isEitherClockActive() before calling this.
void Arrangement::reversionDone() {

	int32_t actualPos = getLivePos();

	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (!currentSong->isOutputActiveInArrangement(output)) {
			continue;
		}

		int32_t i = output->clipInstances.search(actualPos + 1, LESS);
		ClipInstance* clipInstance = output->clipInstances.getElement(i);
		if (clipInstance && clipInstance->pos + clipInstance->length > actualPos + 1) {
			resumeClipInstancePlayback(clipInstance);
		}
		else {
			output->cutAllSound(); // I'd kinda prefer to "release" all voices...
		}
	}
}

bool Arrangement::isOutputAvailable(Output* output) {
	if (!playbackHandler.playbackState || !output->getActiveClip()) {
		return true;
	}

	if (output->recordingInArrangement) {
		return false;
	}

	if (!currentSong->isOutputActiveInArrangement(output)) {
		return true;
	}

	int32_t i = output->clipInstances.search(lastProcessedPos + 1, LESS);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	return (!clipInstance || clipInstance->pos + clipInstance->length <= lastProcessedPos);
}

// For the purpose of stopping or starting sounds during playback if we edited the stuff
void Arrangement::rowEdited(Output* output, int32_t startPos, int32_t endPos, Clip* clipRemoved,
                            ClipInstance* clipInstanceAdded) {

	int32_t actualPos = getLivePos();

	if (hasPlaybackActive() && actualPos >= startPos && actualPos < endPos) {
		if (clipRemoved) {
			clipRemoved->expectNoFurtherTicks(currentSong);
		}

		if (clipInstanceAdded) {
			resumeClipInstancePlayback(clipInstanceAdded);
		}
	}

	playbackHandler.expectEvent();
}

// First, be sure the clipInstance has a Clip
Error Arrangement::doUniqueCloneOnClipInstance(ClipInstance* clipInstance, int32_t newLength, bool shouldCloneRepeats) {
	if (!currentSong || !clipInstance || !clipInstance->clip)
		return Error::BUG;
	// Notification ranges use signed positions. Validate both the existing
	// instance and its requested replacement before any allocating callbacks.
	if (clipInstance->pos < 0 || clipInstance->length <= 0 || (newLength != -1 && newLength <= 0))
		return Error::BUG;
	const int64_t source_end = int64_t{clipInstance->pos} + clipInstance->length;
	const int64_t target_end = int64_t{clipInstance->pos} + (newLength == -1 ? clipInstance->length : newLength);
	if (source_end > INT32_MAX || target_end > INT32_MAX)
		return Error::BUG;
	Song* source_song = currentSong;
	Clip* oldClip = clipInstance->clip;
	auto* source_output = oldClip->output;
	const int32_t sourcePos = clipInstance->pos;
	const int32_t source_length = clipInstance->length;
	auto source_revision = [](deluge::gui::ui_session::Id id) {
		return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
	};
	const auto source_local_revision = source_revision(deluge::gui::ui_session::Id::Local);
	const auto source_remote_revision = source_revision(deluge::gui::ui_session::Id::Remote);
	auto source_valid = [&] {
		if (currentSong != source_song || source_revision(deluge::gui::ui_session::Id::Local) != source_local_revision
		    || source_revision(deluge::gui::ui_session::Id::Remote) != source_remote_revision)
			return false;
		return clipInstance->clip == oldClip && clipInstance->pos == sourcePos && clipInstance->length == source_length
		       && oldClip->output == source_output;
	};
	bool reserved = source_song->arrangementOnlyClips.ensureEnoughSpaceAllocated(1);
	if (!source_valid())
		return Error::BUG;
	if (!reserved)
		return Error::INSUFFICIENT_RAM;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(oldClip);

	Error error = oldClip->clone(modelStack, true);
	if (!source_valid() || modelStack->song != source_song)
		return Error::BUG;
	if (error != Error::NONE) {
		return error;
	}

	Clip* newClip = (Clip*)modelStack->getTimelineCounterAllowNull();
	// A successful clone must return a distinct, unpublished object. Do not
	// mutate or destroy an unexpected result whose ownership is unknown.
	if (!newClip || newClip == oldClip || source_song->contains_clip_for_undo(newClip))
		return Error::BUG;
	auto* clone_output = newClip->output;

	newClip->section = 255;
	newClip->activeIfNoSolo = false; // Always need to set arrangement-only Clips like this on create

	if (shouldCloneRepeats && newLength != -1) {
		if (newClip->type == ClipType::INSTRUMENT) {
			Song* owner = currentSong;
			auto revision = [](deluge::gui::ui_session::Id id) {
				return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
			};
			const auto local_revision = revision(deluge::gui::ui_session::Id::Local);
			const auto remote_revision = revision(deluge::gui::ui_session::Id::Remote);
			bool repeated = ((InstrumentClip*)newClip)->repeatOrChopToExactLength(modelStack, newLength);
			if (!repeated || !source_valid() || modelStack->song != owner
			    || modelStack->getTimelineCounterAllowNull() != newClip || owner->contains_clip_for_undo(newClip)
			    || newClip->output != clone_output) {
				// An unpublished clone is ours to destroy only while its context
				// remains intact. Invalidating callbacks may already have freed it.
				if (currentSong == owner && modelStack->song == owner
				    && revision(deluge::gui::ui_session::Id::Local) == local_revision
				    && revision(deluge::gui::ui_session::Id::Remote) == remote_revision
				    && modelStack->getTimelineCounterAllowNull() == newClip && !owner->contains_clip_for_undo(newClip)
				    && newClip->output == clone_output && clipInstance->clip != newClip
				    && (!newClip->output || !newClip->output->clipHasInstance(newClip))) {
					owner->deleteClipObject(newClip, false, InstrumentRemoval::NONE);
				}
				return Error::BUG;
			}
		}
	}

	// Add to Song
	Error insert_error = currentSong->arrangementOnlyClips.insertClipAtIndex(newClip, 0);
	if (insert_error != Error::NONE) {
		if (currentSong == source_song && modelStack->song == source_song
		    && source_revision(deluge::gui::ui_session::Id::Local) == source_local_revision
		    && source_revision(deluge::gui::ui_session::Id::Remote) == source_remote_revision
		    && modelStack->getTimelineCounterAllowNull() == newClip && !source_song->contains_clip_for_undo(newClip)
		    && newClip->output == clone_output && clipInstance->clip != newClip
		    && (!newClip->output || !newClip->output->clipHasInstance(newClip))) {
			source_song->deleteClipObject(newClip, false, InstrumentRemoval::NONE);
		}
		return insert_error;
	}
	auto discard_unused_clone = [&] {
		if (currentSong != source_song || modelStack->song != source_song
		    || source_revision(deluge::gui::ui_session::Id::Local) != source_local_revision
		    || source_revision(deluge::gui::ui_session::Id::Remote) != source_remote_revision
		    || modelStack->getTimelineCounterAllowNull() != newClip)
			return;
		int32_t clone_index = source_song->arrangementOnlyClips.getIndexForClip(newClip);
		if (clone_index < 0 || clipInstance->clip == newClip || source_song->sessionClips.getIndexForClip(newClip) >= 0)
			return;
		if (newClip->output != clone_output || (newClip->output && newClip->output->clipHasInstance(newClip)))
			return;
		// Only discard our published clone if no callback has adopted it.
		source_song->arrangementOnlyClips.deleteAtIndex(clone_index);
		source_song->deleteClipObject(newClip, false, InstrumentRemoval::NONE);
	};
	if (!source_valid() || modelStack->song != source_song || !source_song->contains_clip_for_undo(newClip)
	    || newClip->output != clone_output) {
		discard_unused_clone();
		return Error::BUG;
	}

	rowEdited(source_output, clipInstance->pos, clipInstance->pos + clipInstance->length, clipInstance->clip, nullptr);
	if (!source_valid() || modelStack->song != source_song || !source_song->contains_clip_for_undo(newClip)
	    || newClip->output != clone_output) {
		discard_unused_clone();
		return Error::BUG;
	}

	clipInstance->clip = newClip;
	if (newLength != -1) {
		clipInstance->length = newLength;
	}

	rowEdited(source_output, clipInstance->pos, clipInstance->pos + clipInstance->length, nullptr, clipInstance);

	// The final notification can resume playback and invalidate the instance.
	// Check external state before reading the installed instance again.
	if (currentSong != source_song || modelStack->song != source_song
	    || source_revision(deluge::gui::ui_session::Id::Local) != source_local_revision
	    || source_revision(deluge::gui::ui_session::Id::Remote) != source_remote_revision)
		return Error::BUG;
	const int32_t installed_length = newLength == -1 ? source_length : newLength;
	if (!source_song->contains_clip_for_undo(newClip) || newClip->output != clone_output
	    || clipInstance->clip != newClip || clipInstance->pos != sourcePos || clipInstance->length != installed_length)
		return Error::BUG;
	return Error::NONE;
}

int32_t Arrangement::getLivePos(uint32_t* timeRemainder) {
	return lastProcessedPos + playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick(timeRemainder);
}

void Arrangement::stopOutputRecordingAtLoopEnd() {
	playbackHandler.stopOutputRecordingAtLoopEnd = true;
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		session_view_for_session().redrawNumericDisplay();
	}
}

int32_t Arrangement::getPosAtWhichClipWillCut(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	int32_t i = clip->output->clipInstances.search(lastProcessedPos + 1, LESS);
	ClipInstance* clipInstance = clip->output->clipInstances.getElement(i);
	if (!clipInstance || clipInstance->clip != clip) {
		return clip->currentlyPlayingReversed ? (-2147483648) : 2147483647; // This shouldn't normally happen right?
	}

	int32_t cutPos = clipInstance->length - clip->loopLength * clip->repeatCount;
	if (cutPos == clip->loopLength) { // If cutting right at the end of the current repeat...

		if (clip->currentlyPlayingReversed) {
			cutPos = 0; // Since we already knew it was loopLength.
		}

		// See if the next ClipInstance has the same Clip and begins right as this one ends
		ClipInstance* nextClipInstance = clip->output->clipInstances.getElement(i + 1);
		if (nextClipInstance) {
			if (nextClipInstance->clip == clip && nextClipInstance->pos == clipInstance->pos + clipInstance->length) {
				int32_t toAdd = nextClipInstance->length;
				if (clip->currentlyPlayingReversed) {
					toAdd = -toAdd;
				}
				cutPos += toAdd;
			}
		}
	}
	else {
		if (clip->currentlyPlayingReversed) {
			cutPos = clipInstance->length - cutPos;
			if (cutPos > 0 && cutPos >= clip->getLivePos()) { // Same reasoning as comment below.
				cutPos = -2147483648;
			}
		}

		else {
			if (cutPos < clip->loopLength
			    && cutPos <= clip->getLivePos()) { // We first check that cutPos is less than the loopLength, so as not
				                                   // to waste time on the calculations in getLivePos if not necessary
				cutPos = 2147483647;
			}
		}
	}

	return cutPos;
}

bool Arrangement::willClipContinuePlayingAtEnd(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	int32_t cutPos = getPosAtWhichClipWillCut(modelStack);

	if (clip->currentlyPlayingReversed) {
		return (cutPos < 0);
	}
	else {
		return (cutPos > clip->loopLength);
	}
}

// This includes it "looping" before the Clip's full length due to that ClipInstance ending, and there being another
// instance of the same Clip right after.
// TODO: should this now actually check that it's not pingponging?
bool Arrangement::willClipLoopAtSomePoint(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	int32_t i = clip->output->clipInstances.search(lastProcessedPos + 1, LESS);
	ClipInstance* clipInstance = clip->output->clipInstances.getElement(i);
	if (!clipInstance || clipInstance->clip != clip) {
		return false;
	}

	// If we're still not too near the end of this Instance, it'll loop
	int32_t instanceEndPos = clipInstance->pos + clipInstance->length;
	int32_t distanceFromInstanceEnd = instanceEndPos - lastProcessedPos;
	if (distanceFromInstanceEnd > clip->loopLength) {
		return true;
	}

	// See if the next ClipInstance has the same Clip and begins right as this one ends
	ClipInstance* nextClipInstance = clip->output->clipInstances.getElement(i + 1);
	if (nextClipInstance) {
		if (nextClipInstance->clip == clip && nextClipInstance->pos == instanceEndPos) {
			return true;
		}
	}

	// Ok, we're near the end of the ClipInstance, but perhaps the last remaining bit contains a Clip loop-end-point?
	return (clipInstance->length > clip->loopLength * (clip->repeatCount + 1));
}

void Arrangement::endAnyLinearRecording() {
	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		uint32_t timeRemainder;
		int32_t actualPos = getLivePos(&timeRemainder);
		output->endAnyArrangementRecording(currentSong, actualPos, timeRemainder);
	}

	arranger_view_for_session().mustRedrawTickSquares = true; // Tick square shouldn't be red anymore

	uiNeedsRendering(&arranger_view_for_session(), 0xFFFFFFFF, 0);
}
