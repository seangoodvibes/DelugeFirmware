#include "CppUTest/TestHarness.h"
#include "gui/menu_item/runtime_feature/setting.h"
using deluge::gui::menu_item::runtime_feature::Setting;
TEST_GROUP(RuntimeSettingCache){void setup() override{session::detail::active = session::Id::Local;
runtimeFeatureSettings = {};
}
void teardown() override {
	session::detail::active = session::Id::Local;
}
setting_fixture& setting(RuntimeFeatureSettingType type) {
	return runtimeFeatureSettings.settings[static_cast<size_t>(type)];
}
}
;
TEST(RuntimeSettingCache, dependent_setting_change_reloads_both_panel_caches) {
	Setting menu(RuntimeFeatureSettingType::LightShiftLed);
	menu.readCurrentValue();
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
	}
	// Another setting enables this value without committing this menu item.
	setting(RuntimeFeatureSettingType::LightShiftLed).value = 1;
	LONGS_EQUAL(1, menu.getValue());
	{
		session::Scope remote(session::Id::Remote);
		LONGS_EQUAL(1, menu.getValue());
	}
}
TEST(RuntimeSettingCache, distinct_menu_instances_resolve_the_same_live_setting) {
	Setting first(RuntimeFeatureSettingType::LightShiftLed);
	Setting second(RuntimeFeatureSettingType::LightShiftLed);
	first.readCurrentValue();
	second.readCurrentValue();
	first.commit(1);
	LONGS_EQUAL(1, second.getValue());
	second.commit(0);
	LONGS_EQUAL(0, first.getValue());
}
TEST(RuntimeSettingCache, live_setting_value_is_mapped_to_option_index_before_next_edit) {
	auto& model = setting(RuntimeFeatureSettingType::LightShiftLed);
	model.options[0].value = 10;
	model.options[1].value = 40;
	model.value = 10;
	Setting menu(RuntimeFeatureSettingType::LightShiftLed);
	menu.readCurrentValue();
	model.value = 40;
	LONGS_EQUAL(1, menu.getValue());
	menu.commit(0);
	LONGS_EQUAL(10, model.value);
	LONGS_EQUAL(0, menu.getValue());
}
TEST(RuntimeSettingCache, unrelated_setting_change_does_not_discard_pending_selection) {
	Setting menu(RuntimeFeatureSettingType::LightShiftLed);
	menu.readCurrentValue();
	menu.setValue(1);
	setting(RuntimeFeatureSettingType::ShiftIsSticky).value = 1;
	LONGS_EQUAL(1, menu.getValue());
	menu.writeCurrentValue();
	LONGS_EQUAL(1, setting(RuntimeFeatureSettingType::LightShiftLed).value);
}
