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

class AutomationNoteEditorView final : public AutomationView {
public:
	AutomationNoteEditorView();

	// used to identify the UI as a clip UI or not.
	ClipMinder* toClipMinder() { return this; }

private:
	// edit pad action
	bool toggleVelocityPadSelectionMode(SquareInfo& squareInfo);
	void noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                       int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	void velocityPadSelectionAction(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip, int32_t x,
	                                int32_t y, int32_t velocity, SquareInfo& squareInfo);
	void velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                           int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	int32_t getVelocityFromY(int32_t y);
	int32_t getYFromVelocity(int32_t velocity);
	void addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity);
	void adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t velocity,
	                        int32_t newVelocity, uint8_t squareType);
	void setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t newVelocity);
	void setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
	                     SquareInfo rowSquareInfo[kDisplayWidth], int32_t velocityIncrement);
	void recordNoteEditPadAction(int32_t x, int32_t velocity);

	// Automation View Render Functions
	void renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth, int32_t xScroll,
	                      uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
	                      SquareInfo& squareInfo);
	void renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t xScroll,
	                      int32_t xZoom, SquareInfo& squareInfo);
	void renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t yDisplay,
	                      uint8_t squareType, int32_t value);
};

extern AutomationNoteEditorView automationNoteEditorView;
