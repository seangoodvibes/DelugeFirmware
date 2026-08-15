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

#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_queue_manager.h"

struct ConnectedUSBMIDIDevice::USBSendContext {
	uint32_t& message_out;
	int32_t& cc_budget_packets_remaining;
};

struct ConnectedDINMIDIDevice::DINSendContext {
	uint8_t* out_bytes;
	int32_t budget_bytes;
	int32_t uart_space;
	int32_t max_len;
	int32_t cc_uart_budget;
	QueuePriority& popped_priority;
};

struct USBSendRules {
	uint16_t queue_count(ConnectedUSBMIDIDevice& device, QueuePriority priority) const {
		return device.queue_manager_.queue_count(static_cast<uint8_t>(priority));
	}
	MIDIQueueManager::PriorityLaneTraversalResult
	handle_cc_lane(ConnectedUSBMIDIDevice& device, QueuePriority priority,
	               ConnectedUSBMIDIDevice::USBSendContext& context) const {
		(void)priority;
		uint32_t head_message = device.queue_manager_.head(QUEUE_PRIORITY_CC);
		auto cc_result = MIDIQueueManager::try_fair_pop_cc(
		    MIDIQueueManager::is_channel_cc_status_byte(static_cast<uint8_t>((head_message >> 8) & 0xFF)),
		    context.cc_budget_packets_remaining > 0,
		    [&device](uint32_t& message_out) { return device.pop_fair_queued_cc_message(message_out); },
		    context.message_out);
		if (cc_result == MIDIQueueManager::CCFairPopResult::Popped) {
			context.cc_budget_packets_remaining--;
			return MIDIQueueManager::PriorityLaneTraversalResult::Popped;
		}
		if (cc_result == MIDIQueueManager::CCFairPopResult::NotCC) {
			return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
		}
		return MIDIQueueManager::PriorityLaneTraversalResult::SkipLane;
	}
	bool pop_lane(ConnectedUSBMIDIDevice& device, QueuePriority priority,
	              ConnectedUSBMIDIDevice::USBSendContext& context) const {
		return device.queue_manager_.pop_head(static_cast<uint8_t>(priority), context.message_out);
	}
};

struct DINSendRules {
	uint16_t queue_count(ConnectedDINMIDIDevice& device, QueuePriority priority) const {
		return device.queue_manager_.queue_count(static_cast<uint8_t>(priority));
	}
	MIDIQueueManager::PriorityLaneTraversalResult
	handle_cc_lane(ConnectedDINMIDIDevice& device, QueuePriority priority,
	               ConnectedDINMIDIDevice::DINSendContext& context) const {
		uint8_t status = device.queue_manager_.head(static_cast<uint8_t>(priority));
		int32_t message_len = 0;
		auto head_check = MIDIQueueManager::validate_head_message_pop(
		    status, device.queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.budget_bytes,
		    context.uart_space, context.max_len, message_len);
		if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
			return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
		}

		bool head_is_cc = MIDIQueueManager::is_three_byte_channel_cc(status, message_len);
		auto pop_fair_cc = [&device](uint8_t* out_bytes, int32_t budget_bytes, int32_t uart_space, int32_t max_len,
		                             QueuePriority& popped_priority) -> bool {
			if (budget_bytes < 3 || uart_space < 3 || max_len < 3) {
				return false;
			}

			if (device.queue_manager_.queue_count(QUEUE_PRIORITY_CC) < 3) {
				return false;
			}

			auto begin_scan = [&device](uint16_t& cursor, uint16_t& limit) -> bool {
				cursor = 0;
				limit = device.queue_manager_.queue_count(QUEUE_PRIORITY_CC);
				return limit >= 3;
			};
			auto next_scan = [&device](uint16_t& cursor, uint16_t limit, uint16_t& candidate_offset,
			                           uint8_t& controller) -> MIDIQueueManager::CandidateScanResult {
				if (cursor >= limit) {
					return MIDIQueueManager::CandidateScanResult::NoMore;
				}

				uint8_t message_status = device.queue_manager_.read_at(QUEUE_PRIORITY_CC, cursor);
				int32_t message_len = bytesPerStatusMessage(message_status);
				if (message_len <= 0 || cursor + message_len > limit) {
					return MIDIQueueManager::CandidateScanResult::Invalid;
				}

				uint16_t offset = cursor;
				cursor = static_cast<uint16_t>(cursor + message_len);
				if (!MIDIQueueManager::is_three_byte_channel_cc(message_status, message_len)) {
					return MIDIQueueManager::CandidateScanResult::Skip;
				}

				controller = device.queue_manager_.read_at(QUEUE_PRIORITY_CC, static_cast<uint16_t>(offset + 1));
				candidate_offset = offset;
				return MIDIQueueManager::CandidateScanResult::Candidate;
			};
			auto remove_selected = [&device](uint16_t target_offset, uint8_t* out) -> bool {
				uint16_t queue_size = device.queue_manager_.queue_count(QUEUE_PRIORITY_CC);
				auto read_at = [&device](uint16_t logical_offset) -> uint8_t {
					return device.queue_manager_.read_at(QUEUE_PRIORITY_CC, logical_offset);
				};
				auto reset_queue = [&device]() { device.queue_manager_.clear(QUEUE_PRIORITY_CC); };
				auto append_from_scratch = [&device](uint8_t value) {
					device.queue_manager_.push(QUEUE_PRIORITY_CC, value);
				};
				return MIDIQueueManager::remove_logical_span_and_repack(queue_size, target_offset, 3, out,
				                                                        device.cc_reorder_scratch_.data(), read_at,
				                                                        reset_queue, append_from_scratch);
			};

			bool popped =
			    device.queue_manager_.pop_fair_cc_candidate(begin_scan, next_scan, remove_selected, out_bytes);
			if (popped) {
				popped_priority = QUEUE_PRIORITY_CC;
			}
			return popped;
		};
		auto cc_result = MIDIQueueManager::try_fair_pop_cc(head_is_cc, context.cc_uart_budget >= 3, pop_fair_cc,
		                                                   context.out_bytes, context.budget_bytes, context.uart_space,
		                                                   context.max_len, context.popped_priority);
		if (cc_result == MIDIQueueManager::CCFairPopResult::Popped) {
			return MIDIQueueManager::PriorityLaneTraversalResult::Popped;
		}
		if (cc_result == MIDIQueueManager::CCFairPopResult::NotCC) {
			return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
		}

		return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
	}
	bool pop_lane(ConnectedDINMIDIDevice& device, QueuePriority priority,
	              ConnectedDINMIDIDevice::DINSendContext& context) const {
		if (priority == QUEUE_PRIORITY_CLOCK) {
			if (context.budget_bytes < 1 || context.uart_space < 1 || context.max_len < 1) {
				return false;
			}
			return device.queue_manager_.pop_head(static_cast<uint8_t>(priority), context.out_bytes[0]);
		}

		uint8_t status = device.queue_manager_.head(static_cast<uint8_t>(priority));
		int32_t message_len = 0;
		auto head_check = MIDIQueueManager::validate_head_message_pop(
		    status, device.queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.budget_bytes,
		    context.uart_space, context.max_len, message_len);
		if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
			return false;
		}

		return device.queue_manager_.pop_many(static_cast<uint8_t>(priority), context.out_bytes, message_len);
	}
};
