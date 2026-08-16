/*
 * Copyright © 2026 Sean Ditny
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

#include "io/midi/midi_queue_manager.h"
#include "io/midi/midi_engine.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "drivers/uart/uart.h"

extern uint8_t anyUSBSendingStillHappening[];
}

#include <algorithm>
#include <cstring>

/*
 * MIDI Queue Manager Information
 *
 * This file intentionally keeps only shared queue policy and helper routines.
 * Transport-specific implementations live in queue_manager_types/ and call back into
 * this class for common behavior.
 */

namespace {
inline uint8_t status_byte(uint32_t packed) {
	return static_cast<uint8_t>((packed >> 8) & 0xFF);
}

inline uint8_t data_1(uint32_t packed) {
	return static_cast<uint8_t>((packed >> 16) & 0xFF);
}

inline uint8_t data_2(uint32_t packed) {
	return static_cast<uint8_t>((packed >> 24) & 0xFF);
}

// DIN link throughput in Q8 fixed-point bytes/second (31.25 kbps ~= 3125 bytes/s).
constexpr int32_t k_serial_bytes_per_second_Q8 = 3125 * 256;
// Maximum accumulated send budget (Q8 bytes) allowed for one burst after idle time.
constexpr int32_t k_serial_queue_budget_max_Q8 = MIDI_TX_BUFFER_SIZE * 256;
// Reserve some UART TX space so we do not fill the hardware buffer to the edge.
constexpr int32_t k_serial_uart_headroom_bytes = 16;
// Limit how much lowest-priority CC traffic can be staged ahead in the DIN UART buffer.
// This keeps dense CC bursts from sitting on the wire ahead of later clock or note bytes
// even when the software queues themselves are draining cleanly.
constexpr int32_t k_serial_buffered_cc_bytes_cap = 24;

} // namespace

/// Classifies an outgoing MIDI message into shared queue priorities.
QueuePriority MIDIQueueManager::classify_message(MIDIMessage message) {
	if (message.isSystemMessage()) {
		// Keep transport / realtime bytes at the highest priority lane.
		return QUEUE_PRIORITY_CLOCK;
	}

	switch (static_cast<MIDIStatusType>(message.statusType)) {
	case MIDIStatusType::NoteOff:
	case MIDIStatusType::NoteOn:
		return QUEUE_PRIORITY_NOTES;

	case MIDIStatusType::PolyphonicAftertouch:
	case MIDIStatusType::ChannelAftertouch:
	case MIDIStatusType::PitchBend:
		return QUEUE_PRIORITY_EXPRESSION;

	case MIDIStatusType::ControlChange:
		if (message.data1 == 1 || message.data1 == 74) {
			return QUEUE_PRIORITY_EXPRESSION;
		}
		return QUEUE_PRIORITY_CC;

	default:
		return QUEUE_PRIORITY_CC;
	}
}

QueuePriority MIDIQueueManagerUSB::classify_packed_usb_priority(uint32_t packed) {
	uint8_t cin = static_cast<uint8_t>(packed & 0x0F);
	if (cin >= 0x4 && cin <= 0x7) {
		return QUEUE_PRIORITY_SYSEX;
	}

	uint8_t status = status_byte(packed);
	MIDIMessage decoded{
	    .statusType = static_cast<uint8_t>((status >> 4) & 0x0F),
	    .channel = static_cast<uint8_t>(status & 0x0F),
	    .data1 = data_1(packed),
	    .data2 = data_2(packed),
	};
	return MIDIQueueManager::classify_message(decoded);
}

uint16_t MIDIQueueManagerUSB::queue_count(QueuePriority priority) const {
	return queue_manager_.queue_count(static_cast<uint8_t>(priority));
}

/// Returns total queued USB packet count across all priority lanes.
uint32_t MIDIQueueManagerUSB::total_queued_messages() const {
	return queue_manager_.total_queued_messages();
}

MIDIQueueManager::PriorityLaneTraversalResult MIDIQueueManagerUSB::handle_cc_lane(QueuePriority priority,
                                                                                  USBSendContext& context) const {
	(void)priority;
	(void)context;
	return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
}

