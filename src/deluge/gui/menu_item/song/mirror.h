#pragma once

#include "gui/menu_item/menu_item.h"
#include "hid/mirror.h"

namespace deluge::gui::menu_item::song {
class Mirror final : public MenuItem {
public:
	using MenuItem::MenuItem;
	MenuItem* selectButtonPress() override {
		hid::mirror::start();
		return NO_NAVIGATION;
	}
	bool shouldEnterSubmenu() override { return false; }
};
} // namespace deluge::gui::menu_item::song
