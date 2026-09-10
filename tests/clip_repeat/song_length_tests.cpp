#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/clip/clip_length_resync.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace song_length_tests {
struct Clip;
struct Song;
struct ModelStackWithTimelineCounter {
	Song* song = nullptr;
	Clip* clip = nullptr;
	Clip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithTimelineCounter* addTimelineCounter(Clip* target) {
		clip = target;
		return this;
	}
};
static ModelStackWithTimelineCounter stack;
constexpr int MODEL_STACK_MAX_SIZE = 64;
ModelStackWithTimelineCounter* setupModelStackWithSong(char*, Song* owner) {
	stack = {};
	stack.song = owner;
	return &stack;
}
static std::vector<int> calls;
static std::function<void(int)> callback;
void visit(int stage) {
	calls.push_back(stage);
	if (callback)
		callback(stage);
}
struct Action {
	bool record_succeeds = true;
	bool recordClipLengthChange(Clip*, uint32_t) {
		visit(1);
		return record_succeeds;
	}
};
struct Output {
	void clipLengthChanged(Clip*, uint32_t) { visit(3); }
};
struct Clip {
	int32_t loopLength = 96;
	Output* output;
	int type = 0;
	void lengthChanged(ModelStackWithTimelineCounter*, uint32_t, Action*) { visit(2); }
	void resumePlayback(ModelStackWithTimelineCounter*) { visit(5); }
};
struct Song {
	bool registered = true;
	Clip* syncScalingClip = nullptr;

	bool contains_clip_for_undo(Clip*) { return registered; }
	uint32_t getInputTickScale() { return 1; }
	void inputTickScalePotentiallyJustChanged(uint32_t) { visit(0); }
	bool isClipActive(Clip*) { return true; }
	bool setClipLength(Clip*, uint32_t, Action*, bool = true);
};
static Song* currentSong;
static struct Playback {
	bool isEitherClockActive() { return true; }
	void expectEvent() { visit(4); }
} playbackHandler;
static struct Mode {
	void reSyncClip(ModelStackWithTimelineCounter*, bool, bool) { visit(4); }
} mode;
static Mode* currentPlaybackMode = &mode;
#include "song_length_method.inc"

TEST_GROUP(SongLengthCallbacks) {
	Song song;
	Output output;
	Action action;
	void setup() override {
		currentSong = &song;
		callback = {};
		calls.clear();
	}
	void teardown() override {
		callback = {};
		currentSong = nullptr;
	}
};
TEST(SongLengthCallbacks, registered_target_deleted_at_each_callback_stops_following_stages) {
	for (int stage = 0; stage < 6; ++stage) {
		calls.clear();
		song.registered = true;
		auto* clip = new Clip{96, &output};
		song.syncScalingClip = clip;
		callback = [&](int current_stage) {
			if (current_stage == stage) {
				song.registered = false;
				delete clip;
			}
		};
		CHECK_FALSE(song.setClipLength(clip, 48, &action));
		callback = {};
		LONGS_EQUAL(stage + 1, calls.size());
	}
}
TEST(SongLengthCallbacks, changed_length_stops_after_callback_and_unpublished_success_is_preserved) {
	Clip clip{96, &output};
	callback = [&](int stage) {
		if (stage == 2)
			clip.loopLength = 24;
	};
	CHECK_FALSE(song.setClipLength(&clip, 48, &action));
	LONGS_EQUAL(2, calls.size());
	callback = {};
	calls.clear();
	clip.loopLength = 96;
	song.registered = false;
	CHECK_TRUE(song.setClipLength(&clip, 48, &action));
	LONGS_EQUAL(5, calls.size());
	LONGS_EQUAL(48, clip.loopLength);
}
TEST(SongLengthCallbacks, invalid_lengths_do_not_dispatch) {
	Clip clip{96, &output};
	for (uint32_t length : {0u, 0x80000000u, 0xffffffffu})
		CHECK_FALSE(song.setClipLength(&clip, length, &action));
	CHECK_TRUE(calls.empty());
	LONGS_EQUAL(96, clip.loopLength);
}
TEST(SongLengthCallbacks, redirected_model_stack_stops_after_trim_or_resync) {
	Song replacement_song;
	Clip replacement_clip{96, &output};
	for (int stage : {2, 4}) {
		for (bool change_song : {false, true}) {
			Clip clip{96, &output};
			calls.clear();
			callback = [&](int current_stage) {
				if (current_stage == stage) {
					if (change_song)
						stack.song = &replacement_song;
					else
						stack.clip = &replacement_clip;
				}
			};
			CHECK_FALSE(song.setClipLength(&clip, 48, &action));
			LONGS_EQUAL(stage, calls.size());
			LONGS_EQUAL(stage, calls.back());
		}
	}
}
TEST(SongLengthCallbacks, changed_ui_owner_stops_at_each_callback_boundary) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		deluge::model::clip_length_resync_allowed() = true;
		for (int stage = 0; stage < 6; ++stage) {
			Clip clip{96, &output};
			song.syncScalingClip = &clip;
			calls.clear();
			callback = [&](int current_stage) {
				if (current_stage == stage)
					detail::active = owner == Id::Local ? Id::Remote : Id::Local;
			};
			const bool result = song.setClipLength(&clip, 48, &action);
			detail::active = owner;
			CHECK_FALSE(result);
			LONGS_EQUAL(stage + 1, calls.size());
		}
	}
}
TEST(SongLengthCallbacks, properly_scoped_nested_service_preserves_resize_owner) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		deluge::model::clip_length_resync_allowed() = true;
		Clip clip{96, &output};
		song.syncScalingClip = &clip;
		calls.clear();
		callback = [&](int) {
			Scope nested(owner == Id::Local ? Id::Remote : Id::Local);
			CHECK_TRUE(current() != owner);
		};
		CHECK_TRUE(song.setClipLength(&clip, 48, &action));
		CHECK_TRUE(current() == owner);
		LONGS_EQUAL(6, calls.size());
	}
}
TEST(SongLengthCallbacks, failed_undo_recording_stops_before_trim_output_and_playback) {
	for (bool scaling_clip : {false, true}) {
		for (bool shrink : {false, true}) {
			Clip clip{96, &output};
			song.syncScalingClip = scaling_clip ? &clip : nullptr;
			action.record_succeeds = false;
			calls.clear();
			CHECK_FALSE(song.setClipLength(&clip, shrink ? 48 : 192, &action));
			LONGS_EQUAL(scaling_clip ? 2 : 1, calls.size());
			LONGS_EQUAL(1, calls.back());
			LONGS_EQUAL(shrink ? 48 : 192, clip.loopLength);
		}
	}
}
} // namespace song_length_tests