bool MIDIQueueManagerUSB::pop_lane(QueuePriority priority, USBSendContext& context) {
	return queue_manager_.pop_head(static_cast<uint8_t>(priority), context.message_out);
}

void MIDIQueueManagerUSB::enqueue_message(uint32_t full_message) {
	// Total packets currently queued across all priority lanes for this device.
	uint32_t queued = total_queued_messages();
	// If backlog grows, opportunistically kick a flush to keep latency bounded.
	if (queued > 16) {
		// Only trigger a new flush when a send transaction is not already active.
		if (anyUSBSendingStillHappening[0] == 0) {
			midiEngine.flushUSBMIDIOutput();
		}
	}

	QueuePriority priority = classify_packed_usb_priority(full_message);
	// Occupancy of just the selected priority lane we are about to enqueue into.
	uint16_t queue_size = queue_count(priority);
	// Keep one slot free in each ring so full/empty states stay distinguishable.
	if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
		// If nothing is currently transmitting, try flushing now to free space.
		if (anyUSBSendingStillHappening[0] == 0) {
			midiEngine.flushUSBMIDIOutput();
		}
		// Re-check after opportunistic flush.
		queue_size = queue_count(priority);
		if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
			// Still full: drop this message rather than overwrite unread queued data.
			// TODO: show some error message
			return;
		}
	}

	// Write the packet through shared queue-state helper.
	queue_manager_.push(static_cast<uint8_t>(priority), full_message);

	// Signal that at least one USB packet is waiting so flush logic can schedule transmission.
	anythingInUSBOutputBuffer = true;
}

bool MIDIQueueManagerUSB::has_buffered_send_data() const {
	// True when at least one queued USB-MIDI packet exists across any priority lane.
	return total_queued_messages() > 0;
}

int MIDIQueueManagerUSB::send_buffer_space() const {
	// Total queued USB-MIDI packets currently buffered across all priority lanes.
	uint32_t queued = total_queued_messages();
	// Maximum packets we can queue: usable slots per lane (ring-1) times number of lanes.
	uint32_t totalCapacity = (MIDI_SEND_BUFFER_LEN_RING - 1) * QUEUE_PRIORITY_COUNT;
	// each 4-byte MIDI-USB message contains 3 bytes of serial MIDI data
	// Report remaining space in serial-MIDI-byte units to match caller expectations.
	return (totalCapacity - queued) * 3;
}

/// This tries to read data from the ring buffer, and moves data into the smaller "dataSendingNow" buffer where
/// it is ready to be used by the hardware driver.
bool MIDIQueueManagerUSB::consume_queued_messages(uint8_t* data_sending_now, uint8_t& num_bytes_sending_now,
                                                  bool usb_host_mode) {
	// Snapshot total queued packets across all priority lanes.
	uint32_t queued = total_queued_messages();
	if (queued == 0) {
		// Nothing pending: caller should not start a USB send transfer.
		return false;
	}

	int32_t i = 0;
	// many devices do not accept more than 64 bytes of data at a time
	// likely this can be inferred from the device metadata somehow?
	// some seem to take even less, especially with hubs involved. The hydrasynth seems to only respond to a max of
	// 2 messages per transfer, the third gets blocked. For MPE this leads to ignoring note ons as the x and y
	// resets are sent before the note on
	uint32_t max_size = usb_host_mode ? MIDI_SEND_BUFFER_LEN_INNER_HOST : MIDI_SEND_BUFFER_LEN_INNER;
	// Build at most one USB transfer worth of packets: no more than queued, and no more than the mode/device cap.
	int32_t to_send = std::min<uint32_t>(queued, max_size);
	// Serialize `to_send` prioritized 4-byte USB-MIDI packets into dataSendingNow.
	for (i = 0; i < to_send; i++) {
		uint32_t message = 0;
		// Pull one prioritized USB-MIDI packet (4 bytes) from the multi-lane ring queues.
		USBSendContext context{message};
		bool popped = false;
		for (uint8_t lane = static_cast<uint8_t>(QUEUE_PRIORITY_CLOCK);
		     lane < static_cast<uint8_t>(QUEUE_PRIORITY_COUNT); lane++) {
			QueuePriority priority = static_cast<QueuePriority>(lane);
			if (!queue_count(priority)) {
				continue;
			}

			if (priority == QUEUE_PRIORITY_CC) {
				auto cc_result = handle_cc_lane(priority, context);
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Popped) {
					popped = true;
					break;
				}
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Abort) {
					break;
				}
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::SkipLane) {
					continue;
				}
				if (cc_result != MIDIQueueManager::PriorityLaneTraversalResult::PopLane) {
					continue;
				}
			}

			if (pop_lane(priority, context)) {
				popped = true;
				break;
			}
		}
		if (!popped) {
			// Queue state changed unexpectedly (or became empty); stop assembling this transfer.
			break;
		}
		// Pack each 32-bit USB-MIDI event contiguously for the driver DMA/transfer buffer.
		memcpy(data_sending_now + (i * 4), &message, 4);
	}

	// `i` is the number of USB-MIDI event packets queued, each packet is 4 bytes.
	num_bytes_sending_now = i * 4;

	// Tell caller whether we assembled at least one packet to transmit.
	return i > 0;
}

