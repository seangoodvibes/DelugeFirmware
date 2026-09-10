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

#include "modulation/patch/patch_cable_set.h"
#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "io/midi/midi_device.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param_pool.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_pool.h"
#include "modulation/patch/patch_cable_sound.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/algorithm/quick_sorter.h"
#include "util/misc.h"
#include <string.h>
#include <utility>

namespace params = deluge::modulation::params;

void flagCable(uint32_t* flags, int32_t c) {
	int32_t idx;
	if constexpr (kNumUnsignedIntegersToRepPatchCables > 1) {
		idx = c >> 5;
	}
	else {
		idx = 0;
	}
	flags[idx] |= ((uint32_t)1 << (c & 31));
}

void unflagCable(uint32_t* flags, int32_t c) {
	int32_t idx;
	if constexpr (kNumUnsignedIntegersToRepPatchCables > 1) {
		idx = c >> 5;
	}
	else {
		idx = 0;
	}
	flags[idx] &= ~((uint32_t)1 << (c & 31));
}

inline void PatchCableSet::freeDestinationMemory(bool destructing) {
	for (int32_t g = 0; g < 2; g++) {
		if (destinations[g]) {
			delugeDealloc(destinations[g]);
			if (!destructing) {
				destinations[g] = nullptr;
			}
		}
	}
}

PatchCableSet::PatchCableSet(ParamCollectionSummary* summary) : ParamCollection(sizeof(PatchCableSet), summary) {
	numUsablePatchCables = 0;
	sourcesPatchedToAnything[0] = sourcesPatchedToAnything[1] = 0;
	numPatchCables = 0;
	destinations[GLOBALITY_LOCAL] = nullptr;
	destinations[GLOBALITY_GLOBAL] = nullptr;
}

PatchCableSet::~PatchCableSet() {
	clear_cables();
}

void PatchCableSet::clear_cables(ParamCollectionSummary* summary) {
	for (auto*& cable : patch_cables_)
		patch_cable_pool::get().release(std::exchange(cable, nullptr));
	numPatchCables = numUsablePatchCables = 0;
	sourcesPatchedToAnything[0] = sourcesPatchedToAnything[1] = 0;
	freeDestinationMemory(false);
	if (summary)
		refresh_automation_flags(summary);
}

PatchCable* PatchCableSet::append_cable() {
	if (numPatchCables == kMaxNumPatchCables)
		return nullptr;
	auto* cable = patch_cable_pool::get().acquire();
	if (cable)
		patch_cables_[numPatchCables++] = cable;
	return cable;
}

Error PatchCableSet::setup_cable(PatchSource source, uint8_t destination, int32_t value) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(destination);
	auto index = getPatchCableIndex(source, descriptor, nullptr, true);
	if (index == 255)
		return Error::INSUFFICIENT_RAM;
	patch_cables_[index]->setup(source, destination, value);
	return Error::NONE;
}

bool PatchCableSet::isSourcePatchedToSomething(PatchSource s) {
	uint32_t sourcesPatchedToSomething =
	    sourcesPatchedToAnything[GLOBALITY_LOCAL] | sourcesPatchedToAnything[GLOBALITY_GLOBAL];
	return sourcesPatchedToSomething & (1 << util::to_underlying(s));
}

// To be called when setupPatching() hasn't been called yet
bool PatchCableSet::isSourcePatchedToSomethingManuallyCheckCables(PatchSource s) {
	for (int32_t c = 0; c < numPatchCables; c++) {
		if (patch_cables_[c]->from == s) {
			return true;
		}
	}

	return false;
}

bool PatchCableSet::doesParamHaveSomethingPatchedToIt(int32_t p) {
	return getDestinationForParam(p);
}

void PatchCableSet::swapCables(int32_t c1, int32_t c2) {
	std::swap(patch_cables_[c1], patch_cables_[c2]);
}

Destination* PatchCableSet::getDestinationForParam(int32_t p) {
	int32_t globality = (p < params::FIRST_GLOBAL) ? GLOBALITY_LOCAL : GLOBALITY_GLOBAL;

	// Special case - no Destinations at all
	if (!destinations[globality]) {
		return nullptr;
	}

	ParamDescriptor destinationParamDescriptor;
	destinationParamDescriptor.setToHaveParamOnly(p);

	// TODO: don't just linear search. But then, there won't be more than like 4 Destinations usually....
	for (Destination* destination = &destinations[globality][0]; destination->sources; destination++) {
		if (destination->destinationParamDescriptor == destinationParamDescriptor) {
			return destination;
		}
	}
	return nullptr;
}

const int32_t neutralRangeAdjustmentValue = 536870912;
extern int32_t rangeFinalValues[];

void PatchCableSet::setupPatching(ModelStackWithParamCollection const* modelStack) {

	// Deallocate any old memory
	freeDestinationMemory(false);
	numUsablePatchCables = 0;
	sourcesPatchedToAnything[0] = sourcesPatchedToAnything[1] = 0;
	refresh_automation_flags(modelStack->summary);

	// Allocate new memory - max size we might need
	for (int32_t g = 0; g < 2; g++) {
		destinations[g] =
		    (Destination*)GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Destination) * (kMaxNumPatchCables + 1));

		// If couldn't...
		if (!destinations[g]) {

			// If we'd got the first one successfully, deallocate it again
			if (g == 1) {
				delugeDealloc(destinations[0]);
				destinations[0] = nullptr;
			}

			// And get out
			return;
		}
	}

	int32_t numPotentiallyUsablePatchCables = numPatchCables;

	// Reorder patch cables so the usable ones are first. Look at each one in turn...
	for (numUsablePatchCables = 0; numUsablePatchCables < numPotentiallyUsablePatchCables; numUsablePatchCables++) {

		// If not usable, try and find one at the end to swap with
		if (!patchCableIsUsable(numUsablePatchCables, modelStack)) {
			while (true) {
				numPotentiallyUsablePatchCables--;
				if (numPotentiallyUsablePatchCables == numUsablePatchCables) {
					goto numUsablePatchCablesWorkedOut;
				}

				if (patchCableIsUsable(numPotentiallyUsablePatchCables, modelStack)) {
					swapCables(numPotentiallyUsablePatchCables, numUsablePatchCables);
					break;
				}
			}
		}
	}

