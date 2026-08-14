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
#include <utility>

class ConnectedUSBMIDIDevice;

/// Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// This class contains only static shared behavior used by queue manager
/// types in queue_manager_types/:
/// 1. Message classification into queue priorities.
/// 2. Saturating controller-debt accounting.
/// 3. RR+debt fair-controller candidate selection.
///
/// Transport-specific queue storage, enqueue/dequeue, pacing, and mutation
/// logic lives in ConnectedDINMIDIDevice and ConnectedUSBMIDIDevice.
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
	/// Saturating increment helper for per-controller fairness debt.
	static void bump_controller_debt(uint8_t* debt, uint8_t controller);
	/// Shared RR+debt candidate selection used by USB and DIN fair dequeue paths.
	static bool select_fair_controller_candidate(std::array<uint16_t, kMaxMIDIValue + 1> const& first_offsets,
	                                             uint8_t next_controller, uint8_t const* controller_debt,
	                                             uint16_t& selected_offset, uint8_t& selected_controller);
	/// Initializes per-controller first-offset snapshot state to "no offset found" and returns the same array.
	static std::array<uint16_t, kMaxMIDIValue + 1>&
	initialize_first_controller_offsets(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets);
	/// Records a controller's first queued CC offset once per queue snapshot.
	static void record_first_controller_offset(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets,
	                                           uint8_t controller, uint16_t offset);
	/// Commits fair-dequeue service for one controller: clear debt and rotate RR cursor.
	static void commit_fair_controller_service(std::array<uint8_t, kMaxMIDIValue + 1>& controller_debt,
	                                           uint8_t& next_controller, uint8_t selected_controller);

	/// Shared fair-CC dequeue orchestration using named owner methods.
	///
	/// The owner provides transport-specific routines:
	/// - collect_candidates(first_offsets): scan queue snapshot and record per-controller first offsets.
	/// - remove_selected(selected_offset, out): atomically remove selected candidate.
	///
	/// This centralizes common flow:
	/// initialize offsets -> select RR/debt candidate -> remove -> commit fairness state.
	template <typename Owner, typename RemoveArg, typename CallArg>
	static bool pop_fair_cc_candidate(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets,
	                                  std::array<uint8_t, kMaxMIDIValue + 1>& controller_debt, uint8_t& next_controller,
	                                  Owner& owner,
	                                  bool (Owner::*collect_candidates)(std::array<uint16_t, kMaxMIDIValue + 1>&),
	                                  bool (Owner::*remove_selected)(uint16_t, RemoveArg), CallArg&& out_arg) {
		auto& offsets = initialize_first_controller_offsets(first_offsets);
		if (!(owner.*collect_candidates)(offsets)) {
			return false;
		}

		uint16_t selected_offset = 0;
		uint8_t selected_controller = 0;
		if (!select_fair_controller_candidate(offsets, next_controller, controller_debt.data(), selected_offset,
		                                      selected_controller)) {
			return false;
		}

		if (!(owner.*remove_selected)(selected_offset, out_arg)) {
			return false;
		}

		commit_fair_controller_service(controller_debt, next_controller, selected_controller);
		return true;
	}

	/// Shared candidate collection for fair CC dequeue.
	///
	/// Owners provide scan methods:
	/// - begin_scan(cursor, limit): initializes scan bounds.
	/// - next_scan(...): advances one step and reports Candidate/Skip/NoMore/Invalid.
	template <typename Owner>
	static bool
	collect_first_controller_offsets_from_scan(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets, Owner& owner,
	                                           bool (Owner::*begin_scan)(uint16_t& cursor, uint16_t& limit) const,
	                                           CandidateScanResult (Owner::*next_scan)(uint16_t& cursor, uint16_t limit,
	                                                                                   uint16_t& candidate_offset,
	                                                                                   uint8_t& controller) const) {
		uint16_t cursor = 0;
		uint16_t limit = 0;
		if (!(owner.*begin_scan)(cursor, limit)) {
			return false;
		}

		bool saw_any_cc = false;
		while (true) {
			uint16_t candidate_offset = 0;
			uint8_t controller = 0;
			CandidateScanResult step = (owner.*next_scan)(cursor, limit, candidate_offset, controller);
			if (step == CandidateScanResult::NoMore) {
				break;
			}
			if (step == CandidateScanResult::Invalid) {
				return false;
			}
			if (step == CandidateScanResult::Candidate) {
				saw_any_cc = true;
				record_first_controller_offset(first_offsets, controller, candidate_offset);
			}
		}

		return saw_any_cc;
	}

	/// Shared enqueue policy wrapper for CC coalescing and debt bookkeeping.
	///
	/// Transport owners provide transport-specific coalesce and enqueue methods.
	template <typename Owner, typename MessageT>
	static bool enqueue_with_cc_policy(Owner& owner, QueuePriority priority, MessageT message, bool allow_coalesce,
	                                   bool track_debt, uint8_t controller,
	                                   std::array<uint8_t, kMaxMIDIValue + 1>& controller_debt,
	                                   bool (Owner::*coalesce)(MessageT),
	                                   bool (Owner::*enqueue_priority_message)(QueuePriority, MessageT)) {
		if (priority == QUEUE_PRIORITY_CC && allow_coalesce && (owner.*coalesce)(message)) {
			return true;
		}

		bool queued_ok = (owner.*enqueue_priority_message)(priority, message);
		if (queued_ok && priority == QUEUE_PRIORITY_CC && track_debt && controller <= kMaxMIDIValue) {
			bump_controller_debt(controller_debt.data(), controller);
		}

		return queued_ok;
	}

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

	/// Shared scan helper used by USB and DIN coalescing to find the newest queued
	/// channel-CC entry matching status+controller.
	template <typename Owner>
	static int32_t
	find_latest_matching_cc_offset(Owner const& owner, uint8_t wanted_status, uint8_t wanted_controller,
	                               bool (Owner::*begin_scan)(uint16_t& cursor, uint16_t& limit) const,
	                               CoalesceScanResult (Owner::*next_scan)(uint16_t& cursor, uint16_t limit,
	                                                                      uint16_t& candidate_offset, uint8_t& status,
	                                                                      uint8_t& controller) const) {
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
			CoalesceScanResult step = (owner.*next_scan)(cursor, limit, candidate_offset, status, controller);
			if (step == CoalesceScanResult::NoMore) {
				break;
			}
			if (step == CoalesceScanResult::Invalid) {
				// Keep behavior tolerant: stop scanning but preserve earlier match.
				break;
			}
			if (step == CoalesceScanResult::Matchable && status == wanted_status && controller == wanted_controller) {
				latest_offset = static_cast<int32_t>(candidate_offset);
			}
		}

		return latest_offset;
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
