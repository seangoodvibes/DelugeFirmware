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

#include "processing/sound/sound_drum.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "mem_functions.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include <new>

SoundDrum::~SoundDrum() {
	retire_lifetime();
}

bool SoundDrum::readTagFromFile(Deserializer& reader, char const* tagName) {
	if (!strcmp(tagName, "path")) {
		reader.readTagOrAttributeValueString(&path);
		reader.exitTag("path");
	}
	else if (readDrumTagFromFile(reader, tagName)) {
		// Delegation is also considered a success.
	}
	else {
		return false;
	}

	return true;
}

void SoundDrum::resetTimeEnteredState() {
	// the sound drum might have multiple voices sounding, but only one will be sustaining and switched to hold
	for (const ActiveVoice& voice : this->voices()) {
		voice->envelopes[0].resetTimeEntered();
	}
}

void SoundDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, int16_t const* mpeValues,
                       int32_t fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	if (!modelStack || !modelStack->paramManager)
		return;
	auto drum_lifetime = watch_lifetime();
	if (!drum_lifetime.alive())
		return;
	auto* source_kit = kit;
	auto kit_lifetime = source_kit ? source_kit->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (source_kit && (!kit_lifetime.alive() || source_kit->getDrumIndex(this) < 0))
		return;
	auto* routed_clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounterAllowNull());
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != source_kit))
		return;
	auto* row = modelStack->getNoteRowAllowNull();
	const auto row_id = row ? modelStack->noteRowId : 0;
	if (row
	    && (!routed_clip || routed_clip->find_note_row_from_id(row_id) != row || row->drum != this
	        || modelStack->paramManager != &row->paramManager))
		return;
	const auto row_identity = row ? row->undo_identity : 0;
	auto* param_manager = modelStack->paramManager;
	auto* stack_song = modelStack->song;
	auto* source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto source_mode = polyphonic;
	const auto revision = arpeggiator.instruction_revision();
	int16_t mpe_values[kNumExpressionDimensions];
	if (mpeValues)
		std::copy_n(mpeValues, kNumExpressionDimensions, mpe_values);
	if (source_mode == PolyphonyMode::CHOKE && source_kit) {
		source_kit->choke();
		if (!drum_lifetime.alive() || !kit_lifetime.alive() || (routed_clip && !clip_lifetime.alive())
		    || kit != source_kit || source_kit->getDrumIndex(this) < 0 || currentSong != source_song
		    || deluge::gui::ui_session::current() != source_owner || polyphonic != source_mode
		    || arpeggiator.instruction_revision() != revision || modelStack->song != stack_song
		    || modelStack->getTimelineCounterAllowNull() != routed_clip || modelStack->getNoteRowAllowNull() != row
		    || modelStack->paramManager != param_manager || (routed_clip && routed_clip->output != source_kit))
			return;
		if (row
		    && (modelStack->noteRowId != row_id || routed_clip->find_note_row_from_id(row_id) != row
		        || row->undo_identity != row_identity || row->drum != this))
			return;
	}
	Sound::noteOn(modelStack, &arpeggiator, kNoteForDrum, mpeValues ? mpe_values : nullptr, sampleSyncLength, ticksLate,
	              samplesLate, velocity, fromMIDIChannel);
}

void SoundDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity) {
	Sound::noteOff(modelStack, &arpeggiator, kNoteForDrum);
}

extern bool expressionValueChangesMustBeDoneSmoothly;

void SoundDrum::expressionEvent(int32_t newValue, int32_t expressionDimension) {
	if (expressionDimension < 0 || expressionDimension >= kNumExpressionDimensions)
		return;
	auto drum_lifetime = watch_lifetime();
	if (!drum_lifetime.alive())
		return;

	int32_t s = expressionDimension + util::to_underlying(PatchSource::X);

	// sourcesChanged |= 1 << s; // We'd ideally not want to apply this to all voices though...
	for (const ActiveVoice& voice : this->voices()) {
		if (expressionValueChangesMustBeDoneSmoothly) {
			voice->expressionEventSmooth(newValue, s);
		}
		else {
			voice->expressionEventImmediate(*this, newValue, s);
		}
	}

	// Must update MPE values in Arp too - useful either if it's on, or if we're in true monophonic mode - in either
	// case, we could need to suddenly do a note-on for a different note that the Arp knows about, and need these MPE
	// values.
	arpeggiator.active_note.mpeValues[expressionDimension] = newValue >> 16;
}