numUsablePatchCablesWorkedOut:

	// Do another pass, ensuring that for every range-adjust*ing* cable, its destination range-adjust*ed* cable actually
	// exists.
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
goAgainWithoutIncrement:
		ParamDescriptor* ourDescriptor = &patch_cables_[c]->destinationParamDescriptor;

		// Initialize this other stuff at the same time:
		patch_cables_[c]->rangeAdjustmentPointer = &neutralRangeAdjustmentValue;

		// If range-adjust*ing*...
		if (!ourDescriptor->isJustAParam()) {

			// Find the range-adjust*ed* cable, whose range/depth we're adjusting.
			int32_t destinationCableIndex = getPatchCableIndex(ourDescriptor->getBottomLevelSource(),
			                                                   ourDescriptor->getDestination(), nullptr, false);

			// If doesn't exist, make this range-adjust*ing* cable "unusable".
			if (destinationCableIndex == 255) {
				numUsablePatchCables--;
				if (numUsablePatchCables == c) {
					break;
				}
				swapCables(c, numUsablePatchCables);
				goto goAgainWithoutIncrement;
			}
		}
	}

	int32_t numDestinations[2];
	numDestinations[GLOBALITY_LOCAL] = 0;
	numDestinations[GLOBALITY_GLOBAL] = 0;
	sourcesPatchedToAnything[GLOBALITY_LOCAL] = 0;
	sourcesPatchedToAnything[GLOBALITY_GLOBAL] = 0;

	// Group cables by destination and create Destination objects
	for (int32_t c = 0; c < numUsablePatchCables;) {

		ParamDescriptor destinationParamDescriptor = patch_cables_[c]->destinationParamDescriptor;
		int32_t p = destinationParamDescriptor.getJustTheParam();
		int32_t globality = (p < params::FIRST_GLOBAL) ? GLOBALITY_LOCAL : GLOBALITY_GLOBAL;

		// Remember that this is the first cable for the param
		destinations[globality][numDestinations[globality]].destinationParamDescriptor = destinationParamDescriptor;
		destinations[globality][numDestinations[globality]].firstCable = c;
		uint32_t sources = 1 << util::to_underlying(patch_cables_[c]->from);

		// Go through the rest of the cables and see which ones want grouping with this one (due to same destination
		// descriptor).
		for (int32_t c2 = c + 1; c2 < numUsablePatchCables; c2++) {

			// If same destination descriptor...
			if (patch_cables_[c2]->destinationParamDescriptor == destinationParamDescriptor) {

				sources |= (1 << util::to_underlying(patch_cables_[c2]->from));

				c++; // Always keep c pointing to the first one we haven't investigated yet

				if (c2 != c) {
					swapCables(c2, c); // If this wasn't the "first next" cable, do a swap
				}
			}
		}

		// Move on, and store that this was the end of this grouping
		c++;
		destinations[globality][numDestinations[globality]].endCable = c;
		destinations[globality][numDestinations[globality]].sources = sources;
		sourcesPatchedToAnything[globality] |= sources; // Collect this info
		numDestinations[globality]++;
	}

	// Finish stuff up, for each globality
	for (int32_t globality = 0; globality < 2; globality++) {

		// If no Destinations here at all, free memory
		if (!numDestinations[globality]) {
			delugeDealloc(destinations[globality]);
			destinations[globality] = nullptr;
		}

		// Otherwise...
		else {

			// Probably shorten memory, from max size which we allocated
			if (numDestinations[globality] < kMaxNumPatchCables) {
				GeneralMemoryAllocator::get().shortenRight(destinations[globality],
				                                           sizeof(Destination) * (numDestinations[globality] + 1));
			}

			// Write the "end of list" marker
			destinations[globality][numDestinations[globality]].sources = 0;
			destinations[globality][numDestinations[globality]].destinationParamDescriptor.setToNull();

			// Sort the Destinations by their DestinationDescriptor. That's just the Destinations that we're sorting -
			// not the actual cables, which are just grouped by param but we don't care about their order otherwise.
			if (numDestinations[globality] >= 2) {
				QuickSorter quickSorter(sizeof(Destination), 32,
				                        destinations[globality]); // Pretty sure 32 is right? Was previously 16, which I
				                                                  // guess worked because we
				// currently only have "destinations" containing up to one source and one param
				// (i.e. the destination is another cable, whose range/depth we're controlling).
				quickSorter.sort(numDestinations[globality]);
			}

			// For each range-adjusting Destination...
			Destination* destination = destinations[globality];
			int32_t i = 0;
			for (; destination->destinationParamDescriptor.data < (uint32_t)0xFFFFFF00; destination++) {

				ParamDescriptor cableDestination = destination->destinationParamDescriptor.getDestination();

				// Get the cable whose range we're adjusting to store a reference back to the one adjusting its range
				int32_t c = getPatchCableIndex(destination->destinationParamDescriptor.getBottomLevelSource(),
				                               cableDestination);
				if (c != 255) {
					patch_cables_[c]->rangeAdjustmentPointer = &rangeFinalValues[i];
				}

				// Ensure that any changes to the range/depth (because of sources patched to the range/depth, i.e. the
				// cable we're looking at right now) also cause the cable whose range/depth we're adjusting (and so also
				// the param that that goes to) to recompute.
				Destination* thatDestination = destination + 1;
				while (thatDestination->destinationParamDescriptor != cableDestination) {
					thatDestination++;

#if ALPHA_OR_BETA_VERSION
					// If getting crashes here, well I previously fixed a bug where sometimes the range-adjust*ed* cable
					// was not "allowed", so was not present here, but the adjust*ing* cable still was here, which this
					// code can't handle. So check that again?
					if (thatDestination >= (&destinations[globality][numDestinations[globality]])) {
						FREEZE_WITH_ERROR("E434");
					}
#endif
				}
				thatDestination->sources |= destination->sources;

				// And, any time the cable whose range we're adjusting recomputes, we need to also recompute its range
				// so that that value is handy.
				// TODO: if we ever wanted to allow another level of range-adjustment, this would probably need
				// expanding.
				destination->sources |=
				    1 << util::to_underlying(destination->destinationParamDescriptor.getBottomLevelSource());

				i++;
			}
		}
	}

	// Also, as we've just re-arranged patched cables, we need to check again whether any have interpolation active, or
	// are even automated.
	refresh_automation_flags(modelStack->summary);
}

