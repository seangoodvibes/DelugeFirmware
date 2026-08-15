/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "deluge/io/midi/midi_queue_definitions.h"
#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/midi_queue_manager.h"
#include "model/midi/message.h"
#include "util/container/vector/named_thing_vector.h"
#include <array>
class Serializer;
class Deserializer;

#else
#include "definitions.h"
struct MIDICableUSB;
#endif

// size in 32-bit messages
// NOTE: increasing this even more doesn't work.
// Looks like a hardware limitation (maybe we more in FS mode)?
#define MIDI_SEND_BUFFER_LEN_INNER 32
// Seems to be the max for a hydrasynth on a usb hub? We should figure out how to find this from the device config but I
// haven't seen anything below this yet. Widi bud's can do 3, both do fine at 16 without a hub involved
#define MIDI_SEND_BUFFER_LEN_INNER_HOST 2

#ifdef __cplusplus
/*A ConnectedUSBMIDIDevice is used directly to interface with the USB driver
 * When a ConnectedUSBMIDIDevice has a numMessagesQueued>=MIDI_SEND_BUFFER_LEN and tries to add another,
 * all outputs are sent. The send routine calls the USB output function, points the
 * USB pipes FIFO buffer directly at the dataSendingNow array, and then sends.
 * Sends can also be triggered by the midiAndGateOutput interrupt
 *
 * Reads are more complicated.
 * Actual reads are done by usb_cstd_usb_task, which has a commented out interrupt associated
 * The function is instead called in the midiengine::checkincomingUSBmidi function, which is called
 * in the audio engine loop
 *
 * The USB read function is configured by setupUSBHostReceiveTransfer, which is called to
 * setup the next device after each successful read. Data is written directly into the receiveData
 * array from the USB device, it's set as the USB pipe address during midi engine setup
 */
class ConnectedUSBMIDIDevice {
public:
	MIDICableUSB* cable[4]; // If NULL, then no cable is connected here
	ConnectedUSBMIDIDevice();
	void enqueue_message(uint32_t fullMessage);
	void setup();

	// move data from ring buffer to dataSendingNow, assuming it is free
	/// Drains queued USB data into the hardware-send buffer.
	bool consume_queued_messages();
	/// Queue occupancy check (boolean form): true when any USB lane has queued output.
	/// Conceptually matches DIN `has_serial_data()`.
	bool hasBufferedSendData();
	/// Remaining USB queue capacity (reported in serial-MIDI-byte equivalent units).
	int sendBufferSpace();
#else
// warning - accessed as a C struct from usb driver
struct ConnectedUSBMIDIDevice {
	struct MIDICableUSB* device[4];
#endif
	uint8_t currentlyWaitingToReceive;
	uint8_t sq; // Only for connections as HOST
	uint8_t canHaveMIDISent;
	uint16_t numBytesReceived;
	__attribute__((aligned(8))) uint8_t receiveData[64];

	// This buffer is passed directly to the USB driver, and is limited to what the hardware allows
	uint8_t dataSendingNow[MIDI_SEND_BUFFER_LEN_INNER * 4];
	// This will show a value after the general flush function is called, throughout other Devices being sent to before
	// this one, and until we've completed our send
	uint8_t numBytesSendingNow;
	uint8_t maxPortConnected;

#ifdef __cplusplus

private:
	/* ------------ MIDI Queue Manager ------------ */
	// This is a ring buffer for data waiting to be sent which doesn't fit the smaller buffer above.
	// Any code which wants to send midi data would use the writing side and append more messages.
	// When we are ready to send data on this device, we consume data on the reading side and move it into the
	// smaller dataSendingNow buffer above.
	// Messages are queued in priority-specific rings and consumed in priority order.
	MIDIQueueManagerDeviceState<uint32_t, MIDI_SEND_BUFFER_LEN_RING, QUEUE_PRIORITY_COUNT> queue_manager_{};

	/// Clears USB queue contents and fairness bookkeeping for this device.
	/// This is the queue-reset counterpart to DIN output-state reset.
	void reset_queue_storage();
	/// Queue occupancy (count form): total queued USB packets across all priority lanes.
	/// Conceptually the counted version of DIN `has_serial_data()`.
	uint32_t total_queued_messages();

	/// Scratch buffer used when removing a queued CC frame and compacting survivors.
	std::array<uint32_t, MIDI_SEND_BUFFER_LEN_RING> cc_reorder_scratch{};
	friend struct USBSendRules;

	struct USBSendContext;
	/* ------------ MIDI Queue Manager ------------ */
#endif
};

#ifdef __cplusplus
class ConnectedDINMIDIDevice {
public:
	ConnectedDINMIDIDevice() = default;

	/// Resets DIN pacing/budget state to a known baseline at the provided sample timestamp.
	/// Note: this does not clear queued DIN bytes.
	void reset_serial_state(uint32_t now_sample_timer);
	/// Queue occupancy check (boolean form): true when any DIN lane has queued output.
	/// Conceptually matches USB `hasBufferedSendData()`.
	[[nodiscard]] bool has_serial_data() const;
	/// Classifies, optionally coalesces, and enqueues one outgoing MIDI message into DIN priority lanes.
	void enqueue_message(MIDIMessage message);
	/// Drains queued DIN bytes into UART using pacing budget, lane priorities, and CC gating.
	void consume_queued_messages(uint32_t now_sample_timer);

private:
	/// Number of active serial-priority lanes [clock..CC] scanned during dequeue.
	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_CC + 1;
	/// Per-priority byte rings holding pending DIN output grouped by queue policy.
	MIDIQueueManagerDeviceState<uint8_t, 512, k_serial_priority_count> queue_manager_{};
	/// Last sample-timer tick used to accrue DIN pacing budget.
	uint32_t serial_budget_last_update_{0};
	/// Token-bucket send budget in Q8 bytes (8 fractional bits).
	int32_t serial_budget_Q8_{0};
	/// Scratch buffer used when removing a queued CC frame and compacting survivors.
	std::array<uint8_t, MIDIQueueLane<uint8_t, 512>::k_capacity> cc_reorder_scratch_{};
	friend struct DINSendRules;

	struct DINSendContext;
};
#endif

#ifdef __cplusplus
namespace MIDIDeviceManager {

void slowRoutine();
MIDICable* readDeviceReferenceFromFile(Deserializer& reader);
void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile(Deserializer& reader);
void readDevicesFromFile();

extern MIDICableUSBUpstream upstreamUSBMIDICable1;
extern MIDICableUSBUpstream upstreamUSBMIDICable2;
extern MIDICableUSBUpstream upstreamUSBMIDICable3;
extern MIDICableDINPorts dinMIDIPorts;

extern bool differentiatingInputsByDevice;

extern NamedThingVector hostedMIDIDevices;

extern uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
extern uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput;
extern bool anyChangesToSave;
} // namespace MIDIDeviceManager

#endif

extern struct ConnectedUSBMIDIDevice connectedUSBMIDIDevices[][MAX_NUM_USB_MIDI_DEVICES];
#ifdef __cplusplus
extern ConnectedDINMIDIDevice connectedDINMIDIDevice;
#endif
