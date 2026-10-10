#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_navigation_state.h"
#include <functional>
#include <memory>

namespace song_cleanup_test {
struct Song;
struct Output;
struct Clip {
	Output* output = nullptr;
	bool isArrangementOnlyClip() { return true; }
};
struct ModelStack {
	Song* song;
};
constexpr int MODEL_STACK_MAX_SIZE = sizeof(ModelStack);
struct Output {
	int picks = 0;
	void pickAnActiveClipIfPossible(ModelStack*, bool) { ++picks; }
};
struct ClipArray {
	Clip* clip = nullptr;
	int getIndexForClip(Clip* target) { return clip == target ? 0 : -1; }
	void deleteAtIndex(int) { clip = nullptr; }
};
struct Action {
	uint64_t action_identity = 1;
	std::function<bool()> record;
	bool recordClipExistenceChange(Song*, ClipArray*, Clip*, ExistenceChangeType) {
		auto callback = record;
		return callback();
	}
};
struct {
	Action* firstAction[2]{};
	int deletions = 0;
	void deleteAllLogs() { ++deletions; }
} actionLogger;
struct Song {
	ClipArray arrangementOnlyClips;
	Output* output = nullptr;
	int clip_deletions = 0;
	bool contains_clip_for_undo(Clip* clip) { return arrangementOnlyClips.clip == clip; }
	bool owns_output_for_undo(Output* candidate) { return output == candidate; }
	void deleteClipObject(Clip*, bool, InstrumentRemoval) { ++clip_deletions; }
	Error deletingClipInstanceForClip(Output*, Clip*, Action*, bool, bool);
};
static Song* currentSong;
static ModelStack* setupModelStackWithSong(char* memory, Song* song) {
	return new (memory) ModelStack{song};
}
#include "song_cleanup_method.inc"
TEST_GROUP(SongCleanup) {
	Song song;
	Output output;
	Clip clip;
	Action action;
	void setup() override {
		currentSong = &song;
		song.output = &output;
		clip.output = &output;
		song.arrangementOnlyClips.clip = &clip;
		action.record = [] { return false; };
		actionLogger.firstAction[BEFORE] = &action;
		actionLogger.deletions = 0;
	}
	void teardown() override {
		currentSong = nullptr;
		actionLogger.firstAction[BEFORE] = nullptr;
	}
	void check_retained() {
		LONGS_EQUAL(0, actionLogger.deletions);
		LONGS_EQUAL(0, song.clip_deletions);
		LONGS_EQUAL(0, output.picks);
	}
};
TEST(SongCleanup, valid_failure_preserves_history_when_requested) {
	CHECK(song.deletingClipInstanceForClip(&output, &clip, &action, true, true) == Error::INSUFFICIENT_RAM);
	check_retained();
	POINTERS_EQUAL(&clip, song.arrangementOnlyClips.clip);
}
TEST(SongCleanup, valid_failure_still_allows_requested_destructive_fallback) {
	CHECK(song.deletingClipInstanceForClip(&output, &clip, &action, true, false) == Error::NONE);
	LONGS_EQUAL(1, actionLogger.deletions);
	LONGS_EQUAL(1, song.clip_deletions);
	LONGS_EQUAL(1, output.picks);
}
TEST(SongCleanup, changed_song_prevents_destructive_fallback) {
	action.record = [&] {
		currentSong = nullptr;
		return false;
	};
	CHECK(song.deletingClipInstanceForClip(&output, &clip, &action, true, false) == Error::BUG);
	check_retained();
}
TEST(SongCleanup, destroyed_registered_action_prevents_destructive_fallback) {
	auto target = std::make_unique<Action>();
	actionLogger.firstAction[BEFORE] = target.get();
	target->record = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		target.reset();
		return false;
	};
	CHECK(song.deletingClipInstanceForClip(&output, &clip, target.get(), true, false) == Error::BUG);
	check_retained();
}
TEST(SongCleanup, removed_clip_or_output_prevents_destructive_fallback) {
	for (bool remove_clip : {false, true}) {
		song.output = &output;
		song.arrangementOnlyClips.clip = &clip;
		action.record = [&] {
			if (remove_clip)
				song.arrangementOnlyClips.clip = nullptr;
			else
				song.output = nullptr;
			return false;
		};
		CHECK(song.deletingClipInstanceForClip(&output, &clip, &action, true, false) == Error::BUG);
		check_retained();
	}
}
TEST(SongCleanup, peer_structural_change_prevents_destructive_fallback) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		action.record = [owner] {
			navigation.for_owner(owner).structural_refresh.request();
			return false;
		};
		CHECK(song.deletingClipInstanceForClip(&output, &clip, &action, true, false) == Error::BUG);
		check_retained();
	}
}
} // namespace song_cleanup_test
