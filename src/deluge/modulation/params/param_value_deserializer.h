#pragma once

#include "util/d_stringbuf.h"
#include <charconv>

namespace deluge::modulation::params {

template <typename reader_type>
bool read_current_value(reader_type& reader, int32_t& value) {
	if (!reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
		return false;
	}
	// Read the prefix one character at a time: a two-character request can consume
	// a one-digit decimal's closing quote without returning its digit.
	char const* next = reader.readNextCharsOfTagOrAttributeValue(1);
	if (!next) {
		return false;
	}
	char first = *next;
	next = reader.readNextCharsOfTagOrAttributeValue(1);
	char second = next ? *next : 0;
	if (first == '0' && second == 'x') {
		char const* digits = reader.readNextCharsOfTagOrAttributeValue(8);
		if (!digits) {
			return false;
		}
		value = hexToIntFixedLength(digits, 8);
		// Leave any following node records for read_nodes(); true means hex scalar,
		// not that automation is necessarily present.
		return true;
	}
	// INT32_MIN needs all 11 numeric characters. Allow one more read to consume
	// the closing quote; otherwise JSON traversal loses the following attribute.
	// Only the first 11 characters participate in the numeric conversion below.
	char digits[12] = {first, second};
	for (int32_t index = 2; second && index < 12; ++index) {
		next = reader.readNextCharsOfTagOrAttributeValue(1);
		if (!next) {
			break;
		}
		digits[index] = *next;
	}
	std::from_chars(digits, digits + 11, value);
	return false;
}

} // namespace deluge::modulation::params