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

#pragma once

#include "modulation/automation/auto_param.h"

class MIDIParam {
public:
	MIDIParam() = default;
	~MIDIParam();
	MIDIParam(const MIDIParam&) = delete;
	MIDIParam& operator=(const MIDIParam&) = delete;
	int32_t get_current_value() const { return current_value_; }
	void set_current_value(int32_t value) { current_value_ = value; }
	AutoParam* get_auto_param(bool allow_creation = false);
	bool is_automated() const { return automation_ && automation_->isAutomated(); }
	void rebind_automation();
	void release_automation();
	void release_unautomated();
	Error clone_automation(bool copy_automation, int32_t reverse_length);
	Error read_from_file(Deserializer& reader, int32_t automation_limit);
	void write_to_file(Serializer& writer);

	uint8_t cc = 0; // Must remain the first member: the vector sorts on this byte.

private:
	friend class MIDIParamCollection;
	int32_t current_value_ = 0;
	AutoParam* automation_ = nullptr;
};
