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

#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "model/output_parameter_visitors.h"
#include "modulation/automation/auto_param.h"
#include "modulation/patch/patch_cable_set.h"

namespace {
using deluge::modulation::params::Kind;
class PresetParamChange final : public Consequence {
public:
	Output* output;
	ModControllable* owner;
	Kind kind;
	int32_t id;
	int32_t value;
	// Moves every bound clip value too, when value can't hold the whole baseline after a rebase.
	int64_t offset_shift{0};
	PresetParamChange(Output* output, ModControllable* owner, Kind kind, int32_t id, int32_t value)
	    : output(output), owner(owner), kind(kind), id(id), value(value) {
		type = PRESET_PARAM_CHANGE;
	}
	Error revert(TimeType /*time*/, ModelStack* stack) override {
		auto* preset = owner->getPresetValueStorage(kind, id);
		if (preset == nullptr && kind != Kind::PATCH_CABLE) {
			return Error::INSUFFICIENT_RAM;
		}
		int32_t previous = preset != nullptr ? *preset : 0;
		Error error = output->setPresetParameter(stack->song, owner, kind, id, value, false, offset_shift);
		if (error == Error::NONE) {
			value = previous;
			offset_shift = -offset_shift;
		}
		return error;
	}
};
void recordPresetChange(Output* output, ModControllable* owner, Kind kind, int32_t id, int32_t previous) {
	Action* action = actionLogger.getParameterChangeAction(ActionOrigin::LINKED_PARAMETER_MENU);
	if (action != nullptr) {
		bool found = false;
		for (auto* cons = action->firstConsequence; cons != nullptr; cons = cons->next) {
			if (cons->type != Consequence::PRESET_PARAM_CHANGE) {
				continue;
			}
			auto* change = static_cast<PresetParamChange*>(cons);
			found |= change->output == output && change->owner == owner && change->kind == kind && change->id == id;
		}
		if (!found) {
			void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(PresetParamChange));
			if (memory != nullptr) {
				// Ownership passes to the action, which destroys and deallocates the consequence.
				// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
				auto* change = new (memory) PresetParamChange(output, owner, kind, id, previous);
				action->addConsequence(change);
			}
		}
	}
}
// Creating a route fails only where a clip's cables are full. Checked for every clip first, so a
// failure leaves no new route in earlier clips.
PLACE_SDRAM_TEXT bool cablesHaveRoomFor(Output* output, Song* song, ModControllable* owner, int32_t id) {
	Error error =
	    visitOutputParameters(output, song, owner, Kind::PATCH_CABLE, id, [&](ModelStackWithParamId* stack, Clip*) {
		    auto* cables = static_cast<PatchCableSet*>(stack->paramCollection);
		    bool full = cables->getClipValueStorage(id) == nullptr && cables->numPatchCables >= kMaxNumPatchCables;
		    return full ? Error::INSUFFICIENT_RAM : Error::NONE;
	    });
	return error == Error::NONE;
}
} // namespace

PLACE_SDRAM_TEXT void Output::rebasePresetHistory(ModControllable* owner, Kind kind, int32_t id, int64_t difference) {
	// Both undo and redo records restore an absolute baseline.
	for (Action* list : actionLogger.firstAction) {
		for (Action* action = list; action != nullptr; action = action->nextAction) {
			for (auto* cons = action->firstConsequence; cons != nullptr; cons = cons->next) {
				if (cons->type != Consequence::PRESET_PARAM_CHANGE) {
					continue;
				}
				auto* change = static_cast<PresetParamChange*>(cons);
				if (change->output == this && change->owner == owner && change->kind == kind && change->id == id) {
					int64_t target = int64_t{change->value} + change->offset_shift + difference;
					change->value = ClipParamValue::resolve(target);
					change->offset_shift = target - change->value;
				}
			}
		}
	}
}

PLACE_SDRAM_TEXT Error Output::setPresetParameter(Song* song, ModControllable* owner, Kind kind, int32_t id,
                                                  int32_t value, bool recordUndo, int64_t offset_shift) {
	if (owner == nullptr) {
		return Error::NONE;
	}
	// A clip without the route already follows a zero baseline, so it needs no new route.
	bool materialize = kind != Kind::PATCH_CABLE || value != 0 || offset_shift != 0;
	if (kind == Kind::PATCH_CABLE && materialize && !cablesHaveRoomFor(this, song, owner, id)) {
		return Error::INSUFFICIENT_RAM;
	}
	auto* preset = owner->getPresetValueStorage(kind, id);
	if (preset == nullptr && kind == Kind::PATCH_CABLE) {
		preset = static_cast<ModControllableAudio*>(owner)->reuseZeroPresetPatchCable(song, id);
	}
	if (preset == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	int32_t previous = *preset;
	if (previous == value && offset_shift == 0) {
		return Error::NONE;
	}

	// Visit both session and arrangement clips, including the matching drum row
	// when the edited baseline belongs to a sound within a kit.

	// Materialize missing dynamic parameters before changing the shared value.
	// New entries inherit the old baseline with no local adjustment.
	Error error = visitOutputParameters(this, song, owner, kind, id, [&](ModelStackWithParamId* stack, Clip*) -> Error {
		owner->bindPresetValues(stack->paramManager);
		auto* collection = stack->paramCollection;
		bool existed = collection->getClipValueStorage(id) != nullptr;
		if (!existed && !materialize) {
			return Error::NONE;
		}
		auto* param = collection->getAutoParamFromId(stack, true)->autoParam;
		if (!param) {
			return Error::INSUFFICIENT_RAM;
		}
		if (!existed) {
			collection->getClipValueStorage(id)->setValue(previous);
		}
		return Error::NONE;
	});
	if (error != Error::NONE) {
		return error;
	}

	if (recordUndo) {
		recordPresetChange(this, owner, kind, id, previous);
	}
	if (offset_shift != 0) {
		shiftPresetOffsets(song, kind, id, preset, offset_shift);
	}
	*preset = value;
	parameterBaselinesChanged();
	return announcePresetParameterChange(song, owner, kind, id, previous, nullptr, offset_shift);
}
