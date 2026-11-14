/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "extern.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/note/clipboard_icons.h"
#include "gui/views/note_clipboard.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item::note {
class Clipboard final : public MenuItem {
public:
	using Operation = ClipboardOperation;
	Clipboard(l10n::String name, Operation operation) : MenuItem(name), operation_(operation) {}
	MenuItem* selectButtonPress() override {
		if (sdRoutineLock) {
			return NO_NAVIGATION;
		}
		switch (operation_) {
		case Operation::CUT:
			noteClipboard.copySelection(true);
			break;
		case Operation::COPY:
			noteClipboard.copySelection(false);
			break;
		case Operation::PASTE_FIRMLY:
			noteClipboard.paste(true);
			break;
		case Operation::PASTE_GENTLY:
			noteClipboard.paste(false);
			break;
		case Operation::DELETE:
			noteClipboard.deleteSelection();
			break;
		}
		return NO_NAVIGATION;
	}
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override {
		if (operation_ == Operation::PASTE_FIRMLY || operation_ == Operation::PASTE_GENTLY) {
			noteClipboard.setPasteMode(operation_ == Operation::PASTE_FIRMLY);
		}
		return MenuItem::checkPermissionToBeginSession(modControllable, whichThing, currentRange);
	}
	void getColumnLabel(StringBuf& label) override {
		if (operation_ == Operation::PASTE_FIRMLY || operation_ == Operation::PASTE_GENTLY) {
			label.append(l10n::get(operation_ == Operation::PASTE_FIRMLY ? l10n::String::STRING_FOR_FIRMLY
			                                                             : l10n::String::STRING_FOR_GENTLY));
		}
		else {
			MenuItem::getColumnLabel(label);
		}
	}
	bool shouldEnterSubmenu() override { return false; }
	bool showNotification() const override { return false; }
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		drawClipboardIcon(hid::display::OLED::main, slot.start_x + (slot.width - 18) / 2, slot.start_y + 1, operation_);
	}

private:
	Operation operation_;
};
} // namespace deluge::gui::menu_item::note