void PatchCableSet::refresh_automation_flags(ParamCollectionSummary* summary) {
	summary->resetAutomationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
	summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
	for (int32_t c = 0; c < numUsablePatchCables; ++c) {
		if (patch_cables_[c]->is_automated()) {
			flagCable(summary->whichParamsAreAutomated, c);
			if (patch_cables_[c]->get_auto_param()->hasInterpolationIncrement())
				flagCable(summary->whichParamsAreInterpolating, c);
		}
	}
}

// Searches unusable ones too
bool PatchCableSet::doesDestinationDescriptorHaveAnyCables(ParamDescriptor destinationParamDescriptor) {
	for (int32_t c = 0; c < numPatchCables; c++) {
		if (patch_cables_[c]->destinationParamDescriptor == destinationParamDescriptor) {
			return true;
		}
	}
	return false;
}

bool PatchCableSet::isSourcePatchedToDestinationDescriptorVolumeInspecific(PatchSource s,
                                                                           ParamDescriptor destinationParamDescriptor) {
	if (destinationParamDescriptor.getJustTheParam() == params::GLOBAL_VOLUME_POST_FX) {

		if (getPatchCableIndex(s, destinationParamDescriptor) != 255) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::GLOBAL_VOLUME_POST_REVERB_SEND);

		if (getPatchCableIndex(s, destinationParamDescriptor) != 255) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::LOCAL_VOLUME);

		return (getPatchCableIndex(s, destinationParamDescriptor) != 255);
	}
	else {
		return (getPatchCableIndex(s, destinationParamDescriptor) != 255);
	}
}

bool PatchCableSet::isAnySourcePatchedToParamVolumeInspecific(ParamDescriptor destinationParamDescriptor) {

	if (destinationParamDescriptor.getJustTheParam() == params::GLOBAL_VOLUME_POST_FX) {

		if (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor)) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::GLOBAL_VOLUME_POST_REVERB_SEND);

		if (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor)) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::LOCAL_VOLUME);

		return (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor));
	}
	else {
		return (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor));
	}
}

// Returns the index of the cable going from this source to this param, if one exists - even if it's not usable.
// If one doesn't exist, it creates one, and this may involve overwriting an unusable one
// Only need to supply a ModelStack if createIfNotFound == true AND you want to allow setupPatching to be called (which
// you should, unless you're going to do it yourself).
uint8_t PatchCableSet::getPatchCableIndex(PatchSource from, ParamDescriptor destinationParamDescriptor,
                                          ModelStackWithParamCollection const* modelStack, bool createIfNotFound) {
	int32_t c;
	for (c = 0; c < numPatchCables; c++) {
		if (patch_cables_[c]->from == from
		    && patch_cables_[c]->destinationParamDescriptor == destinationParamDescriptor) {
			return c;
		}
	}

	// If no patch cable exists for this combination yet, try to create one - if we're allowed
	if (!createIfNotFound) {
		return 255;
	}

	// If all patch cables are full up, see if we can overwrite an "unusable" one
	if (numPatchCables >= kMaxNumPatchCables) {
		/* Deactivated - what if we overwrote one currently being edited in the SoundEditor? Just fail for now
		 *
		if (numUsablePatchCables < maxNumPatchCables) {
		    c = numUsablePatchCables;
		    goto claimPatchCable;
		}
		 */

		// Or if there was no space even there, fail.
		return 255;
	}

	// Or, if there was an actual blank space, just use that
	else {
		c = numPatchCables;
		if (!append_cable())
			return 255;
		patch_cables_[c]->initAmount(0);
		patch_cables_[c]->from = from;
		patch_cables_[c]->destinationParamDescriptor = destinationParamDescriptor;
		patch_cables_[c]->setDefaultPolarity();
		// Re-setup the patching, to place this cable where it needs to be
		if (modelStack) {
			setupPatching(modelStack);
		}

		// And since that will have shuffled the cables around, we need to get our one's index again
		for (c = 0; c < numPatchCables; c++) {
			if (patch_cables_[c]->from == from
			    && patch_cables_[c]->destinationParamDescriptor == destinationParamDescriptor) {
				return c;
			}
		}
	}

	// Shouldn't ever get here, but just in case...
	return 255;
}

void PatchCableSet::deletePatchCable(ModelStackWithParamCollection const* modelStack, uint8_t c) {
	if (c >= numPatchCables) {
		return; // Could probably happen. (Still?)
	}
	patch_cable_pool::get().release(patch_cables_[c]);
	--numPatchCables;
	patch_cables_[c] = patch_cables_[numPatchCables];
	patch_cables_[numPatchCables] = nullptr;
	setupPatching(modelStack);
}

