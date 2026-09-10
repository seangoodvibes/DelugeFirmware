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

#include "modulation/midi/midi_param.h"
#include "modulation/automation/auto_param_pool.h"
#include <cstring>
#include <utility>

MIDIParam::~MIDIParam() {
	release_automation();
}

AutoParam* MIDIParam::get_auto_param(bool allow_creation) {
	if (!automation_ && allow_creation) {
		automation_ = auto_param_pool::get().acquire();
		rebind_automation();
	}
	return automation_;
}

void MIDIParam::rebind_automation() {
	if (automation_)
		automation_->bind_current_value(current_value_);
}

void MIDIParam::release_automation() {
	auto_param_pool::get().release(std::exchange(automation_, nullptr));
}

void MIDIParam::release_unautomated() {
	if (!is_automated())
		release_automation();
}

Error MIDIParam::clone_automation(bool copy_automation, int32_t reverse_length) {
	// Called only after the vector's raw copy. Never release the source pointer.
	auto* source = std::exchange(automation_, nullptr);
	if (!copy_automation || !source || !source->isAutomated())
		return Error::NONE;
	auto* destination = get_auto_param(true);
	if (!destination)
		return Error::INSUFFICIENT_RAM;
	memcpy(destination, source, sizeof(AutoParam));
	rebind_automation();
	auto error = destination->beenCloned(true, reverse_length);
	release_unautomated();
	return error;
}

Error MIDIParam::read_from_file(Deserializer& reader, int32_t automation_limit) {
	AutoParam loaded;
	auto error = loaded.readFromFile(reader, automation_limit);
	release_automation();
	current_value_ = loaded.getCurrentValue();
	if (error != Error::NONE)
		return error;
	if (loaded.isAutomated()) {
		auto* destination = get_auto_param(true);
		if (!destination)
			return Error::INSUFFICIENT_RAM;
		destination->nodes.swapStateWith(&loaded.nodes);
	}
	return Error::NONE;
}

void MIDIParam::write_to_file(Serializer& writer) {
	if (automation_)
		automation_->writeToFile(writer, true);
	else {
		AutoParam scalar;
		scalar.setCurrentValueBasicForSetup(current_value_);
		scalar.writeToFile(writer, false);
	}
}
