#pragma once

#include "modulation/params/param_value_deserializer.h"

namespace deluge::modulation::params {

template <typename reader_type, typename automation_type>
void read_param(reader_type& reader, int32_t& value, automation_type& automation, int32_t read_automation_up_to_pos,
                uint32_t mask, uint32_t& automated_flags, uint32_t& interpolating_flags) {
	// Load the scalar independently; replacing automation must not overwrite it
	// with a node value or leave nodes from the previously loaded parameter.
	bool has_hex_value = read_current_value(reader, value);
	if (read_automation_up_to_pos && has_hex_value) {
		automation.read_automation(reader, read_automation_up_to_pos);
	}
	else {
		automation.deleteAutomationBasicForSetup();
		// JSON marks the scalar read as complete, so exitTag() will not skip an
		// unused node payload. Consume it without allocating to reach the next key.
		if (has_hex_value) {
			while (reader.readNextCharsOfTagOrAttributeValue(16)) {}
		}
	}
	if (automation.isAutomated()) {
		automated_flags |= mask;
	}
	else {
		automated_flags &= ~mask;
	}
	// Playback interpolation state belongs to the old payload, never the new load.
	interpolating_flags &= ~mask;
}

} // namespace deluge::modulation::params