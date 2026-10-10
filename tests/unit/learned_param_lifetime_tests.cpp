#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace learned_param_lifetime_test {
constexpr int CC_NUMBER_NONE = -1;
enum class Error { NONE, FAILED };
enum class ClipType { INSTRUMENT };
struct MIDICable {};
struct ModelStackWithTimelineCounter;
struct Owner {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
};
struct Output : Owner {};
std::function<void()> on_clone, on_lookup, on_write, on_refresh;
int lookups, writes, refreshes, clones, manager_lookups;
Error clone_error;
struct TimelineCounter {
	void possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter*, Error* error) {
		++clones;
		*error = clone_error;
		if (on_clone)
			on_clone();
	}
};
struct NoteRow {
	uint64_t undo_identity = 1;
};
struct Clip : TimelineCounter, Owner {
	Output* output = nullptr;
	ClipType type = ClipType::INSTRUMENT;
};
struct InstrumentClip : Clip {
	NoteRow* row = nullptr;
	NoteRow* find_note_row_from_id(int) { return row; }
};
int song;
int* currentSong = &song;
struct ModelStackWithTimelineCounter {
	int* song = currentSong;
	Clip* clip = nullptr;
	bool timelineCounterIsSet() { return clip; }
	Clip* getTimelineCounter() { return clip; }
	Clip* getTimelineCounterAllowNull() { return clip; }
};
struct ModelStackWithThreeMainThings {
	int manager;
	int* paramManager = &manager;
	NoteRow* row = nullptr;
	int noteRowId = 0;
	NoteRow* getNoteRowAllowNull() { return row; }
} things;
struct MIDIKnob {
	struct {
		bool matches = true;
		bool equalsNoteOrCC(MIDICable*, int, int) { return matches; }
	} midiInput;
	int paramDescriptor = 7;
	bool relative = true;
};
struct ModelStackWithAutoParam;
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
struct MidiTakeover {
	static int calculateKnobPos(MIDICable&, int, int value, MIDIKnob*, bool, int, bool) { return value; }
};
struct {
	int modLength = 0, modPos = 0;
	ModelStackWithTimelineCounter activeModControllableModelStack;
} view;
auto& view_for_session() {
	return view;
}
struct Automation {
	bool onArrangerView = false;
	void possiblyRefreshAutomationEditorGrid(Clip*, int kind, int id) {
		LONGS_EQUAL(9, kind);
		LONGS_EQUAL(7, id);
		++refreshes;
		if (on_refresh)
			on_refresh();
	}
} automation;
auto& automation_view_for_session() {
	return automation;
}
Automation* getRootUI() {
	return &automation;
}
Clip* selected_clip;
Clip* getCurrentClip() {
	return selected_clip;
}
struct ModControllableAudio : Owner {
	std::vector<MIDIKnob> midi_knobs{2};
	ModelStackWithThreeMainThings* addNoteRowIndexAndStuff(ModelStackWithTimelineCounter*, int) {
		++manager_lookups;
		return &things;
	}
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob&, ModelStackWithThreeMainThings*) {
		++lookups;
		if (on_lookup)
			on_lookup();
		return &param_stack;
	}
	bool offerReceivedCCToLearnedParamsForClip(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*,
	                                           int, const deluge::lifetime::callback_validation*);
	bool offerReceivedPitchBendToLearnedParams(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*,
	                                           int, const deluge::lifetime::callback_validation*);
};
#include "learned_param_lifetime.inc"
} // namespace learned_param_lifetime_test
using namespace learned_param_lifetime_test;
TEST_GROUP(learned_param_lifetime) {
	std::unique_ptr<ModControllableAudio> owner;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<Output> output;
	std::unique_ptr<AutoParam> param;
	std::unique_ptr<ParamCollection> collection;
	ModelStackWithTimelineCounter stack;
	MIDICable cable;
	void reset() {
		owner = std::make_unique<ModControllableAudio>();
		clip = std::make_unique<InstrumentClip>();
		output = std::make_unique<Output>();
		param = std::make_unique<AutoParam>();
		collection = std::make_unique<ParamCollection>();
		param_stack = {param.get(), collection.get(), 7};
		clip->output = output.get();
		selected_clip = clip.get();
		things.paramManager = &things.manager;
		things.row = nullptr;
		currentSong = &song;
		stack = {};
		stack.clip = clip.get();
		lookups = writes = refreshes = clones = manager_lookups = 0;
		clone_error = Error::NONE;
		on_clone = on_lookup = on_write = on_refresh = {};
		view.modLength = view.modPos = 0;
		automation.onArrangerView = false;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_clone = on_lookup = on_write = on_refresh = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	bool send(bool pitch = false, bool guarded = true) {
		auto watch = owner->watch_lifetime();
		const auto valid = [&] { return watch.alive(); };
		const deluge::lifetime::callback_validation validation{valid};
		if (pitch)
			return owner->offerReceivedPitchBendToLearnedParams(cable, 2, 3, 64, &stack, -1,
			                                                    guarded ? &validation : nullptr);
		return owner->offerReceivedCCToLearnedParamsForClip(cable, 2, 3, 64, &stack, -1,
		                                                    guarded ? &validation : nullptr);
	}
};
TEST(learned_param_lifetime, live_cc_visits_knobs_and_pitch_preserves_first_match_behavior) {
	CHECK(send());
	LONGS_EQUAL(2, writes);
	LONGS_EQUAL(2, refreshes);
	reset();
	CHECK(send(true));
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(0, refreshes);
}
TEST(learned_param_lifetime, clone_owner_deletion_cancels_lookup) {
	for (bool pitch : {false, true}) {
		reset();
		on_clone = [&] { owner.reset(); };
		CHECK(send(pitch));
		LONGS_EQUAL(0, lookups);
	}
}
TEST(learned_param_lifetime, clone_output_deletion_cancels_unguarded_callers) {
	for (bool pitch : {false, true}) {
		reset();
		on_clone = [&] {
			output.reset();
			owner.reset();
		};
		CHECK(send(pitch, false));
		LONGS_EQUAL(0, lookups);
	}
}
TEST(learned_param_lifetime, clone_clip_deletion_cancels_before_reacquisition) {
	on_clone = [&] { clip.reset(); };
	CHECK(send());
	LONGS_EQUAL(0, lookups);
}
TEST(learned_param_lifetime, knob_vector_replacement_cancels_after_clone) {
	on_clone = [&] { owner->midi_knobs.clear(); };
	CHECK(send());
	LONGS_EQUAL(0, lookups);
}
TEST(learned_param_lifetime, knob_descriptor_retarget_cancels_after_clone) {
	on_clone = [&] { ++owner->midi_knobs[0].paramDescriptor; };
	CHECK(send(true));
	LONGS_EQUAL(0, lookups);
}
TEST(learned_param_lifetime, lookup_deletion_cancels_before_parameter_access) {
	for (bool pitch : {false, true}) {
		reset();
		on_lookup = [&] {
			owner.reset();
			param.reset();
			collection.reset();
		};
		CHECK(send(pitch));
		LONGS_EQUAL(0, writes);
	}
}
TEST(learned_param_lifetime, notification_owner_deletion_cancels_display_and_next_knob) {
	on_write = [&] {
		owner.reset();
		param.reset();
		collection.reset();
	};
	CHECK(send());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(0, refreshes);
}
TEST(learned_param_lifetime, notification_can_destroy_parameter_before_display) {
	owner->midi_knobs.resize(1);
	on_write = [&] {
		param.reset();
		collection.reset();
	};
	CHECK(send());
	LONGS_EQUAL(1, refreshes);
}
TEST(learned_param_lifetime, refresh_deletion_cancels_next_knob) {
	on_refresh = [&] { owner.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(1, refreshes);
}
TEST(learned_param_lifetime, clone_failure_preserves_original_without_lookup) {
	clone_error = Error::FAILED;
	CHECK(send());
	CHECK(clip != nullptr);
	LONGS_EQUAL(0, lookups);
}
TEST(learned_param_lifetime, valid_clone_retarget_is_accepted) {
	InstrumentClip clone;
	clone.output = output.get();
	on_clone = [&] { stack.clip = &clone; };
	CHECK(send());
	LONGS_EQUAL(2, writes);
}
TEST(learned_param_lifetime, cloned_target_deletion_during_lookup_cancels) {
	auto clone = std::make_unique<InstrumentClip>();
	clone->output = output.get();
	on_clone = [&] { stack.clip = clone.get(); };
	on_lookup = [&] { clone.reset(); };
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
TEST(learned_param_lifetime, replaced_row_during_lookup_cancels) {
	NoteRow row;
	clip->row = &row;
	things.row = &row;
	on_lookup = [&] { ++row.undo_identity; };
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
TEST(learned_param_lifetime, session_change_after_notification_cancels_display) {
	on_write = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	CHECK(send());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(0, refreshes);
}

TEST(learned_param_lifetime, midi_binding_change_during_clone_cancels_lookup) {
	on_clone = [&] { owner->midi_knobs[0].midiInput.matches = false; };
	CHECK(send());
	LONGS_EQUAL(0, lookups);
}
TEST(learned_param_lifetime, model_stack_manager_retarget_during_lookup_cancels_write) {
	int replacement;
	on_lookup = [&] { things.paramManager = &replacement; };
	CHECK(send());
	LONGS_EQUAL(0, writes);
}
TEST(learned_param_lifetime, retired_clone_target_is_rejected_before_lookup) {
	InstrumentClip clone;
	clone.output = output.get();
	clone.lifetime.retire();
	on_clone = [&] { stack.clip = &clone; };
	CHECK(send());
	LONGS_EQUAL(0, lookups);
	LONGS_EQUAL(0, manager_lookups);
}
