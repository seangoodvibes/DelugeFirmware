/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "param.h"
#include "dsp/dx/dx7note.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include <algorithm>
#include <cstring>

namespace deluge::gui::menu_item {

DxParam dxParam{l10n::String::STRING_FOR_DX_PARAM};

void DxParam::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

#define MAX_PARAM_IDX 143

int DxParam::getValue() {
	if (panel_state().param >= 0 && panel_state().param <= MAX_PARAM_IDX) {
		return panel_state().patch->params[panel_state().param];
	}
	else if (panel_state().param == -1) {
		return panel_state().patch->random_detune;
	}
	else {
		return 0;
	}
}

void DxParam::setValue(int val) {
	if (panel_state().param >= 0 && panel_state().param <= MAX_PARAM_IDX) {
		panel_state().patch->params[panel_state().param] = val;
	}
	else if (panel_state().param == -1) {
		panel_state().patch->random_detune = val;
	}

	// NOTE: we don't use currentSource as currently only OSC1 can be DX7
	sound_editor_for_session().currentSound->sources[0].dxPatchChanged = true;
}

void DxParam::readValueAgain() {
	panel_state().patch = sound_editor_for_session().currentSound->sources[0].ensureDxPatch();
	panel_state().displayValue = getValue();
	panel_state().flash_row = 1;

	int x = -1, y = -1;

	panel_state().upper_limit = 99;
	int op = panel_state().param / 21;
	int idx = panel_state().param % 21;
	if (0 <= panel_state().param && panel_state().param < 6 * 21) {
		if (idx == 11 || idx == 12) {
			panel_state().upper_limit = 3;
		}
		else if (idx == 13 || idx == 15) {
			panel_state().upper_limit = 7;
		}
		else if (idx == 14) {
			panel_state().upper_limit = 3;
		}
		else if (idx == 20) {
			panel_state().upper_limit = 14;
		}

		y = 7 - op;
		if (idx < 8) {
			x = idx;
		}
		else if (idx >= 16) {
			x = (idx - 16) + 8;
		}
		else if (idx == 16) {
			x = 13;
		}
		panel_state().flash_row = 7 - op;
	}
	else if (panel_state().param == 6 * 21 + 8) {
		panel_state().upper_limit = 31;
	}
	else if (panel_state().param == 6 * 21 + 9) {
		panel_state().upper_limit = 7;
	}
	else if (panel_state().param == 6 * 21 + 10) {
		panel_state().upper_limit = 1;
	}

	panel_state().blink_next = false;

	if (panel_state().param >= 6 * 21 && panel_state().param < 6 * 21 + 11) {
		y = 1;
		x = panel_state().param - 6 * 21;
	}
	else if (panel_state().param >= 6 * 21 + 11 && panel_state().param < 6 * 21 + 11 + 7) {
		y = 0;
		x = panel_state().param - 6 * 21 - 11;
	}
	else if (panel_state().param == -1) {
		y = 0;
		x = 7;
	}

	drawValue();
	if (x >= 0 && y >= 0) {
		// TODO: different color!
		sound_editor_for_session().setupShortcutBlink(x, y, 1);
		sound_editor_for_session().blinkShortcut();
	}
	else {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}

	blinkSideColumn();
}

bool DxParam::hasSideColumn() {
	// TODO: check if dx column is really active
	return getRootUI() == &keyboard_screen_for_session();
}

void DxParam::blinkSideColumn() {
	if (hasSideColumn()) {
		uiNeedsRendering(&keyboard_screen_for_session(), 0, 0xFFFFFFFF);
		panel_state().blink_next = !panel_state().blink_next;
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, panel_state().blink_next ? 100 : 300);
	}
	else {
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	}
}
ActionResult DxParam::timerCallback() {
	blinkSideColumn();
	return ActionResult::DEALT_WITH;
}

void DxParam::selectEncoderAction(int32_t offset) {
	int value = getValue();

	int newval = value + offset;
	if (newval > panel_state().upper_limit) {
		newval = panel_state().upper_limit;
	}
	else if (newval < 0) {
		newval = 0;
	}

	setValue(newval);
	panel_state().displayValue = newval;

	drawValue();
}

