#pragma once

#include "modulation/params/param_value_serializer.h"

namespace deluge::modulation::params {

template <typename writer_type, typename automation_type>
void write_param_as_attribute(writer_type& writer, char const* name, int32_t value, automation_type& automation,
                              bool write_automation, bool only_if_contains_something,
                              int32_t const* value_override = nullptr) {
	// Omission is based on the source, not the override or whether nodes will be saved.
	if (only_if_contains_something && value == 0 && !automation.isAutomated()) {
		return;
	}
	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator(name);
	writer.write("\"");
	// Only automated parameters use timeline overrides, matching AutoParam::writeToFile().
	// Overrides affect output only; the owner's scalar must remain unchanged.
	write_current_value(writer, value_override && automation.isAutomated() ? *value_override : value);
	if (write_automation && automation.isAutomated()) {
		automation.write_automation(writer);
	}
	writer.write("\"");
}

} // namespace deluge::modulation::params