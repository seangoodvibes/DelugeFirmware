/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoder_input_state.h"
#include <cstdint>

namespace deluge::hid::encoders {

/// Translate accumulated encoder ticks into UI actions.
///
/// @param skipActioning  When true, drains and clears encoder state but suppresses most UI actions
///                       (used during SD card I/O and audio engine yield paths).
/// @return true if any encoder produced an action this call.
bool interpretEncoders(bool skipActioning = false);

// These APIs retain the caller's UI session. Queue once, service until pending
// becomes false, then acknowledge dispatch and advance to the next input.
bool queue_session_encoder(uint8_t index, int32_t delta);
bool interpret_session_encoders(bool skipActioning = false);
bool session_encoders_pending();
void clear_session_encoders();

/// Scheduler task entry point: actions any pending encoder movement, then re-blocks itself so it
/// won't run again until the encoder IRQ unblocks it.
void interpretEncodersTask();

} // namespace deluge::hid::encoders
