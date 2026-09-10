#include "CppUTest/TestHarness.h"
#include "hid/mirror.h"
#include "hid/mirror_midi_filter.h"
#include "hid/mirror_protocol.h"
#include "io/midi/usb_send_range.h"
#include "mirror_environment.h"
#include <cstring>

// Only the USB queue storage/driver interface is doubled. The queue methods
// below are extracted verbatim from midi_device_manager.cpp at build time.
constexpr uint32_t MIDI_SEND_BUFFER_LEN_RING = 1024, MIDI_SEND_RING_MASK = 1023;
constexpr uint32_t MIDI_SEND_BUFFER_LEN_INNER = 32, MIDI_SEND_BUFFER_LEN_INNER_HOST = 2;
struct CriticalSectionGuard {};
constexpr int USB_HOST = 1;
inline int g_usb_usbmode = 0;
inline bool anyUSBSendingStillHappening[1] = {true}, anythingInUSBOutputBuffer = false;
struct ConnectedUSBMIDIDevice {
	MIDICable* cable[4]{};
	uint32_t connection_generation = 0;
	int currentlyWaitingToReceive = 0, numBytesReceived = 0, maxPortConnected = 0;
	void setup();
	uint32_t ringBufWriteIdx = 0, ringBufReadIdx = 0;
	uint32_t sendDataRingBuf[MIDI_SEND_BUFFER_LEN_RING]{};
	uint8_t dataSendingNow[MIDI_SEND_BUFFER_LEN_INNER * 4]{};
	int numBytesSendingNow = 0;
	void bufferMessage(uint32_t);
	bool hasBufferedSendData();
	int sendBufferSpace();
	bool consumeSendData();
	void discard_queued_non_sys_ex();
};
#include "usb_queue_methods.inc"

namespace mirror = deluge::hid::mirror;
namespace proto = deluge::hid::mirror::protocol;
TEST_GROUP(USBQueueRuntime) {
	MIDICable peer, unrelated;
	ConnectedUSBMIDIDevice device;
	void setup() override {
		fixture::now += 10;
		sdRoutineLock = false;
		AudioEngine::audioRoutineLocked = false;
		mirror::routine();
		for (auto& d : connectedUSBMIDIDevices[0])
			d = {};
		connectedUSBMIDIDevices[0][0] = {true, {&peer}};
		device.cable[0] = &peer;
		g_usb_usbmode = 0;
		anyUSBSendingStillHappening[0] = true;
		anythingInUSBOutputBuffer = false;
		fixture::on_input = {};
	}
	void teardown() override {
		peer.connectionFlags = 0;
		mirror::routine();
	}
	void negotiate() {
		uint8_t bytes[] = {
		    proto::command, proto::version, static_cast<uint8_t>(proto::Op::Request), 42, 0, 0, 0, 0, 1, 0xF7};
		physical_display.oled = true;
		mirror::received(peer, bytes, sizeof(bytes));
		CHECK_TRUE(mirror::is_host_client_connection(&peer));
	}
	uint32_t output(int i) {
		uint32_t value;
		memcpy(&value, device.dataSendingNow + i * 4, 4);
		return value;
	}
};
TEST(USBQueueRuntime, musical_packets_are_rejected_at_enqueue_only_for_negotiated_connection) {
	negotiate();
	for (uint8_t port = 0; port < 16; ++port) {
		for (uint32_t packet : {0x007f4099u, 0x000001bbu, 0x0000f80fu})
			device.bufferMessage((packet & ~0xf0u) | (port << 4));
	}
	CHECK_FALSE(device.hasBufferedSendData());
	CHECK_FALSE(anythingInUSBOutputBuffer);
	device.cable[0] = &unrelated;
	device.bufferMessage(0x007f4099);
	CHECK_TRUE(device.hasBufferedSendData());
}
TEST(USBQueueRuntime, packets_queued_before_negotiation_are_filtered_at_dequeue) {
	device.bufferMessage(0x007f4099);
	device.bufferMessage(0x0000f80f);
	device.bufferMessage(0x007bf004);
	device.bufferMessage(0x0000f705);
	negotiate();
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(8, device.numBytesSendingNow);
	UNSIGNED_LONGS_EQUAL(0x007bf004, output(0));
	UNSIGNED_LONGS_EQUAL(0x0000f705, output(1));
	CHECK_FALSE(device.hasBufferedSendData());
}
TEST(USBQueueRuntime, all_filtered_queue_produces_no_transfer) {
	device.bufferMessage(0x007f4099);
	negotiate();
	CHECK_FALSE(device.consumeSendData());
	LONGS_EQUAL(0, device.numBytesSendingNow);
	CHECK_FALSE(device.hasBufferedSendData());
}
TEST(USBQueueRuntime, both_usb_roles_respect_transfer_limits_and_preserve_sys_ex_order) {
	negotiate();
	for (int mode : {0, USB_HOST}) {
		g_usb_usbmode = mode;
		for (uint32_t i = 0; i < 40; ++i)
			device.bufferMessage(0x04 | (i << 8));
		uint32_t expected = 0;
		while (device.consumeSendData()) {
			CHECK_TRUE(device.numBytesSendingNow <= (mode == USB_HOST ? 8 : 128));
			for (int i = 0; i < device.numBytesSendingNow / 4; ++i)
				UNSIGNED_LONGS_EQUAL(0x04 | (expected++ << 8), output(i));
		}
		LONGS_EQUAL(40, expected);
	}
}
TEST(USBQueueRuntime, unsigned_queue_indices_wrap_and_do_not_touch_in_flight_buffer_at_enqueue) {
	negotiate();
	device.ringBufReadIdx = device.ringBufWriteIdx = 0xfffffffe;
	memset(device.dataSendingNow, 0xa5, sizeof(device.dataSendingNow));
	for (uint32_t i = 0; i < 4; ++i)
		device.bufferMessage(0x04 | (i << 8));
	for (auto b : device.dataSendingNow)
		LONGS_EQUAL(0xa5, b);
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(16, device.numBytesSendingNow);
	for (uint32_t i = 0; i < 4; ++i)
		UNSIGNED_LONGS_EQUAL(0x04 | (i << 8), output(i));
	CHECK_FALSE(device.hasBufferedSendData());
	LONGS_EQUAL(3072, device.sendBufferSpace());
}

