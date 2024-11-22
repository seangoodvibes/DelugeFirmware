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
#include "gui/views/automation/layout.h"

AutomationView automationView{};
AutomationLayout automationLayout{};

AutomationLayout* currentAutomationLayout = nullptr;

AutomationView::AutomationView() {
	currentAutomationLayout = (AutomationLayout*)&automationLayout;
	onArrangerView = false;
	onMenuView = false;
	automationParamType = AutomationParamType::PER_SOUND;
}

// called everytime you open up the automation view
bool AutomationView::opened() {
	initializeView();

	openedInBackground();

	focusRegained();

	return true;
}

void AutomationView::initializeView() {
	return currentAutomationLayout->initializeView();
}

void AutomationView::openedInBackground() {
	return currentAutomationLayout->openedInBackground();
}

// Initializes some stuff to begin a new editing session
void AutomationView::focusRegained() {
	return currentAutomationLayout->focusRegained();
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
bool AutomationView::possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind,
                                                         int32_t paramID) {
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

ActionResult AutomationView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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

// used with Select Encoder action to get the X, Y grid shortcut coordinates of the parameter selected
void AutomationView::getLastSelectedParamShortcut(Clip* clip) {
	return currentAutomationLayout->getLastSelectedParamShortcut(clip);
}

void AutomationView::getLastSelectedParamArrayPosition(Clip* clip) {
	return currentAutomationLayout->getLastSelectedParamArrayPosition(clip);
}

bool AutomationView::isMultiPadPressSelected() {
	return currentAutomationLayout->isMultiPadPressSelected();
}

// called by melodic_instrument.cpp or kit.cpp
void AutomationView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {
	return currentAutomationLayout->noteRowChanged(clip, noteRow);
}

// resets the Parameter Selection which sends you back to the Automation Overview screen
// these values are saved on a clip basis
void AutomationView::initParameterSelection(bool updateDisplay) {
	return currentAutomationLayout->initParameterSelection(updateDisplay);
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

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationView::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
                                                      int32_t knobPosRight) {
	return currentAutomationLayout->setAutomationKnobIndicatorLevels(modelStack, knobPosLeft, knobPosRight);
}

bool AutomationView::interpolationBefore() {
	return currentAutomationLayout->interpolationBefore();
}

bool AutomationView::interpolationAfter() {
	return currentAutomationLayout->interpolationAfter();
}

void AutomationView::resetShortcutBlinking() {
	return currentAutomationLayout->resetShortcutBlinking();
}