bool PatchCableSet::patchCableIsUsable(uint8_t c, ModelStackWithThreeMainThings const* modelStack) {
	ParamDescriptor* ourDescriptor = &patch_cables_[c]->destinationParamDescriptor;

	if (ourDescriptor->isNull()) {
		return false; // When would this ever be the case?
	}

	// An unresolved range placeholder: readPatchCablesFromFile() rewrites these into
	// (param, source) form when it can identify the range-adjusted cable; a leftover
	// bare PLACEHOLDER_RANGE (e.g. from old-firmware files whose "range" cables can't
	// be resolved) must never reach the patcher — PLACEHOLDER_RANGE (89) classifies as
	// a "global param" and the patcher would index paramFinalValues[89 - FIRST_GLOBAL],
	// 34 entries out of bounds (a silent memory-corrupting write on hardware).
	if (ourDescriptor->isSetToParamWithNoSource(params::PLACEHOLDER_RANGE)) {
		return false;
	}

	int32_t p = ourDescriptor->getJustTheParam();

	// If a range-adjusting cable, we'll go by whether the cable that it adjusts is allowed. This is nearly perfect and
	// will eliminate some stuff now. And our caller will do a further pass to check that the corresponding
	// range-adjust*ed* cable actually exists.
	PatchSource s = ourDescriptor->getTopLevelSource();

	// Or, if not a range-adjusting cable, do the normal thing.
	if (s == PatchSource::NOT_AVAILABLE) {
		s = patch_cables_[c]->from;
	}
	return (patch_cable_acceptance(modelStack, s, p) == PatchCableAcceptance::ALLOWED);
}

int32_t PatchCableSet::getModifiedPatchCableAmount(int32_t c, int32_t p) {
	// For some params, we square the cable strength, to make it slope up more slowly at first, so we have access
	// to small effects as well as big
	int32_t output;
	int32_t amount = patch_cables_[c]->get_current_value();
	switch (p) {
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case params::GLOBAL_DELAY_RATE:
		output = (amount >> 15) * (amount >> 16);
		if (amount < 0) {
			output = -output;
		}

		if (p == params::LOCAL_PITCH_ADJUST) {
			// If patching to master pitch, adjust range so that, on max range, the velocity-editing steps correspond
			// with whole semitones
			if (patch_cables_[c]->from == PatchSource::VELOCITY) {
				// output = (output / 3 << 1);
				output = multiply_32x32_rshift32_rounded(output, 1431655765) << 1;
			}
			else {
				output = multiply_32x32_rshift32_rounded(output, 1518500250)
				         << 1; // Divides by square root of 2. Gives us 3 octaves of shifting rather than 4
			}
		}

		return output;
	default:
		return amount;
	}
}

// No need to supply Sound if you don't need setupPatching() to be called now (cos you're gonna call it later)
void PatchCableSet::removeAllPatchingToParam(ModelStackWithParamCollection* modelStack, uint8_t p) {
	for (int32_t c = 0; c < numPatchCables;) {
		if (patch_cables_[c]->destinationParamDescriptor.getJustTheParam() == p) {
			deletePatchCable(modelStack, c);
			c = 0; // Compaction and setup may move another matching cable before this index.
		}
		else
			++c;
	}
}

#define FOR_EACH_FLAGGED_PARAM(whichParams)                                                                            \
	for (int32_t i = kNumUnsignedIntegersToRepPatchCables - 1; i >= 0; i--) {                                          \
		uint32_t whichParamsHere = whichParams[i];                                                                     \
		while (whichParamsHere) {                                                                                      \
			int32_t whichBit = 31 - clz(whichParamsHere);                                                              \
			whichParamsHere &= ~((uint32_t)1 << whichBit);                                                             \
			int32_t c = whichBit + (i << 5);

#define FOR_EACH_PARAM_END                                                                                             \
	}                                                                                                                  \
	}

void PatchCableSet::tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating)

	AutoParam* param = patch_cables_[c]->get_auto_param();
	int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);

	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);

	int32_t oldValue = param->getCurrentValue();
	bool shouldNotify = param->tickSamples(numSamples);
	if (shouldNotify) { // Should always actually be true...
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}

void PatchCableSet::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	AutoParam* param = patch_cables_[c]->get_auto_param();
	int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);

	int32_t oldValue = param->getCurrentValue();
	param->setPlayPos(pos, modelStackWithAutoParam, reversed);

	FOR_EACH_PARAM_END

	ParamCollection::setPlayPos(pos, modelStack, reversed);
}

void PatchCableSet::playbackHasEnded(ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating)

	patch_cables_[c]->get_auto_param()->resetInterpolationIncrement();

	FOR_EACH_PARAM_END

	modelStack->summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
}

void PatchCableSet::grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	AutoParam* param = patch_cables_[c]->get_auto_param();
	int32_t oldValue = param->getCurrentValue();
	int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);
	bool shouldNotify = param->grabValueFromPos(pos, modelStackWithAutoParam);
	if (shouldNotify) {
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}

void PatchCableSet::generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
                                    bool shouldPingpong) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	patch_cables_[c]->get_auto_param()->generateRepeats(oldLength, newLength, shouldPingpong);

	FOR_EACH_PARAM_END
}

