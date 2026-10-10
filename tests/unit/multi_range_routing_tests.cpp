#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
namespace multi_range_routing_test {
struct sample_range {
	int32_t topNote = 60;
};
} // namespace multi_range_routing_test
using MultiRange = multi_range_routing_test::sample_range;
namespace multi_range_routing_test {
namespace ui_session = deluge::gui::ui_session;
constexpr int kOLEDMenuNumOptionsVisible = 3;
enum class RangeEdit { OFF, LEFT, RIGHT };
struct MenuItem {};
int ui_token = 0;
int ui_tokens[2];
int* getCurrentUI() {
	return &ui_tokens[ui_token];
}
std::function<void()> on_delete;
bool deletion_success = true;
int deletions = 0;
struct sound {
	bool try_delete_multi_range(int32_t, int32_t, const deluge::lifetime::callback_validation* validation) {
		if (on_delete)
			on_delete();
		if (!deletion_success || !validation->valid())
			return false;
		++deletions;
		return true;
	}
};
namespace l10n {
enum class String { STRING_FOR_LAST_RANGE_CANT_DELETE, STRING_FOR_RANGE_DELETED };
String get(String value) {
	return value;
}
} // namespace l10n
struct source {
	int defaultRangeI = 0;
	struct range_list {
		sample_range range;
		int getNumElements() { return 12; }
		sample_range* getElement(int index) {
			CHECK_TRUE(index >= 0 && index < getNumElements());
			return &range;
		}
	} ranges;
	void getOrCreateFirstRange() {}
	int getRangeIndex(int note) { return note / 10; }
};
struct editor {
	source* currentSource = nullptr;
	sound* currentSound = nullptr;
	int currentSourceIndex = 0;
	sample_range* currentMultiRange = nullptr;
	int currentMultiRangeIndex = 0;
	RangeEdit editingRangeEdge = RangeEdit::OFF;
	int range_changes = 0;
	void setCurrentMultiRange(int index) {
		currentMultiRangeIndex = index;
		currentMultiRange = currentSource->ranges.getElement(index);
	}
	void possibleChangeToCurrentRangeDisplay() { ++range_changes; }
};
ui_session::State<editor> editors;
editor& sound_editor_for_session() {
	return editors.active();
}
struct test_display {
	bool oled = true;
	int popups = 0;
	bool hasPopup() { return false; }
	void displayPopup(l10n::String) { ++popups; }
	bool haveOLED() { return oled; }
} display_instance;
auto* display = &display_instance;
int oled_redraws = 0;
void renderUIsForOled() {
	++oled_redraws;
}
struct Range {
	void beginSession(MenuItem*) {}
};
struct MultiRange : Range {
	ui_session::State<MenuItem*> destinations;
	ui_session::State<int32_t> scroll_positions, values;
	MenuItem*& destination_for_session();
	int32_t& scroll_for_session();
	int getValue() { return values.active(); }
	void setValue(int value) { values.active() = value; }
	void beginSession(MenuItem*);
	MenuItem* selectButtonPress();
	void noteOnToChangeRange(int32_t);
	void deletePress();
	int numeric_redraws = 0;
	void drawValue() { ++numeric_redraws; }
};
#include "multi_range_routing.inc"
TEST_GROUP(MultiRangeRouting) {
	source local_source, remote_source;
	sound selected_sound;
	void setup() override {
		editors = {};
		on_delete = {};
		deletion_success = true;
		deletions = 0;
		ui_token = 0;
		display_instance.popups = 0;
		editors.for_owner(ui_session::Id::Local).currentSound = &selected_sound;
		editors.for_owner(ui_session::Id::Remote).currentSound = &selected_sound;
		editors.for_owner(ui_session::Id::Local).currentSource = &local_source;
		editors.for_owner(ui_session::Id::Remote).currentSource = &remote_source;
		display_instance.oled = true;
		oled_redraws = 0;
	}
	void teardown() override {
		on_delete = {};
	}
};
TEST(MultiRangeRouting, interleaved_entry_preserves_destination_and_viewport) {
	MultiRange menu;
	MenuItem local_destination, remote_destination;
	local_source.defaultRangeI = 8;
	remote_source.defaultRangeI = 1;
	{
		ui_session::Scope local(ui_session::Id::Local);
		menu.destination_for_session() = &local_destination;
		menu.beginSession(nullptr);
		LONGS_EQUAL(6, menu.scroll_for_session());
	}
	{
		ui_session::Scope remote(ui_session::Id::Remote);
		CHECK(menu.selectButtonPress() == nullptr);
		menu.destination_for_session() = &remote_destination;
		menu.beginSession(nullptr);
		LONGS_EQUAL(0, menu.scroll_for_session());
		CHECK(menu.selectButtonPress() == &remote_destination);
		menu.noteOnToChangeRange(50);
		LONGS_EQUAL(3, menu.scroll_for_session());
		LONGS_EQUAL(5, sound_editor_for_session().currentMultiRangeIndex);
	}
	ui_session::Scope local(ui_session::Id::Local);
	CHECK(menu.selectButtonPress() == &local_destination);
	LONGS_EQUAL(8, menu.getValue());
	LONGS_EQUAL(6, menu.scroll_for_session());
	LONGS_EQUAL(8, sound_editor_for_session().currentMultiRangeIndex);
	menu.noteOnToChangeRange(20);
	LONGS_EQUAL(2, menu.scroll_for_session());
	LONGS_EQUAL(3, menu.scroll_positions.for_owner(ui_session::Id::Remote));
}
TEST(MultiRangeRouting, editing_edge_blocks_note_navigation_and_unchanged_notes_do_not_redraw) {
	MultiRange menu;
	ui_session::Scope remote(ui_session::Id::Remote);
	menu.beginSession(nullptr);
	sound_editor_for_session().editingRangeEdge = RangeEdit::LEFT;
	menu.noteOnToChangeRange(70);
	LONGS_EQUAL(0, menu.getValue());
	LONGS_EQUAL(0, oled_redraws);
	sound_editor_for_session().editingRangeEdge = RangeEdit::OFF;
	menu.noteOnToChangeRange(0);
	LONGS_EQUAL(0, oled_redraws);
	display_instance.oled = false;
	menu.noteOnToChangeRange(70);
	LONGS_EQUAL(7, menu.getValue());
	LONGS_EQUAL(0, menu.scroll_for_session());
	LONGS_EQUAL(1, menu.numeric_redraws);
	LONGS_EQUAL(1, sound_editor_for_session().range_changes);
}
TEST(MultiRangeRouting, failed_deletion_does_not_publish_menu_changes) {
	ui_session::Scope local(ui_session::Id::Local);
	MultiRange menu;
	menu.setValue(5);
	deletion_success = false;
	menu.deletePress();
	LONGS_EQUAL(5, menu.getValue());
	LONGS_EQUAL(0, sound_editor_for_session().range_changes);
	LONGS_EQUAL(0, display_instance.popups);
	LONGS_EQUAL(0, oled_redraws);
}
TEST(MultiRangeRouting, changed_source_during_deletion_does_not_edit_replacement) {
	ui_session::Scope local(ui_session::Id::Local);
	MultiRange menu;
	menu.setValue(5);
	on_delete = [&] { sound_editor_for_session().currentSource = &remote_source; };
	menu.deletePress();
	LONGS_EQUAL(0, deletions);
	LONGS_EQUAL(60, remote_source.ranges.range.topNote);
	LONGS_EQUAL(0, display_instance.popups);
}
TEST(MultiRangeRouting, changed_screen_during_deletion_cancels_publication) {
	ui_session::Scope local(ui_session::Id::Local);
	MultiRange menu;
	on_delete = [] { ui_token = 1; };
	menu.deletePress();
	LONGS_EQUAL(0, deletions);
	LONGS_EQUAL(0, sound_editor_for_session().range_changes);
}
TEST(MultiRangeRouting, changed_range_selection_during_deletion_is_preserved) {
	ui_session::Scope local(ui_session::Id::Local);
	MultiRange menu;
	menu.setValue(5);
	on_delete = [&] { menu.setValue(8); };
	menu.deletePress();
	LONGS_EQUAL(0, deletions);
	LONGS_EQUAL(8, menu.getValue());
	LONGS_EQUAL(0, display_instance.popups);
}
TEST(MultiRangeRouting, successful_deletion_updates_selected_range_and_display) {
	ui_session::Scope local(ui_session::Id::Local);
	MultiRange menu;
	menu.setValue(5);
	menu.deletePress();
	LONGS_EQUAL(1, deletions);
	LONGS_EQUAL(4, menu.getValue());
	LONGS_EQUAL(4, sound_editor_for_session().currentMultiRangeIndex);
	LONGS_EQUAL(1, display_instance.popups);
	LONGS_EQUAL(1, oled_redraws);
}
TEST(MultiRangeRouting, invalid_range_selection_does_not_access_range_or_delete) {
	ui_session::Scope local(ui_session::Id::Local);
	MultiRange menu;
	menu.setValue(-1);
	menu.deletePress();
	menu.setValue(12);
	menu.deletePress();
	LONGS_EQUAL(0, deletions);
	LONGS_EQUAL(0, display_instance.popups);
}
} // namespace multi_range_routing_test
