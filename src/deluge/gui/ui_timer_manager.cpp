/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "gui/ui_timer_manager.h"
#include "definitions_cxx.hpp"
#include "gui/ui/graphics_routing.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "hid/hid_sysex.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/mirror.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"

#include <algorithm>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
}

UITimerManager uiTimerManager{};
extern void inputRoutine();
extern void batteryLEDBlink();

UITimerManager::UITimerManager() = default;

void UITimerManager::pause_for_mirror() {
	state_.pause_local(AudioEngine::audioSampleTimer);
}

void UITimerManager::resume_from_mirror() {
	state_.resume_local(AudioEngine::audioSampleTimer);
}

void UITimerManager::routine() {
	const auto owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope scope(owner);
	auto& bank = state_.bank(owner);
	if (deluge::hid::mirror::is_client()) {
		// The OLED select/deselect handshake still needs its hardware timeout.
		// Every musical/UI timer remains suspended while the host owns the UI.
		auto& timer = getTimer(TimerName::OLED_LOW_LEVEL);
		if (timer.active && static_cast<int32_t>(timer.triggerTime - AudioEngine::audioSampleTimer) < 0) {
			timer.active = false;
			oledLowLevelTimerCallback();
		}
		return;
	}

	int32_t timeTilNextEvent = (uint32_t)(bank.next_event - AudioEngine::audioSampleTimer);
	if (timeTilNextEvent >= 0) {
		return;
	}

	for (int32_t i = 0; i < util::to_underlying(TimerName::NUM_TIMERS); i++) {
		auto name = static_cast<TimerName>(i);
		auto& timer = bank.timers[i];
		if (timer.active) {

			int32_t timeTil = (uint32_t)(timer.triggerTime - AudioEngine::audioSampleTimer);
			if (timeTil < 0) {
				timer.active = false;

				switch (name) {

				case TimerName::TAP_TEMPO_SWITCH_OFF:
					playbackHandler.tapTempoAutoSwitchOff();
					break;

				case TimerName::MIDI_LEARN_FLASH:
					view_for_session().midiLearnFlash();
					break;

				case TimerName::DEFAULT_ROOT_NOTE:
					if (getCurrentUI() == &keyboard_screen_for_session()) {
						keyboard_screen_for_session().flashDefaultRootNote();
					}
					else if (getCurrentUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) {
						instrument_clip_view_for_session().flashDefaultRootNote();
					}
					break;

				case TimerName::PLAY_ENABLE_FLASH: {
					view_for_session().flashPlayRoutine();
					break;
				}
				case TimerName::DISPLAY:
					if (display->haveOLED()) {
						auto* oled = static_cast<deluge::hid::display::OLED*>(display);
						oled->timerRoutine();
					}
					else {
						display->timerRoutine();
					}

					break;

				case TimerName::LOADING_ANIMATION:
					if (display->haveOLED()) {
						auto* oled = static_cast<deluge::hid::display::OLED*>(display);
						oled->timerRoutine();
					}
					else {
						display->timerRoutine();
					}

					break;

				case TimerName::MOD_ENCODER_POPUP_FLUSH:
					view_for_session().flushPendingModEncoderValuePopup();
					break;

				case TimerName::LED_BLINK:
				case TimerName::LED_BLINK_TYPE_1:
					indicator_leds::ledBlinkTimeout(i - util::to_underlying(TimerName::LED_BLINK));
					break;

				case TimerName::LEVEL_INDICATOR_BLINK:
					indicator_leds::blinkKnobIndicatorLevelTimeout();
					break;

				case TimerName::SHORTCUT_BLINK:
					sound_editor_for_session().blinkShortcut();
					break;

				case TimerName::INTERPOLATION_SHORTCUT_BLINK:
					automation_view_for_session().blinkInterpolationShortcut();
					break;

				case TimerName::PAD_SELECTION_SHORTCUT_BLINK:
					automation_view_for_session().blinkPadSelectionShortcut();
					break;

				case TimerName::NOTE_ROW_BLINK:
					instrument_clip_view_for_session().blinkSelectedNoteRow();
					break;

				case TimerName::SELECTED_CLIP_PULSE:
					session_view_for_session().gridPulseSelectedClip();
					break;

				case TimerName::MATRIX_DRIVER:
					PadLEDs::timerRoutine();
					break;

				case TimerName::UI_SPECIFIC: {
					ActionResult result = getCurrentUI()->timerCallback();
					if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
						timer.active = true; // Come back soon and try again.
					}
					break;
				}
				case TimerName::BACK_MENU_EXIT: {
					ActionResult result = getCurrentUI()->exitUI();
					if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
						timer.active = true;
					}
					break;
				}

				case TimerName::DISPLAY_AUTOMATION:
					if (((getCurrentUI() == &automation_view_for_session())
					     || (getRootUI() == &automation_view_for_session()))
					    && automation_view_for_session().inAutomationEditor()) {

						automation_view_for_session().displayAutomation();

						if (getCurrentUI() == &sound_editor_for_session()) {
							sound_editor_for_session().getCurrentMenuItem()->readValueAgain();
						}
					}

					else {
						view_for_session().displayAutomation();
					}
					break;

				case TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION:
					// midi follow and midi feedback enabled
					// re-send midi cc's because learned parameter values may have changed
					// only send updates when playback is active
					if (playbackHandler.isEitherClockActive()
					    && (midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED)) {
						uint32_t sendRate = 0;
						if (midiEngine.midiFollowFeedbackAutomation == MIDIFollowFeedbackAutomationMode::LOW) {
							sendRate = kLowFeedbackAutomationRate;
						}
						else if (midiEngine.midiFollowFeedbackAutomation == MIDIFollowFeedbackAutomationMode::MEDIUM) {
							sendRate = kMediumFeedbackAutomationRate;
						}
						else if (midiEngine.midiFollowFeedbackAutomation == MIDIFollowFeedbackAutomationMode::HIGH) {
							sendRate = kHighFeedbackAutomationRate;
						}
						// check time elapsed since previous automation update is greater than or equal to send rate
						// if so, send another automation feedback message
						if ((AudioEngine::audioSampleTimer - midiFollow.timeAutomationFeedbackLastSent) >= sendRate) {
							view_for_session().sendMidiFollowFeedback(nullptr, kNoSelection, true);
							midiFollow.timeAutomationFeedbackLastSent = AudioEngine::audioSampleTimer;
						}
					}
					// if automation feedback was previously sent and now playback is stopped,
					// send one more update to sync controller with deluge's current values
					// for automated params only
					else if (midiFollow.timeAutomationFeedbackLastSent != 0) {
						view_for_session().sendMidiFollowFeedback(nullptr, kNoSelection, true);
						midiFollow.timeAutomationFeedbackLastSent = 0;
					}
					break;

				case TimerName::READ_INPUTS:
					inputRoutine();
					break;

				case TimerName::BATT_LED_BLINK:
					batteryLEDBlink();
					break;

				case TimerName::GRAPHICS_ROUTINE:
					if (deluge::gui::ui_session::graphics_output_ready(
					        [] { return uartGetTxBufferSpace(UART_ITEM_PIC_PADS) > kNumBytesInColUpdateMessage; })) {
						getCurrentUI()->graphicsRoutine();
					}
					setTimer(TimerName::GRAPHICS_ROUTINE, 15);
					break;

				case TimerName::OLED_LOW_LEVEL:
					if (deluge::hid::display::have_oled_screen) {
						oledLowLevelTimerCallback();
					}
					break;

				case TimerName::OLED_CONSOLE:
					if (display->haveOLED()) {
						auto* oled = static_cast<deluge::hid::display::OLED*>(display);
						oled->consoleTimerEvent();
					}
					break;

				case TimerName::OLED_SCROLLING_AND_BLINKING:
					if (display->haveOLED()) {
						deluge::hid::display::OLED::scrollingAndBlinkingTimerEvent();
					}
					break;

				case TimerName::SCREENSAVER:
					if (display->haveOLED()) {
						deluge::hid::display::Screensaver::timerEvent();
					}
					break;

				case TimerName::SYSEX_DISPLAY:
					HIDSysex::sendDisplayIfChanged();
					break;

				// explicit fallthrough cases
				case TimerName::METER_INDICATOR_BLINK: // really nothing for this?
				case TimerName::NUM_TIMERS:;
				}
			}
		}
	}

	workOutNextEventTime();
}

void UITimerManager::setTimer(TimerName which, int32_t ms) {
	setTimerSamples(which, ms * 44);
}

void UITimerManager::setTimerSamples(TimerName which, int32_t samples) {
	state_.set(which, AudioEngine::audioSampleTimer, samples);
}

void UITimerManager::setTimerByOtherTimer(TimerName which, TimerName fromTimer) {
	state_.follow(which, fromTimer, AudioEngine::audioSampleTimer);
}

void UITimerManager::unsetTimer(TimerName which) {
	state_.unset(which, AudioEngine::audioSampleTimer);
}

bool UITimerManager::isTimerSet(TimerName which) {
	return getTimer(which).active;
}

void UITimerManager::workOutNextEventTime() {
	state_.recompute(deluge::gui::ui_session::current(), AudioEngine::audioSampleTimer);
}
