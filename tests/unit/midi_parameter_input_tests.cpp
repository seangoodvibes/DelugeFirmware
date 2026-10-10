#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <array>
namespace midi_parameter_input_test {
struct ModelStackWithTimelineCounter;
struct TimelineCounter {
	Error clone_error = Error::NONE;
	TimelineCounter* clone_target = nullptr;
	int clone_calls = 0;
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter*, Error* = nullptr);
};
struct MIDICable {};
namespace params {
enum class Kind { PATCHED };
}
struct collection_fixture {
	int paramValueToKnobPos(int value, void*) { return value; }
	int knobPosToParamValue(int value, void*) { return value; }
	params::Kind getParamKind() { return params::Kind::PATCHED; }
};
struct MIDIKnob {
	struct {
		bool matches = true;
		bool equalsNoteOrCC(MIDICable*, int, int) { return matches; }
	} midiInput;
	bool relative = true;
};
constexpr int CC_NUMBER_NONE = 255;
namespace MidiTakeover {
static int calculateKnobPos(MIDICable&, int, int value, MIDIKnob*, bool, int, bool) {
	return value;
}
} // namespace MidiTakeover
struct Clip : TimelineCounter {};
static Clip* getCurrentClip() {
	return nullptr;
}
struct RootUI {};
static RootUI* getRootUI() {
	return nullptr;
}
static struct : RootUI {
	bool onArrangerView = false;
	void possiblyRefreshAutomationEditorGrid(Clip*, params::Kind, int) {}
} automation;
static auto& automation_view_for_session() {
	return automation;
}
struct ModelStackWithAutoParam;
struct param_fixture {
	int writes = 0, value = -1, position = -1, length = -1;
	int getCurrentValue() { return 0; }
	int getValuePossiblyAtPos(int, ModelStackWithAutoParam*) { return 0; }
	bool delete_linear_nodes = true;
	void setValuePossiblyForRegion(int next_value, ModelStackWithAutoParam*, int next_position, int next_length,
	                               bool remove = true) {
		++writes;
		value = next_value;
		position = next_position;
		length = next_length;
		delete_linear_nodes = remove;
	}
};
struct ModelStackWithAutoParam {
	param_fixture* autoParam = nullptr;
	collection_fixture* paramCollection = nullptr;
	int paramId = 0;
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
	collection_fixture collection;
	ModelStackWithAutoParam parameter_stack{&parameter, &collection};
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
struct ModControllableAudio : MelodicInstrument {
	std::array<MIDIKnob, 1> midi_knobs;
	ModelStackWithThreeMainThings* addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* stack, int) {
		++manager_lookups;
		return &stack->row.things;
	}
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob&, ModelStackWithThreeMainThings*) {
		++parameter_lookups;
		return lookup_available ? &parameter_stack : nullptr;
	}
	bool offerReceivedCCToLearnedParamsForClip(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*,
	                                           int32_t);
	bool offerReceivedPitchBendToLearnedParams(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*,
	                                           int32_t);
};
#include "learned_parameter_input.inc"
#include "midi_parameter_input.inc"
} // namespace midi_parameter_input_test
using namespace midi_parameter_input_test;
TEST_GROUP(MidiParameterInput) {
	MelodicInstrument instrument;
	ModControllableAudio learned;
	MIDICable cable;
	bool send_learned(bool pitch, ModelStackWithTimelineCounter* target) {
		return pitch ? learned.offerReceivedPitchBendToLearnedParams(cable, 0, 0, 64, target, -1)
		             : learned.offerReceivedCCToLearnedParamsForClip(cable, 0, 7, 42, target, -1);
	}
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

TEST(MidiParameterInput, learned_mappings_stop_after_clone_failure) {
	original.clone_error = Error::INSUFFICIENT_RAM;
	for (bool pitch : {false, true})
		CHECK(send_learned(pitch, &stack));
	LONGS_EQUAL(2, original.clone_calls);
	LONGS_EQUAL(0, learned.manager_lookups);
	LONGS_EQUAL(0, learned.parameter_lookups);
	LONGS_EQUAL(0, learned.parameter.writes);
	POINTERS_EQUAL(&original, stack.timeline);
}
TEST(MidiParameterInput, learned_mapping_success_and_missing_targets) {
	for (bool pitch : {false, true}) {
		CHECK_FALSE(send_learned(pitch, nullptr));
		CHECK(send_learned(pitch, &stack));
		learned.lookup_available = false;
		CHECK(send_learned(pitch, &stack));
		learned.lookup_available = true;
	}
	LONGS_EQUAL(2, learned.parameter.writes);
	LONGS_EQUAL(0, learned.parameter.value);
}
TEST(MidiParameterInput, unmatched_learned_mappings_do_not_clone) {
	learned.midi_knobs[0].midiInput.matches = false;
	for (bool pitch : {false, true})
		CHECK_FALSE(send_learned(pitch, &stack));
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, learned.parameter.writes);
}
