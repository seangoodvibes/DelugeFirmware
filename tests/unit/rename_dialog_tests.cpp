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
struct text_fixture {
	std::string value;
	Error result = Error::NONE;
	Error set(std::string_view name) {
		value.clear();
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
	void displayText() { ++panels.active().draws; }
	void drawKeys() { ++panels.active().keys; }
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
		panels.for_owner(session::Id::Local).current_ui = &menu;
		panels.for_owner(session::Id::Remote).current_ui = &menu;
	}
	void teardown() override {
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