void PatchCableSet::appendParamCollection(ModelStackWithParamCollection* modelStack,
                                          ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
                                          int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {
	PatchCableSet* otherPatchCableSet = (PatchCableSet*)otherModelStack->paramCollection;

	FOR_EACH_FLAGGED_PARAM(
	    otherModelStack->summary->whichParamsAreAutomated) // Iterate through the *other* ParamManager's stuff

	PatchSource s = otherPatchCableSet->patch_cables_[c]->from;
	int32_t i = getPatchCableIndex(s, otherPatchCableSet->patch_cables_[c]->destinationParamDescriptor);

	if (i != 255) {
		if (auto* destination = patch_cables_[i]->get_auto_param(true)) {
			destination->appendParam(otherPatchCableSet->patch_cables_[c]->get_auto_param(), oldLength,
			                         reverseThisRepeatWithLength, pingpongingGenerally);
			patch_cables_[i]->release_unautomated();
		}
	}

	FOR_EACH_PARAM_END

	refresh_automation_flags(modelStack->summary);
	ticksTilNextEvent = 0;
}

void PatchCableSet::trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
                                 bool maySetupPatching) {

	bool anyStoppedBeingAutomated = false;

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);
	AutoParam* param = patch_cables_[c]->get_auto_param();

	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);
	param->trimToLength(newLength, action, modelStackWithAutoParam);

	if (!param->hasInterpolationIncrement()) {
		unflagCable(modelStack->summary->whichParamsAreInterpolating, c);

		bool stillAutomated = param->isAutomated();
		if (!stillAutomated) {
			anyStoppedBeingAutomated = true;
			patch_cables_[c]->release_unautomated();
			unflagCable(modelStack->summary->whichParamsAreAutomated, c);
		}
	}

	FOR_EACH_PARAM_END

	if (maySetupPatching && anyStoppedBeingAutomated) {
		// In case the absence of automation here, presumably in conjunction with a value of 0, means this Cable is now
		// inconsequential and can be deleted.
		setupPatching(modelStack);
	}

	ticksTilNextEvent = 0;
}

void PatchCableSet::shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
                                      int32_t effectiveLength) {
	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)
	patch_cables_[c]->get_auto_param()->shiftHorizontally(amount, effectiveLength);
	FOR_EACH_PARAM_END
}

AutoParam* PatchCableSet::getParam(ModelStackWithParamCollection const* modelStack, PatchSource source,
                                   ParamDescriptor descriptor, bool allow_creation) {
	int32_t index = getPatchCableIndex(source, descriptor);
	if (index != 255)
		return patch_cables_[index]->get_auto_param(allow_creation);
	if (!allow_creation)
		return nullptr;
	// Reserve automation before adding a cable, so pool failure leaves the set unchanged.
	auto* automation = auto_param_pool::get().acquire();
	if (!automation)
		return nullptr;
	index = getPatchCableIndex(source, descriptor, modelStack, true);
	if (index == 255) {
		auto_param_pool::get().release(automation);
		return nullptr;
	}
	patch_cables_[index]->automation_ = automation;
	patch_cables_[index]->rebind_automation();
	return automation;
}

int32_t PatchCableSet::find_cable(int32_t param_id) const {
	PatchSource source;
	ParamDescriptor descriptor;
	dissectParamId(param_id, &descriptor, &source);
	for (int32_t index = 0; index < numPatchCables; ++index) {
		if (patch_cables_[index]->from == source && patch_cables_[index]->destinationParamDescriptor == descriptor)
			return index;
	}
	return -1;
}

bool PatchCableSet::has_current_value(int32_t param_id) const {
	return find_cable(param_id) >= 0;
}
int32_t PatchCableSet::get_current_value(int32_t param_id) const {
	const int32_t index = find_cable(param_id);
	return index >= 0 ? patch_cables_[index]->get_current_value() : 0;
}

// Might return a ModelStack with NULL autoParam - check for that!
ModelStackWithAutoParam* PatchCableSet::getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation) {
	PatchSource s;
	ParamDescriptor paramDescriptor;
	dissectParamId(modelStack->paramId, &paramDescriptor, &s);
	AutoParam* autoParam = getParam(modelStack, s, paramDescriptor, allowCreation);
	return modelStack->addAutoParam(autoParam);
}

void PatchCableSet::processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped, bool reversed,
                                      bool didPingpong, bool mayInterpolate) {

	ticksTilNextEvent -= ticksSkipped;

	if (ticksTilNextEvent <= 0) {

		modelStack->summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables
		                                              - 1); // We'll repopulate this.

		ticksTilNextEvent = 2147483647;

		FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)
		int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);
		AutoParam* param = patch_cables_[c]->get_auto_param();
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);

		int32_t ticksTilNextEventThisCable = param->processCurrentPos(modelStackWithAutoParam, reversed, didPingpong);
		ticksTilNextEvent = std::min(ticksTilNextEvent, ticksTilNextEventThisCable);

		if (param->hasInterpolationIncrement()) {
			flagCable(modelStack->summary->whichParamsAreInterpolating, c);
		}
		FOR_EACH_PARAM_END
	}
}

Error PatchCableSet::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength,
                                ParamCollectionSummary* summary) {
	// The manager shallow-copied the set. Detach every source-owned pointer before
	// allocating anything so even a partially constructed clone can be destroyed.
	auto source_cables = patch_cables_;
	Destination* source_destinations[2] = {destinations[0], destinations[1]};
	const uint8_t source_count = numPatchCables;
	const uint8_t usable_count = numUsablePatchCables;
	patch_cables_.fill(nullptr);
	destinations[0] = destinations[1] = nullptr;
	numPatchCables = numUsablePatchCables = 0;
	for (uint8_t index = 0; index < source_count; ++index) {
		auto* cable = append_cable();
		if (!cable
		    || cable->clone_from(*source_cables[index], copyAutomation, reverseDirectionWithLength) != Error::NONE) {
			clear_cables(summary);
			return Error::INSUFFICIENT_RAM;
		}
	}
	for (int32_t group = 0; group < 2; ++group) {
		if (!source_destinations[group])
			continue;
		int32_t count = 0;
		do {
			++count;
		} while (source_destinations[group][count - 1].sources);
		destinations[group] =
		    static_cast<Destination*>(GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Destination) * count));
		if (!destinations[group]) {
			clear_cables(summary);
			return Error::INSUFFICIENT_RAM;
		}
		memcpy(destinations[group], source_destinations[group], sizeof(Destination) * count);
	}
	numUsablePatchCables = usable_count;
	if (summary)
		refresh_automation_flags(summary);
	return Error::NONE;
}

