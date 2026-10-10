#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <functional>
#include <string>
#include <string_view>
namespace rename_dialog_test {
namespace session = ::deluge::gui::ui_session;
namespace deluge::gui {
namespace ui_session = ::deluge::gui::ui_session;
}
static std::function<void()> on_base, on_text_set, on_display, on_keys;
struct text_fixture {
	std::string value;
	Error result = Error::NONE;
	Error set(std::string_view name) {
		value.clear();
		if (on_text_set)
			on_text_set();
		if (result == Error::NONE)
			value = name;
		return result;
	}
	bool isEmpty() const { return value.empty(); }
	const char* get() const { return value.c_str(); }
};
struct panel_state {
	text_fixture text;
	Error error = Error::NONE;
	int draws = 0, keys = 0, exits = 0, commits = 0;
	void* current_ui = nullptr;
};
static session::State<panel_state> panels;
static void* getCurrentUI() {
	return panels.active().current_ui;
}
struct display_fixture {
	void displayError(Error error) { panels.active().error = error; }
};
static display_fixture display_instance;
static auto* display = &display_instance;
class QwertyUI {
public:
	bool base_opened = true;
	bool opened() {
		panels.active().text.value.clear();
		if (on_base)
			on_base();
		return base_opened;
	}
};
class RenameUI : public QwertyUI {
public:
	bool available = true, allow_empty = true, commit_result = true;
	std::function<void()> on_commit;
	bool opened();
	void enterKeyPress();
	bool canRename() const { return available; }
	bool allowEmpty() const { return allow_empty; }
	std::string_view getCurrentName() const { return "original"; }
	text_fixture& entered_text_for_session() { return panels.active().text; }
	void displayText() {
		++panels.active().draws;
		if (on_display)
			on_display();
	}
	void drawKeys() {
		++panels.active().keys;
		if (on_keys)
			on_keys();
	}
	void exitUI() { ++panels.active().exits; }
	bool trySetName(std::string_view) {
		++panels.active().commits;
		if (on_commit)
			on_commit();
		return commit_result;
	}
};
#include "rename_dialog_methods.inc"
} // namespace rename_dialog_test
using namespace rename_dialog_test;
TEST_GROUP(RenameDialog) {
	RenameUI menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		panels = {};
		on_base = on_text_set = on_display = on_keys = {};
		panels.for_owner(session::Id::Local).current_ui = &menu;
		panels.for_owner(session::Id::Remote).current_ui = &menu;
	}
	void teardown() override {
		on_base = on_text_set = on_display = on_keys = {};
		session::detail::active = session::Id::Local;
	}
};
TEST(RenameDialog, failed_text_initialization_rejects_open_without_drawing_on_both_owners) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		panels.active().text.result = Error::INSUFFICIENT_RAM;
		CHECK_FALSE(menu.opened());
		CHECK(panels.active().error == Error::INSUFFICIENT_RAM);
		LONGS_EQUAL(0, panels.active().draws);
		LONGS_EQUAL(0, panels.active().keys);
		panels.active().text.result = Error::NONE;
		CHECK(menu.opened());
		STRCMP_EQUAL("original", panels.active().text.get());
		LONGS_EQUAL(1, panels.active().draws);
		LONGS_EQUAL(1, panels.active().keys);
	}
}
TEST(RenameDialog, unavailable_or_failed_base_open_does_not_draw) {
	menu.base_opened = false;
	CHECK_FALSE(menu.opened());
	menu.base_opened = true;
	menu.available = false;
	CHECK_FALSE(menu.opened());
	LONGS_EQUAL(0, panels.active().draws);
	LONGS_EQUAL(0, panels.active().keys);
}
TEST(RenameDialog, changed_ui_during_commit_does_not_exit) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		menu.on_commit = [] { panels.active().current_ui = nullptr; };
		menu.enterKeyPress();
		LONGS_EQUAL(1, panels.active().commits);
		LONGS_EQUAL(0, panels.active().exits);
	}
}
TEST(RenameDialog, changed_owner_during_commit_does_not_exit_either_panel) {
	menu.on_commit = [] { session::detail::active = session::Id::Remote; };
	menu.enterKeyPress();
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).exits);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Remote).exits);
}
TEST(RenameDialog, only_successful_commit_exits_the_initiating_panel) {
	session::Scope scope(session::Id::Remote);
	menu.commit_result = false;
	menu.enterKeyPress();
	LONGS_EQUAL(0, panels.active().exits);
	menu.commit_result = true;
	menu.enterKeyPress();
	LONGS_EQUAL(1, panels.active().exits);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).exits);
}
TEST(RenameDialog, prohibited_empty_name_does_not_commit_or_exit) {
	menu.allow_empty = false;
	menu.enterKeyPress();
	LONGS_EQUAL(0, panels.active().commits);
	LONGS_EQUAL(0, panels.active().exits);
}

TEST(RenameDialog, initialization_callbacks_stop_after_context_invalidation) {
	for (int phase = 0; phase < 4; ++phase) {
		for (int change = 0; change < 3; ++change) {
			session::detail::active = session::Id::Local;
			panels = {};
			panels.active().current_ui = &menu;
			menu.available = true;
			on_base = on_text_set = on_display = on_keys = {};
			auto invalidate = [&] {
				if (change == 0)
					session::detail::active = session::Id::Remote;
				if (change == 1)
					panels.active().current_ui = nullptr;
				if (change == 2)
					menu.available = false;
			};
			std::function<void()>* hooks[] = {&on_base, &on_text_set, &on_display, &on_keys};
			*hooks[phase] = invalidate;
			CHECK_FALSE(menu.opened());
			LONGS_EQUAL(phase >= 2 ? 1 : 0, panels.for_owner(session::Id::Local).draws);
			LONGS_EQUAL(phase == 3 ? 1 : 0, panels.for_owner(session::Id::Local).keys);
			LONGS_EQUAL(0, panels.for_owner(session::Id::Remote).draws);
			LONGS_EQUAL(0, panels.for_owner(session::Id::Remote).keys);
		}
	}
}
TEST(RenameDialog, copy_failure_after_owner_change_does_not_report_to_peer) {
	panels.active().text.result = Error::INSUFFICIENT_RAM;
	on_text_set = [] { session::detail::active = session::Id::Remote; };
	CHECK_FALSE(menu.opened());
	CHECK(panels.for_owner(session::Id::Remote).error == Error::NONE);
}
