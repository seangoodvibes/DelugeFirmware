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

#pragma once

#include "definitions_cxx.hpp"
#include "io/midi/midi_queue_definitions.h"
#include "model/midi/message.h"
#include <array>

template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueManagerDeviceState;

/// Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// This class contains only static shared behavior used by queue manager
/// state in transport device types:
/// 1. Message classification into queue priorities.
/// 2. Shared helpers for queue scanning and head-message validation.
class MIDIQueueManager {
public:
	enum class PriorityLaneTraversalResult {
		PopLane,
		SkipLane,
		Popped,
		Abort,
	};

	enum class HeadMessageCheckResult : uint8_t {
		Invalid,
		InsufficientCapacity,
		Ready,
	};

	/// Classifies an outgoing MIDI message into priority groups.
	static QueuePriority classify_message(MIDIMessage message);

	/// Shared parser+fit gate for queued non-realtime MIDI messages.
	///
	/// Returns Ready only when the queue head decodes to a valid message length
	/// and that full message fits queue occupancy plus all caller limits.
	static HeadMessageCheckResult validate_head_message_pop(uint8_t status, uint16_t queue_size, int32_t budget_bytes,
	                                                        int32_t uart_space, int32_t max_len,
	                                                        int32_t& message_len_out) {
		int32_t message_len = bytesPerStatusMessage(status);
		if (message_len <= 0) {
			return HeadMessageCheckResult::Invalid;
		}
		if (queue_size < message_len || budget_bytes < message_len || uart_space < message_len
		    || max_len < message_len) {
			return HeadMessageCheckResult::InsufficientCapacity;
		}
		message_len_out = message_len;
		return HeadMessageCheckResult::Ready;
	}
};

/// Power-of-two ring buffer lane shared by transport-specific queue managers.
template <typename T, uint16_t Capacity>
class MIDIQueueLane {
public:
	static_assert(Capacity != 0);
	static_assert((Capacity & (Capacity - 1)) == 0);
	static constexpr uint16_t k_capacity = Capacity;

	std::array<T, Capacity> data{};
	uint16_t read_pos{0};
	uint16_t write_pos{0};

	[[nodiscard]] bool empty() const { return read_pos == write_pos; }
	[[nodiscard]] uint16_t size() const { return static_cast<uint16_t>((write_pos - read_pos) & (Capacity - 1)); }
	[[nodiscard]] uint16_t space() const { return static_cast<uint16_t>((Capacity - 1) - size()); }
	[[nodiscard]] T peek(uint16_t offset = 0) const { return data[(read_pos + offset) & (Capacity - 1)]; }

	bool push(T value) {
		uint16_t next = static_cast<uint16_t>((write_pos + 1) & (Capacity - 1));
		if (next == read_pos) {
			return false;
		}
		data[write_pos] = value;
		write_pos = next;
		return true;
	}

	bool pop(T& out) {
		if (empty()) {
			return false;
		}
		out = data[read_pos];
		read_pos = static_cast<uint16_t>((read_pos + 1) & (Capacity - 1));
		return true;
	}

	bool pop_many(T* out, uint16_t count) {
		if (size() < count) {
			return false;
		}
		for (uint16_t i = 0; i < count; i++) {
			out[i] = data[(read_pos + i) & (Capacity - 1)];
		}
		read_pos = static_cast<uint16_t>((read_pos + count) & (Capacity - 1));
		return true;
	}

	void clear() {
		read_pos = 0;
		write_pos = 0;
	}
};

/// Fixed set of power-of-two queue lanes shared by a transport-specific manager.
template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueStorage {
public:
	std::array<MIDIQueueLane<T, Capacity>, LaneCount> lanes{};

	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return lanes[lane].size(); }
	[[nodiscard]] uint32_t total_queued_messages() const {
		uint32_t queued = 0;
		for (auto const& queue_lane : lanes) {
			queued += queue_lane.size();
		}
		return queued;
	}

	[[nodiscard]] T head(uint8_t lane) const { return lanes[lane].peek(); }

	bool pop_head(uint8_t lane, T& out) { return lanes[lane].pop(out); }
	void push(uint8_t lane, T value) { (void)lanes[lane].push(value); }
	void clear(uint8_t lane) { lanes[lane].clear(); }
	[[nodiscard]] bool empty(uint8_t lane) const { return lanes[lane].empty(); }
	[[nodiscard]] uint16_t space(uint8_t lane) const { return lanes[lane].space(); }
};

