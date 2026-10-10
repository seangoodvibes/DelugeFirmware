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

#include "util/container/array/ordered_resizeable_array.h"
#include "util/lifetime.h"

class MultisampleRange;
class MultiWaveTableRange;
class MultiRange;

class MultiRangeArray final : private OrderedResizeableArray {
public:
	MultiRangeArray();
	explicit MultiRangeArray(int32_t element_size);
	MultiRangeArray(const MultiRangeArray&) = delete;
	MultiRangeArray& operator=(const MultiRangeArray&) = delete;
	using OrderedResizeableArray::getElementAddress;
	using OrderedResizeableArray::getNumElements;
	using OrderedResizeableArray::search;
	uint32_t element_size() const { return elementSize; }
	uint32_t layout_revision() const { return layout_revision_; }
	Error insertAtIndex(int32_t index, int32_t count = 1, void* protected_object = nullptr);
	void deleteAtIndex(int32_t index, int32_t count = 1, bool may_shorten = true);
	bool ensureEnoughSpaceAllocated(int32_t additional_count);
	void swapStateWith(MultiRangeArray* other);
	MultiRange* getElement(int32_t i);
	MultiRange* insertMultiRange(int32_t i);
	// On success, the caller owns destruction of ranges transferred to the empty retired table.
	Error changeType(int32_t new_size, MultiRangeArray& retired_ranges,
	                 const deluge::lifetime::callback_validation* validation = nullptr);

private:
	// Invalidate retained range identities before any structural mutation attempt.
	uint32_t layout_revision_ = 0;
};
