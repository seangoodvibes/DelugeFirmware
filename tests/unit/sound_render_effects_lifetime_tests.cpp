#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
namespace sound_render_effects_lifetime_test {
using q31_t = int32_t;
constexpr int kNumSources = 2;
namespace params {
constexpr int FIRST_GLOBAL = 0, GLOBAL_VOLUME_POST_FX = 0, GLOBAL_VOLUME_POST_REVERB_SEND = 1;
constexpr int GLOBAL_MOD_FX_DEPTH = 2, GLOBAL_MOD_FX_RATE = 3, UNPATCHED_COMPRESSOR_THRESHOLD = 4;
} // namespace params
struct StereoSample {
	int value = 0;
	StereoSample operator+(const StereoSample& rhs) const { return {value + rhs.value}; }
};
std::function<void(int)> on_stage;
std::vector<int> stages;
void stage(int index) {
	stages.push_back(index);
	if (on_stage)
		on_stage(index);
}
struct UnpatchedParamSet {
	int threshold = 1;
	int getValue(int) { return threshold; }
};
struct ParamManager {
	UnpatchedParamSet unpatched;
	UnpatchedParamSet* getUnpatchedParamSet() { return &unpatched; }
};
struct ModelStackWithSoundFlags {
	ParamManager* paramManager = nullptr;
};
struct Delay {
	struct State {};
	int repeatsUntilAbandon = 0;
};
enum class RecorderStatus { RECORDING, FINISHED_CAPTURING_BUT_STILL_WRITING };
struct SampleRecorder {
	RecorderStatus status = RecorderStatus::RECORDING;
	void feedAudio(std::span<StereoSample>, bool, int multiplier) {
		LONGS_EQUAL(2, multiplier);
		stage(6);
	}
};
struct Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	int paramFinalValues[4]{10, 20, 30, 40};
	int postReverbVolumeLastTime = 7, modFXType_ = 0;
	int sourcesChanged = 1, startSkippingRenderingAtTime = 0;
	std::bitset<3> expressionSourcesChangedAtSynthLevel{7};
	struct source {
		bool dxPatchChanged = true;
	} sources[2];
	std::vector<int> voices_;
	Delay delay;
	struct {
		void setThreshold(int) {}
		void renderVolNeutral(std::span<StereoSample>, int) { stage(5); }
		void reset() { stage(5); }
	} compressor;
	void processSRRAndBitcrushing(std::span<StereoSample>, int*, ParamManager*) { stage(1); }
	void processFX(std::span<StereoSample>, int, int, int, Delay::State&, int*, ParamManager*, bool, int) { stage(2); }
	void processStutter(std::span<StereoSample>, ParamManager*) { stage(3); }
	void processReverbSendAndVolume(std::span<StereoSample>, int*, int, int, int, int, bool) { stage(4); }
	void reassessRenderSkippingStatus(ModelStackWithSoundFlags*) { stage(7); }
	void doParamLPF(size_t, ModelStackWithSoundFlags*) { stage(8); }
	void process_render_effects(ModelStackWithSoundFlags*, std::span<StereoSample>, std::span<StereoSample>, int32_t*,
	                            Delay::State&, int32_t, SampleRecorder*, const deluge::lifetime::callback_validation*);
};
#include "sound_render_effects_lifetime.inc"
} // namespace sound_render_effects_lifetime_test
using namespace sound_render_effects_lifetime_test;
TEST_GROUP(sound_render_effects_lifetime) {
	std::unique_ptr<Sound> sound;
	std::unique_ptr<ParamManager> manager;
	std::unique_ptr<SampleRecorder> recorder;
	ModelStackWithSoundFlags stack;
	StereoSample samples[1], output[1];
	bool context_valid = true;
	void reset() {
		sound = std::make_unique<Sound>();
		manager = std::make_unique<ParamManager>();
		recorder = std::make_unique<SampleRecorder>();
		stack.paramManager = manager.get();
		samples[0].value = 3;
		output[0].value = 4;
		context_valid = true;
		on_stage = {};
		stages.clear();
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_stage = {};
	}
	void render() {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive() && context_valid; };
		const deluge::lifetime::callback_validation validation{valid};
		Delay::State state;
		sound->process_render_effects(&stack, samples, output, nullptr, state, 0, recorder.get(), &validation);
	}
};
TEST(sound_render_effects_lifetime, live_effects_publish_audio_and_clear_completed_state) {
	render();
	LONGS_EQUAL(8, stages.size());
	for (int i = 0; i < 8; ++i)
		LONGS_EQUAL(i + 1, stages[i]);
	LONGS_EQUAL(7, output[0].value);
	LONGS_EQUAL(20, sound->postReverbVolumeLastTime);
	LONGS_EQUAL(0, sound->sourcesChanged);
	CHECK(sound->expressionSourcesChangedAtSynthLevel.none());
	CHECK_FALSE(sound->sources[0].dxPatchChanged);
}
TEST(sound_render_effects_lifetime, owner_destruction_at_every_stage_stops_later_work) {
	for (int boundary = 1; boundary <= 8; ++boundary) {
		reset();
		on_stage = [&](int index) {
			if (index == boundary) {
				sound.reset();
				manager.reset();
				recorder.reset();
			}
		};
		render();
		LONGS_EQUAL(boundary, stages.size());
		LONGS_EQUAL(boundary <= 6 ? 4 : 7, output[0].value);
	}
}
TEST(sound_render_effects_lifetime, context_invalidation_preserves_unpublished_audio_and_state) {
	for (int boundary = 1; boundary <= 6; ++boundary) {
		reset();
		on_stage = [&](int index) {
			if (index == boundary)
				context_valid = false;
		};
		render();
		LONGS_EQUAL(boundary, stages.size());
		LONGS_EQUAL(4, output[0].value);
		LONGS_EQUAL(7, sound->postReverbVolumeLastTime);
		LONGS_EQUAL(1, sound->sourcesChanged);
		CHECK(sound->expressionSourcesChangedAtSynthLevel.any());
	}
}
TEST(sound_render_effects_lifetime, invalid_entry_does_not_touch_manager_or_output) {
	context_valid = false;
	manager.reset();
	render();
	LONGS_EQUAL(0, stages.size());
	LONGS_EQUAL(4, output[0].value);
}
TEST(sound_render_effects_lifetime, completed_or_missing_recorder_is_skipped) {
	for (bool missing : {false, true}) {
		reset();
		if (missing)
			recorder.reset();
		else
			recorder->status = RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING;
		render();
		LONGS_EQUAL(7, stages.size());
		CHECK(std::find(stages.begin(), stages.end(), 6) == stages.end());
		LONGS_EQUAL(7, output[0].value);
	}
}
TEST(sound_render_effects_lifetime, compressor_reset_cancellation_is_checked) {
	manager->unpatched.threshold = 0;
	on_stage = [&](int index) {
		if (index == 5)
			sound.reset();
	};
	render();
	LONGS_EQUAL(5, stages.size());
	LONGS_EQUAL(4, output[0].value);
}
TEST(sound_render_effects_lifetime, remaining_delay_skips_render_reassessment) {
	sound->delay.repeatsUntilAbandon = 1;
	render();
	LONGS_EQUAL(7, stages.size());
	CHECK(std::find(stages.begin(), stages.end(), 7) == stages.end());
	LONGS_EQUAL(8, stages.back());
}
