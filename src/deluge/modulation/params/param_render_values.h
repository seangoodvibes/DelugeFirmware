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

#pragma once

#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_value.h"
#include <cstdint>

class ParamManager;

// Resolve a parameter set's render values again after a value changed outside playback.
[[gnu::noinline]] void refreshRenderValues(const ParamCollectionSummary& summary);
#ifdef PARAMETER_RESOLVED_VERIFY
void verifyRenderValue(const ParamCollectionSummary& summary, int32_t p, int32_t value);
#endif

// A read-only view of a parameter set's resolved render values for one render operation.
// It is brought up to date when made, so it must not be kept across a change to any
// parameter value other than by playback, or across collection replacement, cloning or
// song changes.
class ParamRenderValues {
public:
	[[nodiscard]] int32_t get(int32_t p) const {
#ifdef PARAMETER_RESOLVED_VERIFY
		verifyRenderValue(summary_, p, values_[p]);
#endif
		return values_[p];
	}

private:
	friend class ParamManager;
	explicit ParamRenderValues(const ParamCollectionSummary& summary)
	    : values_(summary.paramCollection->renderValues_)
#ifdef PARAMETER_RESOLVED_VERIFY
	      ,
	      summary_(summary)
#endif
	{
		if (summary.paramCollection->renderVersion_ != parameter_value_epoch) [[unlikely]] {
			refreshRenderValues(summary);
		}
	}
	const int32_t* values_;
#ifdef PARAMETER_RESOLVED_VERIFY
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
	const ParamCollectionSummary& summary_;
#endif
};
