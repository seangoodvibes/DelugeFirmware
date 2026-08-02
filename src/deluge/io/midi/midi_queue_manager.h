/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// Shared MIDI queue policy and queue-lane helpers used by engine/device manager.
class MidiQueueManager {
public:
	MidiQueueManager();

	enum class QueuePriority : uint8_t {
		CLOCK = 0,
		NOTES,
		EXPRESSION,
		CC,
		SYSEX,
		COUNT,
	};

	/// Classifies an outgoing MIDI message into priority groups.
	static QueuePriority classifyMessage(MIDIMessage message);

	/// Converts generic queue priority into USB ring-lane index constants.
	static uint8_t toUsbPriority(QueuePriority priority);

	/// Returns queued packet count for one USB priority lane.
	static uint16_t usbQueueCount(USBMidiSendQueueStorage const* storage, uint8_t priority);
	/// Returns total queued packet count across all USB priority lanes.
	static uint32_t usbTotalQueuedMessages(USBMidiSendQueueStorage const* storage);
	/// Returns true when any higher-priority USB lane has pending packets.
	static bool usbAnyHigherPriorityHasData(USBMidiSendQueueStorage const* storage, uint8_t priority);
	/// Pops one queued packet according to strict USB priority ordering.
	static bool usbPopPriorityMessage(USBMidiSendQueueStorage* storage, uint32_t& messageOut);
	/// Pushes one packet into the given USB priority lane.
	static void usbPushPriorityMessage(USBMidiSendQueueStorage* storage, uint8_t priority, uint32_t message);
	/// Clears all USB queue lanes/read-write cursors in `storage`.
	static void resetUsbQueueStorage(USBMidiSendQueueStorage* storage);

	/// Resets serial queue pacing state to a known baseline.
	void resetSerialState(uint32_t nowSampleTimer);
	/// Returns whether any serial-priority lane has pending bytes.
	[[nodiscard]] bool hasSerialData() const;
	/// Queues one channel/system MIDI message into the serial-priority queues.
	void enqueueSerialMidiMessage(MIDIMessage message);
	/// Queues one SysEx block into the serial-priority queues.
	void enqueueSerialSysex(uint8_t const* data, int32_t len);
	/// Drains serial-priority queues into UART under pacing and priority rules.
	void flushSerialOutput(uint32_t nowSampleTimer);

	/// Sets CC-message decimation interval in milliseconds.
	void setCcDecimationRateMs(uint8_t rateMs);
	/// Gets CC-message decimation interval in milliseconds.
	[[nodiscard]] uint8_t getCcDecimationRateMs() const { return ccDecimationRateMs_; }
	/// Sets expression-message decimation interval in milliseconds.
	void setExpressionDecimationRateMs(uint8_t rateMs);
	/// Gets expression-message decimation interval in milliseconds.
	[[nodiscard]] uint8_t getExpressionDecimationRateMs() const { return expressionDecimationRateMs_; }
	/// Resets decimation history so first outgoing values pass through.
	void resetMessageDecimationState();
	/// Applies duplicate suppression and timing-based decimation to outgoing messages.
	[[nodiscard]] bool shouldSendAfterDecimation(MIDIMessage message);

private:
	/// Power-of-two byte ring used by each serial priority lane.
	struct SerialByteQueue {
		static constexpr uint16_t kCapacity = 512;

		std::array<uint8_t, kCapacity> data{};
		uint16_t readPos{0};
		uint16_t writePos{0};

		[[nodiscard]] bool empty() const { return readPos == writePos; }
		[[nodiscard]] uint16_t size() const { return (writePos - readPos) & (kCapacity - 1); }
		[[nodiscard]] uint16_t space() const { return (kCapacity - 1) - size(); }
		[[nodiscard]] uint8_t peek(uint16_t offset = 0) const { return data[(readPos + offset) & (kCapacity - 1)]; }
		bool push(uint8_t byte);
		bool pop(uint8_t& out);
		bool popMany(uint8_t* out, uint16_t count);
	};

	/// Per-priority serial queues in strict order: clock > notes > expression > CC > SysEx.
	std::array<SerialByteQueue, static_cast<size_t>(QueuePriority::COUNT)> serialPriorityQueues_{};
	/// Last sample-timer timestamp used for DIN pacing token updates.
	uint32_t serialBudgetLastUpdate_{0};
	/// Q8 fixed-point token bucket of currently permitted DIN bytes.
	int32_t serialDinBudgetQ8_{0};

	/// Refills DIN pacing tokens from elapsed sample time.
	void updateSerialDinBudget(uint32_t nowSampleTimer);
	/// Attempts to enqueue a full byte sequence atomically into one priority lane.
	bool enqueueSerialBytes(QueuePriority priority, uint8_t const* bytes, int32_t len);
	/// Returns true when any lane above `priority` still has queued data.
	[[nodiscard]] bool hasHigherPriorityDataThan(QueuePriority priority) const;
	/// Pops one realtime byte or one complete MIDI message according to lane priority.
	int32_t popNextPrioritizedBytes(uint8_t* outBytes, int32_t maxLen, int32_t budgetBytes, int32_t uartSpace);

	uint8_t ccDecimationRateMs_{1};
	uint8_t expressionDecimationRateMs_{1};
	uint32_t ccDecimationMinSamples_{0};
	uint32_t expressionDecimationMinSamples_{0};

	std::array<std::array<uint8_t, 128>, 16> lastValueMidiCC_{};
	std::array<std::array<uint32_t, 128>, 16> lastMomentMidiCC_{};
	std::array<uint16_t, 16> lastValueMidiPB_{};
	std::array<uint32_t, 16> lastMomentMidiPB_{};
	std::array<uint8_t, 16> lastValueMidiAT_{};
	std::array<uint32_t, 16> lastMomentMidiAT_{};
	std::array<std::array<uint8_t, 128>, 16> lastValueMidiPP_{};
	std::array<std::array<uint32_t, 128>, 16> lastMomentMidiPP_{};
};

extern MidiQueueManager midiQueueManager;
