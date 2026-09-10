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

#include "modulation/midi/midi_param_collection.h"
#include "definitions_cxx.hpp"
#include "model/model_stack.h"
#include "modulation/automation/auto_param_pool.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_output.h"
#include "storage/storage_manager.h"

MIDIParamCollection::MIDIParamCollection(ParamCollectionSummary* summary)
    : ParamCollection(sizeof(MIDIParamCollection), summary) {

	// Just to indicate there could be some automation, cos we don't actually use this variable properly.
	// TODO: at least make this go to 0 when no MIDIParams present.
	summary->whichParamsAreAutomated[0] = 1;
	summary->whichParamsAreInterpolating[0] = 0;
}

MIDIParamCollection::~MIDIParamCollection() = default;

void MIDIParamCollection::tickTicks(int32_t numTicks, ModelStackWithParamCollection* modelStack) {
	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		AutoParam* param = midiParam->get_auto_param();
		if (!param)
			continue;

		if (param->hasInterpolationIncrement()) {
			int32_t oldValue = param->getCurrentValue();
			bool shouldNotify = param->tickTicks(numTicks);
			if (shouldNotify) { // Should always actually be true...
				ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam->cc, param);
				notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
			}
		}
	}
}

Error MIDIParamCollection::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength,
                                      ParamCollectionSummary* summary) {
	auto error = params.clone_automation(copyAutomation, reverseDirectionWithLength);
	if (summary) {
		summary->whichParamsAreAutomated[0] = 1; // Retain the MIDI collection's scheduling sentinel.
		refresh_interpolation(summary);
	}
	return error;
}

void MIDIParamCollection::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {

	// Bend param is the only one which is actually gonna maybe want to set up some interpolation -
	// but for the other ones we still need to initialize them and crucially make sure automation overriding is
	// switched off
	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		AutoParam* param = midiParam->get_auto_param();
		if (!param)
			continue;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam->cc, param);

		param->setPlayPos(pos, modelStackWithAutoParam, reversed);
	}

	ParamCollection::setPlayPos(pos, modelStack, reversed);
}

void MIDIParamCollection::generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength,
                                          uint32_t newLength, bool shouldPingpong) {
	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		if (auto* param = midiParam->get_auto_param())
			param->generateRepeats(oldLength, newLength, shouldPingpong);
	}
}

void MIDIParamCollection::appendParamCollection(ModelStackWithParamCollection* modelStack,
                                                ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
                                                int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {

	MIDIParamCollection* otherMIDIParamCollection = (MIDIParamCollection*)otherModelStack->paramCollection;
	for (int32_t i = 0; i < otherMIDIParamCollection->params.getNumElements(); i++) {
		MIDIParam* otherMidiParam = otherMIDIParamCollection->params.getElement(i);

		// Find param in *this* collection for the same CC. There should be one
		int32_t j = params.searchExact(otherMidiParam->cc);

		if (j != -1) {
			MIDIParam* midiParam = params.getElement(j);
			if (otherMidiParam->is_automated()) {
				if (auto* param = midiParam->get_auto_param(true))
					param->appendParam(otherMidiParam->get_auto_param(), oldLength, reverseThisRepeatWithLength,
					                   pingpongingGenerally);
				midiParam->release_unautomated();
			}
		}
	}

	ticksTilNextEvent = 0;
	refresh_interpolation(modelStack->summary);
}

void MIDIParamCollection::trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
                                       bool maySetupPatching) {
	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		AutoParam* param = midiParam->get_auto_param();
		if (!param)
			continue;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam->cc, param);

		param->trimToLength(newLength, action, modelStackWithAutoParam);
		midiParam->release_unautomated();
	}
	ticksTilNextEvent = 0;
	refresh_interpolation(modelStack->summary);
}

void MIDIParamCollection::shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
                                            int32_t effectiveLength) {
	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		if (auto* param = midiParam->get_auto_param())
			param->shiftHorizontally(amount, effectiveLength);
	}
}

void MIDIParamCollection::processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped,
                                            bool reversed, bool didPingpong, bool mayInterpolate) {

	ticksTilNextEvent -= ticksSkipped;
	if (ticksTilNextEvent <= 0) {
		bool interpolating = false;

		ticksTilNextEvent = 2147483647;

		for (int32_t i = 0; i < params.getNumElements(); i++) {
			MIDIParam* midiParam = params.getElement(i);
			AutoParam* param = midiParam->get_auto_param();
			if (!param)
				continue;
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam->cc, param);

			int32_t ticksTilNextEventThisParam = param->processCurrentPos(modelStackWithAutoParam, reversed,
			                                                              didPingpong, false, true); // No interpolating
			ticksTilNextEvent = std::min(ticksTilNextEvent, ticksTilNextEventThisParam);
			if (param->hasInterpolationIncrement()) {
				interpolating = true;
			}
		}
		modelStack->summary->whichParamsAreInterpolating[0] = static_cast<uint32_t>(interpolating);
	}
}

Error MIDIParamCollection::remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) {
	if (state->nodes.getNumElements()) {
		auto* context = getAutoParamFromId(modelStack, true);
		if (!context->autoParam)
			return Error::INSUFFICIENT_RAM;
		context->autoParam->swapState(state, context);
	}
	else {
		auto* owner = params.getOrCreateParamFromCC(modelStack->paramId);
		if (!owner)
			return Error::INSUFFICIENT_RAM;
		if (auto* param = owner->get_auto_param())
			param->swapState(state, modelStack->addAutoParam(param));
		else {
			AutoParam scalar;
			scalar.bind_current_value(owner->current_value_);
			scalar.swapState(state, modelStack->addAutoParam(&scalar));
		}
	}
	return Error::NONE;
}

