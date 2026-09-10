#include "util/shift_value.h"

int32_t shift_value(int32_t value, int32_t offset) {
	int64_t new_value = (int64_t)value + offset;
	if (new_value >= (int64_t)2147483648u) {
		value = 2147483647;
	}
	else if (new_value < (int64_t)2147483648u * -1) {
		value = -2147483648;
	}
	else {
		value = new_value;
	}
	return value;
}
