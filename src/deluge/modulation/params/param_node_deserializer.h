#pragma once

#include "definitions_cxx.hpp"
#include "modulation/params/param_node.h"
#include "util/d_stringbuf.h"

namespace deluge::modulation::automation {

template <typename reader_type, typename nodes_type>
Error read_nodes(reader_type& reader, nodes_type& nodes, int32_t read_automation_up_to_pos) {
	if (!read_automation_up_to_pos) {
		return Error::NONE;
	}
	int32_t num_elements_to_allocate_for = 0;
	int32_t previous_pos = -1;
	while (true) {
		if (num_elements_to_allocate_for <= 0) {
			uint32_t chars_remaining = reader.getNumCharsRemainingInValueBeforeEndOfCluster();
			if (chars_remaining) {
				num_elements_to_allocate_for = (chars_remaining - 1) / 16 + 1;
				nodes.ensureEnoughSpaceAllocated(num_elements_to_allocate_for);
			}
		}
		// Stream one value/position pair across cluster refills. Reading the whole
		// attribute can truncate long automation at the parser's filename buffer limit.
		char const* record = reader.readNextCharsOfTagOrAttributeValue(16);
		if (!record) {
			return Error::NONE;
		}
		--num_elements_to_allocate_for;
		int32_t value = hexToIntFixedLength(record, 8);
		uint32_t encoded_pos = hexToIntFixedLength(record + 8, 8);
		bool interpolated = encoded_pos & (uint32_t{1} << 31);
		int32_t pos = encoded_pos & INT32_MAX;
		if (pos <= previous_pos) {
			continue;
		}
		if (pos >= read_automation_up_to_pos) {
			// Legacy files may put the loop's initial value at its endpoint. Wrap it
			// to zero only when doing so will not replace an existing zero-position node.
			if (pos == read_automation_up_to_pos) {
				ParamNode* first = nodes.getElement(0);
				if (!first || first->pos) {
					Error error = nodes.insertAtIndex(0);
					if (error != Error::NONE) {
						return error;
					}
					first = nodes.getElement(0);
					first->pos = 0;
					first->value = value;
					first->interpolated = interpolated;
				}
			}
			// Reaching the timeline limit stops node creation, not attribute reading.
			// JSON's exitTag() will not skip this tail after a successful scalar read.
			while (reader.readNextCharsOfTagOrAttributeValue(16)) {}
			return Error::NONE;
		}
		previous_pos = pos;
		int32_t index = nodes.insertAtKey(pos, true);
		if (index == -1) {
			return Error::INSUFFICIENT_RAM;
		}
		ParamNode* node = nodes.getElement(index);
		node->value = value;
		node->interpolated = interpolated;
	}
}

} // namespace deluge::modulation::automation