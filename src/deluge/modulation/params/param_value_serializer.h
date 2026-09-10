#pragma once

#include "util/d_stringbuf.h"

namespace deluge::modulation::params {

template <typename writer_type>
void write_current_value(writer_type& writer, int32_t value) {
	char digits[9];
	intToHex(value, digits);
	writer.write("0x");
	writer.write(digits);
}

} // namespace deluge::modulation::params