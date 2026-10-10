#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <array>
#include <cstdint>
namespace knob_indicator_test {
namespace session = ::deluge::gui::ui_session;
constexpr int32_t operator""_i32(unsigned long long value) {
	return static_cast<int32_t>(value);
}
constexpr int kKnobPosOffset = 64, kMaxKnobPos = 128, UI_MODE_STUTTERING = 1;
namespace params {
enum class Kind { NORMAL, PATCH_CABLE };
}
struct ModelStackWithAutoParam;
struct ParamCollection {
	bool has_value = false;
	int32_t value = 0;
	params::Kind kind = params::Kind::NORMAL;
	bool has_current_value(int32_t) { return has_value; }
	int32_t get_current_value(int32_t) { return value; }
	params::Kind getParamKind() { return kind; }
	int32_t paramValueToKnobPos(int32_t value, ModelStackWithAutoParam*) { return value; }
};
struct AutoParam {
	int32_t value = 0;
	int32_t getValuePossiblyAtPos(uint32_t, ModelStackWithAutoParam*) { return value; }
};
struct ModControllable {
	ModelStackWithAutoParam* result = nullptr;
	int32_t fallback = -64;
	template <class T>
	ModelStackWithAutoParam* getParamFromModEncoder(uint8_t, T*, bool) {
		return result;
	}
	int32_t getKnobPosForNonExistentParam(uint8_t, ModelStackWithAutoParam*) { return fallback; }
};
using ModControllableAudio = ModControllable;
struct ModelStackWithAutoParam {
	ModControllable* modControllable = nullptr;
	AutoParam* autoParam = nullptr;
	ParamCollection* paramCollection = nullptr;
	int32_t paramId = 0;
};
static bool bipolar = false, quantized = false, stuttering = false;
static bool isParamBipolar(params::Kind, int32_t) {
	return bipolar;
}
static bool isParamQuantizedStutter(params::Kind, int32_t, ModControllableAudio*) {
	return quantized;
}
static bool isUIModeActive(int) {
	return stuttering;
}
namespace indicator_leds {
struct result {
	int calls = 0;
	int32_t level = -999;
	bool bipolar = false;
};
static session::State<std::array<result, 2>> outputs;
static void setKnobIndicatorLevel(uint8_t index, int32_t level, bool bipolar) {
	auto& target = outputs.active()[index];
	++target.calls;
	target.level = level;
	target.bipolar = bipolar;
}
} // namespace indicator_leds
struct View {
	ModelStackWithAutoParam activeModControllableModelStack;
	uint32_t modPos = 0;
	void setKnobIndicatorLevel(uint8_t);
	int32_t convertPatchCableKnobPosToIndicatorLevel(int32_t);
};
static session::State<View> views;
static View& view_for_session() {
	return views.active();
}
#include "knob_indicator.inc"
} // namespace knob_indicator_test
using namespace knob_indicator_test;
TEST_GROUP(KnobIndicator) {
	ModControllable controllable;
	ModelStackWithAutoParam stack;
	ParamCollection collection;
	AutoParam param;
	void setup() override {
		session::detail::active = session::Id::Local;
		views = {};
		indicator_leds::outputs = {};
		bipolar = quantized = stuttering = false;
		controllable.result = &stack;
		stack.modControllable = &controllable;
		for (auto owner : {session::Id::Local, session::Id::Remote})
			views.for_owner(owner).activeModControllableModelStack.modControllable = &controllable;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(KnobIndicator, absent_controllable_or_mapping_does_not_dereference_target) {
	view_for_session().activeModControllableModelStack.modControllable = nullptr;
	view_for_session().setKnobIndicatorLevel(0);
	view_for_session().activeModControllableModelStack.modControllable = &controllable;
	controllable.result = nullptr;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
}
TEST(KnobIndicator, unavailable_plain_parameter_has_deterministic_off_level) {
	stack.paramId = 0xff10;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].level);
	stack.autoParam = &param;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].level);
}
TEST(KnobIndicator, current_values_and_automation_route_to_owning_panel) {
	stack.paramCollection = &collection;
	collection.has_value = true;
	collection.value = -32;
	view_for_session().setKnobIndicatorLevel(0);
	{
		session::Scope scope(session::Id::Remote);
		stack.autoParam = &param;
		param.value = 32;
		bipolar = true;
		view_for_session().setKnobIndicatorLevel(1);
		LONGS_EQUAL(96, indicator_leds::outputs.active()[1].level);
		CHECK(indicator_leds::outputs.active()[1].bipolar);
		LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	}
	LONGS_EQUAL(32, indicator_leds::outputs.active()[0].level);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[1].calls);
}
TEST(KnobIndicator, nonexistent_and_patch_cable_defaults_are_preserved) {
	stack.paramId = 255;
	controllable.fallback = -16;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(48, indicator_leds::outputs.active()[0].level);
	stack.paramId = 0;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(64, indicator_leds::outputs.active()[0].level);
	CHECK(indicator_leds::outputs.active()[0].bipolar);
}
TEST(KnobIndicator, stutter_steps_and_patch_cable_scaling_are_preserved) {
	stack.paramCollection = &collection;
	collection.has_value = true;
	quantized = true;
	for (auto value : {-64, -32, 0, 32, 64}) {
		collection.value = value;
		view_for_session().setKnobIndicatorLevel(0);
		LONGS_EQUAL(value + 64, indicator_leds::outputs.active()[0].level);
	}
	stuttering = true;
	collection.value = -20;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(44, indicator_leds::outputs.active()[0].level);
	quantized = false;
	collection.kind = params::Kind::PATCH_CABLE;
	collection.value = -64;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(64, indicator_leds::outputs.active()[0].level);
}

TEST(KnobIndicator, missing_fallback_controllable_leaves_indicator_off) {
	stack.paramId = 255;
	stack.modControllable = nullptr;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].level);
	CHECK_FALSE(indicator_leds::outputs.active()[0].bipolar);
}
