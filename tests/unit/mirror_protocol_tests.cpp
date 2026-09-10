#include "CppUTest/TestHarness.h"
#include "hid/mirror_midi_filter.h"
#include "hid/mirror_protocol.h"
#include <array>
#include <climits>
#include <vector>

using namespace deluge::hid::mirror::protocol;

TEST_GROUP(MirrorProtocol){};

TEST(MirrorProtocol, every_payload_length_and_byte_value_round_trips_without_midi_status_bytes) {
	std::array<uint8_t, max_payload> source{}, result{};
	std::array<uint8_t, max_payload * 2> encoded{};
	for (unsigned value = 0; value < 256; ++value) {
		for (size_t i = 0; i < source.size(); ++i)
			source[i] = (value + i * 71) & 255;
		for (size_t length = 0; length <= max_payload; ++length) {
			size_t size = pack({source.data(), length}, encoded.data());
			CHECK_EQUAL(length + (length + 6) / 7, size);
			for (size_t i = 0; i < size; ++i)
				CHECK(encoded[i] < 128);
			CHECK_EQUAL(length, unpack({encoded.data(), size}, result));
			MEMCMP_EQUAL(source.data(), result.data(), length);
		}
	}
}

TEST(MirrorProtocol, malformed_encoding_cannot_overrun_destination) {
	std::array<uint8_t, 2> dest{0x55, 0xAA};
	uint8_t prefix_only[] = {0};
	uint8_t unused_high_bit[] = {2, 0};
	uint8_t statusByte[] = {0, 0xF0};
	uint8_t too_long[] = {0, 1, 2};
	CHECK_EQUAL(-1, unpack(prefix_only, dest));
	CHECK_EQUAL(-1, unpack(unused_high_bit, dest));
	CHECK_EQUAL(-1, unpack(statusByte, dest));
	CHECK_EQUAL(-1, unpack(too_long, {dest.data(), 1}));
	CHECK_EQUAL(0xAA, dest[1]);
}

TEST(MirrorProtocol, only_complete_panel_commands_are_replayable) {
	CHECK_FALSE(valid_panel({}));
	for (unsigned opcode = 0; opcode < 256; ++opcode) {
		size_t length = panel_command_size(opcode);
		std::vector<uint8_t> data(length ? length : 1, 255);
		data[0] = opcode;
		CHECK_EQUAL(length != 0, valid_panel(data));
		if (length > 1)
			CHECK_FALSE(valid_panel({data.data(), length - 1}));
	}
	for (uint8_t forbidden : {0, 18, 22, 225, 244, 245, 247, 248, 249, 250, 251, 252, 253, 254, 255}) {
		CHECK_FALSE(valid_panel({&forbidden, 1}));
	}
	uint8_t combined[] = {188, 224, 0xFF, 0, 128, 255, 152};
	CHECK_TRUE(valid_panel(combined));
	CHECK_FALSE(valid_panel({combined, 5}));
}

TEST(MirrorProtocol, packet_framing_and_version_are_validated_before_dispatch) {
	Packet result{};
	uint8_t heartbeat[] = {command, version, static_cast<uint8_t>(Op::Heartbeat), 127, 127, 127, 127, 0xF7};
	CHECK_TRUE(decode(heartbeat, result));
	CHECK_EQUAL(0x3FFF, result.session);
	CHECK_EQUAL(0x3FFF, result.sequence);
	CHECK_EQUAL(0, result.size);
	for (size_t length = 0; length < sizeof(heartbeat); ++length)
		CHECK_FALSE(decode({heartbeat, length}, result));
	heartbeat[1] = version + 1;
	CHECK_FALSE(decode(heartbeat, result));
	heartbeat[1] = version;
	heartbeat[2] = 127;
	CHECK_FALSE(decode(heartbeat, result));
	heartbeat[2] = static_cast<uint8_t>(Op::Heartbeat);
	heartbeat[4] = 128;
	CHECK_FALSE(decode(heartbeat, result));
}

