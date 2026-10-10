#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <functional>
namespace clip_activation_test {
namespace session = deluge::gui::ui_session;
struct Clip;
struct ModelStack {
	Clip* clip = nullptr;
	ModelStack* addTimelineCounter(Clip* value) {
		clip = value;
		return this;
	}
};
static std::function<void()> on_available, on_activate;
struct Output {
	Clip* active_clip = nullptr;
	int calls = 0;
	bool activate = true;
	void setActiveClip(ModelStack* stack) {
		++calls;
		if (activate)
			active_clip = stack->clip;
		if (on_activate)
			on_activate();
	}
};
struct Clip {
	ClipType type = ClipType::INSTRUMENT;
	Output* output = nullptr;
	bool isActiveOnOutput() { return output && output->active_clip == this; }
};
static session::State<Clip*> clips;
static Clip* getCurrentClip() {
	return clips.active();
}
static int song;
static int* currentSong = &song;
struct playback_fixture {
	bool available = true;
	bool isOutputAvailable(Output*) {
		if (on_available)
			on_available();
		return available;
	}
};
static playback_fixture playback;
static playback_fixture* currentPlaybackMode = &playback;
struct InstrumentClipMinder {
	static bool makeCurrentClipActiveOnInstrumentIfPossible(ModelStack*);
};
#include "clip_activation.inc"
} // namespace clip_activation_test
using namespace clip_activation_test;
TEST_GROUP(ClipActivation) {
	Clip clip;
	Output output;
	ModelStack stack;
	void setup() override {
		session::detail::active = session::Id::Local;
		clips = {};
		clips.active() = &clip;
		clip.output = &output;
		currentSong = &song;
		playback = {};
		currentPlaybackMode = &playback;
		on_available = on_activate = {};
	}
	void teardown() override {
		on_available = on_activate = {};
		session::detail::active = session::Id::Local;
	}
};
TEST(ClipActivation, availability_context_change_prevents_activation) {
	on_available = [] { currentSong = nullptr; };
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	LONGS_EQUAL(0, output.calls);
}
TEST(ClipActivation, activation_must_succeed_and_keep_context) {
	output.activate = false;
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	output.activate = true;
	on_activate = [] { currentSong = nullptr; };
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
}
TEST(ClipActivation, availability_owner_change_restores_source_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		clips.active() = &clip;
		on_available = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, output.calls);
	}
}
TEST(ClipActivation, normal_activation_and_already_active_do_not_repeat_work) {
	CHECK(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	CHECK(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	LONGS_EQUAL(1, output.calls);
}
TEST(ClipActivation, missing_inputs_and_unavailable_output_do_not_activate) {
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(nullptr));
	clips.active() = nullptr;
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	clips.active() = &clip;
	clip.output = nullptr;
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	clip.output = &output;
	clip.type = ClipType::AUDIO;
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	clip.type = ClipType::INSTRUMENT;
	currentPlaybackMode = nullptr;
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	currentPlaybackMode = &playback;
	playback.available = false;
	CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
	LONGS_EQUAL(0, output.calls);
}

TEST(ClipActivation, changed_selection_output_or_playback_cancels_completion) {
	Clip replacement_clip;
	Output replacement_output;
	playback_fixture replacement_playback;
	for (bool during_activation : {false, true}) {
		for (int change = 0; change < 3; ++change) {
			clips.active() = &clip;
			clip.output = &output;
			output.active_clip = nullptr;
			output.calls = 0;
			currentPlaybackMode = &playback;
			on_available = on_activate = {};
			auto change_context = [&] {
				if (change == 0)
					clips.active() = &replacement_clip;
				if (change == 1)
					clip.output = &replacement_output;
				if (change == 2)
					currentPlaybackMode = &replacement_playback;
			};
			if (during_activation)
				on_activate = change_context;
			else
				on_available = change_context;
			CHECK_FALSE(InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(&stack));
			LONGS_EQUAL(during_activation ? 1 : 0, output.calls);
		}
	}
}
