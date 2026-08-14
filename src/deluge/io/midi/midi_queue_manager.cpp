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

#include <limits>

/*
 * MIDI Queue Manager Information
 *
 * This file intentionally keeps only shared queue policy and helper routines.
 * Transport-specific implementations live in queue_manager_types/ and call back into
 * this class for common behavior.
 */

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
