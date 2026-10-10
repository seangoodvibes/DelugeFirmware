#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
namespace kit_render_output_lifetime_test {
int song;
int* currentSong = &song;
constexpr int kMaxSampleValue = 2147483647;
namespace params {
constexpr int UNPATCHED_PITCH_ADJUST = 0;
}
int getFinalParameterValueExp(int, int value) {
	return value;
}
struct StereoSample {};
enum class OutputType { KIT };
enum class StemExportType { DRUM };
struct {
	bool processStarted = false, includeKitFX = true;
	StemExportType currentStemExportType = StemExportType::DRUM;
} stemExport;
struct UnpatchedParamSet {
	int getValue(int) { return 0; }
} unpatched;
struct ParamManager {
	UnpatchedParamSet* getUnpatchedParamSet() { return &unpatched; }
} manager;
struct Kit;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* output = nullptr;
};
struct ModelStackWithTimelineCounter {
	int* song = &kit_render_output_lifetime_test::song;
	Clip* clip = nullptr;
	Clip* getTimelineCounterAllowNull() { return clip; }
};
struct ModelStack {
	int* song = &kit_render_output_lifetime_test::song;
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip* clip) {
		timeline.clip = clip;
		timeline.song = song;
		return &timeline;
	}
};
std::function<void()> on_pre, on_audio, on_post;
int pre_calls = 0, audio_calls = 0, post_calls = 0;
struct GlobalEffectableForClip {
	bool renderedLastTime = false;
	template <class... Args>
	void renderOutput(Args&&...) {
		++audio_calls;
		if (on_audio)
			on_audio();
	}
};
struct Kit : GlobalEffectableForClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Clip* activeClip = nullptr;
	void* recorder = nullptr;
	ParamManager* getParamManager(int*) { return &manager; }
	void setupAndRenderArpPreOutput(ModelStackWithTimelineCounter*, ParamManager*, std::span<StereoSample>) {
		++pre_calls;
		if (on_pre)
			on_pre();
	}
	template <class... Args>
	bool renderGlobalEffectableForClip(Args&&...) {
		++audio_calls;
		if (on_audio)
			on_audio();
		return true;
	}
	void renderNonAudioArpPostOutput(std::span<StereoSample>) {
		++post_calls;
		if (on_post)
			on_post();
	}
	void renderOutput(ModelStack*, std::span<StereoSample>, int32_t*, int32_t, int32_t, bool, bool);
};
#include "kit_render_output_lifetime.inc"
} // namespace kit_render_output_lifetime_test
using namespace kit_render_output_lifetime_test;
TEST_GROUP(kit_render_output_lifetime) {
	std::unique_ptr<Kit> kit;
	std::unique_ptr<Clip> clip;
	ModelStack stack;
	void reset(bool stems = false) {
		kit = std::make_unique<Kit>();
		clip = std::make_unique<Clip>();
		kit->activeClip = clip.get();
		clip->output = kit.get();
		on_pre = {};
		on_audio = {};
		on_post = {};
		pre_calls = audio_calls = post_calls = 0;
		stemExport.processStarted = stems;
		stemExport.includeKitFX = !stems;
		currentSong = &song;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_pre = {};
		on_audio = {};
		on_post = {};
		currentSong = &song;
	}
	void render() {
		StereoSample output[1];
		kit->renderOutput(&stack, output, nullptr, 0, 0, false, true);
	}
};
TEST(kit_render_output_lifetime, live_effects_and_stem_paths_reach_post_arp) {
	for (bool stems : {false, true}) {
		reset(stems);
		render();
		LONGS_EQUAL(1, pre_calls);
		LONGS_EQUAL(1, audio_calls);
		LONGS_EQUAL(1, post_calls);
		if (stems)
			CHECK(kit->renderedLastTime);
	}
}
TEST(kit_render_output_lifetime, pre_arp_can_destroy_owners) {
	on_pre = [&] {
		clip.reset();
		kit.reset();
	};
	render();
	LONGS_EQUAL(0, audio_calls);
	LONGS_EQUAL(0, post_calls);
}
TEST(kit_render_output_lifetime, audio_callbacks_can_destroy_owners) {
	for (bool stems : {false, true}) {
		reset(stems);
		on_audio = [&] {
			clip.reset();
			kit.reset();
		};
		render();
		LONGS_EQUAL(1, audio_calls);
		LONGS_EQUAL(0, post_calls);
	}
}
TEST(kit_render_output_lifetime, stem_callback_retarget_does_not_publish_render_flag) {
	reset(true);
	Clip replacement;
	on_audio = [&] { kit->activeClip = &replacement; };
	render();
	CHECK_FALSE(kit->renderedLastTime);
	LONGS_EQUAL(0, post_calls);
}
TEST(kit_render_output_lifetime, pre_arp_preserves_model_stack_retarget) {
	Clip replacement;
	on_pre = [&] { stack.timeline.clip = &replacement; };
	render();
	LONGS_EQUAL(0, audio_calls);
	POINTERS_EQUAL(&replacement, stack.timeline.clip);
}
TEST(kit_render_output_lifetime, pre_arp_can_delete_only_clip) {
	on_pre = [&] { clip.reset(); };
	render();
	LONGS_EQUAL(0, audio_calls);
	LONGS_EQUAL(0, post_calls);
}
TEST(kit_render_output_lifetime, post_arp_can_destroy_owners) {
	on_post = [&] {
		clip.reset();
		kit.reset();
	};
	render();
	LONGS_EQUAL(1, post_calls);
}
TEST(kit_render_output_lifetime, no_active_clip_renders_live_backup_context) {
	kit->activeClip = nullptr;
	render();
	LONGS_EQUAL(1, audio_calls);
	LONGS_EQUAL(1, post_calls);
}
TEST(kit_render_output_lifetime, retired_or_mismatched_context_does_not_render) {
	for (int target = 0; target < 3; ++target) {
		reset();
		if (target == 0)
			kit->lifetime.retire();
		if (target == 1)
			clip->lifetime.retire();
		if (target == 2)
			clip->output = nullptr;
		render();
		LONGS_EQUAL(0, pre_calls);
		LONGS_EQUAL(0, audio_calls);
	}
}
TEST(kit_render_output_lifetime, pre_arp_rejects_song_change) {
	int replacement_song;
	on_pre = [&] { currentSong = &replacement_song; };
	render();
	currentSong = &song;
	LONGS_EQUAL(0, audio_calls);
	LONGS_EQUAL(0, post_calls);
}
TEST(kit_render_output_lifetime, recorder_replacement_stops_post_arp) {
	int replacement;
	on_audio = [&] { kit->recorder = &replacement; };
	render();
	LONGS_EQUAL(1, audio_calls);
	LONGS_EQUAL(0, post_calls);
}
