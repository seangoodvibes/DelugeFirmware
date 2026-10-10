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

#include "processing/sound/sound_instrument.h"
#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "gui/ui/ui_session.h"
#include "gui/views/view.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include "util/lifetime.h"
#include "util/misc.h"

namespace params = deluge::modulation::params;

SoundInstrument::SoundInstrument() : MelodicInstrument(OutputType::SYNTH) {
}

bool SoundInstrument::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {

	// MelodicInstrument::writeDataToFile(writer, clipForSavingOutputOnly, song); // Nope, this gets called within the
	// below call
	writeMelodicInstrumentAttributesToFile(writer, clipForSavingOutputOnly, song);

	ParamManager* paramManager;

	// If saving Output only...
	if (clipForSavingOutputOnly) {
		paramManager = &clipForSavingOutputOnly->paramManager;

		// Or if saving Song...
	}
	else {

		// If no activeClip, that means no Clip has this Output, so there should be a backedUpParamManager that we
		// should use
		if (!activeClip) {
			paramManager = song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
		}
		else {
			paramManager = nullptr;
		}
	}

	Sound::writeToFile(writer, clipForSavingOutputOnly == nullptr, paramManager,
	                   clipForSavingOutputOnly ? &((InstrumentClip*)clipForSavingOutputOnly)->arpSettings : nullptr,
	                   NULL);

	MelodicInstrument::writeMelodicInstrumentTagsToFile(writer, clipForSavingOutputOnly, song);

	return true;
}

// arpSettings optional - no need if you're loading a new V2.0 song where Instruments are all separate from Clips and
// won't store any arp stuff
Error SoundInstrument::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack =
	    setupModelStackWithSong(modelStackMemory, song)->addTimelineCounter(clip)->addModControllableButNoNoteRow(this);

	return Sound::readFromFile(reader, modelStack, readAutomationUpToPos, &defaultArpSettings);
}

void SoundInstrument::killAllVoices() {
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return;
	auto* source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto revision = arpeggiator.instruction_revision();
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && deluge::gui::ui_session::current() == source_owner && arpeggiator.instruction_revision() == revision;
	};
	const deluge::lifetime::callback_validation validation{context_matches};
	clear_voices(validation);
}

void SoundInstrument::cutAllSound() {
	killAllVoices();
}