void PatchCableSet::readPatchCablesFromFile(Deserializer& reader, int32_t readAutomationUpToPos) {
	clear_cables();

	// These are for loading in old-format presets, back when only one "range adjustable cable" was allowed.
	PatchSource rangeAdjustableCableS = PatchSource::NOT_AVAILABLE;
	int32_t rangeAdjustableCableP = 255;

	char const* tagName;
	reader.match('[');
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())
	       && numPatchCables < kMaxNumPatchCables) { // box and name.
		if (!strcmp(tagName, "patchCable")) {
			reader.match('{'); // inner object value.
			int32_t numCablesAtStartOfThing = numPatchCables;

			PatchSource source = PatchSource::NONE;
			ParamDescriptor destinationParamDescriptor;
			destinationParamDescriptor.setToNull();
			AutoParam tempParam;
			Polarity polarity = Polarity::BIPOLAR;
			bool rangeAdjustable = false;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "source")) {
					source = stringToSource(reader.readTagOrAttributeValue());
					if (source == PatchSource::AFTERTOUCH) {
						polarity = Polarity::UNIPOLAR;
					}
				}
				else if (!strcmp(tagName, "destination")) {
					destinationParamDescriptor.setToHaveParamOnly(params::fileStringToParam(
					    params::Kind::UNPATCHED_SOUND, reader.readTagOrAttributeValue(), true));
				}
				else if (!strcmp(tagName, "amount")) {
					tempParam.readFromFile(reader, readAutomationUpToPos);
				}
				else if (!strcmp(tagName, "rangeAdjustable")) { // Files before V3.2 had this
					rangeAdjustable = reader.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, "polarity")) {
					polarity = stringToPolarity(reader.readTagOrAttributeValue());
				}
				else if (!strcmp(tagName, "depthControlledBy")) {
					reader.match('[');
					while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())
					       && numPatchCables < kMaxNumPatchCables - 1) {
						if (!strcmp(tagName, "patchCable")) {
							reader.match('{');
							PatchSource rangeSource = PatchSource::NONE;
							AutoParam tempRangeParam;
							Polarity rangePolarity = Polarity::BIPOLAR;
							while (*(tagName = reader.readNextTagOrAttributeName())) {
								if (!strcmp(tagName, "source")) {
									rangeSource = stringToSource(reader.readTagOrAttributeValue());
								}
								else if (!strcmp(tagName, "amount")) {
									tempRangeParam.readFromFile(reader, readAutomationUpToPos);
								}
								else if (!strcmp(tagName, "polarity")) {
									rangePolarity = stringToPolarity(reader.readTagOrAttributeValue());
								}
								reader.exitTag();
							}
							reader.match('}'); // leave value
							reader.match('}'); // leave box.
							if (rangeSource != PatchSource::NONE && tempRangeParam.containsSomething(0)) {
								// Ensure no previous patch cable matches this combination
								for (int32_t c = numCablesAtStartOfThing; c < numPatchCables; c++) {
									if (patch_cables_[c]->from == rangeSource) {
										goto doneWithThisRangeCable;
									}
								}

								// And write this range-adjusting cable's details
								if (!append_cable())
									goto doneWithThisRangeCable;
								patch_cables_[numPatchCables - 1]->from = rangeSource;
								patch_cables_[numPatchCables - 1]->take_automation_from(tempRangeParam);
								patch_cables_[numPatchCables - 1]->polarity = rangePolarity;
							}
						} // end of patchcable
doneWithThisRangeCable:
						reader.exitTag();
					} // end of inner patchCable array

				} // end of DepthControlledBy kvp handler
				reader.match(']');
				reader.exitTag();
			}
			if (source != PatchSource::NONE && !destinationParamDescriptor.isNull() && tempParam.containsSomething(0)) {
				// Just make sure this cable is allowed in some capacity
				// Deactivated because maySourcePatchToParam() is a Sound function, and this probably isn't really
				// necessary anyway
				// if (maySourcePatchToParam(patch_cables_[numPatchCables]->from, patch_cables_[numPatchCables]->to) ==
				// PatchCableAcceptance::DISALLOWED) goto loadNextPatchCable;

				if (source == PatchSource::X
				    && destinationParamDescriptor.isSetToParamWithNoSource(
				        params::LOCAL_PITCH_ADJUST)) { // Because I briefly made this possible in a 3.2.0 alpha.
					goto abandonThisCable;
				}

				// Ensure no previous patch cable matches this combination
				for (int32_t c = 0; c < numCablesAtStartOfThing; c++) {
					if (patch_cables_[c]->from == source
					    && patch_cables_[c]->destinationParamDescriptor == destinationParamDescriptor) {
						goto abandonThisCable;
					}
				}

				if (rangeAdjustable) {
					rangeAdjustableCableS = source;
					rangeAdjustableCableP = destinationParamDescriptor.getJustTheParam();
				}

				// Any other cables we just found which control the range/depth of this cable - we need to write their
				// destination, because that wasn't necessarily known before.
				for (int32_t c = numCablesAtStartOfThing; c < numPatchCables; c++) {
					patch_cables_[c]->destinationParamDescriptor = destinationParamDescriptor;
					patch_cables_[c]->destinationParamDescriptor.addSource(source);
				}

				// And write this cable's details
				if (!append_cable())
					goto abandonThisCable;
				patch_cables_[numPatchCables - 1]->from = source;
				patch_cables_[numPatchCables - 1]->destinationParamDescriptor = destinationParamDescriptor;
				patch_cables_[numPatchCables - 1]->take_automation_from(tempParam);
				patch_cables_[numPatchCables - 1]->polarity = polarity;
			}
			else {
abandonThisCable:
				// Discard any range-adjusting ones we'd just done above
				for (int32_t c = numCablesAtStartOfThing; c < numPatchCables; c++) {
					patch_cable_pool::get().release(std::exchange(patch_cables_[c], nullptr));
				}
				numPatchCables = numCablesAtStartOfThing;
			}
		}

		reader.exitTag(nullptr, true); // exit outer patchCable element.
		reader.match('}');             // leave box.
	}
	reader.match(']');

	if (rangeAdjustableCableP != 255) {
		for (int32_t c = 0; c < numPatchCables; c++) {
			if (patch_cables_[c]->destinationParamDescriptor.isSetToParamWithNoSource(params::PLACEHOLDER_RANGE)) {
				patch_cables_[c]->destinationParamDescriptor.setToHaveParamAndSource(rangeAdjustableCableP,
				                                                                     rangeAdjustableCableS);
			}
		}
	}
}

