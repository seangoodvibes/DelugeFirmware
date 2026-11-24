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

#include "gui/context_menu/reset_param_values.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_tempo_change.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include <array>
namespace deluge::gui::context_menu {
PLACE_SDRAM_BSS ResetParamValues reset_param_values{};
char const* ResetParamValues::getTitle() {
	return l10n::get(l10n::String::STRING_FOR_ARE_YOU_SURE_QMARK);
}
std::span<char const*> ResetParamValues::getOptions() {
	if (display->haveOLED()) {
		static std::array options = {l10n::get(l10n::String::STRING_FOR_OK)};
		return options;
	}
	static std::array options = {l10n::get(l10n::String::STRING_FOR_SURE)};
	return options;
}
bool ResetParamValues::acceptCurrentOption() {
	std::array<char, MODEL_STACK_MAX_SIZE> memory_storage{};
	char* memory = memory_storage.data();
	auto* stack = soundEditor.getCurrentModelStack(memory);
	// One undoable step, never merged with neighbouring edits.
	actionLogger.closeAction(ActionType::MISC);
	Action* action =
	    actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED, ActionOrigin::RESET_PARAM_VALUES);
	bool song_params = stack->paramManager == &currentSong->paramManager;
	auto* tempo = currentSong->paramManager.getUnpatchedParamSet()->getClipValueStorage(
	    deluge::modulation::params::UNPATCHED_TEMPO);
	uint64_t time_before = currentSong->timePerTimerTickBig;
	int64_t tempo_before = tempo->getUnclampedValue();
	Error error = stack->paramManager->resetValuesToPreset(stack, action);
	if (song_params) {
		currentSong->updateBPMFromAutomation(true);
		// Undoing the tempo scalar alone would not restore the clock.
		void* memory_for_tempo =
		    action != nullptr ? GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceTempoChange)) : nullptr;
		if (memory_for_tempo != nullptr) {
			// Ownership passes to the action.
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			action->addConsequence(new (memory_for_tempo) ConsequenceTempoChange(
			    time_before, currentSong->timePerTimerTickBig, tempo_before, tempo->getUnclampedValue()));
		}
	}
	actionLogger.closeAction(ActionType::MISC);
	if (error != Error::NONE) {
		display->displayError(error);
	}
	return false;
}
} // namespace deluge::gui::context_menu