void SoundInstrument::renderOutput(ModelStack* modelStack, std::span<StereoSample> output, int32_t* reverbBuffer,
                                   int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                   bool shouldLimitDelayFeedback, bool isClipActive) {
	if (!modelStack || !modelStack->song)
		return;
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive() || !activeClip)
		return;
	auto* routed_clip = static_cast<InstrumentClip*>(activeClip);
	auto clip_lifetime = routed_clip->watch_lifetime();
	if (!clip_lifetime.alive() || routed_clip->output != this)
		return;
	auto* source_song = modelStack->song;
	auto* source_current_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	auto* source_recorder = recorder;
	auto* param_manager = &routed_clip->paramManager;
	const auto row_count = routed_clip->noteRows.getNumElements();
	ParamCollection* collections[PARAM_COLLECTIONS_STORAGE_NUM];
	for (int32_t i = 0; i < PARAM_COLLECTIONS_STORAGE_NUM; ++i)
		collections[i] = param_manager->summaries[i].paramCollection;
	const auto context_matches = [&] {
		if (!output_lifetime.alive() || !clip_lifetime.alive() || activeClip != routed_clip
		    || routed_clip->output != this || modelStack->song != source_song || currentSong != source_current_song
		    || deluge::gui::ui_session::current() != source_owner || recorder != source_recorder
		    || routed_clip->noteRows.getNumElements() != row_count)
			return false;
		for (int32_t i = 0; i < PARAM_COLLECTIONS_STORAGE_NUM; ++i)
			if (param_manager->summaries[i].paramCollection != collections[i])
				return false;
		return true;
	};
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addTimelineCounter(routed_clip)->addOtherTwoThingsButNoNoteRow(this, param_manager);
	const auto clip_context_matches = [&] {
		return context_matches() && modelStackWithThreeMainThings->getTimelineCounter() == routed_clip
		       && modelStackWithThreeMainThings->paramManager == param_manager;
	};
	const deluge::lifetime::callback_validation clip_validation{clip_context_matches};

	if (skippingRendering) {
		compressor.reset();
		compressor.gainReduction = 0;
	}
	else {
		Sound::render(modelStackWithThreeMainThings, output, reverbBuffer, sideChainHitPending, reverbAmountAdjust,
		              shouldLimitDelayFeedback, kMaxSampleValue, recorder, &clip_validation);
	}

	if (!clip_validation.valid())
		return;

	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn && isClipActive) {

		// No time to call the proper function and do error checking, sorry.
		ParamCollectionSummary* patchedParamsSummary = &modelStackWithThreeMainThings->paramManager->summaries[1];
		bool anyInterpolating = false;
		if constexpr (params::kNumParams > 64) {
			anyInterpolating = patchedParamsSummary->whichParamsAreInterpolating[0]
			                   || patchedParamsSummary->whichParamsAreInterpolating[1]
			                   || patchedParamsSummary->whichParamsAreInterpolating[2];
		}
		else {
			anyInterpolating = patchedParamsSummary->whichParamsAreInterpolating[0]
			                   || patchedParamsSummary->whichParamsAreInterpolating[1];
		}
		if (anyInterpolating) {
yesTickParamManagerForClip:
			modelStackWithThreeMainThings->paramManager->toForTimeline()->tickSamples(
			    output.size(), modelStackWithThreeMainThings, &clip_validation);
			if (!clip_validation.valid())
				return;
		}
		else {

			// Try other options too.

			// No time to call the proper function and do error checking, sorry.
			ParamCollectionSummary* unpatchedParamsSummary = &modelStackWithThreeMainThings->paramManager->summaries[0];
			if constexpr (params::UNPATCHED_SOUND_MAX_NUM > 32) {
				if (unpatchedParamsSummary->whichParamsAreInterpolating[0]
				    || unpatchedParamsSummary->whichParamsAreInterpolating[1]) {
					goto yesTickParamManagerForClip;
				}
			}
			else {
				if (unpatchedParamsSummary->whichParamsAreInterpolating[0]) {
					goto yesTickParamManagerForClip;
				}
			}

			// No time to call the proper function and do error checking, sorry.
			ParamCollectionSummary* patchCablesSummary = &modelStackWithThreeMainThings->paramManager->summaries[2];
			if constexpr (kMaxNumPatchCables > 32) {
				if (patchCablesSummary->whichParamsAreInterpolating[0]
				    || patchCablesSummary->whichParamsAreInterpolating[1]) {
					goto yesTickParamManagerForClip;
				}
			}
			else {
				if (patchCablesSummary->whichParamsAreInterpolating[0]) {
					goto yesTickParamManagerForClip;
				}
			}

			// No time to call the proper function and do error checking, sorry.
			ParamCollectionSummary* expressionParamsSummary =
			    &modelStackWithThreeMainThings->paramManager->summaries[3];
			if constexpr (kNumExpressionDimensions > 32) {
				if (expressionParamsSummary->whichParamsAreInterpolating[0]
				    || expressionParamsSummary->whichParamsAreInterpolating[1]) {
					goto yesTickParamManagerForClip;
				}
			}
			else {
				if (expressionParamsSummary->whichParamsAreInterpolating[0]) {
					goto yesTickParamManagerForClip;
				}
			}
		}

		// Do the ParamManagers of each NoteRow, too
		for (int32_t i = 0; i < row_count; i++) {
			NoteRow* thisNoteRow = routed_clip->noteRows.getElement(i);
			// No time to call the proper function and do error checking, sorry.
			ParamCollectionSummary* expressionParamsSummary = &thisNoteRow->paramManager.summaries[0];
			bool result = false;
			if constexpr (kNumExpressionDimensions > 32) {
				result = expressionParamsSummary->whichParamsAreInterpolating[0]
				         || expressionParamsSummary->whichParamsAreInterpolating[1];
			}
			else {
				result = expressionParamsSummary->whichParamsAreInterpolating[0];
			}
			if (result) {
				modelStackWithThreeMainThings->setNoteRow(thisNoteRow, thisNoteRow->y);
				modelStackWithThreeMainThings->paramManager = &thisNoteRow->paramManager;
				const auto row_identity = thisNoteRow->undo_identity;
				const auto row_context_matches = [&] {
					return context_matches() && routed_clip->noteRows.getElement(i) == thisNoteRow
					       && thisNoteRow->undo_identity == row_identity
					       && modelStackWithThreeMainThings->getTimelineCounter() == routed_clip
					       && modelStackWithThreeMainThings->paramManager == &thisNoteRow->paramManager;
				};
				const deluge::lifetime::callback_validation row_validation{row_context_matches};
				thisNoteRow->paramManager.tickSamples(output.size(), modelStackWithThreeMainThings, &row_validation);
				if (!row_validation.valid())
					return;
			}
		}
	}
}

