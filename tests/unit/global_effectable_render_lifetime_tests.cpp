#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <vector>
namespace global_effectable_render_lifetime_test {
using q31_t = int32_t;
constexpr int kMaxSampleValue = INT32_MAX, CACHE_LINE_SIZE = 16, SSI_TX_BUFFER_NUM_SAMPLES = 4;
namespace params {
enum {
	UNPATCHED_VOLUME,
	UNPATCHED_PITCH_ADJUST,
	UNPATCHED_REVERB_SEND_AMOUNT,
	UNPATCHED_PAN,
	UNPATCHED_SIDECHAIN_VOLUME,
	GLOBAL_VOLUME_POST_REVERB_SEND,
	UNPATCHED_SIDECHAIN_SHAPE,
	UNPATCHED_COMPRESSOR_THRESHOLD
};
constexpr int kMaxNumUnpatchedParams = 16;
} // namespace params
int paramNeutralValues[8]{};
int cableToLinearParamShortcut(int value) {
	return value;
}
int getFinalParameterValueVolume(int, int) {
	return 128;
}
int getFinalParameterValueExp(int, int) {
	return 0;
}
int multiply_32x32_rshift32_rounded(int, int) {
	return 0;
}
int multiply_32x32_rshift32(int, int) {
	return 0;
}
struct StereoSample {
	int l = 0, r = 0;
	StereoSample operator+(const StereoSample& other) const { return {l + other.l, r + other.r}; }
};
enum class OutputType { KIT, AUDIO };
enum class RecorderStatus { RECORDING, FINISHED_CAPTURING_BUT_STILL_WRITING };
std::function<void(int)> on_stage;
std::vector<int> stages;
void stage(int id) {
	stages.push_back(id);
	if (on_stage)
		on_stage(id);
}
struct SampleRecorder {
	RecorderStatus status = RecorderStatus::RECORDING;
	void feedAudio(std::span<StereoSample>, bool, int) { stage(5); }
};
struct ModelStackWithThreeMainThings {};
struct UnpatchedParamSet {
	int getValue(int id) { return id == params::UNPATCHED_SIDECHAIN_VOLUME ? INT32_MIN : 0; }
};
struct ParamManager {
	UnpatchedParamSet values;
	struct {
		UnpatchedParamSet* paramCollection;
		uint32_t whichParamsAreInterpolating[2]{1, 0};
	} summaries[1]{{&values}};
	UnpatchedParamSet* getUnpatchedParamSet() { return summaries[0].paramCollection; }
	auto* getUnpatchedParamSetSummary() { return &summaries[0]; }
	ParamManager* toForTimeline() { return this; }
	void tickSamples(size_t, ModelStackWithThreeMainThings*, const deluge::lifetime::callback_validation* = nullptr) {
		stage(6);
	}
};
struct Output;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Output* output = nullptr;
	ParamManager paramManager;
};
struct Song {
	ParamManager* backup = nullptr;
	ParamManager* getBackedUpParamManagerPreferablyWithClip(void*, void*) { return backup; }
} song;
Song* currentSong = &song;
struct ModelStackWithTimelineCounter {
	Song* song = &global_effectable_render_lifetime_test::song;
	Clip* clip = nullptr;
	ModelStackWithThreeMainThings target;
	Clip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(void*, ParamManager*) { return &target; }
};
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Clip* active_clip = nullptr;
	SampleRecorder* recorder = nullptr;
	Clip* getActiveClip() const { return active_clip; }
	SampleRecorder* get_recorder() const { return recorder; }
};
struct Delay {
	struct State {
		int analog_saturation = 8;
	};
};
struct {
	int ticksLeftInCountIn = 0;
	bool isEitherClockActive() { return true; }
} playbackHandler;
struct GlobalEffectableForClip : Output {
	bool renderedLastTime = false;
	int postReverbVolumeLastTime = -1;
	uint32_t clippingAmount = 0;
	uint32_t lastSaturationTanHWorkingValue[2]{};
	struct {
		void registerHit(int) {}
		int render(size_t, int) { return 0; }
	} sidechain;
	struct {
		void setThreshold(int) {}
		void renderVolNeutral(std::span<StereoSample>, int) {}
		void reset() {}
	} compressor;
	Output* toOutput() { return this; }
	Delay::State createDelayWorkingState(ParamManager&, bool, bool) {
		stage(1);
		return {};
	}
	void setupFilterSetConfig(int*, ParamManager*) {}
	int getSidechainVolumeAmountAsPatchCableDepth(ParamManager*) { return 0; }
	bool renderGlobalEffectableForClip(ModelStackWithTimelineCounter*, std::span<StereoSample> audio, int32_t*,
	                                   int32_t*, int, int, bool, bool, int, int, int) {
		audio[0] = {11, 17};
		stage(2);
		return true;
	}
	int getShiftAmountForSaturation() { return 0; }
	int saturate(int value, uint32_t*, int) { return value; }
	void processFilters(std::span<StereoSample>) {}
	void processSRRAndBitcrushing(std::span<StereoSample>, int*, ParamManager*) {}
	void processFXForGlobalEffectable(std::span<StereoSample>, int*, ParamManager*, Delay::State&, bool, int) {
		stage(3);
	}
	void processStutter(std::span<StereoSample>, ParamManager*) { stage(4); }
	void processReverbSendAndVolume(std::span<StereoSample>, int32_t*, int, int, int, int, bool) {}
	void renderOutput(ModelStackWithTimelineCounter*, ParamManager*, std::span<StereoSample>, int32_t*, int, int, bool,
	                  bool, OutputType, SampleRecorder*);
};
#include "global_effectable_render_lifetime.inc"
} // namespace global_effectable_render_lifetime_test
using namespace global_effectable_render_lifetime_test;
TEST_GROUP(global_effectable_render_lifetime) {
	std::unique_ptr<GlobalEffectableForClip> effect;
	std::unique_ptr<Clip> clip;
	std::unique_ptr<SampleRecorder> recorder;
	ModelStackWithTimelineCounter stack;
	StereoSample audio[1];
	void reset() {
		on_stage = {};
		stages.clear();
		currentSong = &song;
		song.backup = nullptr;
		audio[0] = {};
		effect = std::make_unique<GlobalEffectableForClip>();
		clip = std::make_unique<Clip>();
		recorder = std::make_unique<SampleRecorder>();
		effect->active_clip = clip.get();
		effect->recorder = recorder.get();
		clip->output = effect.get();
		stack.clip = clip.get();
		stack.song = &song;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_stage = {};
		currentSong = &song;
		song.backup = nullptr;
	}
	void render() {
		effect->renderOutput(&stack, &clip->paramManager, audio, nullptr, 0, 0, false, true, OutputType::KIT,
		                     recorder.get());
	}
};
TEST(global_effectable_render_lifetime, live_render_mixes_audio_and_ticks_parameters) {
	render();
	LONGS_EQUAL(6, stages.size());
	LONGS_EQUAL(11, audio[0].l);
	LONGS_EQUAL(17, audio[0].r);
	CHECK(effect->renderedLastTime);
}
TEST(global_effectable_render_lifetime, each_callback_can_destroy_output) {
	for (int boundary = 1; boundary <= 6; ++boundary) {
		reset();
		on_stage = [&](int id) {
			if (id == boundary)
				effect.reset();
		};
		render();
		LONGS_EQUAL(boundary, stages.size());
		if (boundary < 6)
			LONGS_EQUAL(0, audio[0].l);
	}
}
TEST(global_effectable_render_lifetime, each_callback_can_destroy_clip_and_parameters) {
	for (int boundary = 1; boundary <= 6; ++boundary) {
		reset();
		on_stage = [&](int id) {
			if (id == boundary)
				clip.reset();
		};
		render();
		LONGS_EQUAL(boundary, stages.size());
	}
}
TEST(global_effectable_render_lifetime, render_callback_retarget_does_not_publish_flag) {
	Clip replacement;
	on_stage = [&](int id) {
		if (id == 2)
			effect->active_clip = &replacement;
	};
	render();
	CHECK_FALSE(effect->renderedLastTime);
	LONGS_EQUAL(2, stages.size());
	LONGS_EQUAL(0, audio[0].l);
}
TEST(global_effectable_render_lifetime, changed_collection_cancels_before_further_processing) {
	UnpatchedParamSet replacement;
	on_stage = [&](int id) {
		if (id == 1)
			clip->paramManager.summaries[0].paramCollection = &replacement;
	};
	render();
	LONGS_EQUAL(1, stages.size());
	LONGS_EQUAL(0, audio[0].l);
}
TEST(global_effectable_render_lifetime, recorder_detachment_cancels_before_using_old_recorder) {
	on_stage = [&](int id) {
		if (id == 3) {
			effect->recorder = nullptr;
			recorder.reset();
		}
	};
	render();
	LONGS_EQUAL(3, stages.size());
	LONGS_EQUAL(0, audio[0].l);
}
TEST(global_effectable_render_lifetime, recorder_can_detach_and_destroy_itself_during_feed) {
	on_stage = [&](int id) {
		if (id == 5) {
			effect->recorder = nullptr;
			recorder.reset();
		}
	};
	render();
	LONGS_EQUAL(5, stages.size());
	LONGS_EQUAL(0, audio[0].l);
}
TEST(global_effectable_render_lifetime, live_backup_manager_can_render_without_clip) {
	ParamManager backup;
	song.backup = &backup;
	effect->active_clip = nullptr;
	stack.clip = nullptr;
	effect->renderOutput(&stack, &backup, audio, nullptr, 0, 0, false, true, OutputType::AUDIO, recorder.get());
	LONGS_EQUAL(6, stages.size());
	LONGS_EQUAL(11, audio[0].l);
}
TEST(global_effectable_render_lifetime, backup_deletion_cancels_without_reading_freed_manager) {
	auto backup = std::make_unique<ParamManager>();
	song.backup = backup.get();
	effect->active_clip = nullptr;
	stack.clip = nullptr;
	on_stage = [&](int id) {
		if (id == 1) {
			song.backup = nullptr;
			backup.reset();
		}
	};
	effect->renderOutput(&stack, backup.get(), audio, nullptr, 0, 0, false, true, OutputType::AUDIO, recorder.get());
	LONGS_EQUAL(1, stages.size());
	LONGS_EQUAL(0, audio[0].l);
}
TEST(global_effectable_render_lifetime, retired_entry_does_not_process) {
	effect->lifetime.retire();
	render();
	LONGS_EQUAL(0, stages.size());
}
