#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
namespace midi_parameter_input_test {
struct ModelStackWithTimelineCounter;
struct TimelineCounter {
	Error clone_error = Error::NONE;
	TimelineCounter* clone_target = nullptr;
	int clone_calls = 0;
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter*, Error* = nullptr);
};
struct ModelStackWithAutoParam;
struct param_fixture {
	int writes = 0, value = -1, position = -1, length = -1;
	bool delete_linear_nodes = true;
	void setValuePossiblyForRegion(int next_value, ModelStackWithAutoParam*, int next_position, int next_length,
	                               bool remove) {
		++writes;
		value = next_value;
		position = next_position;
		length = next_length;
		delete_linear_nodes = remove;
	}
};
struct ModelStackWithAutoParam {
	param_fixture* autoParam = nullptr;
};
struct ModelStackWithThreeMainThings {};
struct ModelStackWithNoteRow {
	ModelStackWithThreeMainThings things;
	ModelStackWithThreeMainThings* addOtherTwoThings(void*, void*) { return &things; }
};
struct ModelStackWithTimelineCounter {
	TimelineCounter* timeline = nullptr;
	void* song = nullptr;
	ModelStackWithNoteRow row;
	bool timelineCounterIsSet() { return timeline != nullptr; }
	TimelineCounter* getTimelineCounter() { return timeline; }
	TimelineCounter* getTimelineCounterAllowNull() { return timeline; }
	ModelStackWithNoteRow* addNoteRow(int, void*) { return &row; }
};
bool TimelineCounter::possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* stack, Error* error) {
	++clone_calls;
	if (error)
		*error = clone_error;
	if (clone_error == Error::NONE && clone_target) {
		stack->timeline = clone_target;
		return true;
	}
	return false;
}
struct view_fixture {
	int modPos = 0, modLength = 0;
	ModelStackWithTimelineCounter activeModControllableModelStack;
};
static deluge::gui::ui_session::State<view_fixture> views;
static auto& view_for_session() {
	return views.active();
}
struct MelodicInstrument {
	param_fixture parameter;
	ModelStackWithAutoParam parameter_stack{&parameter};
	bool lookup_available = true;
	int manager_lookups = 0, parameter_lookups = 0;
	void* toModControllable() { return this; }
	void* getParamManager(void*) {
		++manager_lookups;
		return this;
	}
	ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int, ModelStackWithThreeMainThings*) {
		++parameter_lookups;
		return lookup_available ? &parameter_stack : nullptr;
	}
	void processParamFromInputMIDIChannel(int32_t, int32_t, ModelStackWithTimelineCounter*);
};
#include "midi_parameter_input.inc"
} // namespace midi_parameter_input_test
using namespace midi_parameter_input_test;
TEST_GROUP(MidiParameterInput) {
	MelodicInstrument instrument;
	TimelineCounter original, cloned;
	ModelStackWithTimelineCounter stack;
	void setup() override {
		views = {};
		stack.timeline = &original;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void send() {
		instrument.processParamFromInputMIDIChannel(7, 42, &stack);
	}
};
TEST(MidiParameterInput, failed_clone_does_not_write_original_parameters) {
	original.clone_error = Error::INSUFFICIENT_RAM;
	send();
	LONGS_EQUAL(1, original.clone_calls);
	LONGS_EQUAL(0, instrument.manager_lookups);
	LONGS_EQUAL(0, instrument.parameter_lookups);
	LONGS_EQUAL(0, instrument.parameter.writes);
	POINTERS_EQUAL(&original, stack.timeline);
}
TEST(MidiParameterInput, unchanged_timeline_and_successful_clone_allow_parameter_input) {
	send();
	original.clone_target = &cloned;
	send();
	POINTERS_EQUAL(&cloned, stack.timeline);
	LONGS_EQUAL(2, instrument.parameter.writes);
	LONGS_EQUAL(42, instrument.parameter.value);
	CHECK_FALSE(instrument.parameter.delete_linear_nodes);
}
TEST(MidiParameterInput, unavailable_model_stack_and_parameter_are_safe) {
	instrument.processParamFromInputMIDIChannel(7, 42, nullptr);
	instrument.lookup_available = false;
	send();
	instrument.lookup_available = true;
	instrument.parameter_stack.autoParam = nullptr;
	send();
	LONGS_EQUAL(0, instrument.parameter.writes);
}
TEST(MidiParameterInput, step_editing_uses_active_panel_and_resulting_timeline) {
	namespace panels = deluge::gui::ui_session;
	for (auto owner : {panels::Id::Local, panels::Id::Remote}) {
		panels::Scope scope(owner);
		stack.timeline = &original;
		original.clone_target = &cloned;
		view_for_session().activeModControllableModelStack.timeline = &cloned;
		view_for_session().modPos = owner == panels::Id::Local ? 5 : 17;
		view_for_session().modLength = 8;
		send();
		LONGS_EQUAL(view_for_session().modPos, instrument.parameter.position);
		LONGS_EQUAL(8, instrument.parameter.length);
	}
}