TEST(USBQueueRuntime, queued_filter_survives_session_end_and_preserves_wrapped_sys_ex_order) {
	device.ringBufReadIdx = device.ringBufWriteIdx = 0xfffffffeu;
	device.bufferMessage(0x643c9009);
	device.bufferMessage(0x007bf004);
	device.bufferMessage(0x0000f80f);
	device.bufferMessage(0x0000f705);
	device.numBytesSendingNow = 4;
	device.dataSendingNow[0] = 0x99;
	device.discard_queued_non_sys_ex();
	LONGS_EQUAL(4, device.numBytesSendingNow);
	LONGS_EQUAL(0x99, device.dataSendingNow[0]);
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(8, device.numBytesSendingNow);
	UNSIGNED_LONGS_EQUAL(0x007bf004, output(0));
	UNSIGNED_LONGS_EQUAL(0x0000f705, output(1));
	CHECK_FALSE(device.hasBufferedSendData());
	device.discard_queued_non_sys_ex();
	CHECK_FALSE(device.hasBufferedSendData());
}

TEST(USBQueueRuntime, full_wrapped_queue_rejects_overflow_without_overwriting_oldest_packet) {
	device.ringBufReadIdx = device.ringBufWriteIdx = 0xffffff00u;
	for (uint32_t i = 0; i < MIDI_SEND_BUFFER_LEN_RING; ++i)
		device.bufferMessage(0x04 | (i << 8));
	LONGS_EQUAL(0, device.sendBufferSpace());
	const auto full_write = device.ringBufWriteIdx;
	device.bufferMessage(0x00776604);
	UNSIGNED_LONGS_EQUAL(full_write, device.ringBufWriteIdx);
	uint32_t expected = 0;
	while (device.consumeSendData()) {
		for (int i = 0; i < device.numBytesSendingNow / 4; ++i)
			UNSIGNED_LONGS_EQUAL(0x04 | (expected++ << 8), output(i));
	}
	LONGS_EQUAL(MIDI_SEND_BUFFER_LEN_RING, expected);
	device.bufferMessage(0x0000f705);
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(4, device.numBytesSendingNow);
	UNSIGNED_LONGS_EQUAL(0x0000f705, output(0));
}

