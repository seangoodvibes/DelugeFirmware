#include "CppUTest/TestHarness.h"
#include "util/shift_value.h"

TEST_GROUP(shift_value_tests){};

TEST(shift_value_tests, zero_offset_preserves_values) {
	for (int32_t value : {INT32_MIN, -1, 0, 1, INT32_MAX}) {
		LONGS_EQUAL(value, shift_value(value, 0));
	}
}

TEST(shift_value_tests, shifts_exactly_when_result_is_representable) {
	LONGS_EQUAL(42, shift_value(17, 25));
	LONGS_EQUAL(-42, shift_value(-17, -25));
	LONGS_EQUAL(0, shift_value(INT32_MAX, -INT32_MAX));
	LONGS_EQUAL(-1, shift_value(INT32_MIN, INT32_MAX));
	LONGS_EQUAL(INT32_MAX, shift_value(0, INT32_MAX));
	LONGS_EQUAL(INT32_MIN, shift_value(0, INT32_MIN));
	LONGS_EQUAL(INT32_MAX, shift_value(INT32_MAX - 1, 1));
	LONGS_EQUAL(INT32_MIN, shift_value(INT32_MIN + 1, -1));
}

TEST(shift_value_tests, overflowing_shifts_clamp_without_wrapping) {
	LONGS_EQUAL(INT32_MAX, shift_value(INT32_MAX, 1));
	LONGS_EQUAL(INT32_MIN, shift_value(INT32_MIN, -1));
	LONGS_EQUAL(INT32_MAX, shift_value(INT32_MAX, INT32_MAX));
	LONGS_EQUAL(INT32_MIN, shift_value(INT32_MIN, INT32_MIN));
	LONGS_EQUAL(INT32_MAX, shift_value(17, INT32_MAX));
	LONGS_EQUAL(INT32_MIN, shift_value(-17, INT32_MIN));
}
