/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/note/selected_note.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include <algorithm>

namespace deluge::gui::menu_item::note {

using namespace deluge::hid::display;

class ExpressionValue final : public SelectedNote {
public:
	using SelectedNote::SelectedNote;

	ExpressionValue(l10n::String newName, int32_t newExpressionDimension)
	    : SelectedNote(newName), expressionDimension(newExpressionDimension) {}

	[[nodiscard]] int32_t getMaxValue() const override {
		return expressionDimension == Expression::X_PITCH_BEND ? 8191 : 127;
	}
	[[nodiscard]] int32_t getMinValue() const override {
		return expressionDimension == Expression::X_PITCH_BEND ? -8192 : 0;
	}
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	void readValueAgain() override {
		readCurrentValue();
		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}

	void readCurrentValue() override {
		int32_t xDisplay = instrumentClipView.lastSelectedNoteXDisplay;
		int32_t yDisplay = instrumentClipView.lastSelectedNoteYDisplay;
		if (xDisplay == kNoSelection || yDisplay == kNoSelection) {
			this->setValue(0);
			return;
		}
		if (!instrumentClipView.gridSquareInfo[yDisplay][xDisplay].isValid) {
			this->setValue(0);
			return;
		}
		if (!instrumentClipView.gridSquareInfo[yDisplay][xDisplay].firstNote) {
			this->setValue(0);
			return;
		}

		int32_t noteRowIndex = 0;
		NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		if (!noteRow) {
			this->setValue(0);
			return;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    modelStack->addNoteRow(getCurrentInstrumentClip()->getNoteRowId(noteRow, noteRowIndex), noteRow);

		ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
		ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
		if (!mpeParams) {
			this->setValue(0);
			return;
		}

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams,
		                                                                                            mpeParamsSummary);
		AutoParam* param = &mpeParams->params[expressionDimension];
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    modelStackWithParamCollection->addAutoParam(expressionDimension, param);
		this->setValue(param->getValuePossiblyAtPos(view.modPos, modelStackWithAutoParam) >> 16);
	}

	void selectEncoderAction(int32_t offset) override {
		int32_t xDisplay = instrumentClipView.lastSelectedNoteXDisplay;
		int32_t yDisplay = instrumentClipView.lastSelectedNoteYDisplay;
		if (xDisplay == kNoSelection || yDisplay == kNoSelection) {
			return;
		}
		if (!instrumentClipView.gridSquareInfo[yDisplay][xDisplay].isValid) {
			return;
		}
		Note* selectedNote = instrumentClipView.gridSquareInfo[yDisplay][xDisplay].firstNote;
		if (!selectedNote) {
			return;
		}

		int32_t currentValue = this->getValue();
		int32_t newValue = std::clamp(currentValue + offset, getMinValue(), getMaxValue());

		int32_t noteRowIndex = 0;
		NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		if (!noteRow) {
			return;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    modelStack->addNoteRow(getCurrentInstrumentClip()->getNoteRowId(noteRow, noteRowIndex), noteRow);
		if (noteRow->recordPolyphonicExpressionEvent(modelStackWithNoteRow, ((int32_t)newValue) << 16,
		                                             expressionDimension, false)) {
			this->setValue(newValue);
		}
		readValueAgain();
	}

	void drawValue() override { display->setText(getDisplayString()); }

	void drawPixelsForOled() override {
		OLED::main.drawStringCentred(getDisplayString(), 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                             kTextHugeSizeY);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		OLED::main.drawStringCentered(getDisplayString(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                              kTextSpacingX, kTextSpacingY, slot.width);
	}

	void getNotificationValue(StringBuf& valueBuf) override { valueBuf.append(getDisplayString()); }

	void writeCurrentValue() override { ; }

private:
	const char* getDisplayString() {
		static char buffer[16];
		int32_t value = this->getValue();
		intToString(value, buffer);
		return buffer;
	}

	int32_t expressionDimension;
};

} // namespace deluge::gui::menu_item::note
