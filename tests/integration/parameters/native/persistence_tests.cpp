#include "CppUTest/TestHarness.h"
#include "modulation/params/param_deserializer.h"
#include "modulation/params/param_node_deserializer.h"
#include "modulation/params/param_serializer.h"
#include "modulation/params/param_value_deserializer.h"
#include "modulation/params/param_value_serializer.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include <string>
#include <string_view>
#include <vector>

namespace native_parameter_tests {
extern std::string_view file_contents;
extern size_t read_calls;
extern size_t fail_read_at;
} // namespace native_parameter_tests

namespace {
struct value_writer {
	std::string output;
	void write(char const* text) { output += text; }
};

struct node_storage {
	std::vector<ParamNode> values;
	int32_t fail_insertion_at = -1;
	int32_t insertion_attempts = 0;
	bool fail_reservation = false;
	int32_t reservation_attempts = 0;
	bool ensureEnoughSpaceAllocated(int32_t) {
		++reservation_attempts;
		return !fail_reservation;
	}
	ParamNode* getElement(int32_t index) {
		return static_cast<size_t>(index) < values.size() ? &values[index] : nullptr;
	}
	Error insertAtIndex(int32_t index) {
		if (insertion_attempts++ == fail_insertion_at) {
			return Error::INSUFFICIENT_RAM;
		}
		values.insert(values.begin() + index, ParamNode{});
		return Error::NONE;
	}
	int32_t insertAtKey(int32_t position, bool) {
		if (insertion_attempts++ == fail_insertion_at) {
			return -1;
		}
		values.emplace_back();
		values.back().pos = position;
		return values.size() - 1;
	}
};

template <typename reader_type>
void check_document(bool json, std::string const& value, int32_t expected, int32_t node_count, size_t padding) {
	std::string document = std::string(padding, ' ') + (json ? "{\"value\":\"" : "<params value=\"") + value
	                       + (json ? "\",\"sentinel\":73}" : "\" sentinel=\"73\"/>");
	native_parameter_tests::file_contents = document;
	native_parameter_tests::read_calls = 0;
	reader_type reader;
	if (json) {
		CHECK(reader.match('{'));
	}
	else {
		STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
	}
	STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
	int32_t scalar = 999;
	bool has_hex = deluge::modulation::params::read_current_value(reader, scalar);
	LONGS_EQUAL(expected, scalar);
	node_storage nodes;
	if (has_hex) {
		CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 100000) == Error::NONE);
	}
	LONGS_EQUAL(node_count, nodes.values.size());
	for (int32_t index = 0; index < node_count; ++index) {
		LONGS_EQUAL(index * 2, nodes.values[index].pos);
		LONGS_EQUAL(index * 100, nodes.values[index].value);
		CHECK_EQUAL(bool(index % 2), bool(nodes.values[index].interpolated));
	}
	reader.exitTag("value");
	STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
	LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
	reader.exitTag("sentinel");
	if (node_count) {
		CHECK(native_parameter_tests::read_calls > 1);
	}
}

void check_both(std::string const& value, int32_t expected, int32_t node_count) {
	for (size_t padding = 0; padding < Cluster::size; ++padding) {
		check_document<XMLDeserializer>(false, value, expected, node_count, padding);
		check_document<JsonDeserializer>(true, value, expected, node_count, padding);
	}
}
} // namespace

TEST_GROUP(native_parameter_persistence){void teardown() override{native_parameter_tests::file_contents = {};
native_parameter_tests::fail_read_at = 0;
}
}
;

TEST(native_parameter_persistence, scalar_round_trips_at_every_cluster_alignment) {
	for (int32_t scalar : {INT32_MIN, -1, 0, 1, INT32_MAX}) {
		value_writer writer;
		deluge::modulation::params::write_current_value(writer, scalar);
		check_both(writer.output, scalar, 0);
	}
}

TEST(native_parameter_persistence, legacy_decimal_values_leave_next_attribute_readable) {
	for (int32_t scalar : {INT32_MIN, -1, 0, 7, 12, INT32_MAX}) {
		check_both(std::to_string(scalar), scalar, 0);
	}
}