TEST(USBQueueRuntime, queue_purge_preserves_sys_ex_across_all_cable_numbers) {
	for (uint32_t port = 0; port < 16; ++port) {
		device.bufferMessage(0x643c9009 | (port << 4));
		device.bufferMessage(0x4001b00b | (port << 4));
		device.bufferMessage(0x0000f80f | (port << 4));
		device.bufferMessage(0x007bf004 | (port << 4));
		device.bufferMessage(0x0000f705 | (port << 4));
	}
	device.discard_queued_non_sys_ex();
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(128, device.numBytesSendingNow);
	for (uint32_t port = 0; port < 16; ++port) {
		UNSIGNED_LONGS_EQUAL(0x007bf004 | (port << 4), output(port * 2));
		UNSIGNED_LONGS_EQUAL(0x0000f705 | (port << 4), output(port * 2 + 1));
	}
	CHECK_FALSE(device.hasBufferedSendData());
}

TEST(USBQueueRuntime, empty_dequeue_clears_previous_transfer_length_in_both_usb_roles) {
	for (int mode : {0, USB_HOST}) {
		g_usb_usbmode = mode;
		device.bufferMessage(0x007bf004);
		device.bufferMessage(0x0000f705);
		CHECK_TRUE(device.consumeSendData());
		LONGS_EQUAL(8, device.numBytesSendingNow);
		CHECK_FALSE(device.consumeSendData());
		LONGS_EQUAL(0, device.numBytesSendingNow);
		// Clearing metadata must not rewrite the hardware transfer buffer.
		UNSIGNED_LONGS_EQUAL(0x007bf004, output(0));
		UNSIGNED_LONGS_EQUAL(0x0000f705, output(1));
		CHECK_FALSE(device.consumeSendData());
		LONGS_EQUAL(0, device.numBytesSendingNow);
	}
}

TEST(USBQueueRuntime, purged_queue_cannot_resend_previous_transfer) {
	device.bufferMessage(0x007bf004);
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(4, device.numBytesSendingNow);
	device.bufferMessage(0x643c9009);
	device.discard_queued_non_sys_ex();
	CHECK_FALSE(device.consumeSendData());
	LONGS_EQUAL(0, device.numBytesSendingNow);
	device.bufferMessage(0x0000f705);
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(4, device.numBytesSendingNow);
	UNSIGNED_LONGS_EQUAL(0x0000f705, output(0));
}

TEST(USBQueueRuntime, prepared_device_selection_handles_every_circular_range_and_ready_set) {
	for (int first = 0; first < 4; ++first) {
		for (int last = 0; last < 4; ++last) {
			for (unsigned ready = 0; ready < 16; ++ready) {
				int expected = -1;
				const int length = (last - first + 4) % 4 + 1;
				for (int offset = 0; offset < length; ++offset) {
					int device = (first + offset) % 4;
					if (ready & (1u << device)) {
						expected = device;
						break;
					}
				}
				LONGS_EQUAL(expected, deluge::io::midi::first_ready_usb_device(
				                          first, last, 4, [ready](int device) { return ready & (1u << device); }));
			}
		}
	}
}

TEST(USBQueueRuntime, filtered_queue_is_not_selected_for_host_transmission) {
	device.bufferMessage(0x643c9009);
	negotiate();
	CHECK_FALSE(device.consumeSendData());
	LONGS_EQUAL(-1, deluge::io::midi::first_ready_usb_device(
	                    0, 0, 1, [&](int) { return device.cable[0] && device.numBytesSendingNow > 0; }));
	device.bufferMessage(0x0000f705);
	CHECK_TRUE(device.consumeSendData());
	LONGS_EQUAL(0, deluge::io::midi::first_ready_usb_device(
	                   0, 0, 1, [&](int) { return device.cable[0] && device.numBytesSendingNow > 0; }));
}

TEST(USBQueueRuntime, repeated_setup_advances_connection_generation_and_resets_transfer_state) {
	for (uint32_t generation = 1; generation <= 3; ++generation) {
		device.numBytesSendingNow = 8;
		device.currentlyWaitingToReceive = 1;
		device.numBytesReceived = 12;
		device.maxPortConnected = 2;
		device.setup();
		UNSIGNED_LONGS_EQUAL(generation, device.connection_generation);
		LONGS_EQUAL(0, device.numBytesSendingNow);
		LONGS_EQUAL(0, device.currentlyWaitingToReceive);
		LONGS_EQUAL(0, device.numBytesReceived);
		LONGS_EQUAL(0, device.maxPortConnected);
	}
}
