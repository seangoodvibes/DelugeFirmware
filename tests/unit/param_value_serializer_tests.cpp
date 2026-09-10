#include "CppUTest/TestHarness.h"
#include "modulation/params/param_node_deserializer.h"
#include "modulation/params/param_value_deserializer.h"
#include "modulation/params/param_value_serializer.h"
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct value_reader {
	std::string_view remaining;
	std::array<char, 16> buffer{};
	size_t largest_read = 0;
	bool prepared = true;

	bool prepareToReadTagOrAttributeValueOneCharAtATime() { return prepared; }
	uint32_t getNumCharsRemainingInValueBeforeEndOfCluster() { return std::min<size_t>(remaining.size(), 63); }
	char const* readNextCharsOfTagOrAttributeValue(int32_t count) {
		largest_read = std::max(largest_read, static_cast<size_t>(count));
		if (remaining.size() < static_cast<size_t>(count)) {
			remaining = {};
			return nullptr;
		}
		std::copy_n(remaining.begin(), count, buffer.begin());
		remaining.remove_prefix(count);
		return buffer.data();
	}
};

struct value_writer {
	std::string output;
	void write(char const* text) { output += text; }
};

struct test_nodes {
	std::vector<ParamNode> values;
	bool fail_allocation = false;

	bool ensureEnoughSpaceAllocated(int32_t count) { return !fail_allocation; }
	ParamNode* getElement(int32_t index) {
		return index >= 0 && static_cast<size_t>(index) < values.size() ? &values[index] : nullptr;
	}
	Error insertAtIndex(int32_t index) {
		if (fail_allocation) {
			return Error::INSUFFICIENT_RAM;
		}
		values.insert(values.begin() + index, ParamNode{});
		return Error::NONE;
	}
	int32_t insertAtKey(int32_t pos, bool is_last) {
		if (fail_allocation) {
			return -1;
		}
		values.emplace_back();
		values.back().pos = pos;
		return static_cast<int32_t>(values.size()) - 1;
	}
};

std::string record(int32_t value, uint32_t position) {
	char digits[9];
	intToHex(value, digits);
	std::string result = digits;
	intToHex(position, digits);
	return result + digits;
}

} // namespace

TEST_GROUP(param_value_serializer_tests){};

TEST(param_value_serializer_tests, scalar_round_trip_without_auto_param) {
	for (int32_t expected : {INT32_MIN, -1, 0, 1, INT32_MAX}) {
		value_writer writer;
		deluge::modulation::params::write_current_value(writer, expected);
		LONGS_EQUAL(10, writer.output.size());
		value_reader reader{writer.output};
		int32_t actual = 37;
		CHECK(deluge::modulation::params::read_current_value(reader, actual));
		LONGS_EQUAL(expected, actual);
		CHECK(reader.remaining.empty());
	}
}

TEST(param_value_serializer_tests, decimal_legacy_values) {
	for (int32_t expected : {INT32_MIN, -1, 0, 7, 12, INT32_MAX}) {
		std::string text = std::to_string(expected);
		value_reader reader{text};
		int32_t actual = 37;
		CHECK_FALSE(deluge::modulation::params::read_current_value(reader, actual));
		LONGS_EQUAL(expected, actual);
	}
}

TEST(param_value_serializer_tests, incomplete_values_preserve_existing_value) {
	for (std::string_view text : {"", "0x", "0x1234567"}) {
		value_reader reader{text};
		int32_t value = 37;
		CHECK_FALSE(deluge::modulation::params::read_current_value(reader, value));
		LONGS_EQUAL(37, value);
	}
	value_reader reader{"0x00000001"};
	reader.prepared = false;
	int32_t value = 37;
	CHECK_FALSE(deluge::modulation::params::read_current_value(reader, value));
	LONGS_EQUAL(37, value);
}

TEST(param_value_serializer_tests, scalar_read_leaves_long_automation_payload_untouched) {
	std::string payload;
	for (int32_t index = 0; index < 1024; ++index) {
		payload += "1234567800000001";
	}
	std::string serialized = "0x80000000" + payload;
	value_reader reader{serialized};
	int32_t value = 0;
	CHECK(deluge::modulation::params::read_current_value(reader, value));
	LONGS_EQUAL(INT32_MIN, value);
	CHECK(reader.remaining == payload);
	LONGS_EQUAL(8, reader.largest_read);
}

TEST(param_value_serializer_tests, stream_all_automation_nodes_through_small_buffer) {
	std::string serialized = "0x00000025";
	for (uint32_t index = 0; index < 1024; ++index) {
		serialized += record(-static_cast<int32_t>(index), index | ((index % 2) << 31));
	}
	value_reader reader{serialized};
	int32_t current_value = 0;
	CHECK(deluge::modulation::params::read_current_value(reader, current_value));
	test_nodes nodes;
	CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 2048) == Error::NONE);
	LONGS_EQUAL(37, current_value);
	LONGS_EQUAL(1024, nodes.values.size());
	LONGS_EQUAL(16, reader.largest_read);
	for (int32_t index = 0; index < 1024; ++index) {
		LONGS_EQUAL(index, nodes.values[index].pos);
		LONGS_EQUAL(-index, nodes.values[index].value);
		CHECK_EQUAL(index % 2 != 0, nodes.values[index].interpolated);
	}
}

