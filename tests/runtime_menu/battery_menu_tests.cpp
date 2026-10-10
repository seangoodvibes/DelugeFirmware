#include "CppUTest/TestHarness.h"
#include "gui/menu_item/battery/level.h"
uint16_t batteryMV = 3000;
using deluge::gui::menu_item::battery::Level;
TEST_GROUP(BatteryMenu) {
	Level menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		batteryMV = 3000;
		battery_display.oled = false;
		battery_text = {};
		battery_redraws = {};
		uiTimerManager = {};
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
	void tick(int count) {
		while (count--)
			menu.timerCallback();
	}
};
TEST(BatteryMenu, remote_entry_preserves_local_measurement_window) {
	menu.beginSession(nullptr);
	tick(3);
	batteryMV = 3010;
	{
		session::Scope remote(session::Id::Remote);
		menu.beginSession(nullptr);
	}
	tick(1);
	CHECK(battery_text.active().find("CHG") != std::string::npos);
	CHECK(battery_text.for_owner(session::Id::Remote).find("CHG") == std::string::npos);
}
TEST(BatteryMenu, two_panel_timers_do_not_accelerate_each_others_sampling) {
	menu.beginSession(nullptr);
	{
		session::Scope remote(session::Id::Remote);
		menu.beginSession(nullptr);
	}
	batteryMV = 3010;
	for (int i = 0; i < 2; ++i) {
		tick(1);
		session::Scope remote(session::Id::Remote);
		tick(1);
	}
	CHECK(battery_text.active().find("CHG") == std::string::npos);
	CHECK(battery_text.for_owner(session::Id::Remote).find("CHG") == std::string::npos);
	tick(2);
	CHECK(battery_text.active().find("CHG") != std::string::npos);
	{
		session::Scope remote(session::Id::Remote);
		tick(2);
		CHECK(battery_text.active().find("CHG") != std::string::npos);
	}
}
TEST(BatteryMenu, reopening_resets_only_owners_previous_charging_result) {
	menu.beginSession(nullptr);
	{
		session::Scope remote(session::Id::Remote);
		menu.beginSession(nullptr);
	}
	batteryMV = 3010;
	tick(4);
	{
		session::Scope remote(session::Id::Remote);
		tick(4);
	}
	menu.beginSession(nullptr);
	CHECK(battery_text.active().find("CHG") == std::string::npos);
	{
		session::Scope remote(session::Id::Remote);
		menu.drawValue();
		CHECK(battery_text.active().find("CHG") != std::string::npos);
	}
}
TEST(BatteryMenu, oled_refresh_and_timer_are_routed_to_the_calling_panel) {
	battery_display.oled = true;
	session::Scope remote(session::Id::Remote);
	menu.beginSession(nullptr);
	LONGS_EQUAL(1, battery_redraws.active());
	tick(1);
	LONGS_EQUAL(2, battery_redraws.active());
	LONGS_EQUAL(0, battery_redraws.for_owner(session::Id::Local));
	CHECK(uiTimerManager.state.get(TimerName::UI_SPECIFIC).active);
	LONGS_EQUAL(500, uiTimerManager.state.get(TimerName::UI_SPECIFIC).triggerTime);
	CHECK_FALSE(
	    uiTimerManager.state.bank(session::Id::Local).timers[static_cast<size_t>(TimerName::UI_SPECIFIC)].active);
	menu.drawPixelsForOled();
	STRCMP_EQUAL("25% (3000mV)", battery_text.active().c_str());
}
TEST(BatteryMenu, voltage_bounds_full_status_and_stable_voltage_are_preserved) {
	menu.beginSession(nullptr);
	batteryMV = 3010;
	tick(4);
	CHECK(battery_text.active().find("CHG") != std::string::npos);
	tick(4);
	CHECK(battery_text.active().find("CHG") == std::string::npos);
	batteryMV = 2500;
	menu.drawValue();
	STRCMP_EQUAL("0% (2500mV)", battery_text.active().c_str());
	batteryMV = 4300;
	menu.drawValue();
	STRCMP_EQUAL("100% (4300mV) FULL", battery_text.active().c_str());
}

TEST(BatteryMenu, reopening_discards_stale_charging_status_until_a_new_measurement) {
	menu.beginSession(nullptr);
	batteryMV = 3010;
	tick(4);
	CHECK(battery_text.active().find("CHG") != std::string::npos);
	menu.beginSession(nullptr);
	CHECK(battery_text.active().find("CHG") == std::string::npos);
	tick(4);
	CHECK(battery_text.active().find("CHG") == std::string::npos);
}