TEST(native_parameter_persistence, long_automation_preserves_scalar_nodes_and_parser_position) {
	value_writer writer;
	deluge::modulation::params::write_current_value(writer, 37);
	for (int32_t index = 0; index < 1024; ++index) {
		char digits[9];
		intToHex(index * 100, digits);
		writer.write(digits);
		intToHex(static_cast<uint32_t>(index * 2) | (index % 2 ? 0x80000000u : 0), digits);
		writer.write(digits);
	}
	check_both(writer.output, 37, 1024);
}

namespace {
template <typename writer_type, typename reader_type>
void check_writer_round_trip(bool json, int32_t scalar, int32_t node_count) {
	value_writer encoded;
	deluge::modulation::params::write_current_value(encoded, scalar);
	for (int32_t index = 0; index < node_count; ++index) {
		char digits[9];
		intToHex(index * 100, digits);
		encoded.write(digits);
		intToHex(static_cast<uint32_t>(index * 2) | (index % 2 ? 0x80000000u : 0), digits);
		encoded.write(digits);
	}
	std::string original = encoded.output;
	writer_type writer;
	writer.writeOpeningTagBeginning(json ? nullptr : "params");
	writer.writeAttribute("value", encoded.output.c_str());
	writer.writeAttribute("sentinel", 73);
	writer.closeTag();
	STRCMP_EQUAL(original.c_str(), encoded.output.c_str());
	std::string document(writer.getBufferPtr(), writer.bytesWritten());
	for (size_t padding = 0; padding < Cluster::size; ++padding) {
		std::string input = std::string(padding, ' ') + document;
		native_parameter_tests::file_contents = input;
		reader_type reader;
		if (json) {
			CHECK(reader.match('{'));
		}
		else {
			STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
		}
		STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
		int32_t loaded = 999;
		CHECK(deluge::modulation::params::read_current_value(reader, loaded));
		LONGS_EQUAL(scalar, loaded);
		node_storage nodes;
		CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 100000) == Error::NONE);
		LONGS_EQUAL(node_count, nodes.values.size());
		for (int32_t index = 0; index < node_count; ++index) {
			LONGS_EQUAL(index * 100, nodes.values[index].value);
			LONGS_EQUAL(index * 2, nodes.values[index].pos);
			CHECK_EQUAL(bool(index % 2), bool(nodes.values[index].interpolated));
		}
		reader.exitTag("value");
		STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
		LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
		reader.exitTag("sentinel");
		STRCMP_EQUAL("", reader.readNextTagOrAttributeName());
	}
}

template <typename writer_type>
void check_writer_reset(bool json) {
	writer_type writer;
	writer.writeOpeningTagBeginning(json ? nullptr : "discarded");
	writer.writeAttribute("old", 17);
	writer.reset();
	writer.writeOpeningTagBeginning(json ? nullptr : "params");
	writer.writeAttribute("value", "0x00000025");
	writer.closeTag();
	std::string actual(writer.getBufferPtr(), writer.bytesWritten());
	STRCMP_EQUAL(json ? "\n{\n\t\"value\": \"0x00000025\"}" : "<params\n\tvalue=\"0x00000025\" />\n", actual.c_str());
}
} // namespace

TEST(native_parameter_persistence, production_writers_round_trip_scalars_and_automation) {
	for (int32_t scalar : {INT32_MIN, -1, 0, 37, INT32_MAX}) {
		for (int32_t node_count : {0, 1024}) {
			check_writer_round_trip<XMLSerializer, XMLDeserializer>(false, scalar, node_count);
			check_writer_round_trip<JsonSerializer, JsonDeserializer>(true, scalar, node_count);
		}
	}
}

TEST(native_parameter_persistence, writer_reset_discards_unclosed_document_formatting_state) {
	check_writer_reset<XMLSerializer>(false);
	check_writer_reset<JsonSerializer>(true);
}

