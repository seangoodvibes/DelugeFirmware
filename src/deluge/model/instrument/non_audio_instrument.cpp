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

#include "model/instrument/non_audio_instrument.h"
#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "gui/ui/ui_session.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "processing/engines/cv_engine.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/lifetime.h"
#include <cstring>

static bool dispatch_non_audio_arp_instruction(NonAudioInstrument& instrument, ArpReturnInstruction& instruction,
                                               bool send_offs, bool send_ons, int32_t velocity,
                                               const deluge::lifetime::callback_validation& validation) {
	if (!validation.valid())
		return false;
	if (send_offs) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; ++n) {
			if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE)
				break;
			instrument.noteOffPostArp(instruction.glideNoteCodeOffPostArp[n], instruction.glideOutputMIDIChannelOff[n],
			                          velocity, n);
			if (!validation.valid())
				return false;
		}
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; ++n) {
			if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE)
				break;
			instrument.noteOffPostArp(instruction.noteCodeOffPostArp[n], instruction.outputMIDIChannelOff[n], velocity,
			                          n);
			if (!validation.valid())
				return false;
		}
	}
	if (send_ons && instruction.arpNoteOn) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; ++n) {
			if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE)
				break;
			instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;
			instrument.noteOnPostArp(instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn, n);
			if (!validation.valid())
				return false;
		}
	}
	return true;
}

void NonAudioInstrument::renderOutput(ModelStack* modelStack, std::span<StereoSample> output, int32_t* reverbBuffer,
                                      int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                      bool shouldLimitDelayFeedback, bool isClipActive) {
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return;
	auto* source_song = currentSong;
	auto* stack_song = modelStack ? modelStack->song : nullptr;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto source_channel = getChannel();
	const auto source_type = type;
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && (!modelStack || modelStack->song == stack_song) && deluge::gui::ui_session::current() == source_owner
		       && getChannel() == source_channel && type == source_type;
	};
	if (!routed_clip)
		return;
	auto* arp_settings = &static_cast<InstrumentClip*>(routed_clip)->arpSettings;
	if (arp_settings->mode == ArpMode::OFF)
		return;
	uint32_t gate_threshold = (uint32_t)arp_settings->gate + 2147483648;
	uint32_t phase_increment = arp_settings->getPhaseIncrement(getFinalParameterValueExp(
	    paramNeutralValues[deluge::modulation::params::GLOBAL_ARP_RATE], cableToExpParamShortcut(arp_settings->rate)));
	ArpReturnInstruction instruction;
	arpeggiator.render(arp_settings, &instruction, output.size(), gate_threshold, phase_increment);
	if (!context_matches())
		return;
	const auto revision = arpeggiator.instruction_revision();
	const auto instruction_matches = [&] {
		return context_matches() && arpeggiator.instruction_revision() == revision;
	};
	const deluge::lifetime::callback_validation validation{instruction_matches};
	dispatch_non_audio_arp_instruction(*this, instruction, true, true, kDefaultLiftValue, validation);
}

void NonAudioInstrument::sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCodePreArp,
                                  int16_t const* mpeValues, int32_t fromMIDIChannel, uint8_t velocity,
                                  uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return;
	auto* source_song = currentSong;
	auto* stack_song = modelStack ? modelStack->song : nullptr;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto source_channel = getChannel();
	const auto source_type = type;
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && (!modelStack || modelStack->song == stack_song) && deluge::gui::ui_session::current() == source_owner
		       && getChannel() == source_channel && type == source_type;
	};
	ArpeggiatorSettings* arp_settings = routed_clip ? &static_cast<InstrumentClip*>(routed_clip)->arpSettings : nullptr;
	ArpReturnInstruction instruction;
	if (isOn)
		arpeggiator.noteOn(arp_settings, noteCodePreArp, velocity, &instruction, fromMIDIChannel, mpeValues);
	else
		arpeggiator.noteOff(arp_settings, noteCodePreArp, &instruction);
	if (!context_matches())
		return;
	const auto revision = arpeggiator.instruction_revision();
	const auto instruction_matches = [&] {
		return context_matches() && arpeggiator.instruction_revision() == revision;
	};
	const deluge::lifetime::callback_validation validation{instruction_matches};
	dispatch_non_audio_arp_instruction(*this, instruction, !isOn, isOn || source_type == OutputType::CV, velocity,
	                                   validation);
}

