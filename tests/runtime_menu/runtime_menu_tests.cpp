#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/menu_item/runtime_feature/devSysexSetting.h"
#include <string>
namespace session = deluge::gui::ui_session;
using deluge::gui::menu_item::OptType;
using deluge::gui::menu_item::runtime_feature::DevSysexSetting;
TEST_GROUP(RuntimeMenu){void setup() override{session::detail::active = session::Id::Local;
runtimeFeatureSettings = {};
noise_values = {0, 0x11111111, 0x22222222, 0x33333333};
noise_index = 0;
}
void teardown() override {
	session::detail::active = session::Id::Local;
	noise_values.clear();
}
}
;
TEST(RuntimeMenu, opening_peer_preserves_pending_code_and_retained_label) {
	DevSysexSetting menu(RuntimeFeatureSettingType::DevSysexAllowed);
	menu.readCurrentValue();
	auto local_options = menu.getOptions(OptType::FULL);
	STRCMP_EQUAL("on - 11111111", std::string(local_options[1]).c_str());
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
		auto remote_options = menu.getOptions(OptType::FULL);
		STRCMP_EQUAL("on - 22222222", std::string(remote_options[1]).c_str());
	}
	STRCMP_EQUAL("on - 11111111", std::string(local_options[1]).c_str());
	menu.commit(1);
	LONGS_EQUAL(0x11111111, runtimeFeatureSettings.settings[0].value);
}
TEST(RuntimeMenu, opening_peer_cannot_change_the_code_committed_by_local) {
	DevSysexSetting menu(RuntimeFeatureSettingType::DevSysexAllowed);
	menu.readCurrentValue();
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
	}
	menu.commit(1);
	LONGS_EQUAL(0x11111111, runtimeFeatureSettings.settings[0].value);
}
TEST(RuntimeMenu, committed_host_value_is_reloaded_by_peer_without_regenerating_code) {
	DevSysexSetting menu(RuntimeFeatureSettingType::DevSysexAllowed);
	menu.readCurrentValue();
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
		menu.commit(1);
	}
	LONGS_EQUAL(0x22222222, runtimeFeatureSettings.settings[0].value);
	LONGS_EQUAL(1, menu.getValue());
	auto options = menu.getOptions(OptType::FULL);
	STRCMP_EQUAL("on - 22222222", std::string(options[1]).c_str());
	LONGS_EQUAL(3, noise_index);
	menu.commit(0);
	LONGS_EQUAL(0, runtimeFeatureSettings.settings[0].value);
	{
		session::Scope remote(session::Id::Remote);
		LONGS_EQUAL(0, menu.getValue());
		auto options = menu.getOptions(OptType::FULL);
		STRCMP_EQUAL("on - 33333333", std::string(options[1]).c_str());
	}
}
TEST(RuntimeMenu, options_reload_peer_commit_before_formatting_the_label) {
	DevSysexSetting menu(RuntimeFeatureSettingType::DevSysexAllowed);
	menu.readCurrentValue();
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
		menu.commit(1);
	}
	auto options = menu.getOptions(OptType::FULL);
	STRCMP_EQUAL("on - 22222222", std::string(options[1]).c_str());
	LONGS_EQUAL(1, menu.getValue());
	LONGS_EQUAL(3, noise_index);
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