namespace {
struct automation_storage {
	node_storage nodes;
	Error last_read_error = Error::NONE;
	template <typename reader_type>
	Error read_automation(reader_type& reader, int32_t limit) {
		nodes.values.clear();
		last_read_error = deluge::modulation::automation::read_nodes(reader, nodes, limit);
		return last_read_error;
	}
	void deleteAutomationBasicForSetup() { nodes.values.clear(); }
	bool isAutomated() { return !nodes.values.empty(); }
};

template <typename writer_type, typename reader_type>
void check_reload_transitions(bool json) {
	for (uint32_t mask : {uint32_t{1}, uint32_t{1} << 31}) {
		automation_storage automation;
		int32_t scalar = 999;
		uint32_t automated_flags = ~mask;
		uint32_t interpolating_flags = UINT32_MAX;
		auto load = [&](char const* payload, int32_t limit) {
			writer_type writer;
			writer.writeOpeningTagBeginning(json ? nullptr : "params");
			writer.writeAttribute("value", payload);
			writer.writeAttribute("sentinel", 73);
			writer.closeTag();
			std::string document(writer.getBufferPtr(), writer.bytesWritten());
			native_parameter_tests::file_contents = document;
			reader_type reader;
			if (json) {
				CHECK(reader.match('{'));
			}
			else {
				STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
			}
			STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
			interpolating_flags |= mask;
			deluge::modulation::params::read_param(reader, scalar, automation, limit, mask, automated_flags,
			                                       interpolating_flags);
			CHECK_EQUAL(automation.isAutomated(), bool(automated_flags & mask));
			CHECK_EQUAL(~mask, automated_flags & ~mask);
			CHECK_EQUAL(~mask, interpolating_flags);
			reader.exitTag("value");
			STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
			LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
			reader.exitTag("sentinel");
		};
		for (int32_t repetition = 0; repetition < 3; ++repetition) {
			load("0x000000250000006400000000000000C880000008", 16);
			LONGS_EQUAL(37, scalar);
			LONGS_EQUAL(2, automation.nodes.values.size());
			LONGS_EQUAL(200, automation.nodes.values[1].value);
			CHECK(automation.nodes.values[1].interpolated);
			load("0xFFFFFFD60000012C00000004", 16);
			LONGS_EQUAL(-42, scalar);
			LONGS_EQUAL(1, automation.nodes.values.size());
			LONGS_EQUAL(4, automation.nodes.values[0].pos);
			LONGS_EQUAL(300, automation.nodes.values[0].value);
			load("0x00000011", 16);
			LONGS_EQUAL(17, scalar);
			CHECK_FALSE(automation.isAutomated());
			load("0x000000250000006400000000", 16);
			load("-2147483648", 16);
			LONGS_EQUAL(INT32_MIN, scalar);
			CHECK_FALSE(automation.isAutomated());
			load("0x000000250000006400000000", 16);
			load("0x00000029000000C800000000", 0);
			LONGS_EQUAL(41, scalar);
			CHECK_FALSE(automation.isAutomated());
		}
	}
}
} // namespace

TEST(native_parameter_persistence, reload_replaces_nodes_and_only_updates_target_flags) {
	check_reload_transitions<XMLSerializer, XMLDeserializer>(false);
	check_reload_transitions<JsonSerializer, JsonDeserializer>(true);
}

