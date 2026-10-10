#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include <cstring>
namespace mod_encoder_lookup_test {
struct ModelStackWithTimelineCounter;
struct ModelStackWithThreeMainThings;
struct ModelStackWithAutoParam {};
struct TimelineCounter {
	Error clone_error = Error::NONE;
	TimelineCounter* clone_target = nullptr;
	int clones = 0, activations = 0;
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter*, Error*);
	void getActiveModControllable(ModelStackWithTimelineCounter*) { ++activations; }
};
struct controllable_fixture {
	int lookups = 0;
	bool tails = true;
	ModelStackWithAutoParam parameter;
	bool allowNoteTails(ModelStackWithThreeMainThings*) { return tails; }
	ModelStackWithAutoParam* getParamFromModEncoder(int, ModelStackWithThreeMainThings*) {
		++lookups;
		return &parameter;
	}
};
struct ModelStackWithTimelineCounter {
	TimelineCounter* timeline = nullptr;
	bool timelineCounterIsSet() { return timeline != nullptr; }
	TimelineCounter* getTimelineCounter() { return timeline; }
};
struct ModelStackWithThreeMainThings : ModelStackWithTimelineCounter {
	controllable_fixture* modControllable = nullptr;
	ModelStackWithThreeMainThings* addSoundFlags() { return this; }
};
constexpr size_t MODEL_STACK_MAX_SIZE = sizeof(ModelStackWithThreeMainThings);
static void copyModelStack(void* target, const void* source, size_t size) {
	std::memcpy(target, source, size);
}
bool TimelineCounter::possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* stack, Error* error) {
	++clones;
	*error = clone_error;
	if (clone_error == Error::NONE && clone_target) {
		stack->timeline = clone_target;
		return true;
	}
	return false;
}
struct View {
	ModelStackWithThreeMainThings activeModControllableModelStack;
	ModelStackWithAutoParam* getModelStackWithParam(int32_t, bool&);
};
#include "mod_encoder_lookup.inc"
} // namespace mod_encoder_lookup_test
using namespace mod_encoder_lookup_test;
TEST_GROUP(ModEncoderLookup) {
	View view;
	TimelineCounter original, cloned;
	controllable_fixture controllable;
	bool tails = false;
	void setup() override {
		view.activeModControllableModelStack.timeline = &original;
		view.activeModControllableModelStack.modControllable = &controllable;
	}
};
TEST(ModEncoderLookup, failed_clone_does_not_return_original_parameter) {
	original.clone_error = Error::INSUFFICIENT_RAM;
	POINTERS_EQUAL(nullptr, view.getModelStackWithParam(0, tails));
	LONGS_EQUAL(0, controllable.lookups);
	LONGS_EQUAL(0, original.activations);
	POINTERS_EQUAL(&original, view.activeModControllableModelStack.timeline);
	CHECK(tails);
}
TEST(ModEncoderLookup, changed_timeline_refreshes_controllable_before_lookup) {
	original.clone_target = &cloned;
	POINTERS_EQUAL(&controllable.parameter, view.getModelStackWithParam(0, tails));
	LONGS_EQUAL(1, cloned.activations);
	LONGS_EQUAL(0, original.activations);
	LONGS_EQUAL(1, controllable.lookups);
	POINTERS_EQUAL(&cloned, view.activeModControllableModelStack.timeline);
}
TEST(ModEncoderLookup, unnecessary_cloning_and_no_timeline_preserve_lookup) {
	POINTERS_EQUAL(&controllable.parameter, view.getModelStackWithParam(0, tails));
	LONGS_EQUAL(0, original.activations);
	view.activeModControllableModelStack.timeline = nullptr;
	POINTERS_EQUAL(&controllable.parameter, view.getModelStackWithParam(1, tails));
	LONGS_EQUAL(1, original.clones);
	LONGS_EQUAL(2, controllable.lookups);
}
TEST(ModEncoderLookup, missing_controllable_does_not_clone) {
	view.activeModControllableModelStack.modControllable = nullptr;
	POINTERS_EQUAL(nullptr, view.getModelStackWithParam(0, tails));
	LONGS_EQUAL(0, original.clones);
}
