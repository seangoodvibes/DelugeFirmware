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
		    device, MIDIQueueManager::is_channel_cc_status_byte(static_cast<uint8_t>((head_message >> 8) & 0xFF)),
		    context.cc_budget_packets_remaining > 0, &ConnectedUSBMIDIDevice::pop_fair_queued_cc_message,
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
		auto cc_result = MIDIQueueManager::try_fair_pop_cc(
		    device, head_is_cc, context.cc_uart_budget >= 3, &ConnectedDINMIDIDevice::pop_fair_queued_cc_message,
		    context.out_bytes, context.budget_bytes, context.uart_space, context.max_len, context.popped_priority);
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
