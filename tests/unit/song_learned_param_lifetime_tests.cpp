#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace song_learned_param_lifetime_test {
constexpr int CC_NUMBER_NONE = -1;
struct MIDICable {};
struct TimelineCounter {};
struct Song;
struct ModControllableAudio;
struct ModelStackWithThreeMainThings {
	Song* song = nullptr;
	TimelineCounter* timeline = nullptr;
	ModControllableAudio* modControllable = nullptr;
	int* paramManager = nullptr;
	bool timelineCounterIsSet() { return timeline; }
	TimelineCounter* getTimelineCounter() { return timeline; }
	TimelineCounter* getTimelineCounterAllowNull() { return timeline; }
};
struct ModelStackWithAutoParam;
std::function<void()> on_lookup, on_write, on_refresh;
int lookups, writes, automation_refreshes, performance_refreshes;
struct AutoParam {
	int getValuePossiblyAtPos(int, ModelStackWithAutoParam*) { return 0; }
	int getCurrentValue() { return 0; }
	void setValuePossiblyForRegion(int, ModelStackWithAutoParam*, int, int) {
		++writes;
		if (on_write)
			on_write();
	}
};
struct ParamCollection {
	int paramValueToKnobPos(int value, ModelStackWithAutoParam*) { return value; }
	int knobPosToParamValue(int value, ModelStackWithAutoParam*) { return value; }
	int getParamKind() { return 9; }
};
struct ModelStackWithAutoParam {
	AutoParam* autoParam = nullptr;
	ParamCollection* paramCollection = nullptr;
	int paramId = 7;
} param_stack;
struct MIDIKnob {
	struct {
		bool matches = true;
		bool equalsNoteOrCC(MIDICable*, int, int) { return matches; }
	} midiInput;
	int paramDescriptor = 7;
	bool relative = true;
};
struct MidiTakeover {
	static int calculateKnobPos(MIDICable&, int, int value, MIDIKnob*, bool, int, bool) { return value; }
};
struct {
	int modLength = 0, modPos = 0;
	ModelStackWithThreeMainThings activeModControllableModelStack;
} view;
auto& view_for_session() {
	return view;
}
struct RootUI {};
struct Automation : RootUI {
	void possiblyRefreshAutomationEditorGrid(void*, int kind, int id) {
		LONGS_EQUAL(9, kind);
		LONGS_EQUAL(7, id);
		++automation_refreshes;
		if (on_refresh)
			on_refresh();
	}
} automation;
struct Performance : RootUI {
	void possiblyRefreshPerformanceViewDisplay(int kind, int id, int position) {
		LONGS_EQUAL(9, kind);
		LONGS_EQUAL(7, id);
		LONGS_EQUAL(64, position);
		++performance_refreshes;
		if (on_refresh)
			on_refresh();
	}
} performance;
auto& automation_view_for_session() {
	return automation;
}
auto& performance_view_for_session() {
	return performance;
}
RootUI* root_ui = &automation;
RootUI* getRootUI() {
	return root_ui;
}
struct ModControllableAudio {
	std::vector<MIDIKnob> midi_knobs{2};
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob&, ModelStackWithThreeMainThings*) {
		++lookups;
		if (on_lookup)
			on_lookup();
		return &param_stack;
	}
	bool offerReceivedCCToLearnedParamsForSong(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithThreeMainThings*);
};
struct Song : TimelineCounter {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
	ModControllableAudio globalEffectable;
	int paramManager;
};
Song* currentSong;
#include "song_learned_param_lifetime.inc"
} // namespace song_learned_param_lifetime_test
using namespace song_learned_param_lifetime_test;
TEST_GROUP(song_learned_param_lifetime) {
	std::unique_ptr<Song> song;
	std::unique_ptr<AutoParam> param;
	std::unique_ptr<ParamCollection> collection;
	ModelStackWithThreeMainThings stack;
	MIDICable cable;
	void reset() {
		on_lookup = on_write = on_refresh = {};
		lookups = writes = automation_refreshes = performance_refreshes = 0;
		song = std::make_unique<Song>();
		param = std::make_unique<AutoParam>();
		collection = std::make_unique<ParamCollection>();
		currentSong = song.get();
		stack = {song.get(), song.get(), &song->globalEffectable, &song->paramManager};
		param_stack = {param.get(), collection.get(), 7};
		root_ui = &automation;
		view = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_lookup = on_write = on_refresh = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	bool send() {
		return song->globalEffectable.offerReceivedCCToLearnedParamsForSong(cable, 2, 3, 64, &stack);
	}
};
TEST(song_learned_param_lifetime, live_automation_and_performance_routes_refresh_each_knob) {
	CHECK(send());
	LONGS_EQUAL(2, writes);
	LONGS_EQUAL(2, automation_refreshes);
	reset();
	root_ui = &performance;
	CHECK(send());
	LONGS_EQUAL(2, writes);
	LONGS_EQUAL(2, performance_refreshes);
}
TEST(song_learned_param_lifetime, lookup_song_deletion_cancels_parameter_access) {
	on_lookup = [&] {
		song.reset();
		param.reset();
		collection.reset();
	};
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
TEST(song_learned_param_lifetime, notification_song_deletion_cancels_display_and_iteration) {
	on_write = [&] { song.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(0, automation_refreshes);
}
TEST(song_learned_param_lifetime, parameter_destruction_preserves_display_metadata) {
	song->globalEffectable.midi_knobs.resize(1);
	root_ui = &performance;
	on_write = [&] {
		param.reset();
		collection.reset();
	};
	CHECK(send());
	LONGS_EQUAL(1, performance_refreshes);
}
TEST(song_learned_param_lifetime, display_callback_deletion_cancels_next_knob) {
	on_refresh = [&] { song.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(1, automation_refreshes);
}
TEST(song_learned_param_lifetime, knob_rebinding_during_lookup_cancels_write) {
	on_lookup = [&] { ++song->globalEffectable.midi_knobs[0].paramDescriptor; };
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
TEST(song_learned_param_lifetime, same_address_song_replacement_cancels_lookup) {
	on_lookup = [&] {
		auto* raw = song.get();
		raw->~Song();
		new (raw) Song;
	};
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
TEST(song_learned_param_lifetime, session_switch_during_write_cancels_display) {
	on_write = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	CHECK(send());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(0, automation_refreshes);
}
TEST(song_learned_param_lifetime, foreign_manager_or_retired_song_rejects_entry) {
	int foreign;
	stack.paramManager = &foreign;
	CHECK_FALSE(send());
	stack.paramManager = &song->paramManager;
	song->lifetime.retire();
	CHECK_FALSE(send());
	LONGS_EQUAL(0, lookups);
}
TEST(song_learned_param_lifetime, stack_retarget_during_lookup_cancels_write) {
	on_lookup = [&] { stack.timeline = nullptr; };
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
