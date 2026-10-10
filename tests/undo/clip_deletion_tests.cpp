#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_navigation_state.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace clip_deletion_test {
struct Song;
struct Output {};
static std::function<void(int)> on_stage;
static std::vector<int> visited;
static void run_stage(int stage) {
	visited.push_back(stage);
	auto callback = on_stage;
	if (callback)
		callback(stage);
}
struct Clip {
	Output* output = nullptr;
	ArmState armState = ArmState::OFF;
	bool soloingInSessionMode = true;
	void stopAllNotesPlaying(Song*) { run_stage(1); }
	void abortRecording() { run_stage(3); }
};
struct ClipArray {
	std::vector<Clip*> values;
};
struct Song {
	ClipArray sessionClips, arrangementOnlyClips;
	Output* registered_output = nullptr;
	int32_t get_clip_index_for_undo(ClipArray* array, Clip* target) {
		if (array != &sessionClips && array != &arrangementOnlyClips)
			return -1;
		auto found = std::find(array->values.begin(), array->values.end(), target);
		return found == array->values.end() ? -1 : found - array->values.begin();
	}
	bool owns_output_for_undo(Output* target) { return target && target == registered_output; }
	void invalidate_clip_selection(Clip*) { run_stage(0); }
};
static Song* currentSong = nullptr;
struct ModelStackWithTimelineCounter {
	Song* song = nullptr;
	Clip* target = nullptr;
	Clip* getTimelineCounterAllowNull() { return target; }
};
static struct {
	bool deletingClipWhichCouldBeAbandonedOverdub(Clip*) {
		run_stage(2);
		return true;
	}
	void unsoloClip(Clip*) { run_stage(4); }
} session;
struct ConsequenceClipExistence {
	Clip* clip = nullptr;
	ClipArray* clipArray = nullptr;
	int32_t clipIndex = -1;
	bool shouldBeActiveWhileExistent = false;
	Error prepare_for_deletion(ModelStackWithTimelineCounter* model_stack);
};
#include "clip_delete_preparation.inc"

TEST_GROUP(ClipDeletionPreparation) {
	Song song;
	Output output;
	Clip clip;
	ModelStackWithTimelineCounter stack;
	ConsequenceClipExistence consequence;
	void setup() override {
		currentSong = &song;
		song.registered_output = &output;
		clip.output = &output;
		song.sessionClips.values = {&clip};
		stack = {&song, &clip};
		consequence.clip = &clip;
		consequence.clipArray = &song.sessionClips;
		on_stage = {};
		visited.clear();
	}
	void teardown() override {
		on_stage = {};
		visited.clear();
		currentSong = nullptr;
	}
};

TEST(ClipDeletionPreparation, successful_preparation_reacquires_index_without_removing_clip) {
	Clip other;
	on_stage = [&](int stage) {
		if (stage == 4)
			song.sessionClips.values.insert(song.sessionClips.values.begin(), &other);
	};
	CHECK(consequence.prepare_for_deletion(&stack) == Error::NONE);
	LONGS_EQUAL(5, visited.size());
	LONGS_EQUAL(1, consequence.clipIndex);
	CHECK(consequence.shouldBeActiveWhileExistent);
	POINTERS_EQUAL(&clip, song.sessionClips.values[1]);
}

TEST(ClipDeletionPreparation, song_change_at_each_callback_stops_before_next_access) {
	for (int boundary = 0; boundary < 5; ++boundary) {
		currentSong = &song;
		visited.clear();
		on_stage = [&](int stage) {
			if (stage == boundary)
				currentSong = nullptr;
		};
		CHECK(consequence.prepare_for_deletion(&stack) == Error::BUG);
		LONGS_EQUAL(boundary + 1, visited.size());
		POINTERS_EQUAL(&clip, song.sessionClips.values[0]);
	}
}

TEST(ClipDeletionPreparation, removed_and_destroyed_clip_is_not_accessed_after_any_callback) {
	for (int boundary = 0; boundary < 5; ++boundary) {
		auto target = std::make_unique<Clip>();
		target->output = &output;
		stack.target = target.get();
		consequence.clip = target.get();
		song.sessionClips.values = {target.get()};
		visited.clear();
		on_stage = [&](int stage) {
			if (stage == boundary) {
				song.sessionClips.values.clear();
				target.reset();
			}
		};
		CHECK(consequence.prepare_for_deletion(&stack) == Error::BUG);
		LONGS_EQUAL(boundary + 1, visited.size());
		CHECK_FALSE(target);
	}
}

TEST(ClipDeletionPreparation, changed_stack_output_and_peer_revision_stop_preparation) {
	Song other_song;
	Clip other_clip;
	Output other_output;
	for (int change = 0; change < 6; ++change) {
		stack = {&song, &clip};
		clip.output = &output;
		song.registered_output = &output;
		visited.clear();
		on_stage = [&](int stage) {
			if (stage != 1)
				return;
			switch (change) {
			case 0:
				stack.song = &other_song;
				break;
			case 1:
				stack.target = &other_clip;
				break;
			case 2:
				clip.output = &other_output;
				break;
			case 3:
				song.registered_output = nullptr;
				break;
			case 4:
				deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local)
				    .structural_refresh.request();
				break;
			case 5:
				deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
				    .structural_refresh.request();
				break;
			}
		};
		CHECK(consequence.prepare_for_deletion(&stack) == Error::BUG);
		LONGS_EQUAL(2, visited.size());
	}
}

TEST(ClipDeletionPreparation, changed_owner_stops_but_returned_nested_scope_is_valid) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	on_stage = [&](int stage) {
		if (stage == 1)
			remote.emplace(Id::Remote);
	};
	CHECK(consequence.prepare_for_deletion(&stack) == Error::BUG);
	LONGS_EQUAL(2, visited.size());
	remote.reset();
	visited.clear();
	on_stage = [](int) { Scope temporary(Id::Remote); };
	CHECK(consequence.prepare_for_deletion(&stack) == Error::NONE);
	LONGS_EQUAL(5, visited.size());
}

TEST(ClipDeletionPreparation, arrangement_clip_skips_session_unsolo) {
	song.sessionClips.values.clear();
	song.arrangementOnlyClips.values = {&clip};
	consequence.clipArray = &song.arrangementOnlyClips;
	CHECK(consequence.prepare_for_deletion(&stack) == Error::NONE);
	LONGS_EQUAL(4, visited.size());
	LONGS_EQUAL(0, consequence.clipIndex);
}
} // namespace clip_deletion_test