void DxParam::horizontalEncoderAction(int32_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		if (panel_state().param < 0 || panel_state().param > 6 * 21 || !panel_state().patch)
			return; // TODO: remember last OP param?
		int cur_op = panel_state().param / 21;
		int next_op = cur_op;
		do {
			next_op += offset;
			if (next_op >= 6) {
				next_op = 0;
			}
			else if (next_op < 0) {
				next_op = 5;
			}
		} while (!panel_state().patch->opSwitch(next_op) && next_op != cur_op);

		panel_state().param = (panel_state().param % 21) + 21 * next_op;
	}
	else {
		panel_state().param = panel_state().param + offset;
		if (panel_state().param < -1) {
			panel_state().param = 143;
		}
		else if (panel_state().param >= 143) {
			panel_state().param = -1;
		}
	}

	readValueAgain();

	if (display->have7SEG()) {
		flashParamName();
	}
}

bool DxParam::potentialShortcutPadAction(int32_t x, int32_t y, bool on) {
	int found_param = -2;
	if (y > 1 && x <= 13) {
		int op = 8 - y - 1; // op 0-5
		int idx;
		if (x < 8) {
			idx = x;
		}
		else if (x < 13) {
			idx = (x - 8) + 16;
		}
		else if (x == 13) {
			idx = 15;
		}
		found_param = 21 * op + idx;
	}
	else if (y == 1) {
		if (x < 11) {
			found_param = 6 * 21 + x;
		}
	}
	else { // y == 0
		if (x < 7) {
			found_param = 6 * 21 + 11 + x;
		}
		else if (x == 7) {
			found_param = -1;
		}
	}

	if (found_param >= -1) {
		panel_state().param = found_param;
		readValueAgain();
		if (display->have7SEG()) {
			flashParamName();
		}
	}

	// even if we didn't find param, we want to return true so that we ignore other non-dx shortcut presses
	// while in the dx editor, otherwise you get taken out of the dx editor into the sound editor and it's confusing
	return true;
}

const char* desc_op_long[] = {"env rate1",   "env rate2",  "env rate3",  "env rate4",     "env level1",  "env level2",
                              "env level3",  "env level4", "breakpoint", "left depth",    "right depth", "left curve",
                              "right curve", "rate scale", "ampmod",     "velocity sens", "level",       "mode",
                              "coarse",      "fine",       "detune"};
const char* desc_op_short[] = {"rat1",   "rat2",      "rat3",     "rat4",     "lvl1",     "lvl2",     "lvl3",
                               "lvl4",   "brkp",      "le depth", "ri depth", "le curve", "ri curve", "rate scale",
                               "ampmod", "velo sens", "levl",     "mode",     "coar",     "fine",     "detune"};

const char* desc_global_long[]{"DX7 pitch R1", "DX7 pitch R2",     "DX7 pitch R3",  "DX7 pitch R4",  "DX7 pitch l1",
                               "DX7 pitch l2", "DX7 pitch l3",     "DX7 pitch l4",  "DX7 algorithm", "DX7 feedback",
                               "DX7 osc Sync", "DX7 LFO rate",     "DX7 LFO delay", "DX7 LFO pitch", "DX7 LFO amp",
                               "DX7 LFO sync", "DX7 LFO waveform", "DX7 pitch sens"};

const char* desc_global_short[]{"piR1",       "piR2",      "piR3",    "piR4",     "pil1",     "pil2",
                                "pil3",       "pil4",      "algo",    "fdbk",     "oscSync",  "LFO rate",
                                " LFO delay", "LFO pitch", "LFO amp", "LFO sync", "LFO wave", "pitch sens"};

const char* curves[]{"lin-", "exp-", "exp+", "lin+", "????"};
const char* shapes_long[]{"tri", "saw down", "saw up", "square", "sin", "s-hold"};
const char* shapes_short[]{"tri", "sawd", "sawu", "sqre", "sin", "shld"};

