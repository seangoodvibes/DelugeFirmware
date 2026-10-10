/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "devSysexSetting.h"
#include "gui/l10n/strings.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/d_string.h"
#include "util/functions.h"

namespace deluge::gui::menu_item::runtime_feature {

DevSysexSetting::DevSysexSetting(RuntimeFeatureSettingType ty) : currentSettingIndex(static_cast<uint32_t>(ty)) {
}

uint64_t DevSysexSetting::model_value_revision() const {
	// Reset and settings reloads can replace the code without a menu commit.
	return runtimeFeatureSettings.settings[currentSettingIndex].value;
}

void DevSysexSetting::readCurrentValue() {
	int32_t rawValue = runtimeFeatureSettings.settings[currentSettingIndex].value;
	setValue(rawValue != 0);
	if (rawValue != 0) {
		session_states.active().on_value = rawValue;
	}
	else {
		do {
			session_states.active().on_value = getNoise() & 0x7FFFFFFF;
		} while (session_states.active().on_value == 0);
	}
}

void DevSysexSetting::writeCurrentValue() {
	const auto selected_option = getValue();
	if (selected_option) {
		runtimeFeatureSettings.settings[currentSettingIndex].value = session_states.active().on_value;
	}
	else {
		runtimeFeatureSettings.settings[currentSettingIndex].value = 0;
	}
	// Keep this draft, but associate the cached choice with the committed code.
	setValue(selected_option);
}

deluge::vector<std::string_view> DevSysexSetting::getOptions(OptType optType) {
	(void)optType;
	// Selection can request labels before the value; resolve peer commits first.
	(void)getValue();
	auto& state = session_states.active();
	intToHex(state.on_value, &state.on_label[5]);
	return {
	    l10n::get(l10n::String::STRING_FOR_OFF),
	    state.on_label,
	};
}

std::string_view DevSysexSetting::getName() const {
	return deluge::l10n::getView(runtimeFeatureSettings.settings[currentSettingIndex].displayName);
}

std::string_view DevSysexSetting::getTitle() const {
	return deluge::l10n::getView(runtimeFeatureSettings.settings[currentSettingIndex].displayName);
}

} // namespace deluge::gui::menu_item::runtime_feature
