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

#include "gui/menu_item/shared_value_cache.h"
#include "gui/ui/ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "hid/display/display.h"
#include "menu_item.h"
#include "util/misc.h"

#include <hid/buttons.h>

namespace deluge::gui::menu_item {
template <typename T = int32_t>
class Value : public MenuItem {
public:
	using MenuItem::MenuItem;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;
	void readValueAgain() override;
	void refresh_shared_value() override {
		if (value_.needs_reload(model_value_revision()))
			readValueAgain();
	}
	bool selectEncoderActionEditsInstrument() final { return true; }

	void setValue(T value) { value_.set(value, model_value_revision()); }

	template <util::enumeration E>
	void setValue(E value) {
		value_.set(util::to_underlying(value), model_value_revision());
	}

	T getValue() {
		return value_.get([this] { readCurrentValue(); }, model_value_revision());
	}

	template <util::enumeration E>
	E getValue() {
		return static_cast<E>(getValue());
	}

protected:
	virtual void writeCurrentValue() {}
	// Only menus backed by live shared parameters opt into model revisions.
	virtual uint64_t model_value_revision() const { return 0; }

	void value_committed() {
		value_.committed();
		// Schedule refresh only; never draw the other panel into shared hardware.
		const auto peer =
		    ui_session::current() == ui_session::Id::Local ? ui_session::Id::Remote : ui_session::Id::Local;
		ui_session::navigation.for_owner(peer).shared_model_refresh.request();
	}

	// 7SEG ONLY
	virtual void drawValue() = 0;

private:
	SharedValueCache<T> value_;
};

template <typename T>
void Value<T>::beginSession(MenuItem* navigatedBackwardFrom) {
	if (display->haveOLED()) {
		readCurrentValue();
	}
	else {
		readValueAgain();
	}
}

template <typename T>
void Value<T>::selectEncoderAction(int32_t offset) {
	if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
		Buttons::state().selectButtonPressUsedUp = true;
	}

	writeCurrentValue();
	value_committed();

	// For MenuItems referring to an AutoParam (so UnpatchedParam and PatchedParam), ideally we wouldn't want to render
	// the display here, because that'll happen soon anyway due to a setting of TIMER_DISPLAY_AUTOMATION.
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue(); // Probably not necessary either...
	}
}

template <typename T>
void Value<T>::readValueAgain() {
	readCurrentValue();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

} // namespace deluge::gui::menu_item