namespace {
template <typename writer_type, typename reader_type>
void check_timeline_limit(bool json, bool existing_zero, bool exact_endpoint) {
	value_writer payload;
	deluge::modulation::params::write_current_value(payload, 37);
	auto append_node = [&](int32_t value, uint32_t position) {
		char digits[9];
		intToHex(value, digits);
		payload.write(digits);
		intToHex(position, digits);
		payload.write(digits);
	};
	if (existing_zero) {
		append_node(100, 0);
	}
	append_node(200, 4);
	append_node(300, (exact_endpoint ? 8u : 9u) | 0x80000000u);
	for (uint32_t position = 10; position < 100; ++position) {
		append_node(400, position);
	}
	writer_type writer;
	writer.writeOpeningTagBeginning(json ? nullptr : "params");
	writer.writeAttribute("value", payload.output.c_str());
	writer.writeAttribute("sentinel", 73);
	writer.closeTag();
	std::string document(writer.getBufferPtr(), writer.bytesWritten());
	for (size_t padding = 0; padding < Cluster::size; ++padding) {
		std::string input = std::string(padding, ' ') + document;
		native_parameter_tests::file_contents = input;
		reader_type reader;
		if (json) {
			CHECK(reader.match('{'));
		}
		else {
			STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
		}
		STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
		automation_storage automation;
		int32_t scalar = 999;
		uint32_t automated_flags = 2;
		uint32_t interpolating_flags = 3;
		deluge::modulation::params::read_param(reader, scalar, automation, 8, 1, automated_flags, interpolating_flags);
		LONGS_EQUAL(37, scalar);
		LONGS_EQUAL(3, automated_flags);
		LONGS_EQUAL(2, interpolating_flags);
		bool has_zero = existing_zero || exact_endpoint;
		LONGS_EQUAL(has_zero ? 2 : 1, automation.nodes.values.size());
		if (has_zero) {
			LONGS_EQUAL(0, automation.nodes.values[0].pos);
			LONGS_EQUAL(existing_zero ? 100 : 300, automation.nodes.values[0].value);
			CHECK_EQUAL(!existing_zero, bool(automation.nodes.values[0].interpolated));
		}
		auto const& last = automation.nodes.values.back();
		LONGS_EQUAL(4, last.pos);
		LONGS_EQUAL(200, last.value);
		CHECK_FALSE(last.interpolated);
		reader.exitTag("value");
		STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
		LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
		reader.exitTag("sentinel");
		STRCMP_EQUAL("", reader.readNextTagOrAttributeName());
	}
}
} // namespace

TEST(native_parameter_persistence, timeline_limit_preserves_endpoint_rules_and_next_attribute) {
	for (bool existing_zero : {false, true}) {
		for (bool exact_endpoint : {false, true}) {
			check_timeline_limit<XMLSerializer, XMLDeserializer>(false, existing_zero, exact_endpoint);
			check_timeline_limit<JsonSerializer, JsonDeserializer>(true, existing_zero, exact_endpoint);
		}
	}
}

namespace {
template <typename reader_type>
void check_incomplete_payload(bool json, bool disk_failure) {
	std::string prefix = json ? "{\"value\":\"" : "<params value=\"";
	std::string payload = "0x000000250000006400000000000000C8800000040000012C00000008";
	for (size_t available = 0; available <= payload.size(); ++available) {
		for (size_t alignment = 0; alignment < (disk_failure ? 1 : Cluster::size); ++alignment) {
			size_t padding = disk_failure
			                     ? (Cluster::size - (prefix.size() + available) % Cluster::size) % Cluster::size
			                     : alignment;
			std::string input = std::string(padding, ' ') + prefix;
			size_t payload_start = input.size();
			input += disk_failure ? payload : payload.substr(0, available);
			if (disk_failure) {
				input += json ? "\",\"sentinel\":73}" : "\" sentinel=\"73\"/>";
			}
			native_parameter_tests::file_contents = input;
			native_parameter_tests::read_calls = 0;
			native_parameter_tests::fail_read_at = disk_failure ? (payload_start + available) / Cluster::size + 1 : 0;
			reader_type reader;
			if (json) {
				CHECK(reader.match('{'));
			}
			else {
				STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
			}
			STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
			automation_storage automation;
			automation.nodes.values.emplace_back();
			automation.nodes.values.back().pos = 99;
			int32_t scalar = 999;
			uint32_t automated_flags = 3;
			uint32_t interpolating_flags = 3;
			deluge::modulation::params::read_param(reader, scalar, automation, 100, 1, automated_flags,
			                                       interpolating_flags);
			// A lone '0' is a valid legacy decimal; an incomplete '0x...' leaves the scalar unchanged.
			LONGS_EQUAL(available >= 10 ? 37 : (available == 1 ? 0 : 999), scalar);
			size_t complete_nodes = available >= 10 ? (available - 10) / 16 : 0;
			LONGS_EQUAL(complete_nodes, automation.nodes.values.size());
			for (size_t index = 0; index < complete_nodes; ++index) {
				LONGS_EQUAL((index + 1) * 100, automation.nodes.values[index].value);
				LONGS_EQUAL(index * 4, automation.nodes.values[index].pos);
				CHECK_EQUAL(index == 1, bool(automation.nodes.values[index].interpolated));
			}
			LONGS_EQUAL(complete_nodes ? 3 : 2, automated_flags);
			LONGS_EQUAL(2, interpolating_flags);
			CHECK(native_parameter_tests::read_calls <= (payload_start + available) / Cluster::size + 3);
			if (disk_failure) {
				CHECK(native_parameter_tests::read_calls >= native_parameter_tests::fail_read_at);
			}
		}
	}
}
} // namespace

