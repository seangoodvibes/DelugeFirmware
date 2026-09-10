/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "util/functions.h"
#include <cstring>

char const* sourceToString(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return "lfo1";

	case PatchSource::LFO_GLOBAL_2:
		return "lfo3";

	case PatchSource::LFO_LOCAL_1:
		return "lfo2";

	case PatchSource::LFO_LOCAL_2:
		return "lfo4";

	case PatchSource::ENVELOPE_0:
		return "envelope1";

	case PatchSource::ENVELOPE_1:
		return "envelope2";

	case PatchSource::ENVELOPE_2:
		return "envelope3";

	case PatchSource::ENVELOPE_3:
		return "envelope4";

	case PatchSource::VELOCITY:
		return "velocity";

	case PatchSource::NOTE:
		return "note";

	case PatchSource::SIDECHAIN:
		return "compressor";

	case PatchSource::RANDOM:
		return "random";

	case PatchSource::AFTERTOUCH:
		return "aftertouch";

	case PatchSource::X:
		return "x";

	case PatchSource::Y:
		return "y";

	default:
		return "none";
	}
}

PatchSource stringToSource(char const* string) {
	for (int32_t s = 0; s < kNumPatchSources; s++) {
		auto patchSource = static_cast<PatchSource>(s);
		if (!strcmp(string, sourceToString(patchSource))) {
			return patchSource;
		}
	}
	return PatchSource::NONE;
}
