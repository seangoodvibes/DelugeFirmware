/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "modulation/midi/midi_param_move.h"
#include "model/model_stack.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_set.h"
#include "util/functions.h"

Error move_midi_parameter_state(ModelStackWithAutoParam const* source, ModelStackWithAutoParam* destination) {
	if (!source->autoParam)
		return Error::NONE;
	if (!destination->autoParam)
		return Error::INSUFFICIENT_RAM;
	if (source->paramCollection == destination->paramCollection && source->paramId == destination->paramId)
		return Error::NONE;

	AutoParamState state;
	source->autoParam->swapState(&state, source);
	if (source->paramCollection->getParamKind() == deluge::modulation::params::Kind::MIDI) {
		static_cast<MIDIParamCollection*>(source->paramCollection)->params.deleteAtKey(source->paramId);
	}
	else {
#if ALPHA_OR_BETA_VERSION
		if (source->paramCollection->getParamKind() != deluge::modulation::params::Kind::EXPRESSION)
			FREEZE_WITH_ERROR("E415");
		if (source->paramId >= kNumExpressionDimensions)
			FREEZE_WITH_ERROR("E416");
#endif
		static_cast<ExpressionParamSet*>(source->paramCollection)->setCurrentValueBasicForSetup(source->paramId, 0);
	}
	destination->autoParam->swapState(&state, destination);
	return Error::NONE;
}
