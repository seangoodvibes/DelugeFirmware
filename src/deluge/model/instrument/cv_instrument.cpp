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

#include "model/instrument/cv_instrument.h"
#include "gui/ui/ui_session.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "modulation/params/param_set.h"
#include "processing/engines/cv_engine.h"
#include "storage/storage_manager.h"
#include "util/lifetime.h"
#include <cstring>

CVInstrument::CVInstrument() : NonAudioInstrument(OutputType::CV) {
	monophonicPitchBendValue = 0;
	polyPitchBendValue = 0;
}

void CVInstrument::noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) {
	if (!arpNote || noteIndex < 0 || noteIndex >= ARP_MAX_INSTRUCTION_NOTES)
		return;
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return;
	auto* routed_clip = activeClip;
	auto clip_lifetime = routed_clip ? routed_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (routed_clip && (!clip_lifetime.alive() || routed_clip->output != this))
		return;
	const auto revision = arpeggiator.instruction_revision();
	const auto source_channel = getChannel();
	const auto pitch_mode = cvmode[0];
	const auto second_mode = cvmode[1];
	auto* source_song = currentSong;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto context_matches = [&] {
		return output_lifetime.alive() && (!routed_clip || clip_lifetime.alive()) && activeClip == routed_clip
		       && (!routed_clip || routed_clip->output == this) && currentSong == source_song
		       && deluge::gui::ui_session::current() == source_owner && getChannel() == source_channel
		       && cvmode[0] == pitch_mode && cvmode[1] == second_mode && arpeggiator.instruction_revision() == revision;
	};
	const auto velocity = arpNote->velocity;
	// Multiplication also defines the conversion for negative MPE values.
	polyPitchBendValue = static_cast<int32_t>(arpNote->mpeValues[0]) * 65536;
	updatePitchBendOutput(false);
	if (!context_matches())
		return;
	const auto pitch_channel = getPitchChannel();
	arpNote->outputMemberChannel[noteIndex] = pitch_channel;
	cvEngine.sendNote(true, pitch_channel, noteCodePostArp);
	if (!context_matches())
		return;
	if (second_mode == CVMode::velocity) {
		cvEngine.sendVoltageOut(1, velocity << 8);
	}
}

void CVInstrument::noteOffPostArp(int32_t noteCodePostArp, int32_t oldMIDIChannel, int32_t velocity,
                                  int32_t noteIndex) {

	cvEngine.sendNote(false, getPitchChannel(), noteCodePostArp);
}

void CVInstrument::polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
                                                            int32_t expressionDimension, ArpNote* arpNote,
                                                            int32_t noteIndex) {
	if (cvEngine.isNoteOn(getPitchChannel(), noteCodeAfterArpeggiation)) {
		if (!expressionDimension) { // Pitch bend only, handles different polyphonic vs mpe pitch scales
			polyPitchBendValue = newValue;
			updatePitchBendOutput();
		}
		else {
			// send the combined mono and poly expression
			lastCombinedPolyExpression[expressionDimension] = newValue;
			sendMonophonicExpressionEvent(expressionDimension);
		}
	}
}

void CVInstrument::monophonicExpressionEvent(int32_t newValue, int32_t expressionDimension) {
	if (!expressionDimension) { // Pitch bend only
		monophonicPitchBendValue = newValue;
		updatePitchBendOutput();
	}
	else {
		lastMonoExpression[expressionDimension] = newValue;
		sendMonophonicExpressionEvent(expressionDimension);
	}
}

void CVInstrument::updatePitchBendOutput(bool outputToo) {

	ParamManager* paramManager = getParamManager(nullptr);
	if (paramManager) {
		ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
		if (expressionParams) {
			cachedBendRanges[BEND_RANGE_MAIN] = expressionParams->bendRanges[BEND_RANGE_MAIN];
			cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL];
		}
	}

	// If couldn't update bend ranges that way, no worries - we'll keep our cached ones, cos the user probably intended
	// that.

	int32_t totalBendAmount = // (1 << 23) represents one semitone. So full 32-bit range can be +-256 semitones. This is
	                          // different to the equivalent calculation in Voice, which needs to get things into a
	                          // number of octaves.
	    (monophonicPitchBendValue >> 8) * cachedBendRanges[BEND_RANGE_MAIN]
	    + (polyPitchBendValue >> 8) * cachedBendRanges[BEND_RANGE_FINGER_LEVEL];

	cvEngine.setCVPitchBend(getPitchChannel(), totalBendAmount, outputToo);
}