Error SoundInstrument::loadAllAudioFiles(bool mayActuallyReadFiles) {
	auto owner_lifetime = watch_lifetime();
	auto* source_song = currentSong;
	auto song_lifetime = source_song ? source_song->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	const auto context_valid = [&] {
		return owner_lifetime.alive() && (!source_song || song_lifetime.alive()) && currentSong == source_song
		       && deluge::gui::ui_session::current() == source_owner;
	};
	if (!context_valid())
		return Error::ABORTED_BY_USER;
	deluge::lifetime::callback_validation validation(context_valid);
	const bool alternate_path =
	    mayActuallyReadFiles && audioFileManager.alternateLoadDirStatus == AlternateLoadDirStatus::NONE_SET;
	if (alternate_path) {
		auto error = setupDefaultAudioFileDir();
		if (error != Error::NONE)
			return error;
	}
	auto error = context_valid() ? Sound::loadAllAudioFiles(mayActuallyReadFiles, &validation) : Error::ABORTED_BY_USER;
	if (alternate_path)
		audioFileManager.thingFinishedLoading();
	return error;
}

void SoundInstrument::resyncLFOs() {
	resyncGlobalLFOs();
}

ModControllable* SoundInstrument::toModControllable() {
	return this;
}

void SoundInstrument::setupPatching(ModelStackWithTimelineCounter* modelStack) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull();
	ParamManagerForTimeline* paramManager;
	if (clip) {
		paramManager = &clip->paramManager;

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(this, paramManager);

		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
	}
	else {
		paramManager =
		    (ParamManagerForTimeline*)modelStack->song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(paramManager);
	}

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    paramManager->getPatchCableSet(modelStack->addOtherTwoThingsButNoNoteRow(this, paramManager));

	PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

	patchCableSet->setupPatching(modelStackWithParamCollection);
}

bool SoundInstrument::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {

	bool clipChanged = MelodicInstrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (clipChanged) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

		if (modelStack) {
			ParamManager* paramManager = &modelStack->getTimelineCounter()->paramManager;
			patcher.performInitialPatching(*this, *paramManager);

			// Grab mono expression params
			ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
			if (expressionParams) {
				for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
					monophonicExpressionValues[i] = expressionParams->getValue(i);
				}
			}
			else {
				for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
					monophonicExpressionValues[i] = 0;
				}
			}
			expressionSourcesChangedAtSynthLevel.set();
		}
	}
	return clipChanged;
}

void SoundInstrument::setupWithoutActiveClip(ModelStack* modelStack) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(nullptr);

	setupPatching(modelStackWithTimelineCounter);

	ParamManager* paramManager =
	    modelStackWithTimelineCounter->song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
	if (!paramManager) {
		FREEZE_WITH_ERROR("PM43"); // was E173
	}
	patcher.performInitialPatching(*this, *paramManager);

	// Clear mono expression params
	for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
		monophonicExpressionValues[i] = 0;
	}
	expressionSourcesChangedAtSynthLevel.set();

	Instrument::setupWithoutActiveClip(modelStack);
}

void SoundInstrument::prepareForHibernationOrDeletion() {
	Sound::prepareForHibernation();
}

void SoundInstrument::setupPatchingForAllParamManagers(Song* song) {
	song->setupPatchingForAllParamManagersForInstrument(this);
}

void SoundInstrument::deleteBackedUpParamManagers(Song* song) {
	song->deleteBackedUpParamManagersForModControllable(this);
}

extern bool expressionValueChangesMustBeDoneSmoothly;

void SoundInstrument::monophonicExpressionEvent(int32_t newValue, int32_t expressionDimension) {
	expressionSourcesChangedAtSynthLevel[expressionDimension] = true;
	monophonicExpressionValues[expressionDimension] = newValue;
}