void PatchCableSet::writePatchCablesToFile(Serializer& writer, bool writeAutomation) {
	if (!numPatchCables) {
		return;
	}

	// Patch cables
	writer.writeArrayStart("patchCables");
	for (int32_t c = 0; c < numPatchCables;
	     c++) { // I have a feeling this should actually only do up to "numUsablePatchCables"... otherwise we end up
		        // with like FM-related cables written to file for a subtractive preset... etc.
		if (!patch_cables_[c]->destinationParamDescriptor.isJustAParam()) {
			continue; // If it's a depth-controlling cable, we'll deal with that separately, below.
		}

		writer.writeOpeningTagBeginning("patchCable", true);
		writer.writeAttribute("source", sourceToString(patch_cables_[c]->from));
		writer.writeAttribute("destination",
		                      params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
		                                               patch_cables_[c]->destinationParamDescriptor.getJustTheParam()));
		writer.writeAttribute("polarity", polarityToString(patch_cables_[c]->polarity).data());
		writer.insertCommaIfNeeded();
		writer.write("\n");
		writer.printIndents();
		writer.writeTagNameAndSeperator("amount");
		writer.write("\"");
		patch_cables_[c]->write_amount(writer, writeAutomation);
		writer.write("\"");

		// See if another cable(s) controls the range/depth of this cable
		ParamDescriptor paramDescriptor = patch_cables_[c]->destinationParamDescriptor;
		paramDescriptor.addSource(patch_cables_[c]->from);
		bool anyDepthControllingCablesFound = false;
		for (int32_t d = 0; d < numPatchCables; d++) {
			if (patch_cables_[d]->destinationParamDescriptor == paramDescriptor) {
				if (!anyDepthControllingCablesFound) {
					anyDepthControllingCablesFound = true;
					writer.writeOpeningTagEnd();
					writer.writeArrayStart("depthControlledBy");
				}

				writer.writeOpeningTagBeginning("patchCable", true);
				writer.writeAttribute("source", sourceToString(patch_cables_[d]->from));
				writer.writeAttribute("polarity", polarityToString(patch_cables_[d]->polarity).data());
				writer.insertCommaIfNeeded();
				writer.write("\n");
				writer.printIndents();
				writer.writeTagNameAndSeperator("amount");
				writer.write("\"");
				patch_cables_[d]->write_amount(writer, writeAutomation);
				writer.write("\"");
				writer.closeTag(true);
			}
		}

		if (anyDepthControllingCablesFound) {
			writer.writeArrayEnding("depthControlledBy");
			writer.writeClosingTag("patchCable", true, true);
		}
		else {
			writer.closeTag(true);
		}
	}
	writer.writeArrayEnding("patchCables");
}

int32_t PatchCableSet::getParamId(ParamDescriptor destinationParamDescriptor, PatchSource s) {
	destinationParamDescriptor.addSource(s);
	return destinationParamDescriptor.data;
}

void PatchCableSet::dissectParamId(uint32_t paramId, ParamDescriptor* destinationParamDescriptor, PatchSource* s) {
	ParamDescriptor paramDescriptor;
	paramDescriptor.data = paramId;

	*destinationParamDescriptor = paramDescriptor.getDestination();
	*s = paramDescriptor.getBottomLevelSource();
}

