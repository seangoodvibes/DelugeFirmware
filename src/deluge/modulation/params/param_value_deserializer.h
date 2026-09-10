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
	// A signed 32-bit decimal needs at most 11 characters. Consume the entire
	// attribute even if it is invalid, so the next XML/JSON attribute stays readable.
	char digits[11] = {first};
	size_t length = 1;
	bool too_long = false;
	if (next) {
		digits[length++] = second;
		while ((next = reader.readNextCharsOfTagOrAttributeValue(1))) {
			if (length < sizeof(digits)) {
				digits[length++] = *next;
			}
			else {
				too_long = true;
			}
		}
	}
	int32_t parsed_value;
	auto result = std::from_chars(digits, digits + length, parsed_value);
	if (!too_long && result.ec == std::errc{} && result.ptr == digits + length) {
		value = parsed_value;
	}
	return false;
}

} // namespace deluge::modulation::params