// Alternative to what's in the NonAudioInstrument:: implementation, which would almost work here, but we cut corner for
// Sound by avoiding going through the Arp and just talk directly to the Voices. (Despite my having made it now actually
// need to talk to the Arp too, as below...) Note, this virtual function actually overrides/implements from two base
// classes - MelodicInstrument and ModControllable.
void SoundInstrument::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
                                                               int32_t channelOrNoteNumber,
                                                               MIDICharacteristic whichCharacteristic) {
	if (expressionDimension < 0 || expressionDimension >= kNumExpressionDimensions
	    || (whichCharacteristic != MIDICharacteristic::NOTE && whichCharacteristic != MIDICharacteristic::CHANNEL))
		return;
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return;
	auto* source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && deluge::gui::ui_session::current() == source_owner;
	};
	const deluge::lifetime::callback_validation validation{context_matches};

	int32_t s = expressionDimension + util::to_underlying(PatchSource::X);
	for (const auto& voice : this->voices()) {
		if (voice->inputCharacteristics[util::to_underlying(whichCharacteristic)] == channelOrNoteNumber) {
			if (expressionValueChangesMustBeDoneSmoothly) {
				voice->expressionEventSmooth(newValue, s);
			}
			else {
				voice->expressionEventImmediate(*this, newValue, s);
			}
		}
	}

	// Must update MPE values in Arp too - useful either if it's on, or if we're in true monophonic mode - in either
	// case, we could need to suddenly do a note-on for a different note that the Arp knows about, and need these MPE
	// values.
	int32_t n, nEnd;
	if (whichCharacteristic == MIDICharacteristic::NOTE) {
		n = arpeggiator.notes.search(channelOrNoteNumber, GREATER_OR_EQUAL);
		if (n < arpeggiator.notes.getNumElements()) {
			nEnd = 0;
			goto lookAtArpNote;
		}
		return;
	}
	nEnd = arpeggiator.notes.getNumElements();
	for (n = 0; n < nEnd; n++) {
lookAtArpNote:
		ArpNote* arpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[util::to_underlying(whichCharacteristic)] == channelOrNoteNumber) {
			arpNote->mpeValues[expressionDimension] = newValue >> 16;
		}
	}

	// Let the Sound know about this polyphonic expression event
	// The Sound class will use it to send MIDI out (if enabled in the sound config)
	send_polyphonic_expression_midi(newValue, expressionDimension, channelOrNoteNumber, whichCharacteristic,
	                                validation);
}

void SoundInstrument::sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCode,
                               int16_t const* mpeValues, int32_t fromMIDIChannel, uint8_t velocity,
                               uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	if (!modelStack)
		return;
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive() || !inValidState)
		return;
	auto* active_clip = activeClip;
	auto active_clip_lifetime = active_clip ? active_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (active_clip && (!active_clip_lifetime.alive() || active_clip->output != this))
		return;
	auto* routed_clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounterAllowNull());
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return;
	auto* row = modelStack->getNoteRowAllowNull();
	const auto row_id = row ? modelStack->noteRowId : 0;
	if (row && (!routed_clip || routed_clip->find_note_row_from_id(row_id) != row))
		return;
	const auto row_identity = row ? row->undo_identity : 0;
	auto* param_manager = modelStack->paramManager;
	auto* stack_song = modelStack->song;
	auto* source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto context_matches = [&] {
		if (!output_lifetime.alive() || (active_clip && !active_clip_lifetime.alive())
		    || (routed_clip && !clip_lifetime.alive()) || !inValidState || activeClip != active_clip
		    || (active_clip && active_clip->output != this) || (routed_clip && routed_clip->output != this)
		    || modelStack->getTimelineCounterAllowNull() != routed_clip || modelStack->getNoteRowAllowNull() != row
		    || modelStack->paramManager != param_manager || modelStack->song != stack_song || currentSong != source_song
		    || deluge::gui::ui_session::current() != source_owner)
			return false;
		return !row
		       || (modelStack->noteRowId == row_id && routed_clip->find_note_row_from_id(row_id) == row
		           && row->undo_identity == row_identity);
	};
	const deluge::lifetime::callback_validation validation{context_matches};
	if (isOn) {
		noteOn(modelStack, &arpeggiator, noteCode, mpeValues, sampleSyncLength, ticksLate, samplesLate, velocity,
		       fromMIDIChannel, &validation);
	}
	else {
		noteOff(modelStack, &arpeggiator, noteCode, &validation);
	}
}

ArpeggiatorSettings* SoundInstrument::getArpSettings(InstrumentClip* clip) {
	return MelodicInstrument::getArpSettings(clip);
}

bool SoundInstrument::readTagFromFile(Deserializer& reader, char const* tagName) {
	return MelodicInstrument::readTagFromFile(reader, tagName);
}

void SoundInstrument::compensateInstrumentVolumeForResonance(ModelStackWithThreeMainThings* modelStack) {
	Sound::compensateVolumeForResonance(modelStack);
}

void SoundInstrument::loadCrucialAudioFilesOnly() {
	loadAllAudioFiles(true);
}