[[nodiscard]] std::string_view DxParam::getTitle() const {
	char* buffer = title_buffers_.active().data();

	if (panel_state().param < 0) {
		return "random detune";
	}

	int op = panel_state().param / 21;
	int idx = panel_state().param % 21;
	if (panel_state().param < 6 * 21) {

		strcpy(buffer, "op0 ");
		buffer[2] = '6' - op;

		const char* desc;
		if (idx < 8) {
			desc = "envelope";
		}
		else if (idx < 13) {
			desc = "scaling";
		}
		else if (idx < 16) {
			desc = "params";
		}
		else {
			desc = "tune/level";
		}

		strcpy(buffer + 4, desc);

		return buffer;
	}
	else if (panel_state().param < 6 * 21 + 8) {
		return "dx7 pitch env";
	}
	else if (panel_state().param >= 137 && panel_state().param < 144) {
		return "dx7 LFO";
	}
	else if (panel_state().param < 6 * 21 + 18) {
		return desc_global_long[panel_state().param - 6 * 21];
	}

	return "DX7 PARAM";
}

void DxParam::flashParamName() {
	if (0 <= panel_state().param && panel_state().param < 6 * 21) {
		char buf[12] = {0};
		int op = panel_state().param / 21;
		int idx = panel_state().param % 21;
		buf[0] = 'o';
		buf[1] = '6' - op;
		if (idx < 4) {
			buf[2] = 'r';
			buf[3] = '1' + idx;
		}
		else if (idx < 8) {
			buf[2] = 'l';
			buf[3] = '1' + (idx - 4);
		}
		else {
			if (hasSideColumn()) {
				strcpy(buf, desc_op_short[idx]);
			}
			else {
				buf[2] = ' ';
				strcpy(buf + 3, desc_op_short[idx]);
			}
		}
		display->setScrollingText(buf, 0, 600, 1);
	}
	else if (panel_state().param >= 6 * 21 && panel_state().param < 6 * 21 + 18) {
		display->setScrollingText(desc_global_short[panel_state().param - 6 * 21], 0, 600, 1);
	}
	else {
		display->setScrollingText(getTitle().begin(), 0, 600, 1);
	}
}

using deluge::hid::display::OLED;
static void show(const char* text, int r, int c, bool inv = false) {
	int ybel = 7 + (2 + r) * (kTextSizeYUpdated + 2);
	int xpos = 5 + c * kTextSpacingX;
	OLED::main_for_session().drawString(text, xpos, ybel, kTextSpacingX, kTextSizeYUpdated);
	if (inv) {
		int width = strlen(text);
		OLED::main_for_session().invertArea(xpos - 1, kTextSpacingX * width + 1, ybel - 1, ybel + kTextSizeYUpdated);
	}
}

static void show(int val, int r, int c, bool inv = false) {
	char buffer[12];
	intToString(val, buffer, 2);
	show(buffer, r, c, inv);
}

static void renderEnvelope(uint8_t* params, int op, int idx) {
	show("rate", 0, 0, false);
	show("levl", 1, 0, false);
	for (int i = 0; i < 4; i++) {
		int val = params[op * 21 + i];
		show(val, 0, 5 + i * 3, (i == idx));
		val = params[op * 21 + 4 + i];
		show(val, 1, 5 + i * 3, (i + 4 == idx));
	}
}