bool CVInstrument::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {
	// NonAudioInstrument::writeDataToFile(clipForSavingOutputOnly, song); // Nope, this gets called within the below
	// call
	writeMelodicInstrumentAttributesToFile(writer, clipForSavingOutputOnly, song);
	writer.writeAttribute("cv2Source", static_cast<int32_t>(cvmode[1]));
	if (clipForSavingOutputOnly || !midiInput.containsSomething()) {
		return false; // If we don't need to write a "device" tag, opt not to end the opening tag
	}

	writer.writeOpeningTagEnd();
	MelodicInstrument::writeMelodicInstrumentTagsToFile(writer, clipForSavingOutputOnly, song);
	return true;
}
bool CVInstrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	if (NonAudioInstrument::readTagFromFile(reader, tagName)) {
		return true;
	}
	else if (!strcmp(tagName, "cv2Source")) {
		cvmode[1] = static_cast<CVMode>(reader.readTagOrAttributeValueInt());
	}
	else {
		return false;
	}

	reader.exitTag();
	return true;
}

bool CVInstrument::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {
	auto output_lifetime = watch_lifetime();
	if (!output_lifetime.alive())
		return false;
	auto* new_clip = modelStack ? static_cast<Clip*>(modelStack->getTimelineCounter()) : nullptr;
	if (modelStack && !new_clip)
		return false;
	auto clip_lifetime = new_clip ? new_clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (new_clip && (!clip_lifetime.alive() || new_clip->output != this))
		return false;
	auto* source_song = currentSong;
	auto* stack_song = modelStack ? modelStack->song : nullptr;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto source_channel = getChannel();
	const auto first_mode = cvmode[0];
	const auto second_mode = cvmode[1];
	const bool clip_changed = NonAudioInstrument::setActiveClip(modelStack, maySendMIDIPGMs);
	if (!output_lifetime.alive() || (new_clip && !clip_lifetime.alive()) || activeClip != new_clip
	    || (new_clip && new_clip->output != this) || currentSong != source_song
	    || deluge::gui::ui_session::current() != source_owner || getChannel() != source_channel
	    || cvmode[0] != first_mode || cvmode[1] != second_mode
	    || (modelStack && (modelStack->song != stack_song || modelStack->getTimelineCounter() != new_clip)))
		return clip_changed;
	if (clip_changed) {
		auto* expression_params = new_clip ? new_clip->paramManager.getExpressionParamSet() : nullptr;
		monophonicPitchBendValue = expression_params ? expression_params->getValue(0) : 0;
		if (expression_params) {
			cachedBendRanges[BEND_RANGE_MAIN] = expression_params->bendRanges[BEND_RANGE_MAIN];
			cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = expression_params->bendRanges[BEND_RANGE_FINGER_LEVEL];
		}
		// Cache the bend for the next note without changing the current output voltage.
		updatePitchBendOutput(false);
	}
	return clip_changed;
}

void CVInstrument::setupWithoutActiveClip(ModelStack* modelStack) {
	NonAudioInstrument::setupWithoutActiveClip(modelStack);

	monophonicPitchBendValue = 0;
}
void CVInstrument::sendMonophonicExpressionEvent(int32_t dimension) {
	int32_t new_value = add_saturate(lastCombinedPolyExpression[dimension], lastMonoExpression[dimension]) >> 16;
	switch (cvmode[1]) {

	case CVMode::off:
		break;
	case CVMode::pitch:
		break;
	case CVMode::mod:
		if (dimension == Expression::Y_SLIDE_TIMBRE) {
			cvEngine.sendVoltageOut(1, std::max<int32_t>(new_value, 0));
		}
		break;
	case CVMode::aftertouch:
		if (dimension == Expression::Z_PRESSURE) {
			cvEngine.sendVoltageOut(1, new_value);
		}
		break;
	case CVMode::velocity:
		break;
	}
}
void CVInstrument::setCV2Mode(CVMode mode) {
	cvmode[1] = mode;
}

CVInstrument::~CVInstrument() {
	retire_lifetime();
}
