#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
namespace multi_range_routing_test {
namespace ui_session = deluge::gui::ui_session;
constexpr int kOLEDMenuNumOptionsVisible = 3;
enum class RangeEdit { OFF, LEFT, RIGHT };
struct MenuItem {};
struct source {
	int defaultRangeI = 0;
	struct range_list {
		int getNumElements() { return 12; }
	} ranges;
	void getOrCreateFirstRange() {}
	int getRangeIndex(int note) { return note / 10; }
};
struct editor {
	source* currentSource = nullptr;
	void* currentMultiRange = nullptr;
	int currentMultiRangeIndex = 0;
	RangeEdit editingRangeEdge = RangeEdit::OFF;
	int range_changes = 0;
	void setCurrentMultiRange(int index) { currentMultiRangeIndex = index; }
	void possibleChangeToCurrentRangeDisplay() { ++range_changes; }
};
ui_session::State<editor> editors;
editor& sound_editor_for_session() {
	return editors.active();
}
struct test_display {
	bool oled = true;
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
	int numeric_redraws = 0;
	void drawValue() { ++numeric_redraws; }
};
#include "multi_range_routing.inc"
TEST_GROUP(MultiRangeRouting) {
	source local_source, remote_source;
	void setup() override {
		editors = {};
		editors.for_owner(ui_session::Id::Local).currentSource = &local_source;
		editors.for_owner(ui_session::Id::Remote).currentSource = &remote_source;
		display_instance.oled = true;
		oled_redraws = 0;
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
} // namespace multi_range_routing_test
