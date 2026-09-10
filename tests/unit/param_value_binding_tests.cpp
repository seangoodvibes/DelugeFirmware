#include "CppUTest/TestHarness.h"
#include "modulation/params/param_value_binding.h"

TEST_GROUP(param_value_binding_tests){};

TEST(param_value_binding_tests, bound_writes_and_owner_writes_use_same_value) {
	int32_t owner_value = 17;
	deluge::modulation::params::param_value_binding binding;
	binding.bind(owner_value);
	LONGS_EQUAL(17, binding.value());
	binding.value() = -42;
	LONGS_EQUAL(-42, owner_value);
	owner_value = INT32_MAX;
	LONGS_EQUAL(INT32_MAX, binding.value());
}

TEST(param_value_binding_tests, rebinding_copied_binding_does_not_modify_source) {
	int32_t source_value = 17;
	deluge::modulation::params::param_value_binding source;
	source.bind(source_value);
	auto clone = source;
	int32_t clone_value = source_value;
	clone.bind(clone_value);
	clone.value() = -42;
	LONGS_EQUAL(17, source_value);
	LONGS_EQUAL(-42, clone_value);
}

TEST(param_value_binding_tests, standalone_values_remain_independent_after_copy) {
	deluge::modulation::params::param_value_binding source;
	source.value() = 17;
	auto clone = source;
	clone.value() = -42;
	LONGS_EQUAL(17, source.value());
	LONGS_EQUAL(-42, clone.value());
}
TEST(param_value_binding_tests, standalone_binding_starts_at_zero) {
	deluge::modulation::params::param_value_binding binding;
	LONGS_EQUAL(0, binding.value());
}

TEST(param_value_binding_tests, binding_uses_existing_owner_value_without_overwriting_it) {
	deluge::modulation::params::param_value_binding binding;
	binding.value() = 17;
	int32_t owner_value = -42;
	binding.bind(owner_value);
	LONGS_EQUAL(-42, owner_value);
	LONGS_EQUAL(-42, binding.value());
	binding.value() = INT32_MIN;
	LONGS_EQUAL(INT32_MIN, owner_value);
}

TEST(param_value_binding_tests, rebinding_leaves_previous_owner_unchanged) {
	int32_t first_owner = 17, second_owner = -42;
	deluge::modulation::params::param_value_binding binding;
	binding.bind(first_owner);
	binding.value() = 99;
	binding.bind(second_owner);
	LONGS_EQUAL(-42, binding.value());
	binding.value() = INT32_MAX;
	LONGS_EQUAL(99, first_owner);
	LONGS_EQUAL(INT32_MAX, second_owner);
	first_owner = 123;
	LONGS_EQUAL(INT32_MAX, binding.value());
}
