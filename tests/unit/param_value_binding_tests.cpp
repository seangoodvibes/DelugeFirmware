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