// Inherit / overrides from both MelodicInstrument and ModControllable
void NonAudioInstrument::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
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
	const auto source_channel = getChannel();
	const auto revision = arpeggiator.instruction_revision();
	const auto note_count = arpeggiator.notes.getNumElements();
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && deluge::gui::ui_session::current() == source_owner && getChannel() == source_channel
		       && arpeggiator.instruction_revision() == revision && arpeggiator.notes.getNumElements() == note_count;
	};
	int32_t first_note = 0;
	int32_t end_note = note_count;
	if (whichCharacteristic == MIDICharacteristic::NOTE) {
		first_note = arpeggiator.notes.search(channelOrNoteNumber, GREATER_OR_EQUAL);
		if (first_note >= note_count)
			return;
		end_note = first_note + 1;
	}
	for (int32_t n = first_note; n < end_note; ++n) {
		auto* arp_note = static_cast<ArpNote*>(arpeggiator.notes.getElementAddress(n));
		if (arp_note->inputCharacteristics[util::to_underlying(whichCharacteristic)] != channelOrNoteNumber)
			continue;
		arp_note->mpeValues[expressionDimension] = newValue >> 16;
		for (int32_t i = 0; i < ARP_MAX_INSTRUCTION_NOTES; ++i) {
			if (arp_note->noteCodeOnPostArp[i] == ARP_NOTE_NONE
			    || arp_note->outputMemberChannel[i] == MIDI_CHANNEL_NONE)
				break;
			polyphonicExpressionEventPostArpeggiator(newValue, arp_note->noteCodeOnPostArp[i], expressionDimension,
			                                         arp_note, i);
			if (!context_matches() || arpeggiator.notes.getElementAddress(n) != arp_note)
				return;
		}
	}
}

// Returns num ticks til next arp event
int32_t NonAudioInstrument::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return 2147483647;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return 2147483647;
	auto* source_song = currentSong;
	auto* stack_song = modelStack ? modelStack->song : nullptr;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto source_channel = getChannel();
	const auto source_type = type;
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && (!modelStack || modelStack->song == stack_song) && deluge::gui::ui_session::current() == source_owner
		       && getChannel() == source_channel && type == source_type;
	};
	if (!routed_clip)
		return 2147483647;
	ArpReturnInstruction instruction;
	const auto ticks_until_next =
	    arpeggiator.doTickForward(&static_cast<InstrumentClip*>(routed_clip)->arpSettings, &instruction, currentPos,
	                              routed_clip->currentlyPlayingReversed);
	if (!context_matches())
		return 2147483647;
	const auto revision = arpeggiator.instruction_revision();
	const auto instruction_matches = [&] {
		return context_matches() && arpeggiator.instruction_revision() == revision;
	};
	const deluge::lifetime::callback_validation validation{instruction_matches};
	if (!dispatch_non_audio_arp_instruction(*this, instruction, true, true, kDefaultLiftValue, validation))
		return 2147483647;
	return ticks_until_next;
}

// Unlike other Outputs, these don't have ParamManagers backed up at the Song level
ParamManager* NonAudioInstrument::getParamManager(Song* song) {

	if (activeClip) {
		return &activeClip->paramManager;
	}
	else {
		return nullptr;
	}
}

bool NonAudioInstrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	char const* slotXMLTag = getSlotXMLTag();

	if (!strcmp(tagName, slotXMLTag)) {
		setChannel(reader.readTagOrAttributeValueInt());
	}

	else {
		return MelodicInstrument::readTagFromFile(reader, tagName);
	}

	reader.exitTag();
	return true;
}

bool NonAudioInstrument::needsEarlyPlayback() const {
	return (type == OutputType::MIDI_OUT && channel == MIDI_CHANNEL_TRANSPOSE);
}