TEST(native_parameter_persistence, truncated_scalar_and_nodes_preserve_only_complete_records) {
	check_incomplete_payload<XMLDeserializer>(false, false);
	check_incomplete_payload<JsonDeserializer>(true, false);
}

TEST(native_parameter_persistence, disk_errors_during_scalar_and_node_refills_terminate_safely) {
	check_incomplete_payload<XMLDeserializer>(false, true);
	check_incomplete_payload<JsonDeserializer>(true, true);
}

namespace {
template <typename writer_type, typename reader_type>
void check_allocation_failure(bool json, bool endpoint, int32_t fail_at, bool fail_reservation) {
	// Endpoint cases have no zero-position node, forcing insertAtIndex() to wrap the endpoint.
	char const* payload = endpoint ? "0x000000250000006400000004000000C8800000080000012C0000000C"
	                               : "0x000000250000006400000000000000C8800000040000012C00000008";
	writer_type writer;
	writer.writeOpeningTagBeginning(json ? nullptr : "params");
	writer.writeAttribute("value", payload);
	writer.writeAttribute("sentinel", 73);
	writer.closeTag();
	std::string document(writer.getBufferPtr(), writer.bytesWritten());
	for (size_t padding = 0; padding < Cluster::size; ++padding) {
		std::string input = std::string(padding, ' ') + document;
		native_parameter_tests::file_contents = input;
		reader_type reader;
		if (json) {
			CHECK(reader.match('{'));
		}
		else {
			STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
		}
		STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
		automation_storage automation;
		automation.nodes.values.emplace_back();
		automation.nodes.values.back().pos = 99;
		automation.nodes.fail_insertion_at = fail_at;
		automation.nodes.fail_reservation = fail_reservation;
		int32_t scalar = -999;
		uint32_t automated_flags = UINT32_MAX;
		uint32_t interpolating_flags = UINT32_MAX;
		constexpr uint32_t mask = uint32_t{1} << 31;
		deluge::modulation::params::read_param(reader, scalar, automation, endpoint ? 8 : 16, mask, automated_flags,
		                                       interpolating_flags);
		LONGS_EQUAL(37, scalar);
		CHECK(automation.last_read_error == (fail_at < 0 ? Error::NONE : Error::INSUFFICIENT_RAM));
		int32_t retained = fail_at < 0 ? 3 : fail_at;
		LONGS_EQUAL(retained, automation.nodes.values.size());
		LONGS_EQUAL(fail_at < 0 ? 3 : fail_at + 1, automation.nodes.insertion_attempts);
		for (int32_t index = 0; index < retained; ++index) {
			LONGS_EQUAL((index + 1) * 100, automation.nodes.values[index].value);
			LONGS_EQUAL(endpoint ? 4 : index * 4, automation.nodes.values[index].pos);
			CHECK_EQUAL(index == 1, bool(automation.nodes.values[index].interpolated));
		}
		CHECK_EQUAL(retained != 0, bool(automated_flags & mask));
		CHECK_EQUAL(~mask, automated_flags & ~mask);
		CHECK_EQUAL(~mask, interpolating_flags);
		if (fail_at < 0) {
			CHECK(automation.nodes.reservation_attempts > 0);
			reader.exitTag("value");
			STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
			LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
		}
	}
}
} // namespace

TEST(native_parameter_persistence, allocation_failures_preserve_scalar_and_partial_reload_flags) {
	for (bool fail_reservation : {false, true}) {
		for (int32_t fail_at : {0, 1, 2}) {
			check_allocation_failure<XMLSerializer, XMLDeserializer>(false, false, fail_at, fail_reservation);
			check_allocation_failure<JsonSerializer, JsonDeserializer>(true, false, fail_at, fail_reservation);
		}
		check_allocation_failure<XMLSerializer, XMLDeserializer>(false, true, 1, fail_reservation);
		check_allocation_failure<JsonSerializer, JsonDeserializer>(true, true, 1, fail_reservation);
	}
}

