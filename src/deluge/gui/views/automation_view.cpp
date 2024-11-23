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

#include "gui/views/automation_view.h"
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
#include "gui/ui/rename/rename_midi_cc_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation/automation_layout.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
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

deluge::gui::views::AutomationView automationView{};

namespace deluge::gui::views {

AutomationLayout* curentAutomationLayout = nullptr;

AutomationView::AutomationView() {
	currentAutomationLayout = (AutomationLayout*)&automationLayout;
}

// called everytime you open up the automation view
bool AutomationView::opened() {	
	initializeView();

	openedInBackground();

	focusRegained();

	return true;
}

void AutomationView::initializeView() {
	return curentAutomationLayout->initializeView();
}

// Initializes some stuff to begin a new editing session
void AutomationView::focusRegained() {
	return currentAutomationLayout->focusRegained();
}

void AutomationView::openedInBackground() {
	return currentAutomationLayout->openedInBackground();
}

// used for the play cursor
void AutomationView::graphicsRoutine() {
	return currentAutomationLayout->graphicsRoutine();
}

// used to return whether Automation View is in the AUTOMATION_ARRANGER_VIEW UI Type, AUTOMATION_INSTRUMENT_CLIP_VIEW or
// AUTOMATION_AUDIO_CLIP_VIEW UI Type
AutomationSubType AutomationView::getAutomationSubType() {
	return currentAutomationLayout->getAutomationSubType();
}

// rendering
bool AutomationView::possiblyRefreshAutomationEditorGrid(Clip* clip, params::Kind paramKind, int32_t paramID) {
	return currentAutomationLayout->possiblyRefreshAutomationEditorGrid(clip, paramKind, paramID);
}

// called whenever you call uiNeedsRendering(this) somewhere else
// used to render automation overview, automation editor
// used to setup the shortcut blinking
bool AutomationView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	return currentAutomationLayout->renderMainPads(whichRows, image, occupancyMask, drawUndefinedArea);
}

// defers to arranger, audio clip or instrument clip sidebar render functions
// depending on the active clip
bool AutomationView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	return currentAutomationLayout->renderSidebar(whichRows, image, occupancyMask);
}

void AutomationView::renderDisplay(int32_t knobPosLeft, int32_t knobPosRight, bool modEncoderAction) {
	return currentAutomationLayout->renderDisplay(knobPosLeft, knobPosRight, modEncoderAction);
}

// adjust the LED meters and update the display

/*updated function for displaying automation when playback is enabled (called from ui_timer_manager).
Also used internally in the automation instrument clip view for updating the display and led
indicators.*/

void AutomationView::displayAutomation(bool padSelected, bool updateDisplay) {
	return currentAutomationLayout->displayAutomation(padSelected, updateDisplay);
}

// button action

ActionResult AutomationView::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	return currentAutomationLayout->buttonAction(b, on, inCardRoutine);
}

// pad action
// handles shortcut pad action for automation (e.g. when you press shift + pad on the grid)
// everything else is pretty much the same as instrument clip view
ActionResult AutomationView::padAction(int32_t x, int32_t y, int32_t velocity) {
	return currentAutomationLayout->padAction(x, y, velocity);
}

// horizontal encoder actions:
// scroll left / right
// zoom in / out
// adjust clip length
// shift automations left / right
// adjust velocity in note editor
ActionResult AutomationView::horizontalEncoderAction(int32_t offset) {
	return currentAutomationLayout->horizontalEncoderAction(offset);
}

uint32_t AutomationView::getMaxLength() {
	return currentAutomationLayout->getMaxLength();
}

uint32_t AutomationView::getMaxZoom() {
	return currentAutomationLayout->getMaxZoom();
}

// vertical encoder action
// no change compared to instrument clip view version
// not used with Audio Clip Automation View
ActionResult AutomationView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return currentAutomationLayout->verticalEncoderAction(offset, inCardRoutine);
}

// mod encoder action

// used to change the value of a step when you press and hold a pad on the timeline
// used to record live automations in
void AutomationView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	return currentAutomationLayout->modEncoderAction(whichModEncoder, offset);
}

// used to copy paste automation or to delete automation of the current selected parameter
void AutomationView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	return currentAutomationLayout->modEncoderButtonAction(whichModEncoder, on);
}

// select encoder action

// used to change the parameter selection and reset shortcut pad settings so that new pad can be blinked
// once parameter is selected
// used to fine tune the values of non-midi parameters
void AutomationView::selectEncoderAction(int8_t offset) {
	return currentAutomationLayout->selectEncoderAction(offset);
}

// called by melodic_instrument.cpp or kit.cpp
void AutomationView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {
	return currentAutomationLayout->noteRowChanged(clip, noteRow);
}

// called by playback_handler.cpp
void AutomationView::notifyPlaybackBegun() {
	return currentAutomationLayout->notifyPlaybackBegun();
}

int32_t AutomationView::getNavSysId() const {
	return currentAutomationLayout->getNavSysId();
}

bool AutomationView::inAutomationEditor() {
	return currentAutomationLayout->inAutomationEditor();
}

// used to check if we're automating a note row specific param type
// e.g. velocity, probability, poly expression, etc.
bool AutomationView::inNoteEditor() {
	return currentAutomationLayout->inNoteEditor();
}

// created this function to undo any existing interpolation shortcut blinking so that it doesn't get
// rendered in automation view also created it so that you can reset blinking when interpolation is
// turned off or when you enter/exit automation view
void AutomationView::resetInterpolationShortcutBlinking() {
	return currentAutomationLayout->resetInterpolationShortcutBlinking();
}

void AutomationView::blinkInterpolationShortcut() {
	return currentAutomationLayout->blinkInterpolationShortcut();
}

// used to blink waveform shortcut when in pad selection mode
void AutomationView::resetPadSelectionShortcutBlinking() {
	return currentAutomationLayout->resetPadSelectionShortcutBlinking();
}

void AutomationView::blinkPadSelectionShortcut() {
	return currentAutomationLayout->blinkPadSelectionShortcut();
}

} // namespace deluge::gui::views

