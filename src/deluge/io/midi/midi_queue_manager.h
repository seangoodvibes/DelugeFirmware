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
#include <limits>
#include <utility>

class ConnectedUSBMIDIDevice;

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

	enum class CandidateScanResult : uint8_t {
		NoMore,
		Skip,
		Candidate,
		Invalid,
	};

	enum class CoalesceScanResult : uint8_t {
		NoMore,
		Skip,
		Matchable,
		Invalid,
	};

	enum class CCFairPopResult : uint8_t {
		NotCC,
		BudgetBlocked,
		PopFailed,
		Popped,
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
	/// Callers provide transport-specific queue read/reset/append member callbacks,
	/// while this helper centralizes the in-bounds check, removed-span skip, and
	/// survivor compaction flow.
	template <typename Owner, typename QueueValue, typename ScratchValue>
	static bool remove_logical_span_and_repack(uint16_t queue_size, uint16_t target_offset, uint16_t remove_count,
	                                           Owner& owner, QueueValue* removed_out, ScratchValue* scratch_buffer,
	                                           QueueValue (Owner::*read_at)(uint16_t) const,
	                                           void (Owner::*reset_queue)(),
	                                           void (Owner::*append_from_scratch)(ScratchValue)) {
		if (target_offset + remove_count > queue_size) {
			return false;
		}

		for (uint16_t i = 0; i < remove_count; i++) {
			removed_out[i] = (owner.*read_at)(static_cast<uint16_t>(target_offset + i));
		}

		uint16_t scratch_size = 0;
		for (uint16_t i = 0; i < queue_size; i++) {
			if (i >= target_offset && i < static_cast<uint16_t>(target_offset + remove_count)) {
				continue;
			}
			scratch_buffer[scratch_size] = static_cast<ScratchValue>((owner.*read_at)(i));
			scratch_size++;
		}

		(owner.*reset_queue)();
		for (uint16_t i = 0; i < scratch_size; i++) {
			(owner.*append_from_scratch)(scratch_buffer[i]);
		}
		return true;
	}

	/// Shared CC gate helper: if head is CC and budget allows, attempt fair-pop.
	template <typename Owner, typename PopFn, typename... Args>
	static CCFairPopResult try_fair_pop_cc(Owner& owner, bool head_is_cc, bool budget_ok, PopFn pop_fair_fn,
	                                       Args&&... args) {
		if (!head_is_cc) {
			return CCFairPopResult::NotCC;
		}
		if (!budget_ok) {
			return CCFairPopResult::BudgetBlocked;
		}
		if ((owner.*pop_fair_fn)(std::forward<Args>(args)...)) {
			return CCFairPopResult::Popped;
		}
		return CCFairPopResult::PopFailed;
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

/// Stateful queue-policy facade instantiated per transport/device.
///
/// Owns controller-fairness state and the policy algorithms that operate on it,
/// while delegating transport-specific queue storage and mutation to
/// caller-provided adapters.
class MIDICCQueuePolicy {
public:
	std::array<uint16_t, kMaxMIDIValue + 1> first_offsets{};
	std::array<uint8_t, kMaxMIDIValue + 1> controller_debt{};
	uint8_t next_controller{0};

	/// Collects the first queued CC offset for each controller from a scan snapshot.
	template <typename Owner>
	bool collect_first_controller_offsets_from_scan(
	    Owner& owner, bool (Owner::*begin_scan)(uint16_t& cursor, uint16_t& limit) const,
	    MIDIQueueManager::CandidateScanResult (Owner::*next_scan)(uint16_t& cursor, uint16_t limit,
	                                                              uint16_t& candidate_offset, uint8_t& controller)
	        const) {
		first_offsets.fill(k_no_controller_offset);

		uint16_t cursor = 0;
		uint16_t limit = 0;
		if (!(owner.*begin_scan)(cursor, limit)) {
			return false;
		}

		bool saw_any_cc = false;
		while (true) {
			uint16_t candidate_offset = 0;
			uint8_t controller = 0;
			// Walk the queue snapshot until the scan reports exhaustion or error.
			MIDIQueueManager::CandidateScanResult step =
			    (owner.*next_scan)(cursor, limit, candidate_offset, controller);
			if (step == MIDIQueueManager::CandidateScanResult::NoMore) {
				// No more scan entries remain in this snapshot.
				break;
			}
			if (step == MIDIQueueManager::CandidateScanResult::Invalid) {
				// The snapshot was malformed; abort so callers can fall back safely.
				return false;
			}
			if (step == MIDIQueueManager::CandidateScanResult::Candidate) {
				saw_any_cc = true;
				// Keep the first queued CC offset for each controller only.
				if (controller <= kMaxMIDIValue && first_offsets[controller] == k_no_controller_offset) {
					first_offsets[controller] = candidate_offset;
				}
			}
		}

		return saw_any_cc;
	}

	/// Returns the latest matching CC offset from a coalesce scan snapshot.
	template <typename Owner>
	int32_t find_latest_matching_cc_offset(Owner const& owner, uint8_t wanted_status, uint8_t wanted_controller,
	                                       bool (Owner::*begin_scan)(uint16_t& cursor, uint16_t& limit) const,
	                                       MIDIQueueManager::CoalesceScanResult (Owner::*next_scan)(
	                                           uint16_t& cursor, uint16_t limit, uint16_t& candidate_offset,
	                                           uint8_t& status, uint8_t& controller) const) const {
		uint16_t cursor = 0;
		uint16_t limit = 0;
		if (!(owner.*begin_scan)(cursor, limit)) {
			return -1;
		}

		int32_t latest_offset = -1;
		while (true) {
			uint16_t candidate_offset = 0;
			uint8_t status = 0;
			uint8_t controller = 0;
			// Scan the snapshot once and remember the newest matching CC entry.
			MIDIQueueManager::CoalesceScanResult step =
			    (owner.*next_scan)(cursor, limit, candidate_offset, status, controller);
			if (step == MIDIQueueManager::CoalesceScanResult::NoMore) {
				// Nothing else to inspect in this queue snapshot.
				break;
			}
			if (step == MIDIQueueManager::CoalesceScanResult::Invalid) {
				// Invalid data cannot be coalesced safely.
				break;
			}
			if (step == MIDIQueueManager::CoalesceScanResult::Matchable && status == wanted_status
			    && controller == wanted_controller) {
				// Later matching entries supersede earlier ones in-place.
				latest_offset = static_cast<int32_t>(candidate_offset);
			}
		}

		return latest_offset;
	}

	/// Enqueues with CC coalescing and fairness debt tracking.
	template <typename Owner, typename MessageT>
	bool enqueue_with_cc_policy(Owner& owner, QueuePriority priority, MessageT message, bool allow_coalesce,
	                            bool track_debt, uint8_t controller, bool (Owner::*coalesce)(MessageT),
	                            bool (Owner::*enqueue_priority_message)(QueuePriority, MessageT)) {
		if (priority == QUEUE_PRIORITY_CC && allow_coalesce && (owner.*coalesce)(message)) {
			// Coalesced CC messages do not need a fresh enqueue.
			return true;
		}

		bool queued_ok = (owner.*enqueue_priority_message)(priority, message);
		if (queued_ok && priority == QUEUE_PRIORITY_CC && track_debt && controller <= kMaxMIDIValue) {
			// Remember that this controller has pending pressure for fair dequeue.
			bump_controller_debt(controller);
		}

		return queued_ok;
	}

	/// Pops the fair CC candidate selected from the current queue snapshot.
	template <typename Owner, typename RemoveArg, typename CallArg>
	bool pop_fair_cc_candidate(Owner& owner,
	                           bool (Owner::*collect_candidates)(std::array<uint16_t, kMaxMIDIValue + 1>&),
	                           bool (Owner::*remove_selected)(uint16_t, RemoveArg), CallArg&& out_arg) {
		if (!(owner.*collect_candidates)(first_offsets)) {
			// No CC candidates were present in the snapshot.
			return false;
		}

		uint16_t selected_offset = 0;
		uint8_t selected_controller = 0;
		if (!select_fair_controller_candidate(selected_offset, selected_controller)) {
			// The scan snapshot did not yield a viable controller target.
			return false;
		}

		if (!(owner.*remove_selected)(selected_offset, std::forward<CallArg>(out_arg))) {
			// The selected CC disappeared before removal could complete.
			return false;
		}

		commit_fair_controller_service(selected_controller);
		return true;
	}

	/// Saturating increment for per-controller fairness debt.
	///
	/// Debt models relative enqueue pressure: controllers that accumulate more
	/// unsent writes become more likely to be selected by fair dequeue.
	void bump_controller_debt(uint8_t controller) {
		if (controller <= kMaxMIDIValue && controller_debt[controller] < k_max_controller_debt) {
			controller_debt[controller]++;
		}
	}

	/// Clears the tracked fairness debt for one controller.
	void clear_controller_debt(uint8_t controller) {
		if (controller <= kMaxMIDIValue) {
			controller_debt[controller] = 0;
		}
	}

private:
	static constexpr uint16_t k_no_controller_offset = 0xFFFF;
	static constexpr uint8_t k_max_controller_debt = std::numeric_limits<uint8_t>::max();

	/// Selects one controller candidate using RR baseline with debt override.
	///
	/// - RR baseline: first eligible controller encountered in rotated order.
	/// - Debt override: if any eligible controller has positive debt, pick the
	///   highest-debt one (rotation order implicitly breaks ties).
	bool select_fair_controller_candidate(uint16_t& selected_offset, uint8_t& selected_controller) const {
		uint16_t first_round_robin_offset = k_no_controller_offset;
		uint8_t first_round_robin_controller = 0;
		uint16_t debt_selected_offset = k_no_controller_offset;
		uint8_t debt_selected_controller = 0;
		uint8_t debt_selected_value = 0;

		for (uint16_t search = 0; search < (kMaxMIDIValue + 1); search++) {
			// Rotate from the RR start cursor and wrap into the MIDI controller domain.
			uint8_t controller = static_cast<uint8_t>((next_controller + search) & kMaxMIDIValue);
			// Sentinel means this controller has no queued CC candidate in this snapshot.
			uint16_t target_offset = first_offsets[controller];
			if (target_offset == k_no_controller_offset) {
				continue;
			}

			if (first_round_robin_offset == k_no_controller_offset) {
				// Latch the first eligible hit in rotated order as the RR fallback.
				first_round_robin_offset = target_offset;
				first_round_robin_controller = controller;
			}

			// Debt tracks relative enqueue pressure; keep the highest-debt eligible candidate.
			uint8_t debt = controller_debt[controller];
			if (debt_selected_offset == k_no_controller_offset || debt > debt_selected_value) {
				debt_selected_offset = target_offset;
				debt_selected_controller = controller;
				debt_selected_value = debt;
			}
		}

		if (first_round_robin_offset == k_no_controller_offset) {
			// No controller had a queued CC candidate in this snapshot.
			return false;
		}

		// Default to RR baseline; override only when a valid debt candidate has pressure.
		selected_offset = first_round_robin_offset;
		selected_controller = first_round_robin_controller;
		if (debt_selected_offset != k_no_controller_offset && debt_selected_value > 0) {
			// Positive debt wins over the RR baseline to keep fairness moving.
			selected_offset = debt_selected_offset;
			selected_controller = debt_selected_controller;
		}

		return true;
	}

	/// Commits fair-dequeue service for one controller: clear debt and rotate RR cursor.
	void commit_fair_controller_service(uint8_t selected_controller) {
		if (selected_controller <= kMaxMIDIValue) {
			// The selected controller has just been serviced, so clear its debt.
			controller_debt[selected_controller] = 0;
		}
		// Advance the RR cursor to the next controller after the serviced one.
		next_controller = static_cast<uint8_t>((selected_controller + 1) & kMaxMIDIValue);
	}
};
