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
	void bufferMessage(uint32_t fullMessage, QueuePriority priority);
	void setup();

	// move data from ring buffer to dataSendingNow, assuming it is free
	bool consumeSendData();
	bool hasBufferedSendData();
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
	/* ------------ MIDI Queue Manager ------------ */
	// This is a ring buffer for data waiting to be sent which doesn't fit the smaller buffer above.
	// Any code which wants to send midi data would use the writing side and append more messages.
	// When we are ready to send data on this device, we consume data on the reading side and move it into the
	// smaller dataSendingNow buffer above.
	// Messages are queued in priority-specific rings and consumed in priority order.
	std::array<MIDIQueueLane<uint32_t, MIDI_SEND_BUFFER_LEN_RING>, QUEUE_PRIORITY_COUNT> sendDataRingBuf{};

	/// Clears all queue storage and fairness bookkeeping for this upstream USB device.
	void reset_queue_storage();
	/// Returns the queued packet count for one upstream USB priority lane.
	uint16_t queue_count(QueuePriority priority);
	/// Returns the total number of queued upstream USB packets across all priority lanes.
	uint32_t total_queued_messages();

	/// Pushes one packed USB MIDI packet into the selected priority lane with CC coalescing/fairness tracking.
	void push_priority_message(QueuePriority priority, uint32_t message);
	// USB CC fairness/coalescing state mirrors DIN behavior per connected USB device.
	/// Replaces newest pending matching CC packet value instead of appending another packet.
	bool coalesce_queued_cc(uint32_t message);
	/// Initializes scan bounds for USB CC coalescing.
	bool begin_coalesce_cc_scan(uint16_t& cursor, uint16_t& limit) const;
	/// Advances USB CC coalescing scan by one step.
	MIDIQueueManager::CoalesceScanResult next_coalesce_cc_scan_step(uint16_t& cursor, uint16_t limit,
	                                                                uint16_t& candidate_offset, uint8_t& status,
	                                                                uint8_t& controller) const;
	/// Appends one packet into the selected USB priority lane.
	bool enqueue_priority_message(QueuePriority priority, uint32_t message);
	/// Reads one logical packet from any USB priority lane by offset from lane head.
	uint32_t read_priority_queue_message_at(QueuePriority priority, uint16_t logical_offset) const;
	/// Reads the current head packet from any USB priority lane.
	uint32_t read_priority_queue_head(QueuePriority priority) const;
	/// Pops one head packet from any USB priority lane.
	bool pop_priority_queue_head(QueuePriority priority, uint32_t& message_out);
	/// Appends one packet into any USB priority lane.
	void append_priority_queue_message(QueuePriority priority, uint32_t message);
	/// Reads one logical entry from the USB CC lane snapshot by offset from lane head.
	uint32_t read_cc_queue_message_at(uint16_t logical_offset) const;
	/// Resets USB CC lane cursors to an empty queue image.
	void reset_cc_queue();
	/// Appends one packet to USB CC lane during replay of compacted survivors.
	void append_cc_queue_message(uint32_t message);
	/// Removes queued CC packet at target offset and compacts remaining packets in-order.
	bool remove_queued_cc_message_at_offset(uint16_t target_offset, uint32_t& message_out);
	/// Collects first queued CC offsets per controller for fair USB dequeue.
	bool collect_fair_cc_candidates(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets);
	/// Initializes scan bounds for USB fair-CC candidate collection.
	bool begin_fair_cc_candidate_scan(uint16_t& cursor, uint16_t& limit) const;
	/// Advances USB fair-CC candidate scan by one step.
	MIDIQueueManager::CandidateScanResult next_fair_cc_candidate_scan_step(uint16_t& cursor, uint16_t limit,
	                                                                       uint16_t& candidate_offset,
	                                                                       uint8_t& controller) const;

	/// Pops one highest-priority eligible packet, applying CC fairness and CC budget limits.
	bool pop_priority_message(uint32_t& message_out, int32_t& cc_budget_packets_remaining);
	/// Pops one queued CC packet chosen by shared round-robin/debt fairness policy.
	bool pop_fair_queued_cc_message(uint32_t& message_out);

	/// Scratch buffer used when removing a queued CC frame and compacting survivors.
	std::array<uint32_t, MIDI_SEND_BUFFER_LEN_RING> cc_reorder_scratch{};
	/// Per-device CC fairness state used by shared queue policy helpers.
	MIDICCQueuePolicy cc_policy{};
	/* ------------ MIDI Queue Manager ------------ */
#endif
};

#ifdef __cplusplus
class ConnectedDINMIDIDevice {
public:
	ConnectedDINMIDIDevice() = default;