void MIDIParamCollection::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {

	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);

		if (auto* param = midiParam->get_auto_param()) {
			if (param->isAutomated()) {
				auto* context = modelStack->addAutoParam(midiParam->cc, param);
				param->deleteAutomation(action, context, false);
			}
			midiParam->release_unautomated();
		}
	}
	refresh_interpolation(modelStack->summary);
}

ModelStackWithAutoParam* MIDIParamCollection::getAutoParamFromId(ModelStackWithParamId* modelStack,
                                                                 bool allowCreation) {
	auto* owner = params.getParamFromCC(modelStack->paramId);
	if (owner)
		return modelStack->addAutoParam(owner->get_auto_param(allowCreation));
	if (!allowCreation)
		return modelStack->addAutoParam(nullptr);
	// Do not add a CC entry if the AutoParam reservation cannot be satisfied.
	auto* automation = auto_param_pool::get().acquire();
	if (!automation)
		return modelStack->addAutoParam(nullptr);
	owner = params.getOrCreateParamFromCC(modelStack->paramId);
	if (!owner) {
		auto_param_pool::get().release(automation);
		return modelStack->addAutoParam(nullptr);
	}
	owner->automation_ = automation;
	owner->rebind_automation();
	return modelStack->addAutoParam(automation);
}

bool MIDIParamCollection::has_current_value(int32_t param_id) const {
	return params.getParamFromCC(param_id) != nullptr;
}

int32_t MIDIParamCollection::get_current_value(int32_t param_id) const {
	auto* owner = params.getParamFromCC(param_id);
	return owner ? owner->get_current_value() : 0;
}

void MIDIParamCollection::refresh_interpolation(ParamCollectionSummary* summary) {
	summary->whichParamsAreInterpolating[0] = 0;
	for (int32_t index = 0; index < params.getNumElements(); ++index) {
		auto* param = params.getElement(index)->get_auto_param();
		if (param && param->hasInterpolationIncrement())
			summary->whichParamsAreInterpolating[0] = 1;
	}
}

int32_t MIDIParamCollection::autoparamValueToCC(int32_t newValue) {
	int32_t rShift = 25;
	int32_t roundingAmountToAdd = 1 << (rShift - 1);
	int32_t maxValue = 2147483647 - roundingAmountToAdd;

	if (newValue > maxValue) {
		newValue = maxValue;
	}
	return (newValue + roundingAmountToAdd) >> rShift;
}
// For MIDI CCs, which prior to V2.0 did interpolation
// Returns error code
Error MIDIParamCollection::makeInterpolatedCCsGoodAgain(int32_t clipLength) {

	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);

		if (midiParam->cc >= 120) {
			return Error::NONE;
		}
		auto* param = midiParam->get_auto_param();
		if (!param)
			continue;
		Error error = param->makeInterpolationGoodAgain(clipLength, 25);
		midiParam->release_unautomated();
		if (error != Error::NONE) {
			return error;
		}
	}

	return Error::NONE;
}

void MIDIParamCollection::grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) {
	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);

		AutoParam* param = midiParam->get_auto_param();
		if (!param)
			continue;

		// With MIDI, we only want to send these out if the param is actually automated and the value is
		// actually different
		if (param->isAutomated()) {

			int32_t oldValue = param->getCurrentValue();
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam->cc, param);
			bool shouldSend = param->grabValueFromPos(pos, modelStackWithAutoParam);

			if (shouldSend) {
				notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
			}
		}
	}
}

void MIDIParamCollection::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
                                                          Action* action, ModelStackWithParamCollection* modelStack) {

	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		AutoParam* param = midiParam->get_auto_param();
		if (!param)
			continue;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam->cc, param);

		param->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStackWithAutoParam);
		midiParam->release_unautomated();
	}
	refresh_interpolation(modelStack->summary);
}

void MIDIParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                       bool automationChanged, bool automatedBefore,
                                                       bool automatedNow) {

	ParamCollection::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                              automatedNow);

	notify_midi_param_value_change(modelStack, oldValue, modelStack->autoParam->getCurrentValue());
	if (!automatedNow) {
		if (auto* owner = params.getParamFromCC(modelStack->paramId))
			owner->release_unautomated();
		refresh_interpolation(modelStack->summary);
	}
}

bool MIDIParamCollection::mayParamInterpolate(int32_t paramId) {
	return false;
}

int32_t MIDIParamCollection::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	return ParamCollection::knobPosToParamValue(knobPos, modelStack);
}

void MIDIParamCollection::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {
	ParamCollection::notifyPingpongOccurred(modelStack);

	for (int32_t i = 0; i < params.getNumElements(); i++) {
		MIDIParam* midiParam = params.getElement(i);
		if (auto* param = midiParam->get_auto_param())
			param->notifyPingpongOccurred();
	}
}

void MIDIParamCollection::writeToFile(Serializer& writer) {
	if (params.getNumElements()) {

		writer.writeOpeningTag("midiParams");

		for (int32_t i = 0; i < params.getNumElements(); i++) {
			MIDIParam* midiParam = params.getElement(i);
			int32_t cc = midiParam->cc;

			writer.writeOpeningTag("param");
			if (cc == CC_NUMBER_NONE) { // Why would I have put this in here?
				writer.writeTag("cc", "none");
			}
			else {
				writer.writeTag("cc", cc);
			}

			writer.writeOpeningTag("value", false);
			midiParam->write_to_file(writer);
			writer.writeClosingTag("value", false);

			writer.writeClosingTag("param");
		}

		writer.writeClosingTag("midiParams");
	}
}

/*
    for (int32_t i = 0; i < params.getNumElements(); i++) {
        MIDIParam* midiParam = params.getElement(i);

    }
*/