/// Resets all USB per-priority queues and read/write cursors.
void MIDIQueueManagerUSB::reset_queue_storage() {
	queue_manager_.clear_all();
}

/// Resets serial pacing state so the next flush starts from a known baseline.
void MIDIQueueManagerDIN::reset_serial_state(uint32_t now_sample_timer) {
	serial_budget_last_update_ = now_sample_timer;
	serial_budget_Q8_ = 0;
}

/// Returns whether any serial-priority lane currently has data pending.
bool MIDIQueueManagerDIN::has_serial_data() const {
	return queue_manager_.has_any_data();
}

// Verify if a full CC message can be sent and return status for the caller to decide whether to abort, pop or skip this
// lane.
MIDIQueueManager::PriorityLaneTraversalResult MIDIQueueManagerDIN::handle_cc_lane(QueuePriority priority,
                                                                                  DINSendContext& context) const {
	uint8_t status = queue_manager_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.budget_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
	}
	return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
}

bool MIDIQueueManagerDIN::pop_lane(QueuePriority priority, DINSendContext& context) {
	if (priority == QUEUE_PRIORITY_CLOCK) {
		// Require full message fit in queue/budget/space for atomic send.
		if (context.budget_bytes < 1 || context.uart_space < 1 || context.max_len < 1) {
			return false;
		}
		bool popped = queue_manager_.pop_head(static_cast<uint8_t>(priority), context.out_bytes[0]);
		if (popped) {
			context.popped_priority = priority;
		}
		return popped;
	}

	uint8_t status = queue_manager_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.budget_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		return false;
	}

	bool popped = queue_manager_.pop_many(static_cast<uint8_t>(priority), context.out_bytes, message_len);
	if (popped) {
		context.popped_priority = priority;
	}
	return popped;
}

/// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void MIDIQueueManagerDIN::enqueue_message(MIDIMessage message) {
	// Convert message to wire bytes and queue by shared priority policy.
	uint8_t status = message.channel | (message.statusType << 4);
	int32_t message_length = bytesPerStatusMessage(status);
	if (message_length <= 0) {
		return;
	}

	QueuePriority priority = MIDIQueueManager::classify_message(message);
	uint8_t lane = static_cast<uint8_t>(priority);
	// Reject atomically if lane is full; never enqueue partial messages.
	if (queue_manager_.space(lane) < message_length) {
		return;
	}

	// queue the message bytes into the selected priority lane in order: status, data1, data2.
	uint8_t raw_bytes[3] = {status, message.data1, message.data2};
	for (int32_t i = 0; i < message_length; i++) {
		queue_manager_.push(lane, raw_bytes[i]);
	}
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void MIDIQueueManagerDIN::consume_queued_messages(uint32_t now_sample_timer) {
	if (!has_serial_data()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance.
	uint32_t delta_samples = now_sample_timer - serial_budget_last_update_;
	if (delta_samples) {
		serial_budget_last_update_ = now_sample_timer;
		serial_budget_Q8_ +=
		    static_cast<int32_t>((static_cast<uint64_t>(delta_samples) * k_serial_bytes_per_second_Q8) / kSampleRate);
		if (serial_budget_Q8_ > k_serial_queue_budget_max_Q8) {
			serial_budget_Q8_ = k_serial_queue_budget_max_Q8;
		}
	}

	// Track total free MIDI UART capacity separately from the usable space for this flush.
	// The raw value is also used below to estimate how many non-clock bytes are already
	// staged in hardware/software UART buffering.
	int32_t raw_uart_space = uartGetTxBufferSpace(UART_ITEM_MIDI);
	int32_t uart_space = raw_uart_space - k_serial_uart_headroom_bytes;
	if (uart_space <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}

	// Bound how much queued CC traffic is staged ahead in the UART so later
	// clock/note traffic can still preempt dense automation bursts.
	int32_t cc_uart_budget =
	    std::max<int32_t>(0, k_serial_buffered_cc_bytes_cap - (MIDI_TX_BUFFER_SIZE - raw_uart_space));

	// Convert Q8 token budget to whole bytes available for this drain pass.
	int32_t send_allowance_bytes = serial_budget_Q8_ >> 8;
	constexpr size_t k_clock_idx = QUEUE_PRIORITY_CLOCK;
	// If no budget exists and no realtime clock is waiting, defer this pass.
	if (send_allowance_bytes <= 0 && queue_manager_.empty(static_cast<uint8_t>(k_clock_idx))) {
		return;
	}

	if (send_allowance_bytes <= 0) {
		// Allow one realtime byte to pass when budget is depleted.
		send_allowance_bytes = 1;
	}

	int32_t sent = 0;
	constexpr size_t k_cc_idx = QUEUE_PRIORITY_CC;
	// Keep draining while both UART capacity and token budget remain.
	while (uart_space > 0 && send_allowance_bytes > 0) {
		uint8_t bytes_to_send[3] = {0, 0, 0};
		QueuePriority popped_priority = QUEUE_PRIORITY_CC;
		DINSendContext context{bytes_to_send, send_allowance_bytes, uart_space, 3, popped_priority};
		bool popped = false;
		for (uint8_t lane = static_cast<uint8_t>(k_clock_idx); lane <= static_cast<uint8_t>(k_cc_idx); lane++) {
			QueuePriority priority = static_cast<QueuePriority>(lane);
			if (!queue_manager_.queue_count(static_cast<uint8_t>(priority))) {
				continue;
			}

			if (priority == QUEUE_PRIORITY_CC) {
				auto cc_result = handle_cc_lane(priority, context);
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Popped) {
					popped = true;
					break;
				}
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Abort) {
					break;
				}
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::SkipLane) {
					continue;
				}
				if (cc_result != MIDIQueueManager::PriorityLaneTraversalResult::PopLane) {
					continue;
				}
			}

			if (pop_lane(priority, context)) {
				popped = true;
				break;
			}
		}
		if (!popped) {
			break;
		}

		int32_t bytes_popped = 1;
		if (popped_priority != QUEUE_PRIORITY_CLOCK) {
			bytes_popped = bytesPerStatusMessage(bytes_to_send[0]);
			if (bytes_popped <= 0) {
				break;
			}
		}

		bool is_cc_message = (popped_priority == QUEUE_PRIORITY_CC);
		if (is_cc_message && cc_uart_budget < bytes_popped) {
			// Yield until the hardware drains enough queued CC traffic; clock, note, and
			// expression messages remain eligible so higher-priority output can still preempt
			// dense automation at the next flush.
			break;
		}

		for (int32_t i = 0; i < bytes_popped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytes_to_send[i]);
		}
		sent += bytes_popped;
		if (is_cc_message) {
			// Only lowest-priority CC traffic consumes this staging cap. Higher-priority lanes
			// still use queue ordering plus available UART space, but are not blocked by CC-only
			// occupancy accounting.
			cc_uart_budget -= bytes_popped;
		}
		uart_space -= bytes_popped;
		send_allowance_bytes -= bytes_popped;
	}

	if (sent > 0) {
		// Convert whole bytes back to Q8 units and debit pacing bucket.
		serial_budget_Q8_ = std::max<int32_t>(0, serial_budget_Q8_ - sent * 256);
	}
}
