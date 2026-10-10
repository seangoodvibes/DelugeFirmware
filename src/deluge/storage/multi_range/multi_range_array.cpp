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

#include "storage/multi_range/multi_range_array.h"
#include "definitions_cxx.hpp"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include <new>
#include <utility>

#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

MultiRangeArray::MultiRangeArray() : MultiRangeArray(sizeof(MultisampleRange)) {
}

MultiRangeArray::MultiRangeArray(int32_t element_size)
    : OrderedResizeableArray(element_size, 16, __builtin_offsetof(MultiRange, topNote), 0, 0) {
}
#pragma GCC diagnostic pop

// This function could sorta be done without...
MultiRange* MultiRangeArray::getElement(int32_t i) {
	return (MultiRange*)getElementAddress(i);
}

MultiRange* MultiRangeArray::insertMultiRange(int32_t i) {
	Error error = insertAtIndex(i);
	if (error != Error::NONE) {
		return nullptr;
	}
	void* memory = getElementAddress(i);
	MultiRange* range;

	if (elementSize == sizeof(MultisampleRange)) {
		range = new (memory) MultisampleRange();
	}
	else {
		range = new (memory) MultiWaveTableRange();
	}
	return range;
}

Error MultiRangeArray::changeType(int32_t new_size, MultiRangeArray& retired_ranges,
                                  const deluge::lifetime::callback_validation* validation) {
	if (validation && !validation->valid())
		return Error::ABORTED_BY_USER;
	const auto expected_layout = ++layout_revision_;

	if (!numElements) {
		elementSize = new_size;
		return Error::NONE;
	}

	MultiRangeArray new_array;
	new_array.elementSize = new_size;
	Error error = new_array.insertAtIndex(0, numElements);
	// The detached allocation may yield and invalidate the original owner or storage.
	if ((validation && !validation->valid()) || layout_revision_ != expected_layout)
		return Error::ABORTED_BY_USER;
	if (error != Error::NONE) {
		return error;
	}

	// We're changing range types, but want to preserve their topNotes.
	for (int32_t i = 0; i < numElements; i++) {
		MultiRange* old_range = (MultiRange*)getElementAddress(i);
		void* new_memory = new_array.getElementAddress(i);

		MultiRange* new_range;

		if (new_size == sizeof(MultisampleRange)) {
			new_range = new (new_memory) MultisampleRange();
		}
		else {
			new_range = new (new_memory) MultiWaveTableRange();
		}

		new_range->topNote = old_range->topNote;
	}

	// Publish storage without destroying holders. The caller must publish its type
	// and editor state before releasing these detached old ranges.
	swapStateWith(&new_array);
	retired_ranges.swapStateWith(&new_array);
	return Error::NONE;
}

Error MultiRangeArray::insertAtIndex(int32_t index, int32_t count, void* protected_object) {
	++layout_revision_;
	return OrderedResizeableArray::insertAtIndex(index, count, protected_object);
}

void MultiRangeArray::deleteAtIndex(int32_t index, int32_t count, bool may_shorten) {
	++layout_revision_;
	OrderedResizeableArray::deleteAtIndex(index, count, may_shorten);
}

bool MultiRangeArray::ensureEnoughSpaceAllocated(int32_t additional_count) {
	++layout_revision_;
	return OrderedResizeableArray::ensureEnoughSpaceAllocated(additional_count);
}

void MultiRangeArray::swapStateWith(MultiRangeArray* other) {
	++layout_revision_;
	++other->layout_revision_;
	OrderedResizeableArray::swapStateWith(other);
	std::swap(elementSize, other->elementSize);
}