static void renderScaling(uint8_t* params, int op, int idx) {
	char buffer[12];

	for (int i = 0; i < 2; i++) {
		int val = params[op * 21 + 9 + i];
		show(val, 0, 1 + i * 11, (9 + i == idx));

		val = params[op * 21 + 11 + i];
		show(curves[std::min(val, 4)], 1, 1 + i * 11, (11 + i == idx));
	}

	int ybelmid = 7 + 2 * (kTextSizeYUpdated + 2) + ((kTextSizeYUpdated + 1) >> 1);
	int val = params[op * 21 + 8];
	intToString(val, buffer, 2);
	int xpos = 14 + 6 * kTextSpacingX;
	OLED::main_for_session().drawString(buffer, xpos, ybelmid, kTextSpacingX, kTextSizeYUpdated);
	if (8 == idx) {
		OLED::main_for_session().invertArea(xpos - 1, kTextSpacingX * 2 + 1, ybelmid - 1, ybelmid + kTextSizeYUpdated);
	}

	int noteCode = val + 17;
	ui::keyboard::KeyboardState& state = getCurrentInstrumentClip()->keyboard_state_for_session();
	if (getRootUI() == &keyboard_screen_for_session()
	    && state.currentLayout == KeyboardLayoutType::KeyboardLayoutTypeIsomorphic) {
		int notedisp = noteCode - state.isomorphic.scrollOffset;
		int x = notedisp;
		int y = 0;
		while (x > 16 && y < 7) {
			x -= state.isomorphic.rowInterval;
			y += 1;
		}
		if (x > 10 && y < 7 && state.isomorphic.rowInterval <= 7) {
			x -= state.isomorphic.rowInterval;
			y += 1;
		}

		if (x >= 0 && x < 16) {
			// TODO: different color than the main shortcut blink? or at least different frequency
			sound_editor_for_session().setupShortcutBlink(x, y, 2);
			sound_editor_for_session().blinkShortcut();
		}
		else {
			uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		}
	}
}

static void renderSensParams(uint8_t* params, int op, int idx) {
	show("rate scale", 0, 1);
	show(params[op * 21 + 13], 0, 12, idx == 13);

	show("ampmod", 1, 0);
	show(params[op * 21 + 14], 1, 7, idx == 14);

	show("velo", 1, 11);
	show(params[op * 21 + 15], 1, 16, idx == 15);
}

static void renderTuning(uint8_t* params, int op, int param) {
	int mode = params[op * 21 + 17];
	const char* text = mode ? "fixed" : "ratio";
	show(text, 0, 1, (17 == param));

	for (int i = 0; i < 3; i++) {
		int val = params[op * 21 + i + 18];
		if (i + 18 == 20)
			val -= 7;
		show(val, 0, 7 + i * 3, (i + 18 == param));
	}

	show("level", 1, 1);

	int val = params[op * 21 + 16];
	show(val, 1, 7, (16 == param));
}

static void renderLFO(uint8_t* params, int param) {
	char buffer[12];
	int ybel = 5 + 2 * (kTextSizeYUpdated + 2) + 2;
	int ybel2 = ybel + (kTextSizeYUpdated + 2);

	for (int i = 0; i < 2; i++) {
		int val = params[137 + i];
		show(val, 0, 1 + i * 3, (137 + i == param));
	}

	for (int i = 0; i < 2; i++) {
		int val = params[139 + i];
		intToString(val, buffer, 2);
		int xpre = 10 + (i * 9) * kTextSpacingX;
		int xpos = xpre + 6 * kTextSpacingX;
		const char* text = (i == 0) ? "pitch" : "  amp";
		show(text, 1, 1 + i * 9);
		show(val, 1, 1 + i * 9 + 6, (i + 139 == param));
	}

	const char* sync = (params[141] ? "sync" : "    ");
	show(sync, 0, 7, (141 == param));
	int shap = std::min((int)params[142], 5);
	show(shapes_long[shap], 0, 12, (142 == param));

	int val = params[143];
	intToString(val, buffer, 1);
	show(buffer, 1, 10, (143 == param));
}

