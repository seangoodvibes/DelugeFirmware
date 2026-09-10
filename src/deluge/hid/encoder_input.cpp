/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoder_input.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/screensaver.h"
#include "hid/encoder_input_bank.h"
#include "hid/encoders.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/mirror.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "util/functions.h"

namespace deluge::hid::encoders {

static uint32_t timeNextSDTestAction = 0;
static int32_t nextSDTestDirection = 1;

void interpretEncodersTask() {
	// Block before draining so an IRQ that arrives during interpretation can re-wake this task.
	// Blocking after interpretEncoders() can strand a freshly queued tick until the next encoder IRQ.
	blockTask(EncoderTaskID);
	interpretEncoders(false);
}

namespace {
PLACE_SDRAM_BSS gui::ui_session::State<EncoderInputBank> injected_encoders;
bool interpret_encoder_bank(DetentedEncoder* const* funcPtrs, ContinuousEncoder* const* mod_ptrs, bool skipActioning);
} // namespace

bool queue_session_encoder(uint8_t index, int32_t delta) {
	return injected_encoders.active().queue(index, delta);
}

bool session_encoders_pending() {
	return injected_encoders.active().pending();
}
void clear_session_encoders() {
	injected_encoders.active().clear();
}

bool interpret_session_encoders(bool skipActioning) {
	auto& bank = injected_encoders.active();
	EncoderInputBank::Dispatch dispatch(bank);
	if (!dispatch)
		return false;
	DetentedEncoder* functions[] = {&bank.functions[0], &bank.functions[1], &bank.functions[2], &bank.functions[3]};
	ContinuousEncoder* mods[] = {&bank.mods[0], &bank.mods[1]};
	return interpret_encoder_bank(functions, mods, skipActioning);
}

bool interpretEncoders(bool skipActioning) {
	// This entry point drains physical IRQ counters, even if a Remote operation yielded.
	gui::ui_session::Scope hardware(gui::ui_session::Id::Local);
	if (deluge::hid::mirror::encoders())
		return true;
	DetentedEncoder* functions[] = {&scrollY, &scrollX, &tempo, &select};
	ContinuousEncoder* mods[] = {&mod0, &mod1};
	return interpret_encoder_bank(functions, mods, skipActioning);
}

namespace {
bool interpret_encoder_bank(DetentedEncoder* const* funcPtrs, ContinuousEncoder* const* mod_ptrs, bool skipActioning) {
	// do not interpret encoders when stem export is underway
	if (stemExport.processStarted) {
		return false;
	}

	skipActioning |= sdRoutineLock; // if the "sd routine" is yielding then always defer actioning encoders
	bool anything = false;

	if (!skipActioning) {
		input_state().waiting_for_card_routine_end = 0;
	}

#if SD_TEST_MODE_ENABLED
	if (!skipActioning && playbackHandler.isEitherClockActive()
	    && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

		if (getRandom255() < 96)
			nextSDTestDirection *= -1;
		getCurrentUI()->selectEncoderAction(nextSDTestDirection);

		int32_t random = getRandom255();

		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 6); // * 44 / 13;
		anything = true;
	}
#endif

	for (int32_t e = 0; e < (int32_t)kNumFunctionEncoders; e++) {
		// 0=scrollY 1=scrollX 2=tempo 3=select
		bool isScrollY = (e == 0);
		if (!isScrollY) {

			// Basically disables all function encoders during SD routine
			if (skipActioning && currentUIMode != UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
				continue;
			}
		}

		if (input_state().waiting_for_card_routine_end & (1 << e)) {
			continue;
		}

		auto& fe = *funcPtrs[e];
		if (fe.pending()) {
			anything = true;

			int32_t detentDelta = fe.take();

			// Handlers that take int8_t (tempo, select) get a saturating narrowing cast so that very fast
			// spinning cannot overflow the parameter type.  Horizontal/vertical actions take int32_t and
			// receive the full accumulated delta for natural acceleration.
			auto saturatedDelta = static_cast<int8_t>(std::clamp(detentDelta, (int32_t)-128, (int32_t)127));

			ActionResult result;

			switch (e) {

			case 1: // scrollX
				result = getCurrentUI()->horizontalEncoderAction(detentDelta);
				// Actually, after coding this up, I realise I actually have it above stopping the X encoder from even
				// getting here during the SD routine. Ok so we'll leave it that way, in addition to me having made all
				// the horizontalEncoderAction() calls SD-routine-safe
checkResult:
				if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
					input_state().waiting_for_card_routine_end |= (1 << e);
					fe.restore(detentDelta); // Put it back for next time
				}
				break;

			case 0: // scrollY
				if (Buttons::isShiftButtonPressed() && Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
					PadLEDs::changeDimmerInterval(detentDelta);
				}
				else {
					result = getCurrentUI()->verticalEncoderAction(detentDelta, skipActioning);
					goto checkResult;
				}
				break;

			case 2: // tempo
				if ((getCurrentUI() == &instrument_clip_view_for_session()
				     || (getCurrentUI() == &automation_view_for_session()
				         && automation_view_for_session().inNoteEditor()))
				    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::Quantize)
				           == RuntimeFeatureStateToggle::On) {
					instrument_clip_view_for_session().tempoEncoderAction(
					    saturatedDelta, Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					    Buttons::isShiftButtonPressed());
				}
				else {
					playbackHandler.tempoEncoderAction(saturatedDelta,
					                                   Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                   Buttons::isShiftButtonPressed());
				}
				break;

			case 3: // select
				if (Buttons::isButtonPressed(deluge::hid::button::CLIP_VIEW)) {
					PadLEDs::changeRefreshTime(saturatedDelta);
				}
				else if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
					if (currentSong) {
						currentSong->changeThresholdRecordingMode(saturatedDelta);
					}
				}
				else {
					getCurrentUI()->selectEncoderAction(saturatedDelta);
				}
				break;
			}
		}
	}

	if (!skipActioning || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		// Mod knobs
		for (int32_t e = 0; e < 2; e++) {
			// 0=mod0 (lower gold), 1=mod1 (upper gold)
			auto& encoder = *mod_ptrs[e];

			int8_t offset = encoder.take();

			// If encoder turned...
			if (offset != 0) {
				anything = true;

				// Do it, only if
				if (offset + input_state().initial_turn_direction[e] != 0) {
					int8_t offset_accelerated = offset * encoder.calcNextKnobSpeed(offset);

					getCurrentUI()->modEncoderAction(e, offset_accelerated);

					input_state().initial_turn_direction[e] = 0;
				}

				// Otherwise, write this off as an accidental wiggle
				else {
					input_state().initial_turn_direction[e] = offset;
				}
			}
		}
	}

	if (anything) {
		deluge::hid::display::Screensaver::noteActivity();
	}

	return anything;
}

} // namespace

} // namespace deluge::hid::encoders
