/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "gui/menu_item/horizontal_menu.h"
#include "util/d_stringbuf.h"
#include <span>

class Clip;
class Output;

namespace deluge::gui::menu_item::voice_usage {

enum class Mode {
	TOTAL,
	SYNTH,
	KIT,
	AUDIO,
};

class ClipList final : public MenuItem {
public:
	ClipList(l10n::String newName, l10n::String newTitle, Mode newMode, const char* newColumnLabel);

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void endSession() override;
	void readCurrentValue() override;
	void drawPixelsForOled() override;
	void drawValue();
	void drawName() override;
	void selectEncoderAction(int32_t offset) override;
	MenuItem* selectButtonPress() override;
	ActionResult timerCallback() override;
	bool isSubmenu() override;
	bool selectEncoderActionIsPermitted() override;
	bool showNotification() const override;
	void getColumnLabel(StringBuf& label) override;
	void getNotificationValue(StringBuf& valueBuf) override;
	void renderInHorizontalMenu(const SlotPosition& slot) override;

private:
	struct Entry {
		Clip* clip;
		OutputType outputType;
		int32_t voiceCount;
		int32_t sortIndex;
	};

	void refreshEntries(Clip* clipToKeepSelected = nullptr);
	void drawOledRows();
	void drawSevenSegmentValue();
	void setSelectedIndex(int32_t newIndex);
	void preserveSelection(Clip* clipToKeepSelected, int32_t previousIndex);
	[[nodiscard]] int32_t getSummaryCount() const;
	[[nodiscard]] int32_t getSelectedVoiceCount() const;
	void formatSelectedDescription(StringBuf& description) const;
	void formatEntryDescription(const Entry& entry, StringBuf& description) const;
	void scheduleTimer();

	Mode mode_;
	const char* columnLabel_;
	deluge::vector<Entry> entries_;
	int32_t selectedIndex_{0};
	int32_t currentValue_{0};
	uint32_t lastSelectionMoveTime_{0};
	bool showingDescriptionOn7Seg_{false};
};

class VoiceUsageMenu final : public HorizontalMenu {
public:
	VoiceUsageMenu(l10n::String newName, l10n::String newTitle, std::span<MenuItem*> newItems, MenuItem* defaultItem);

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void endSession() override;
	void selectEncoderAction(int32_t offset) override;
	ActionResult timerCallback() override;

private:
	void refreshChildValues();
	void scheduleTimer();
};

} // namespace deluge::gui::menu_item::voice_usage