static void renderAlgorithm(uint8_t* params) {

	char buffer[12];
	intToString(params[134] + 1, buffer, 2);
	OLED::main_for_session().drawString(buffer, 116, 7, kTextSpacingX, kTextSizeYUpdated);

	FmAlgorithm a = FmCore::algorithms[params[134]];
	for (int i = 0; i < 6; i++) {
		int f = a.ops[5 - i]; // compensate for inverted op order
		char buffer[12];
		int inbus = (f >> 4) & 3;
		int outbus = f & 3;
		const char ib[] = {'.', 'x', 'y', 'z'};
		const char ob[] = {'c', 'x', 'y', 'q'};
		buffer[0] = '1' + i;
		buffer[1] = ':';
		buffer[2] = ib[inbus];
		buffer[3] = (f & OUT_BUS_ADD) ? '+' : '>';
		buffer[4] = ob[outbus];
		buffer[5] = (f & (FB_IN | FB_OUT)) ? 'f' : ' ';
		buffer[6] = ' ';
		buffer[7] = 0;

		int r = i / 3, c = i % 3;
		show(buffer, r, c * 7);
	}
}

void DxParam::drawPixelsForOled() {
	const int y0 = 20;
	char buffer[12];

	if (panel_state().param < 0 || panel_state().param == 135 || panel_state().param == 136) {
		int val = getValue();
		intToString(val, buffer, panel_state().param < 0 ? 2 : 1);
		OLED::main_for_session().drawString(buffer, 50, y0, kTextHugeSpacingX, kTextHugeSizeY);
		return;
	}

	int op = panel_state().param / 21;
	int idx = panel_state().param % 21;
	if (panel_state().param < (6 * 21 + 8) && idx < 8) {
		renderEnvelope(panel_state().patch->params, op, idx); // op== 6 for pitch envelope
	}
	else if (panel_state().param < 6 * 21 && idx < 13) {
		renderScaling(panel_state().patch->params, op, idx);
	}
	else if (panel_state().param < 6 * 21 && idx < 16) {
		renderSensParams(panel_state().patch->params, op, idx);
	}
	else if (panel_state().param < 6 * 21 && idx < 21) {
		renderTuning(panel_state().patch->params, op, idx);
	}
	else if (panel_state().param == 134) {
		renderAlgorithm(panel_state().patch->params);
	}
	else if (panel_state().param >= 137 && panel_state().param < 144) {
		renderLFO(panel_state().patch->params, panel_state().param);
	}
}

void DxParam::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		int op = panel_state().param / 21;
		int idx = panel_state().param % 21;
		int val = getValue();
		const char* text = NULL;
		if (panel_state().param < 6 * 21 && (idx == 17)) {
			text = val ? "fixd" : "rati";
		}
		else if (panel_state().param < 6 * 21 && (idx == 11 || idx == 12)) {
			text = curves[std::min(val, 4)];
		}
		else if (panel_state().param == 142) {
			int shap = std::min(val, 5);
			text = shapes_short[shap];
		}
		else if (panel_state().param == 6 * 21 + 8) {
			val += 1; // algorithms start at one
		}
		else if (panel_state().param < 6 * 21 && (idx == 20)) {
			val -= 7; // detuning -7 - 7
		}
		if (text) {
			display->setText(text);
		}
		else {
			display->setTextAsNumber(val);
		}
	}
}

void DxParam::openForOpOrGlobal(int op) {
	bool was_focused = true; // was already in DxParam
	if (!isUIOpen(&sound_editor_for_session()) || sound_editor_for_session().getCurrentMenuItem() != this) {
		bool success = sound_editor_for_session().setup(getCurrentClip(), this, 0);
		if (!success) {
			return;
		}
		sound_editor_for_session().enterOrUpdateSoundEditor(true);
		was_focused = false;
	}

	int new_param = op < 6 ? op * 21 + 16 : 134;
	bool flash = true;
	if (was_focused) {
		if (panel_state().param == new_param) {
			new_param = op < 6 ? op * 21 + 18 : 135;
		}
		else if (0 <= panel_state().param && panel_state().param < 6 * 21 + 8) {
			// tricky: we allow to go from op env to pitch env and back
			int param_for_op = op * 21 + (panel_state().param % 21);
			if (panel_state().param != param_for_op && param_for_op < 6 * 21 + 8) {
				new_param = param_for_op;
				flash = false;
			}
		}
	}
	panel_state().param = new_param;
	readValueAgain();
	if (flash && display->have7SEG()) {
		flashParamName();
	}
}

} // namespace deluge::gui::menu_item
