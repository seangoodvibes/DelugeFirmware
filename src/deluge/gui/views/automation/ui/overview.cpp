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

#include "gui/views/automation/ui/overview.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/midi/midi_transpose.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include "util/comparison.h"
#include "util/functions.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

namespace params = deluge::modulation::params;
using deluge::modulation::params::kNoParamID;
using deluge::modulation::params::ParamType;
using deluge::modulation::params::patchedParamShortcuts;
using deluge::modulation::params::unpatchedGlobalParamShortcuts;
using deluge::modulation::params::unpatchedNonGlobalParamShortcuts;

using namespace deluge::gui;

constexpr uint8_t kVelocityShortcutX = 15;
constexpr uint8_t kVelocityShortcutY = 1;

AutomationOverview automationOverview{};

AutomationOverview::AutomationOverview() {
}

// renders automation overview
void AutomationOverview::renderMainPads(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                  ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
                                                  Clip* clip, OutputType outputType,
                                                  RGB image[][kDisplayWidth + kSideBarWidth],
                                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                  int32_t xDisplay, bool isMIDICVDrum) {
	bool singleSoundDrum = (outputType == OutputType::KIT && !getAffectEntire()) && !isMIDICVDrum;
	bool affectEntireKit = (outputType == OutputType::KIT && getAffectEntire());
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		RGB& pixel = image[yDisplay][xDisplay];

		if (!isMIDICVDrum) {
			ModelStackWithAutoParam* modelStackWithParam = nullptr;

			if (!onArrangerView && (outputType == OutputType::SYNTH || singleSoundDrum)) {
				if (patchedParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					modelStackWithParam =
					    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
					                                  patchedParamShortcuts[xDisplay][yDisplay], params::Kind::PATCHED);
				}

				else if (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] != kNoParamID) {
					// don't make portamento available for automation in kit rows
					if ((outputType == OutputType::KIT)
					    && (unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay] == params::UNPATCHED_PORTAMENTO)) {
						pixel = colours::black; // erase pad
						continue;
					}

					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, unpatchedNonGlobalParamShortcuts[xDisplay][yDisplay],
					    params::Kind::UNPATCHED_SOUND);
				}

				else if (params::isPatchCableShortcut(xDisplay, yDisplay)) {
					ParamDescriptor paramDescriptor;
					params::getPatchCableFromShortcut(xDisplay, yDisplay, &paramDescriptor);

					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, paramDescriptor.data, params::Kind::PATCH_CABLE);
				}
				// expression params, so sounds or midi/cv, or a single drum
				else if (params::expressionParamFromShortcut(xDisplay, yDisplay) != kNoParamID) {
					uint32_t paramID = params::expressionParamFromShortcut(xDisplay, yDisplay);
					if (paramID != kNoParamID) {
						modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip,
						                                                    paramID, params::Kind::EXPRESSION);
					}
				}
			}

			else if ((onArrangerView || (outputType == OutputType::AUDIO) || affectEntireKit)) {
				int32_t paramID = unpatchedGlobalParamShortcuts[xDisplay][yDisplay];
				if (paramID != kNoParamID) {
					if (onArrangerView) {
						// don't make pitch adjust or sidechain available for automation in arranger
						if ((paramID == params::UNPATCHED_PITCH_ADJUST)
						    || (paramID == params::UNPATCHED_SIDECHAIN_SHAPE)
						    || (paramID == params::UNPATCHED_SIDECHAIN_VOLUME)) {
							pixel = colours::black; // erase pad
							continue;
						}
						modelStackWithParam =
						    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, paramID);
					}
					else {
						modelStackWithParam =
						    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, paramID);
					}
				}
			}

			else if (outputType == OutputType::MIDI_OUT) {
				if (midiCCShortcutsForAutomation[xDisplay][yDisplay] != kNoParamID) {
					modelStackWithParam = getModelStackWithParamForClip(
					    modelStackWithTimelineCounter, clip, midiCCShortcutsForAutomation[xDisplay][yDisplay]);
				}
			}
			else if (outputType == OutputType::CV) {
				uint32_t paramID = params::expressionParamFromShortcut(xDisplay, yDisplay);
				if (paramID != kNoParamID) {
					modelStackWithParam = getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, paramID,
					                                                    params::Kind::EXPRESSION);
				}
			}

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				// highlight pad white if the parameter it represents is currently automated
				if (modelStackWithParam->autoParam->isAutomated()) {
					pixel = {
					    .r = 130,
					    .g = 120,
					    .b = 130,
					};
				}

				else {
					pixel = colours::grey;
				}

				occupancyMask[yDisplay][xDisplay] = 64;
			}
			else {
				pixel = colours::black; // erase pad
			}
		}
		else {
			pixel = colours::black; // erase pad
		}

		if (!onArrangerView && !(outputType == OutputType::KIT && getAffectEntire())
		    && clip->type == ClipType::INSTRUMENT) {
			// highlight velocity pad
			if (xDisplay == kVelocityShortcutX && yDisplay == kVelocityShortcutY) {
				pixel = colours::grey;
				occupancyMask[yDisplay][xDisplay] = 64;
			}
		}
	}
}
void AutomationOverview::renderDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                             Output* output, OutputType outputType) {
	// align string to vertically to the centre of the display
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 24;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 15;
#endif

	// display Automation Overview
	char const* overviewText;
	if (!onArrangerView && (outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
		overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
		deluge::hid::display::OLED::drawPermanentPopupLookingText(overviewText);
	}
	else {
		overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OVERVIEW);
		canvas.drawStringCentred(overviewText, yPos, kTextSpacingX, kTextSpacingY);
	}
}

void AutomationOverview::renderDisplay7SEG(Output* output, OutputType outputType) {
	char const* overviewText;
	if (!onArrangerView && (outputType == OutputType::KIT && !getAffectEntire() && !((Kit*)output)->selectedDrum)) {
		overviewText = l10n::get(l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE);
	}
	else {
		overviewText = l10n::get(l10n::String::STRING_FOR_AUTOMATION);
	}
	display->setScrollingText(overviewText);
}