// Any time it gets edited, we want to grab the default arp settings from the activeClip
void SoundInstrument::beenEdited(bool shouldMoveToEmptySlot) {
	if (activeClip) {
		defaultArpSettings.cloneFrom(&((InstrumentClip*)activeClip)->arpSettings);
	}
	Instrument::beenEdited(shouldMoveToEmptySlot);
}

// Returns num ticks til next arp event
int32_t SoundInstrument::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	if (!modelStack || !modelStack->song)
		return 2147483647;
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive() || !activeClip)
		return 2147483647;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip->watch_lifetime();
	if (!clip_lifetime.alive() || routed_clip->output != this)
		return 2147483647;
	auto* source_song = modelStack->song;
	const auto source_owner = deluge::gui::ui_session::current();

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addTimelineCounter(activeClip)
	        ->addOtherTwoThingsButNoNoteRow(this, getParamManager(modelStack->song));

	UnpatchedParamSet* unpatchedParams = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSet();

	if (!unpatchedParams)
		return 2147483647;
	const auto context_matches = [&] {
		return output_lifetime.alive() && clip_lifetime.alive() && activeClip == routed_clip
		       && routed_clip->output == this && currentSong == source_song && modelStack->song == source_song
		       && modelStackWithThreeMainThings->getTimelineCounterAllowNull() == routed_clip
		       && modelStackWithThreeMainThings->paramManager == &routed_clip->paramManager
		       && routed_clip->paramManager.getUnpatchedParamSet() == unpatchedParams
		       && deluge::gui::ui_session::current() == source_owner;
	};
	if (!context_matches())
		return 2147483647;
	ArpeggiatorSettings* arpSettings = getArpSettings();
	if (!arpSettings)
		return 2147483647;
	arpSettings->updateParamsFromUnpatchedParamSet(unpatchedParams);

	ArpReturnInstruction instruction;

	int32_t ticksTilNextArpEvent =
	    arpeggiator.doTickForward(arpSettings, &instruction, currentPos, activeClip->currentlyPlayingReversed);
	if (!context_matches())
		return 2147483647;
	const auto instruction_revision = arpeggiator.instruction_revision();
	const auto instruction_matches = [&] {
		return context_matches() && arpeggiator.instruction_revision() == instruction_revision;
	};
	const deluge::lifetime::callback_validation instruction_validation{instruction_matches};

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStackWithThreeMainThings->addSoundFlags();

	bool atLeastOneOff = false;
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		atLeastOneOff = true;
		if (!noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.glideNoteCodeOffPostArp[n],
		                            &instruction_validation))
			return 2147483647;
		if (!instruction_matches())
			return 2147483647;
	}
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		atLeastOneOff = true;
		if (!noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp[n],
		                            &instruction_validation))
			return 2147483647;
		if (!instruction_matches())
			return 2147483647;
	}
	if (atLeastOneOff) {
		invertReversed = false;
	}

	process_postarp_notes(modelStackWithSoundFlags, arpSettings, instruction, &instruction_validation);
	if (!instruction_matches())
		return 2147483647;

	return ticksTilNextArpEvent;
}

void SoundInstrument::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                             GlobalEffectableForClip** globalEffectableWithMostReverb,
                                             int32_t* highestReverbAmountFound) {
	if (activeClip) {
		Sound::getThingWithMostReverb(soundWithMostReverb, paramManagerWithMostReverb, globalEffectableWithMostReverb,
		                              highestReverbAmountFound, &activeClip->paramManager);
	}
}

ArpeggiatorBase* SoundInstrument::getArp() {
	return &arpeggiator;
}

bool SoundInstrument::noteIsOn(int32_t noteCode, bool resetTimeEntered) {

	ArpeggiatorSettings* arpSettings = getArpSettings();

	if (arpSettings != nullptr
	    && (arpSettings->mode != ArpMode::OFF || polyphonic == PolyphonyMode::LEGATO
	        || polyphonic == PolyphonyMode::MONO)) {
		int32_t n = arpeggiator.notes.search(noteCode, GREATER_OR_EQUAL);
		if (n >= arpeggiator.notes.getNumElements()) {
			return false;
		}
		ArpNote* arpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
		return (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCode);
	}

	if (!hasActiveVoices()) {
		return false;
	}

	for (const auto& voice : this->voices()) {
		if ((voice->noteCodeAfterArpeggiation == noteCode)
		    && voice->envelopes[0].state < EnvelopeStage::RELEASE) { // Ignore releasing notes. Is this right?
			if (resetTimeEntered) {
				voice->envelopes[0].resetTimeEntered();
			}
			return true;
		}
	}
	return false;
}

SoundInstrument::~SoundInstrument() {
	retire_lifetime();
}