template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueManagerDeviceState {
public:
	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return queue_storage.queue_count(lane); }
	[[nodiscard]] uint32_t total_queued_messages() const { return queue_storage.total_queued_messages(); }
	[[nodiscard]] T head(uint8_t lane) const { return queue_storage.head(lane); }
	bool pop_head(uint8_t lane, T& out) { return queue_storage.pop_head(lane, out); }
	void push(uint8_t lane, T value) { queue_storage.push(lane, value); }
	void clear(uint8_t lane) { queue_storage.clear(lane); }
	[[nodiscard]] bool empty(uint8_t lane) const { return queue_storage.empty(lane); }
	[[nodiscard]] uint16_t space(uint8_t lane) const { return queue_storage.space(lane); }
	[[nodiscard]] bool has_any_data() const { return queue_storage.total_queued_messages() > 0; }
	void clear_all() {
		for (auto& queue_lane : queue_storage.lanes) {
			queue_lane.clear();
		}
	}
	bool pop_many(uint8_t lane, T* out, uint16_t count) { return queue_storage.lanes[lane].pop_many(out, count); }

private:
	MIDIQueueStorage<T, Capacity, LaneCount> queue_storage{};
};

class MIDIQueueManagerUSB {
public:
	void enqueue_message(uint32_t full_message);
	[[nodiscard]] bool has_buffered_send_data() const;
	[[nodiscard]] int send_buffer_space() const;
	bool consume_queued_messages(uint8_t* data_sending_now, uint8_t& num_bytes_sending_now, bool usb_host_mode);
	/// Clears USB queue contents and fairness bookkeeping for this device.
	/// This is the queue-reset counterpart to DIN output-state reset.
	void reset_queue_storage();
	/// Returns queued packet count for one USB priority lane.
	[[nodiscard]] uint16_t queue_count(QueuePriority priority) const;
	/// Queue occupancy (count form): total queued USB packets across all priority lanes.
	/// Conceptually the counted version of DIN `has_serial_data()`.
	[[nodiscard]] uint32_t total_queued_messages() const;

	struct USBSendContext {
		uint32_t& message_out;
	};

private:
	/// Classifies an outgoing MIDI message into priority groups.
	[[nodiscard]] static QueuePriority classify_packed_usb_priority(uint32_t packed);
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           USBSendContext& context) const;
	/// Pops one queued packet according to strict USB priority ordering.
	bool pop_lane(QueuePriority priority, USBSendContext& context);
	// This is a ring buffer for data waiting to be sent which doesn't fit the smaller buffer above.
	// Any code which wants to send midi data would use the writing side and append more messages.
	// When we are ready to send data on this device, we consume data on the reading side and move it into the
	// smaller dataSendingNow buffer above.
	// Messages are queued in priority-specific rings and consumed in priority order.
	MIDIQueueManagerDeviceState<uint32_t, MIDI_SEND_BUFFER_LEN_RING, QUEUE_PRIORITY_COUNT> queue_manager_{};
};

class MIDIQueueManagerDIN {
public:
	/// Resets serial queue pacing state to a known baseline.
	void reset_serial_state(uint32_t now_sample_timer);
	/// Returns whether any serial-priority lane has pending bytes.
	[[nodiscard]] bool has_serial_data() const;
	/// Queues one channel/system MIDI message into the serial-priority queues.
	void enqueue_message(MIDIMessage message);
	/// Drains serial-priority queues into UART under pacing and priority rules.
	void consume_queued_messages(uint32_t now_sample_timer);

	struct DINSendContext {
		uint8_t* out_bytes;
		int32_t budget_bytes;
		int32_t uart_space;
		int32_t max_len;
		QueuePriority& popped_priority;
	};

private:
	/// Number of active serial-priority lanes [clock..CC] scanned during dequeue.
	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_CC + 1;
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           DINSendContext& context) const;
	/// Pops one realtime byte or one complete MIDI message according to lane priority.
	bool pop_lane(QueuePriority priority, DINSendContext& context);
	/// Per-priority byte rings holding pending DIN output grouped by queue policy.
	MIDIQueueManagerDeviceState<uint8_t, 512, k_serial_priority_count> queue_manager_{};
	/// Last sample-timer tick used to accrue DIN pacing budget.
	uint32_t serial_budget_last_update_{0};
	/// Token-bucket send budget in Q8 bytes (8 fractional bits).
	int32_t serial_budget_Q8_{0};
};
