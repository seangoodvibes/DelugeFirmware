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
#include "model/midi/message.h"
#include <array>
#include <utility>

class ConnectedUSBMIDIDevice;

// MUST be an exact power of two
enum MidiQueueRingConstants {
	MIDI_SEND_BUFFER_LEN_RING = 1024,
	MIDI_SEND_RING_MASK = MIDI_SEND_BUFFER_LEN_RING - 1,
};

// Priority order is aligned with the LinnStrument ls_midi.ino strategy:
// clock > notes > expression > CC > SysEx.
typedef enum QueuePriority {
	QUEUE_PRIORITY_CLOCK = 0,
	QUEUE_PRIORITY_NOTES = 1,
	QUEUE_PRIORITY_EXPRESSION = 2,
	QUEUE_PRIORITY_CC = 3,
	QUEUE_PRIORITY_SYSEX = 4,
	QUEUE_PRIORITY_COUNT = 5,
} QueuePriority;

/// Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// This class contains only static shared behavior used by queue manager
/// types in queue_manager_types/:
/// 1. Message classification into queue priorities.
/// 2. Shared helpers for queue scanning, mutation, and CC head gating.
///
/// Transport-specific queue storage, enqueue/dequeue, pacing, and fairness
/// state live in ConnectedDINMIDIDevice and ConnectedUSBMIDIDevice.
class MIDIQueueManager {
public:
	static constexpr uint8_t k_channel_cc_status_nibble = 0x0B;

	/// Returns true for channel-CC status bytes (0xBn).
	static constexpr bool is_channel_cc_status_byte(uint8_t status) {
		return (status >> 4) == k_channel_cc_status_nibble;
	}

	/// Returns true for channel-CC status-type nibbles.
	static constexpr bool is_channel_cc_status_type(uint8_t status_type) {
		return status_type == k_channel_cc_status_nibble;
	}

	/// Returns true only for full 3-byte channel-CC messages.
	static constexpr bool is_three_byte_channel_cc(uint8_t status, int32_t message_len) {
		return message_len == 3 && is_channel_cc_status_byte(status);
	}

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

	/// Shared queue-mutation primitive: remove a logical span and repack survivors.
	///
	/// Callers provide transport-specific queue read/reset/append callables,
	/// while this helper centralizes the in-bounds check, removed-span skip, and
	/// survivor compaction flow.
	template <typename QueueValue, typename ScratchValue, typename ReadAtFn, typename ResetQueueFn,
	          typename AppendFromScratchFn>
	static bool remove_logical_span_and_repack(uint16_t queue_size, uint16_t target_offset, uint16_t remove_count,
	                                           QueueValue* removed_out, ScratchValue* scratch_buffer,
	                                           ReadAtFn&& read_at, ResetQueueFn&& reset_queue,
	                                           AppendFromScratchFn&& append_from_scratch) {
		if (target_offset + remove_count > queue_size) {
			return false;
		}

		for (uint16_t i = 0; i < remove_count; i++) {
			removed_out[i] = read_at(static_cast<uint16_t>(target_offset + i));
		}

		uint16_t scratch_size = 0;
		for (uint16_t i = 0; i < queue_size; i++) {
			if (i >= target_offset && i < static_cast<uint16_t>(target_offset + remove_count)) {
				continue;
			}
			scratch_buffer[scratch_size] = static_cast<ScratchValue>(read_at(i));
			scratch_size++;
		}

		reset_queue();
		for (uint16_t i = 0; i < scratch_size; i++) {
			append_from_scratch(scratch_buffer[i]);
		}
		return true;
	}

	/// Shared strict-priority traversal driven by transport rules.
	///
	/// This is a scaffolding entry point for routing different transport types
	/// (USB packet output, DIN byte-stream output, etc.) through one scheduler
	/// while keeping transport-specific behavior in per-device rules.
	///
	/// Rules contract (methods expected on `rules`):
	/// - `uint16_t queue_count(Device&, QueuePriority)`
	/// - `PriorityLaneTraversalResult handle_cc_lane(Device&, QueuePriority, Context&)`
	/// - `bool pop_lane(Device&, QueuePriority, Context&)`
	template <typename Device, typename Rules, typename Context>
	static bool pop_priority_lanes_with_transport_rules(Device& device, Rules& rules, QueuePriority first_priority,
	                                                    QueuePriority last_priority, Context& context) {
		for (uint8_t lane = static_cast<uint8_t>(first_priority); lane <= static_cast<uint8_t>(last_priority); lane++) {
			QueuePriority priority = static_cast<QueuePriority>(lane);
			if (!rules.queue_count(device, priority)) {
				continue;
			}

			if (priority == QUEUE_PRIORITY_CC) {
				auto cc_result = rules.handle_cc_lane(device, priority, context);
				if (cc_result == PriorityLaneTraversalResult::Popped) {
					return true;
				}
				if (cc_result == PriorityLaneTraversalResult::Abort) {
					return false;
				}
				if (cc_result == PriorityLaneTraversalResult::SkipLane) {
					continue;
				}
				if (cc_result != PriorityLaneTraversalResult::PopLane) {
					continue;
				}
			}

			if (rules.pop_lane(device, priority, context)) {
				return true;
			}
		}

		return false;
	}

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

	void overwrite_at(uint16_t logical_offset, T value) { data[(read_pos + logical_offset) & (Capacity - 1)] = value; }
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

	[[nodiscard]] T read_at(uint8_t lane, uint16_t logical_offset) const { return lanes[lane].peek(logical_offset); }
	[[nodiscard]] T head(uint8_t lane) const { return lanes[lane].peek(); }

	bool pop_head(uint8_t lane, T& out) { return lanes[lane].pop(out); }
	void push(uint8_t lane, T value) { (void)lanes[lane].push(value); }
	void clear(uint8_t lane) { lanes[lane].clear(); }
	void overwrite_at(uint8_t lane, uint16_t logical_offset, T value) {
		lanes[lane].overwrite_at(logical_offset, value);
	}
	[[nodiscard]] bool empty(uint8_t lane) const { return lanes[lane].empty(); }
	[[nodiscard]] uint16_t space(uint8_t lane) const { return lanes[lane].space(); }
};

template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueManagerDeviceState {
public:
	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return queue_storage.queue_count(lane); }
	[[nodiscard]] uint32_t total_queued_messages() const { return queue_storage.total_queued_messages(); }
	[[nodiscard]] T read_at(uint8_t lane, uint16_t logical_offset) const {
		return queue_storage.read_at(lane, logical_offset);
	}
	[[nodiscard]] T head(uint8_t lane) const { return queue_storage.head(lane); }
	bool pop_head(uint8_t lane, T& out) { return queue_storage.pop_head(lane, out); }
	void push(uint8_t lane, T value) { queue_storage.push(lane, value); }
	void clear(uint8_t lane) { queue_storage.clear(lane); }
	void overwrite_at(uint8_t lane, uint16_t logical_offset, T value) {
		queue_storage.overwrite_at(lane, logical_offset, value);
	}
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