TEST(native_parameter_persistence, failed_preallocation_allows_successful_individual_insertions) {
	check_allocation_failure<XMLSerializer, XMLDeserializer>(false, false, -1, true);
	check_allocation_failure<JsonSerializer, JsonDeserializer>(true, false, -1, true);
}

namespace {
struct automation_writer {
	bool automated;
	int32_t write_calls = 0;
	std::string payload = "0000006480000004";
	bool isAutomated() { return automated; }
	template <typename writer_type>
	void write_automation(writer_type& writer) {
		++write_calls;
		writer.write(payload.c_str());
	}
};

template <typename writer_type, typename reader_type>
void check_save_decisions(bool json) {
	for (int32_t source_value : {0, 37, INT32_MIN, INT32_MAX}) {
		for (bool automated : {false, true}) {
			for (bool save_nodes : {false, true}) {
				for (bool omit_empty : {false, true}) {
					for (bool use_override : {false, true}) {
						int32_t value = source_value;
						int32_t override_value = source_value == 0 ? 81 : 0;
						automation_writer automation{automated};
						std::string original_payload = automation.payload;
						writer_type writer;
						writer.writeOpeningTagBeginning(json ? nullptr : "params");
						int32_t before = writer.bytesWritten();
						deluge::modulation::params::write_param_as_attribute(writer, "value", value, automation,
						                                                     save_nodes, omit_empty,
						                                                     use_override ? &override_value : nullptr);
						bool omitted = omit_empty && source_value == 0 && !automated;
						if (omitted) {
							LONGS_EQUAL(before, writer.bytesWritten());
						}
						writer.writeAttribute("sentinel", 73);
						writer.closeTag();
						LONGS_EQUAL(source_value, value);
						LONGS_EQUAL(source_value == 0 ? 81 : 0, override_value);
						CHECK_EQUAL(automated, automation.automated);
						STRCMP_EQUAL(original_payload.c_str(), automation.payload.c_str());
						LONGS_EQUAL(!omitted && automated && save_nodes ? 1 : 0, automation.write_calls);
						std::string document(writer.getBufferPtr(), writer.bytesWritten());
						native_parameter_tests::file_contents = document;
						reader_type reader;
						if (json) {
							CHECK(reader.match('{'));
						}
						else {
							STRCMP_EQUAL("params", reader.readNextTagOrAttributeName());
						}
						if (!omitted) {
							STRCMP_EQUAL("value", reader.readNextTagOrAttributeName());
							int32_t loaded = -999;
							CHECK(deluge::modulation::params::read_current_value(reader, loaded));
							LONGS_EQUAL(use_override && automated ? override_value : source_value, loaded);
							node_storage nodes;
							CHECK(deluge::modulation::automation::read_nodes(reader, nodes, 16) == Error::NONE);
							LONGS_EQUAL(automated && save_nodes ? 1 : 0, nodes.values.size());
							if (!nodes.values.empty()) {
								LONGS_EQUAL(100, nodes.values[0].value);
								LONGS_EQUAL(4, nodes.values[0].pos);
								CHECK(nodes.values[0].interpolated);
							}
							reader.exitTag("value");
						}
						STRCMP_EQUAL("sentinel", reader.readNextTagOrAttributeName());
						LONGS_EQUAL(73, reader.readTagOrAttributeValueInt());
						reader.exitTag("sentinel");
						STRCMP_EQUAL("", reader.readNextTagOrAttributeName());
					}
				}
			}
		}
	}
}
} // namespace

TEST(native_parameter_persistence, xml_save_decisions_preserve_source_and_output_contract) {
	check_save_decisions<XMLSerializer, XMLDeserializer>(false);
}

TEST(native_parameter_persistence, json_save_decisions_preserve_source_and_output_contract) {
	check_save_decisions<JsonSerializer, JsonDeserializer>(true);
}