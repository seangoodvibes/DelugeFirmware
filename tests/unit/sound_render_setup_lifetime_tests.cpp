#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace sound_render_setup_lifetime_test {
using q31_t = int32_t;
constexpr int kNumSources = 2, LFO1_ID = 0, LFO3_ID = 1;
enum class PatchSource { LFO_GLOBAL_1, LFO_GLOBAL_2, SIDECHAIN };
enum class OscType { DX7, OTHER };
namespace util {
template <class T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
namespace params {
constexpr int GLOBAL_LFO_FREQ_1 = 0, GLOBAL_LFO_FREQ_2 = 1, UNPATCHED_SIDECHAIN_SHAPE = 2;
constexpr int GLOBAL_DELAY_FEEDBACK = 0, GLOBAL_DELAY_RATE = 1, FIRST_GLOBAL = 0;
} // namespace params
std::function<void(int)> on_stage;
std::vector<int> stages;
void stage(int index) {
	stages.push_back(index);
	if (on_stage)
		on_stage(index);
}
struct PatchCableSet {
	bool enabled[3]{true, true, true};
	bool isSourcePatchedToSomething(PatchSource source) { return enabled[util::to_underlying(source)]; }
};
struct UnpatchedParamSet {
	int getValue(int) { return 3; }
};
struct ParamManagerForTimeline {
	PatchCableSet cables;
	UnpatchedParamSet unpatched;
	PatchCableSet* getPatchCableSet() { return &cables; }
	UnpatchedParamSet* getUnpatchedParamSet() { return &unpatched; }
};
struct Lfo {
	int id;
	int render(uint32_t, int, uint32_t) {
		const int result = id * 10;
		stage(id);
		return result;
	}
};
struct DxPatch {
	void computeLfo(uint32_t) { stage(3); }
};
struct Delay {
	struct State {
		int delayFeedbackAmount = 0, userDelayRate = 0, analog_saturation = 0;
	};
	void setupWorkingState(State&, uint32_t, bool) { stage(7); }
};
struct {
	uint32_t getTimePerInternalTickInverse(bool) { return 1; }
} playbackHandler;
struct Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	Lfo globalLFO1{1}, globalLFO3{2};
	int lfoConfig[2]{}, globalSourceValues[3]{};
	int paramFinalValues[2]{INT32_MAX, 42};
	uint32_t sourcesChanged = 0;
	struct source {
		OscType oscType = OscType::OTHER;
		DxPatch* dxPatch = nullptr;
	} sources[2];
	struct {
		void registerHit(int) { stage(4); }
		int render(uint32_t, int) {
			stage(5);
			return 30;
		}
	} sidechain;
	struct {
		void performPatching(uint32_t, Sound&, ParamManagerForTimeline&) { stage(6); }
	} patcher;
	Delay delay;
	std::vector<int> voices_;
	uint32_t getGlobalLFOPhaseIncrement(int, int) { return 1; }
	bool process_render_modulation(ParamManagerForTimeline*, uint32_t, int32_t,
	                               const deluge::lifetime::callback_validation*);
	bool prepare_render_delay(Delay::State&, bool, const deluge::lifetime::callback_validation*);
};
#include "sound_render_setup_lifetime.inc"
} // namespace sound_render_setup_lifetime_test
using namespace sound_render_setup_lifetime_test;
TEST_GROUP(sound_render_setup_lifetime) {
	std::unique_ptr<Sound> sound;
	std::unique_ptr<ParamManagerForTimeline> manager;
	DxPatch patch;
	bool context_valid = true;
	void reset() {
		sound = std::make_unique<Sound>();
		manager = std::make_unique<ParamManagerForTimeline>();
		sound->sources[0] = {OscType::DX7, &patch};
		on_stage = {};
		stages.clear();
		context_valid = true;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_stage = {};
	}
	bool render() {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive() && context_valid; };
		const deluge::lifetime::callback_validation validation{valid};
		return sound->process_render_modulation(manager.get(), 32, 1, &validation);
	}
};
TEST(sound_render_setup_lifetime, live_modulation_updates_values_and_patches_changes) {
	CHECK(render());
	LONGS_EQUAL(6, stages.size());
	for (int i = 0; i < 6; ++i)
		LONGS_EQUAL(i + 1, stages[i]);
	LONGS_EQUAL(10, sound->globalSourceValues[0]);
	LONGS_EQUAL(20, sound->globalSourceValues[1]);
	LONGS_EQUAL(30, sound->globalSourceValues[2]);
	LONGS_EQUAL(7, sound->sourcesChanged);
}
TEST(sound_render_setup_lifetime, every_modulation_callback_can_delete_owners) {
	for (int boundary = 1; boundary <= 6; ++boundary) {
		reset();
		on_stage = [&](int index) {
			if (index == boundary) {
				sound.reset();
				manager.reset();
			}
		};
		CHECK_FALSE(render());
		LONGS_EQUAL(boundary, stages.size());
	}
}
TEST(sound_render_setup_lifetime, invalidated_lfo_and_sidechain_results_are_not_published) {
	for (int boundary : {1, 2, 5}) {
		reset();
		on_stage = [&](int index) {
			if (index == boundary)
				context_valid = false;
		};
		CHECK_FALSE(render());
		LONGS_EQUAL(0, sound->globalSourceValues[boundary == 5 ? 2 : boundary - 1]);
	}
}
TEST(sound_render_setup_lifetime, changed_dx_patch_or_type_cancels_following_stages) {
	for (bool change_type : {false, true}) {
		reset();
		on_stage = [&](int index) {
			if (index == 3) {
				if (change_type)
					sound->sources[0].oscType = OscType::OTHER;
				else
					sound->sources[0].dxPatch = nullptr;
			}
		};
		CHECK_FALSE(render());
		LONGS_EQUAL(3, stages.size());
	}
}
TEST(sound_render_setup_lifetime, unpatched_sources_and_unchanged_values_skip_work) {
	for (bool enabled : {false, true}) {
		reset();
		for (bool& value : manager->cables.enabled)
			value = enabled;
		sound->sources[0].dxPatch = nullptr;
		sound->globalSourceValues[0] = 10;
		sound->globalSourceValues[1] = 20;
		sound->globalSourceValues[2] = 30;
		CHECK(render());
		LONGS_EQUAL(enabled ? 4 : 0, stages.size());
		LONGS_EQUAL(0, sound->sourcesChanged);
	}
}
TEST(sound_render_setup_lifetime, rejected_owner_does_not_read_parameter_manager) {
	context_valid = false;
	manager.reset();
	CHECK_FALSE(render());
	LONGS_EQUAL(0, stages.size());
}
TEST(sound_render_setup_lifetime, delay_limits_feedback_and_finishes_state_only_when_valid) {
	for (bool limit : {false, true}) {
		Delay::State state;
		CHECK(sound->prepare_render_delay(state, limit, nullptr));
		LONGS_EQUAL(limit ? (1 << 30) - (1 << 26) : INT32_MAX, state.delayFeedbackAmount);
		LONGS_EQUAL(42, state.userDelayRate);
		LONGS_EQUAL(8, state.analog_saturation);
	}
}
TEST(sound_render_setup_lifetime, delay_callback_can_delete_sound_before_finalization) {
	deluge::lifetime::lifetime_watch watch{sound->lifetime};
	const auto valid = [&] { return watch.alive(); };
	const deluge::lifetime::callback_validation validation{valid};
	Delay::State state;
	on_stage = [&](int) { sound.reset(); };
	CHECK_FALSE(sound->prepare_render_delay(state, false, &validation));
	LONGS_EQUAL(0, state.analog_saturation);
}
