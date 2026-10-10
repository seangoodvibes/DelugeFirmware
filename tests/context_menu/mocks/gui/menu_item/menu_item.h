#pragma once
#include "context_menu_environment.h"
namespace deluge::gui::menu_item {
struct MenuItem {
	virtual ~MenuItem() = default;
	virtual MenuItem* selectButtonPress() { return nullptr; }
	virtual bool shouldEnterSubmenu() { return false; }
};
inline MenuItem no_navigation;
inline auto* NO_NAVIGATION = &no_navigation;
} // namespace deluge::gui::menu_item