TEST(param_value_serializer_tests, skip_out_of_order_and_partial_records) {
	std::string serialized = record(12, 1) + record(99, 1) + record(98, 0) + record(13, 2) + "123";
	value_reader reader{serialized};
	test_nodes nodes;
	CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 10) == Error::NONE);
	LONGS_EQUAL(2, nodes.values.size());
	LONGS_EQUAL(12, nodes.values[0].value);
	LONGS_EQUAL(13, nodes.values[1].value);
}

TEST(param_value_serializer_tests, convert_legacy_loop_end_node_to_zero) {
	std::string serialized = record(12, 4) + record(37, (uint32_t{1} << 31) | 8);
	value_reader reader{serialized};
	test_nodes nodes;
	CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 8) == Error::NONE);
	LONGS_EQUAL(2, nodes.values.size());
	LONGS_EQUAL(0, nodes.values[0].pos);
	LONGS_EQUAL(37, nodes.values[0].value);
	CHECK(nodes.values[0].interpolated);
}

TEST(param_value_serializer_tests, preserve_existing_zero_node_and_respect_length_limit) {
	std::string serialized = record(12, 0) + record(37, 8) + record(99, 9);
	value_reader reader{serialized};
	test_nodes nodes;
	CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 8) == Error::NONE);
	LONGS_EQUAL(1, nodes.values.size());
	LONGS_EQUAL(12, nodes.values[0].value);
	CHECK(reader.remaining.empty());
}

TEST(param_value_serializer_tests, propagate_node_allocation_failures) {
	for (uint32_t pos : {0u, 8u}) {
		std::string serialized = record(37, pos);
		value_reader reader{serialized};
		test_nodes nodes;
		nodes.fail_allocation = true;
		CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 8) == Error::INSUFFICIENT_RAM);
		CHECK(nodes.values.empty());
	}
}

TEST(param_value_serializer_tests, disabled_automation_does_not_consume_nodes) {
	std::string serialized = record(37, 0);
	value_reader reader{serialized};
	test_nodes nodes;
	CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 0) == Error::NONE);
	CHECK(reader.remaining == serialized);
	CHECK(nodes.values.empty());
}
TEST(param_value_serializer_tests, malformed_decimal_values_preserve_owner_and_consume_attribute) {
	for (std::string_view text : {"-", "+1", "abc", "12junk", "1.5", " 7", "7 ", "--1", "1-2"}) {
		value_reader reader{text};
		int32_t value = 37;
		CHECK_FALSE(deluge::modulation::params::read_current_value(reader, value));
		LONGS_EQUAL(37, value);
		CHECK(reader.remaining.empty());
	}
}

TEST(param_value_serializer_tests, overflowing_and_overlength_decimals_preserve_owner_and_consume_attribute) {
	for (std::string_view text : {"2147483648", "-2147483649", "999999999999999999999999", "-999999999999999999999999",
	                              "000000000001", "00000000000junk"}) {
		value_reader reader{text};
		int32_t value = -42;
		CHECK_FALSE(deluge::modulation::params::read_current_value(reader, value));
		LONGS_EQUAL(-42, value);
		CHECK(reader.remaining.empty());
	}
}

TEST(param_value_serializer_tests, failed_legacy_endpoint_insertion_preserves_existing_nodes) {
	std::string serialized = record(37, (uint32_t{1} << 31) | 8);
	value_reader reader{serialized};
	test_nodes nodes;
	CHECK(nodes.insertAtIndex(0) == Error::NONE);
	nodes.values[0].pos = 4;
	nodes.values[0].value = 12;
	nodes.values[0].interpolated = false;
	nodes.fail_allocation = true;
	CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 8) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, nodes.values.size());
	LONGS_EQUAL(4, nodes.values[0].pos);
	LONGS_EQUAL(12, nodes.values[0].value);
	CHECK_FALSE(nodes.values[0].interpolated);
	// Retry from the saved input: a failed reader has already consumed the record.
	nodes.fail_allocation = false;
	value_reader retry{serialized};
	CHECK(deluge::modulation::automation::read_nodes(retry, nodes, 8) == Error::NONE);
	LONGS_EQUAL(2, nodes.values.size());
	LONGS_EQUAL(0, nodes.values[0].pos);
	LONGS_EQUAL(37, nodes.values[0].value);
	CHECK(nodes.values[0].interpolated);
	LONGS_EQUAL(4, nodes.values[1].pos);
	LONGS_EQUAL(12, nodes.values[1].value);
}
