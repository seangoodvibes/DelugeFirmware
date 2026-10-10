#include "CppUTest/TestHarness.h"
#include "model/clip/sample_shift.h"
#include <limits>
TEST_GROUP(UISession){};

TEST(UISession, sample_shift_checks_bounds_and_overflow_before_mutation) {
	using deluge::model::shifted_sample_start;
	constexpr auto max = std::numeric_limits<uint64_t>::max();
	CHECK_FALSE(shifted_sample_start(10, 20, 100, 1, 0).has_value());
	CHECK_FALSE(shifted_sample_start(10, 20, 100, 1, -1).has_value());
	CHECK_FALSE(shifted_sample_start(20, 10, 100, 1, 1).has_value());
	CHECK_FALSE(shifted_sample_start(10, 30, 100, 1, 1).has_value());
	CHECK_FALSE(shifted_sample_start(90, 110, 100, -1, 1).has_value());
	CHECK_FALSE(shifted_sample_start(0, max, max, 2, 1).has_value());
	CHECK_FALSE(shifted_sample_start(max - 1, max, max, -2, 1).has_value());
	CHECK_FALSE(shifted_sample_start(max - 1, max, max, -1, 1).has_value());
	auto edge = shifted_sample_start(10, 20, 20, -1, 1);
	CHECK_TRUE(edge.has_value());
	UNSIGNED_LONGS_EQUAL(20, *edge);
	auto min_tick = shifted_sample_start(0, 1, max, std::numeric_limits<int32_t>::min(), 1);
	CHECK_TRUE(min_tick.has_value());
	CHECK_TRUE(*min_tick == uint64_t{2147483648});
	for (int length : {1, 3, 96}) {
		for (int ticks = -127; ticks <= 127; ++ticks) {
			const int64_t expected = 100000 - static_cast<int64_t>(ticks) * 1000 / length;
			auto result = shifted_sample_start(100000, 101000, 200000, ticks, length);
			bool valid = expected >= 0 && expected <= 200000;
			CHECK_EQUAL(valid, result.has_value());
			if (valid)
				CHECK_TRUE(*result == static_cast<uint64_t>(expected));
		}
	}
}

