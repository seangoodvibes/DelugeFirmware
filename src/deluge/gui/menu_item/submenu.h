/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/session_menu_entries.h"
#include "gui/ui/sound_editor.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class Submenu : public MenuItem {
public:
	enum RenderingStyle { VERTICAL, HORIZONTAL };

	Submenu(l10n::String newName, std::initializer_list<MenuItem*> newItems)
	    : MenuItem(newName), entries_(std::span(newItems.begin(), newItems.size())) {}
	Submenu(l10n::String newName, std::span<MenuItem*> newItems) : MenuItem(newName), entries_(newItems) {}
	Submenu(l10n::String newName, l10n::String title, std::initializer_list<MenuItem*> newItems)
	    : MenuItem(newName, title), entries_(std::span(newItems.begin(), newItems.size())) {}
	Submenu(l10n::String newName, l10n::String title, std::span<MenuItem*> newItems)
	    : MenuItem(newName, title), entries_(newItems) {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void updateDisplay();
	void selectEncoderAction(int32_t offset) override;
	MenuItem* selectButtonPress() final;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	void readValueAgain() final { updateDisplay(); }
	void refresh_shared_value() override;
	void unlearnAction() final;
	bool usesAffectEntire() override;
	bool allowsLearnMode() final;
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final;
	void learnProgramChange(MIDICable& cable, int32_t channel, int32_t programNumber) override;
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) final;
	virtual RenderingStyle renderingStyle() const { return RenderingStyle::VERTICAL; };
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void drawPixelsForOled() override;
	void drawSubmenuItemsForOled(std::span<MenuItem*> options, const int32_t selectedOption);
	/// @brief 	Indicates if the menu-like object should wrap-around. Destined to be virtualized.
	///         At the moment implements the legacy behaviour of wrapping on 7seg but not on OLED.
	bool wrapAround();
	bool isSubmenu() override { return true; }
	virtual bool focusChild(const MenuItem* child);
	void updatePadLights() override;
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override;
	deluge::modulation::params::Kind getParamKind() override;
	uint32_t getParamIndex() override;
	[[nodiscard]] int32_t getOccupiedSlots() const override { return 2; };
	[[nodiscard]] bool showNotification() const override { return false; }

protected:
	using Items = deluge::vector<MenuItem*>;
	using ItemIterator = Items::iterator;
	Items& items_for_session() { return entries_.active().items; }
	const Items& items_for_session() const { return entries_.active().items; }
	ItemIterator& current_item_iterator() { return entries_.active().current; }
	const ItemIterator& current_item_iterator() const { return entries_.active().current; }
	bool& initial_selection_pending() { return entries_.active().initial_selection_pending; }
	uint32_t initial_index_ = 0;

private:
	SessionMenuEntries<Items> entries_;
	bool shouldForwardButtons();
};

} // namespace deluge::gui::menu_item