	/// Resets serial pacing/budget state to a known baseline at the provided sample timestamp.
	void reset_serial_state(uint32_t now_sample_timer);
	/// Returns true when any DIN priority lane has queued bytes waiting to be flushed.
	[[nodiscard]] bool has_serial_data() const;
	/// Classifies, optionally coalesces, and enqueues one outgoing MIDI message into DIN priority lanes.
	void enqueue_serial_message(MIDIMessage message);
	/// Drains queued DIN bytes into UART using pacing budget, lane priorities, and CC gating.
	void flush_serial_output(uint32_t now_sample_timer);

private:
	using SerialByteQueue = MIDIQueueLane<uint8_t, 512>;

	/// Number of active serial-priority lanes [clock..CC] scanned during dequeue.
	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_CC + 1;
	/// Per-priority byte rings holding pending DIN output grouped by queue policy.
	std::array<SerialByteQueue, k_serial_priority_count> serial_priority_queues_{};
	/// Last sample-timer tick used to accrue DIN pacing budget.
	uint32_t serial_budget_last_update_{0};
	/// Token-bucket send budget in Q8 bytes (8 fractional bits).
	int32_t serial_budget_Q8_{0};
	/// Scratch buffer used when removing a queued CC frame and compacting survivors.
	std::array<uint8_t, SerialByteQueue::k_capacity> cc_reorder_scratch_{};
	/// Per-device CC fairness state used by shared queue policy helpers.
	MIDICCQueuePolicy cc_policy_{};

	/// Refills Q8 pacing budget from elapsed sample time and applies idle-burst capping.
	void update_serial_budget(uint32_t now_sample_timer);
	/// Replaces the newest queued matching CC value instead of appending a duplicate write.
	bool coalesce_queued_cc(MIDIMessage message);
	/// Initializes scan bounds for DIN CC coalescing.
	bool begin_coalesce_cc_scan(uint16_t& cursor, uint16_t& limit) const;
	/// Advances DIN CC coalescing scan by one step.
	MIDIQueueManager::CoalesceScanResult next_coalesce_cc_scan_step(uint16_t& cursor, uint16_t limit,
	                                                                uint16_t& candidate_offset, uint8_t& status,
	                                                                uint8_t& controller) const;
	/// Encodes and appends one message into the selected DIN priority lane.
	bool enqueue_priority_message(QueuePriority priority, MIDIMessage message);
	/// Pops one queued 3-byte CC message selected by round-robin/debt fairness policy.
	bool pop_fair_queued_cc_message(uint8_t* out_bytes, int32_t budget_bytes, int32_t uart_space, int32_t max_len,
	                                QueuePriority& popped_priority);
	/// Collects first queued CC offsets per controller for fair DIN dequeue.
	bool collect_fair_cc_candidates(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets);
	/// Initializes scan bounds for DIN fair-CC candidate collection.
	bool begin_fair_cc_candidate_scan(uint16_t& cursor, uint16_t& limit) const;
	/// Advances DIN fair-CC candidate scan by one step.
	MIDIQueueManager::CandidateScanResult next_fair_cc_candidate_scan_step(uint16_t& cursor, uint16_t limit,
	                                                                       uint16_t& candidate_offset,
	                                                                       uint8_t& controller) const;
	/// Shared DIN CC-lane scan step used by fair-candidate and coalesce scans.
	MIDIQueueManager::CoalesceScanResult next_din_cc_scan_step(uint16_t& cursor, uint16_t limit,
	                                                           uint16_t& candidate_offset, uint8_t& status,
	                                                           uint8_t& controller) const;
	/// Reads one logical byte from the DIN CC lane snapshot by offset from lane head.
	uint8_t read_cc_queue_byte_at(uint16_t logical_offset) const;
	/// Resets DIN CC lane cursors to an empty queue image.
	void reset_cc_queue();
	/// Appends one byte to DIN CC lane during replay of compacted survivors.
	void append_cc_queue_byte(uint8_t byte);
	/// Removes one queued CC frame at target offset and compacts remaining queue bytes in-order.
	bool remove_queued_cc_message_at_offset(uint16_t target_offset, uint8_t* out_bytes);
	/// Enqueues one complete byte span atomically into the selected priority lane.
	bool enqueue_serial_bytes(QueuePriority priority, uint8_t const* bytes, int32_t len);
	/// Pops the next eligible realtime or full MIDI message under budget/space constraints.
	int32_t pop_next_prioritized_bytes(uint8_t* out_bytes, int32_t max_len, int32_t budget_bytes, int32_t uart_space,
	                                   int32_t cc_uart_budget, QueuePriority& popped_priority);
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
