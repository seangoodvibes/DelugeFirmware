/*
 * Copyright (c) 2023 Sean Ditny
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

#include "gui/views/automation/editor_layout/mod_controllable/parameter_edit.h"
#include "hid/display/display.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_collection.h"

// Each edit may remove the last node and return its AutoParam to the pool.
void set_parameter_region(ModelStackWithAutoParam* stack, int32_t value, int32_t pos, int32_t length) {
	auto* current = stack->paramCollection->getAutoParamFromId(stack, true);
	if (!current->autoParam) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}
	current->autoParam->setValuePossiblyForRegion(value, current, pos, length);
	stack->paramCollection->getAutoParamFromId(stack, false);
}
