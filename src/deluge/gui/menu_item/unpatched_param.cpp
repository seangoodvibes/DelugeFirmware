/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item {

void UnpatchedParam::readCurrentValue() {
	this->setValue((((int64_t)soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP()) + 2147483648)
	                    * kMaxMenuValue
	                + 2147483648)
	               >> 32);
}

ModelStackWithAutoParam* UnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	return modelStack->getUnpatchedAutoParamFromId(getP());
}

void UnpatchedParam::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory);
	int32_t value = getFinalValue();
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);

	// send midi follow feedback
	int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
	view.sendMidiFollowFeedback(modelStackWithParam, knobPos);

	if (getRootUI() == &automationView) {
		int32_t p = modelStackWithParam->paramId;
		modulation::params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		automationView.possiblyRefreshAutomationEditorGrid(getCurrentClip(), kind, p);
	}
}

int32_t UnpatchedParam::getFinalValue() {
	if (this->getValue() == kMaxMenuValue) {
		return 2147483647;
	}
	else if (this->getValue() == kMinMenuValue) {
		return -2147483648;
	}
	else {
		return (uint32_t)this->getValue() * (2147483648 / kMidMenuValue) - 2147483648;
	}
}

ParamDescriptor UnpatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP() + deluge::modulation::params::UNPATCHED_START);
	return paramDescriptor;
}

ParamSet* UnpatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getUnpatchedParamSet();
}

deluge::modulation::params::Kind UnpatchedParam::getParamKind() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	return getModelStack(modelStackMemory)->paramCollection->getParamKind();
}

uint32_t UnpatchedParam::getParamIndex() {
	return this->getP();
}

void UnpatchedParam::renderSubmenuItemTypeForOled(int32_t xPixel, int32_t yPixel) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	DEF_STACK_STRING_BUF(paramValue, 10);
	paramValue.appendInt(getParamValue());

	std::string stringForSubmenuItemType;
	stringForSubmenuItemType.append(paramValue.c_str());

	// pad value string so it's 3 characters long
	padStringTo(stringForSubmenuItemType, 3);

	image.drawString(stringForSubmenuItemType, xPixel, yPixel, kTextSpacingX, kTextSpacingY);
}

// ---------------------------------------

// ---------------------------------------

} // namespace deluge::gui::menu_item
