/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_output_existence.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "util/misc.h"

ConsequenceOutputExistence::ConsequenceOutputExistence(Output* newOutput, ExistenceChangeType newType) {
	output = newOutput;
	type = newType;
}

// Detached outputs remain owned by the song until restored or song teardown.
// Other consequences may still retain references when this one is discarded.

Error ConsequenceOutputExistence::revert(TimeType time, ModelStack* modelStack) {
	if (time != util::to_underlying(type)) { // Re-create
		if (!modelStack->song->is_output_retained_for_undo(output)) {
			return Error::BUG;
		}
		modelStack->song->addOutput(output, true);
	}

	else { // Re-delete
		if (!modelStack->song->owns_output_for_undo(output, false)) {
			return Error::BUG;
		}
		outputIndex = modelStack->song->removeOutputFromMainList(output);
		if (outputIndex == -1) {
			return Error::BUG;
		}
		modelStack->song->retain_output_for_undo(output);
		output->prepareForHibernationOrDeletion();
	}

	return Error::NONE;
}