TEST(MirrorProtocol, input_validation_prevents_invalid_coordinates_and_encoder_overflow) {
	CHECK_TRUE(valid_input(0, 0, 0));
	CHECK_TRUE(valid_input(0, 179, 1));
	CHECK_FALSE(valid_input(0, 180, 1));
	CHECK_FALSE(valid_input(0, 0, 255));
	CHECK_FALSE(valid_input(0, 0, -1));
	CHECK_TRUE(valid_input(1, 0, -127));
	CHECK_TRUE(valid_input(1, 5, 127));
	CHECK_FALSE(valid_input(1, 6, 1));
	CHECK_FALSE(valid_input(1, 0, 128));
	CHECK_FALSE(valid_input(1, 0, -128));
	CHECK_FALSE(valid_input(1, 0, INT_MIN));
	CHECK_FALSE(valid_input(1, 0, INT_MAX));
	CHECK_FALSE(valid_input(1, 0, 0));
	CHECK_FALSE(valid_input(2, 0, 1));
}

TEST(MirrorProtocol, client_usb_connection_allows_only_sys_ex_on_every_virtual_port) {
	for (uint32_t port = 0; port < 16; ++port) {
		for (uint32_t cin = 0; cin < 16; ++cin) {
			for (uint32_t status = 0; status < 256; ++status) {
				uint32_t packet = (port << 4) | cin | (status << 8);
				bool sysex = cin == 4 || cin == 6 || cin == 7 || (cin == 5 && status == 0xf7);
				CHECK_EQUAL(sysex, deluge::hid::mirror::allow_usb_packet(true, packet));
				CHECK_TRUE(deluge::hid::mirror::allow_usb_packet(false, packet));
			}
		}
	}
}

TEST(MirrorProtocol, client_filter_rejects_clock_notes_cc_and_system_common) {
	for (auto packet : {0x0000f80fu, 0x0000fa0fu, 0x0000fb0fu, 0x0000fc0fu, 0x00649009u, 0x00008008u, 0x007fb00bu,
	                    0x0000f605u, 0x0001f203u, 0x0000c00cu, 0x0000e00eu}) {
		CHECK_FALSE(deluge::hid::mirror::allow_usb_packet(true, packet));
	}
	for (auto packet : {0x017df004u, 0x0000f705u, 0x00f70106u, 0x00f70107u}) {
		CHECK_TRUE(deluge::hid::mirror::allow_usb_packet(true, packet));
	}
}

TEST(MirrorProtocol, input_acknowledgements_cannot_release_another_command) {
	InputAcknowledgement in_flight;
	CHECK_FALSE(in_flight.acknowledge(0));
	CHECK_FALSE(in_flight.begin(0x4000));
	for (uint16_t sequence = 0; sequence < 0x4000; ++sequence) {
		CHECK_TRUE(in_flight.begin(sequence));
		CHECK_FALSE(in_flight.begin((sequence + 1) & 0x3fff));
		CHECK_FALSE(in_flight.acknowledge((sequence + 1) & 0x3fff));
		CHECK_TRUE(in_flight.pending());
		CHECK_EQUAL(sequence, in_flight.sequence());
		CHECK_TRUE(in_flight.acknowledge(sequence));
		CHECK_FALSE(in_flight.pending());
		CHECK_FALSE(in_flight.acknowledge(sequence));
	}
	// A new session must not inherit an outstanding command from the old one.
	CHECK_TRUE(in_flight.begin(12));
	in_flight.reset();
	CHECK_FALSE(in_flight.acknowledge(12));
	CHECK_TRUE(in_flight.begin(0));
	CHECK_TRUE(in_flight.acknowledge(0));
}

TEST(MirrorProtocol, input_acknowledgement_packets_use_version_two_and_packed_sequence_bytes) {
	uint8_t wire[16] = {command, version, static_cast<uint8_t>(Op::InputAck), 1, 0, 2, 0};
	uint8_t payload[] = {255, 63}; // input packet sequence 16383
	size_t length = 7 + pack(payload, wire + 7);
	wire[length++] = 0xf7;
	Packet packet{};
	CHECK_TRUE(decode({wire, length}, packet));
	CHECK_EQUAL(2, packet.size);
	MEMCMP_EQUAL(payload, packet.payload, 2);
	wire[1] = 1;
	CHECK_FALSE(decode({wire, length}, packet));
}

TEST(MirrorProtocol, advertised_session_modes_match_acceptance_and_keep_independent_disabled) {
	CHECK_TRUE(supports_session_mode(session_mode::visible_host));
	CHECK_FALSE(supports_session_mode(session_mode::independent));
	LONGS_EQUAL(1, supported_session_modes);
	for (unsigned value = 0; value < 256; ++value) {
		const bool accepted = supports_session_mode(static_cast<session_mode>(value));
		CHECK_TRUE(accepted == (value == 0));
	}
}
