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

#include "modulation/midi/midi_param_output.h"
#include "io/midi/midi_engine.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param_collection.h"
void MIDIParamCollection::sendMIDI(MIDISource source, int32_t masterChannel, int32_t cc, int32_t newValue,
                                   int32_t midiOutputFilter) {
	int32_t newValueSmall = autoparamValueToCC(newValue);

	midiEngine.sendCC(source, masterChannel, cc, newValueSmall + 64,
	                  midiOutputFilter); // TODO: get master channel
}

void notify_midi_param_value_change(ModelStackWithAutoParam const* model_stack, int32_t old_value, int32_t new_value) {
	if (model_stack->song->isOutputActiveInArrangement((MIDIInstrument*)model_stack->modControllable)) {
		bool current_value_changed = model_stack->modControllable->valueChangedEnoughToMatter(
		    old_value, new_value, deluge::modulation::params::Kind::MIDI, model_stack->paramId);
		if (current_value_changed) {
			MIDIInstrument* instrument = (MIDIInstrument*)model_stack->modControllable;
			int32_t midi_output_filter = instrument->getChannel();
			int32_t master_channel = instrument->getOutputMasterChannel();
			static_cast<MIDIParamCollection*>(model_stack->paramCollection)
			    ->sendMIDI(instrument, master_channel, model_stack->paramId, new_value, midi_output_filter);
		}
	}
}
