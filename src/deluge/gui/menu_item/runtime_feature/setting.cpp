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

#include "setting.h"
#include "gui/menu_item/runtime_feature/setting.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::runtime_feature {

Setting::Setting(RuntimeFeatureSettingType ty) : currentSettingIndex(static_cast<uint32_t>(ty)) {
}

uint64_t Setting::model_value_revision() const {
	// Other settings and loaders can change this value without committing this menu.
	return runtimeFeatureSettings.settings[currentSettingIndex].value;
}

void Setting::readCurrentValue() {
	for (uint32_t idx = 0; idx < RUNTIME_FEATURE_SETTING_MAX_OPTIONS; ++idx) {
		if (runtimeFeatureSettings.settings[currentSettingIndex].options[idx].value
		    == runtimeFeatureSettings.settings[currentSettingIndex].value) {
			this->setValue(idx);
			return;
		}
	}

	this->setValue(0);
}

void Setting::writeCurrentValue() {
	const auto selected_option = getValue();
	runtimeFeatureSettings.settings[currentSettingIndex].value =
	    runtimeFeatureSettings.settings[currentSettingIndex].options[selected_option].value;
	// Associate the committed selection with the new model value, including when
	// another menu later restores the value that preceded this write.
	setValue(selected_option);
}

deluge::vector<std::string_view> Setting::getOptions(OptType optType) {
	(void)optType;
	deluge::vector<std::string_view> options;
	for (const RuntimeFeatureSettingOption& option : runtimeFeatureSettings.settings[currentSettingIndex].options) {
		options.push_back(option.displayName);
	}
	return options;
}

std::string_view Setting::getName() const {
	return deluge::l10n::getView(runtimeFeatureSettings.settings[currentSettingIndex].displayName);
}

std::string_view Setting::getTitle() const {
	return deluge::l10n::getView(runtimeFeatureSettings.settings[currentSettingIndex].displayName);
}

} // namespace deluge::gui::menu_item::runtime_feature
