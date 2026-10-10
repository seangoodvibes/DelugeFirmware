#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "util/lifetime.h"
#include <functional>
#include <optional>
namespace clip_destruction_lifetime_test {
struct Clip;
struct Output {};
static std::function<void()> on_prepare, on_cleanup, on_selection;
struct song_fixture {
	int invalidations = 0;
	void invalidate_clip_selection(Clip*) {
		++invalidations;
		if (on_selection)
			on_selection();
	}
	void deleteBackedUpParamManagersForClip(Clip*) {
		if (on_prepare)
			on_prepare();
	}
	void deleteOrHibernateOutputIfNoClips(Output*) {}
	void deleteOutputThatIsInMainList(Output*) {}
};
static song_fixture song;
static song_fixture* currentSong = &song;
struct ModelStackWithTimelineCounter {
	song_fixture* song = nullptr;
};
static struct {
	bool isEitherClockActive() { return false; }
} playbackHandler;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	Output* output = nullptr;
	virtual ~Clip();
	void retire_lifetime() { lifetime_source.retire(); }
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	bool isActiveOnOutput() { return false; }
	void expectNoFurtherTicks(song_fixture*, bool = true) {}
	void detachFromOutput(ModelStackWithTimelineCounter*, bool) { output = nullptr; }
	void prepareForDestruction(ModelStackWithTimelineCounter*, InstrumentRemoval);
};
struct InstrumentClip : Clip {
	~InstrumentClip() override;
	void deleteBackedUpParamManagerMIDI() {
		if (on_cleanup)
			on_cleanup();
	}
};
struct AudioClip : Clip {
	void* recorder = nullptr;
	~AudioClip() override;
};
static void freezeWithError(const char*) {
	FAIL("Unexpected active recorder during destruction");
}
#include "audio_destruction_lifetime.inc"
#include "clip_destruction_lifetime.inc"
#include "instrument_destruction_lifetime.inc"
} // namespace clip_destruction_lifetime_test
using namespace clip_destruction_lifetime_test;
TEST_GROUP(ClipDestructionLifetime){void setup() override{on_prepare = on_cleanup = on_selection = {};
song = {};
currentSong = &song;
}
void teardown() override {
	on_prepare = on_cleanup = on_selection = {};
}
}
;
TEST(ClipDestructionLifetime, preparation_retires_before_yielding_cleanup) {
	Clip clip;
	auto watch = clip.watch_lifetime();
	ModelStackWithTimelineCounter stack{&song};
	on_prepare = [&] {
		CHECK_FALSE(watch.alive());
		auto late = clip.watch_lifetime();
		CHECK_FALSE(late.alive());
	};
	clip.prepareForDestruction(&stack, InstrumentRemoval::NONE);
	CHECK_FALSE(watch.alive());
}
TEST(ClipDestructionLifetime, instrument_retires_before_parameter_cleanup) {
	std::optional<InstrumentClip> clip(std::in_place);
	auto watch = clip->watch_lifetime();
	on_cleanup = [&] {
		CHECK_FALSE(watch.alive());
		auto late = clip->watch_lifetime();
		CHECK_FALSE(late.alive());
	};
	clip.reset();
	CHECK_FALSE(watch.alive());
	LONGS_EQUAL(1, song.invalidations);
}
TEST(ClipDestructionLifetime, direct_base_destruction_retires_before_selection_cleanup) {
	std::optional<Clip> clip(std::in_place);
	auto watch = clip->watch_lifetime();
	on_selection = [&] { CHECK_FALSE(watch.alive()); };
	clip.reset();
	CHECK_FALSE(watch.alive());
	LONGS_EQUAL(1, song.invalidations);
}
TEST(ClipDestructionLifetime, audio_destruction_without_current_song_still_retires) {
	std::optional<AudioClip> clip(std::in_place);
	auto watch = clip->watch_lifetime();
	currentSong = nullptr;
	clip.reset();
	CHECK_FALSE(watch.alive());
	LONGS_EQUAL(0, song.invalidations);
}