void SoundDrum::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
                                                         int32_t channelOrNoteNumber,
                                                         MIDICharacteristic whichCharacteristic) {
	if (expressionDimension < 0 || expressionDimension >= kNumExpressionDimensions
	    || (whichCharacteristic != MIDICharacteristic::NOTE && whichCharacteristic != MIDICharacteristic::CHANNEL))
		return;
	auto drum_lifetime = watch_lifetime();
	if (!drum_lifetime.alive())
		return;
	auto* source_kit = kit;
	auto kit_lifetime = source_kit ? source_kit->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (source_kit && (!kit_lifetime.alive() || source_kit->getDrumIndex(this) < 0))
		return;
	auto* source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto context_matches = [&] {
		return drum_lifetime.alive() && (!source_kit || kit_lifetime.alive()) && kit == source_kit
		       && (!source_kit || source_kit->getDrumIndex(this) >= 0) && currentSong == source_song
		       && deluge::gui::ui_session::current() == source_owner;
	};
	const deluge::lifetime::callback_validation validation{context_matches};

	// Because this is a Drum, we disregard the noteCode (which is what channelOrNoteNumber always is in our case - but
	// yeah, that's all irrelevant.
	expressionEvent(newValue, expressionDimension);

	// Let the Sound know about this polyphonic expression event
	// The Sound class will use it to send MIDI out (if enabled in the sound config)
	send_polyphonic_expression_midi(newValue, expressionDimension, channelOrNoteNumber, whichCharacteristic,
	                                validation);
}

void SoundDrum::killAllVoices() {
	Sound::killAllVoices();
	arpeggiator.reset();
}

void SoundDrum::setupPatchingForAllParamManagers(Song* song) {
	song->setupPatchingForAllParamManagersForDrum(this);
}

Error SoundDrum::loadAllSamples(bool mayActuallyReadFiles) {
	return Sound::loadAllAudioFiles(mayActuallyReadFiles);
}

void SoundDrum::writeToFileAsInstrument(bool savingSong, ParamManager* paramManager) {
	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning("sound", true);
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.0-alpha");
	Sound::writeToFile(writer, savingSong, paramManager, &arpSettings, NULL);

	if (savingSong) {}

	writer.writeClosingTag("sound", true, true);
}

void SoundDrum::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) {
	writer.writeOpeningTagBeginning("sound", true);
	writeDrumTagsToFile(writer);

	Sound::writeToFile(writer, savingSong, paramManager, &arpSettings, path.get());

	if (savingSong) {
		Drum::writeMIDICommandsToFile(writer);
	}

	writer.writeClosingTag("sound", true, true);
}

Error SoundDrum::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack =
	    setupModelStackWithSong(modelStackMemory, song)->addTimelineCounter(clip)->addModControllableButNoNoteRow(this);

	return Sound::readFromFile(reader, modelStack, readAutomationUpToPos, &arpSettings);
}

// modelStack may be NULL
void SoundDrum::choke(ModelStackWithSoundFlags* modelStack) {
	if (polyphonic == PolyphonyMode::CHOKE) {

		// Don't choke it if it's auditioned
		if ((getRootUI() == &instrument_clip_view_for_session() || getRootUI() == &automation_view_for_session())
		    && instrument_clip_view_for_session().isDrumAuditioned(this)) {
			return;
		}

		// Ok, choke it
		fastReleaseAllVoices(modelStack); // Accepts NULL
	}
}

void SoundDrum::setSkippingRendering(bool newSkipping) {
	if (kit != nullptr && newSkipping != skippingRendering) {
		if (newSkipping) {
			kit->drumsWithRenderingActive.deleteAtKey((int32_t)(Drum*)this);
		}
		else {
			kit->drumsWithRenderingActive.insertAtKey((int32_t)(Drum*)this);
		}
	}

	Sound::setSkippingRendering(newSkipping);
}

uint8_t* SoundDrum::getModKnobMode() {
	return &kit->modKnobMode;
}

void SoundDrum::drumWontBeRenderedForAWhile() {
	Sound::wontBeRenderedForAWhile();
}

std::string SoundDrum::getDrumName() {
	return drumName;
}
