/*
 * Copyright © 2014-2024 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "gui/views/clip_view.h"
#include "hid/button.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/note/note_row.h"
#include "modulation/automation/copied_param_automation.h"

class Action;
class CopiedNoteRow;
class Drum;
class Editor;
class AudioClip;
class Instrument;
class InstrumentClip;
class MidiInstrument;
class ModControllable;
class ModelStackWithAutoParam;
class ModelStackWithNoteRow;
class ModelStackWithThreeMainThings;
class ModelStackWithTimelineCounter;
class Note;
class NoteRow;
class ParamCollection;
class ParamManagerForTimeline;
class ParamNode;
class PatchCableSet;
class Sound;
class SoundDrum;

class AutomationEditor final : public AutomationView {
public:
	AutomationEditor();
	
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	void renderMainPads(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
	                              OutputType outputType, RGB image[][kDisplayWidth + kSideBarWidth],
	                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                              bool isMIDICVDrum);	
	void renderDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Output* output,
	                                         OutputType outputType);
	void renderDisplay7SEG(Output* output, OutputType outputType);	
};

extern AutomationEditor automationEditor;