// Watch the heck out! This might delete the PatchCable, and AutoParam, in question.
void PatchCableSet::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                 bool automationChanged, bool automatedBefore, bool automatedNow) {

	ParamCollection::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                              automatedNow);

	bool haveRedoneSetup = false;

	if (!modelStack->timelineCounterIsSet() || ((Clip*)modelStack->getTimelineCounter())->isActiveOnOutput()) {
		int32_t currentValue = modelStack->autoParam->getCurrentValue();
		bool currentValueChanged = (oldValue != currentValue);

		PatchSource s;
		ParamDescriptor destinationParamDescriptor;
		dissectParamId(modelStack->paramId, &destinationParamDescriptor, &s);

		// Delete the patch cable if its value is now 0 and no automation. This is a bit "dangerous" - it will probably
		// delete the AutoParam that called us!
		if (!modelStack->autoParam->containsSomething(0)) {
			int32_t c = getPatchCableIndex(s, destinationParamDescriptor);
			if (c != 255) {

				// Clone the ModelStack, since we only need the smaller amount of data that makes up a
				// ModelStackWithParamCollection, and our call we make below could overwrite further-down fields of the
				// ModelStack. Although I think in this case it actually doesn't - but best to be safe.
				char localModelStackMemory[MODEL_STACK_MAX_SIZE];
				copyModelStack(localModelStackMemory, modelStack, sizeof(ModelStackWithParamCollection));

				deletePatchCable((ModelStackWithParamCollection*)localModelStackMemory, c);
				haveRedoneSetup = true;
			}
		}

		if (currentValueChanged) {
			int32_t p =
			    destinationParamDescriptor
			        .getJustTheParam(); // Yes also do it if we've altered the "range" of a cable to p.... Although,
			                            // would the call below actually cause all of that to get recalculated?
			notify_patch_cable_value_change(modelStack, p);
		}
	}

	if (!haveRedoneSetup && automatedBefore != automatedNow) {
		PatchSource s;
		ParamDescriptor destinationParamDescriptor;
		dissectParamId(modelStack->paramId, &destinationParamDescriptor, &s);
		int32_t c = getPatchCableIndex(s, destinationParamDescriptor);

		if (c == 255)
			return;
		if (automatedNow && c < numUsablePatchCables) {
			flagCable(modelStack->summary->whichParamsAreAutomated, c);
		}
		else {
			unflagCable(modelStack->summary->whichParamsAreAutomated, c);
			unflagCable(modelStack->summary->whichParamsAreInterpolating, c);
		}
	}

	if (!automatedNow) {
		const int32_t index = find_cable(modelStack->paramId);
		if (index >= 0)
			patch_cables_[index]->release_unautomated();
	}
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true; // Surely this could be more targeted?
}

Error PatchCableSet::remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) {
	PatchSource source;
	ParamDescriptor descriptor;
	dissectParamId(modelStack->paramId, &descriptor, &source);
	const int32_t index = getPatchCableIndex(source, descriptor);
	// A deleted route has additional state (such as polarity) that an automation
	// snapshot cannot restore. Preserve the existing missing-route error.
	if (index == 255)
		return Error::INSUFFICIENT_RAM;
	if (state->nodes.getNumElements()) {
		auto* param = patch_cables_[index]->get_auto_param(true);
		if (!param)
			return Error::INSUFFICIENT_RAM;
		param->swapState(state, modelStack->addAutoParam(param));
		return Error::NONE;
	}
	auto* param = patch_cables_[index]->get_auto_param();
	if (param)
		param->swapState(state, modelStack->addAutoParam(param));
	else {
		AutoParam scalar;
		scalar.bind_current_value(patch_cables_[index]->current_value_);
		scalar.swapState(state, modelStack->addAutoParam(&scalar));
	}
	return Error::NONE;
}

void PatchCableSet::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {
	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);
	AutoParam* autoParam = patch_cables_[c]->get_auto_param();
	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(paramId, autoParam);
	autoParam->deleteAutomation(action, modelStackWithParam, false);
	patch_cables_[c]->release_unautomated();

	FOR_EACH_PARAM_END

	modelStack->summary->resetAutomationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
	modelStack->summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
}

void PatchCableSet::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
                                                    Action* action, ModelStackWithParamCollection* modelStack) {

	bool anyStoppedBeingAutomated = false;

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	int32_t paramId = getParamId(patch_cables_[c]->destinationParamDescriptor, patch_cables_[c]->from);
	AutoParam* param = patch_cables_[c]->get_auto_param();
	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(paramId, param);

	param->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStackWithParam);

	if (!param->hasInterpolationIncrement()) {
		unflagCable(modelStack->summary->whichParamsAreInterpolating, c);

		bool stillAutomated = param->isAutomated();
		if (!stillAutomated) {
			anyStoppedBeingAutomated = true;
			patch_cables_[c]->release_unautomated();
			unflagCable(modelStack->summary->whichParamsAreAutomated, c);
		}
	}

	FOR_EACH_PARAM_END

	if (anyStoppedBeingAutomated) {
		// In case the absence of automation here, presumably in conjunction with a value of 0, means this Cable is now
		// inconsequential and can be deleted.
		setupPatching(modelStack);
	}
}

int32_t PatchCableSet::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	return (paramValue >> 23) - 64;
}

int32_t PatchCableSet::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	int32_t paramValue = 1073741824;
	if (knobPos < 64) {
		paramValue = (knobPos + 64) << 23;
	}
	return paramValue;
}

void PatchCableSet::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {

	ParamCollection::notifyPingpongOccurred(modelStack);

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating)
	patch_cables_[c]->get_auto_param()->notifyPingpongOccurred();
	FOR_EACH_PARAM_END
}

void PatchCableSet::grabVelocityToLevelFromMIDIInput(LearnedMIDI* midiInput) {
	if (midiInput->containsSomething() && midiInput->cable) {
		MIDICable* cable = midiInput->cable;
		if (cable->hasDefaultVelocityToLevelSet()) {

			grabVelocityToLevelFromMIDICable(*cable);
		}
	}
}

void PatchCableSet::grabVelocityToLevelFromMIDICable(MIDICable& cable) {

	PatchCable* patchCable = getPatchCableFromVelocityToLevel();
	if (!patchCable) {
		return;
	}

	patchCable->set_current_value(cable.defaultVelocityToLevel);
}

PatchCable* PatchCableSet::getPatchCableFromVelocityToLevel() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(params::LOCAL_VOLUME);

	int32_t i = getPatchCableIndex(
	    PatchSource::VELOCITY, paramDescriptor, nullptr,
	    true); // Uh... ok this could create a cable but won't setupPatching() for it... was that intentional?
	if (i == 255) {
		return nullptr;
	}
	return patch_cables_[i];
}
