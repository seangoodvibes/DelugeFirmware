#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/clip/clip_length_resync.h"
#include <cstdint>
#include <functional>
namespace clip_view_length_tests {
constexpr int BEFORE = 0;
constexpr int MODEL_STACK_MAX_SIZE = 64;
constexpr int kMaxSequenceLength = 1000;
enum class ActionType { CLIP_LENGTH_DECREASE, CLIP_LENGTH_INCREASE, PATTERN_PASTE };
enum class ActionAddition { ALLOWED, NOT_ALLOWED };
struct Clip {
	int32_t loopLength = 96;
	void* output = nullptr;
	int type = 0;
};
struct ModelStackWithTimelineCounter {};
struct Action {
	deluge::gui::ui_session::Id navigation_owner = deluge::gui::ui_session::Id::Local;
	void* captured_song = nullptr;
	void* captured_output = nullptr;
	bool openForAdditions = true;
	ActionType type = ActionType::CLIP_LENGTH_DECREASE;
	Clip* currentClip = nullptr;
};
static Clip clip;
static Clip* selected_clip = &clip;
struct Song {
	bool success = true;
	std::function<void()> on_resize;
	bool contains_clip_for_undo(Clip* target) { return target == &clip; }
	bool setClipLength(Clip* target, int32_t length, Action*) {
		target->loopLength = length;
		if (on_resize)
			on_resize();
		return success;
	}
	ModelStackWithTimelineCounter* setupModelStackWithCurrentClip(char*) { return nullptr; }
};
static Song* currentSong;
static struct Logger {
	Action* firstAction[1]{};
	Action* next = nullptr;
	bool revert_success = false;
	std::function<void()> on_revert, on_allocate;
	bool revert(int, bool, bool) {
		if (on_revert)
			on_revert();
		return revert_success;
	}
	Action* getNewAction(ActionType, ActionAddition) {
		Action* result = next;
		firstAction[BEFORE] = result;
		if (on_allocate)
			on_allocate();
		return result;
	}
} actionLogger;

static struct Playback {
	bool active = false;
	bool isEitherClockActive() { return active; }
} playbackHandler;
static struct Mode {
	int resyncs = 0;
	std::function<void()> on_resync;
	void reSyncClip(ModelStackWithTimelineCounter*) {
		++resyncs;
		if (on_resync)
			on_resync();
	}
} mode;
static Mode* currentPlaybackMode = &mode;
static int renders = 0;
void* getRootUI() {
	return nullptr;
}
void uiNeedsRendering(void*, uint32_t, int) {
	++renders;
}
struct ClipView {
	int navigation_calls = 0;
	int selection_reads = 0;
	int square_position = 96, extend_amount = 24, chop_amount = 24;
	Clip* getCurrentClip() {
		++selection_reads;
		return selected_clip;
	}
	bool lengthenClip(int32_t, Action*&);
	bool shortenClip(int32_t, Action*&);
	uint32_t changeClipLength(int32_t, uint32_t, Action*&);
	int getSquareFromPos(uint32_t, bool* exact) {
		*exact = true;
		return 1;
	}
	int getPosFromSquare(int) { return square_position; }
	int getLengthExtendAmount(int) { return extend_amount; }
	int getLengthChopAmount(int) { return chop_amount; }
	bool scrollRightToEndOfLengthIfNecessary(uint32_t) {
		++navigation_calls;
		return false;
	}
	bool scrollLeftIfTooFarRight(uint32_t) {
		++navigation_calls;
		return false;
	}
	bool zoomToMax(bool) {
		++navigation_calls;
		return false;
	}
};
#include "clip_view_length_methods.inc"
TEST_GROUP(ClipViewLengthResult) {
	Song song;
	int output = 0;
	ClipView view;
	Action saved_action;
	void setup() override {
		currentSong = &song;
		deluge::model::clip_length_resync_allowed() = true;
		playbackHandler = {};
		mode = {};
		selected_clip = &clip;
		clip = {};
		clip.output = &output;
		actionLogger = {};
		renders = 0;
		saved_action.currentClip = &clip;
		saved_action.captured_song = &song;
		saved_action.captured_output = clip.output;
		saved_action.navigation_owner = deluge::gui::ui_session::current();
	}
	void teardown() override {
		currentSong = nullptr;
	}
};
TEST(ClipViewLengthResult, failed_resize_clears_action_and_skips_navigation_and_rendering) {
	for (int direction : {-1, 1}) {
		clip.loopLength = 96;
		song.success = false;
		actionLogger.next = &saved_action;
		Action* action = nullptr;
		LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
		CHECK_TRUE(action == nullptr);
		LONGS_EQUAL(0, view.navigation_calls);
		LONGS_EQUAL(0, renders);
	}
}
TEST(ClipViewLengthResult, successful_resize_without_undo_action_is_not_failure) {
	for (int direction : {-1, 1}) {
		clip.loopLength = 96;
		Action* action = nullptr;
		LONGS_EQUAL(direction > 0 ? 120 : 72, view.changeClipLength(direction, 96, action));
		CHECK_TRUE(action == nullptr);
	}
	LONGS_EQUAL(2, renders);
}
TEST(ClipViewLengthResult, failed_implicit_undo_stops_before_resize_and_restores_resync) {
	saved_action.navigation_owner = deluge::gui::ui_session::current();
	saved_action.captured_song = &song;
	saved_action.captured_output = clip.output;
	actionLogger.firstAction[BEFORE] = &saved_action;
	Action* action = &saved_action;
	LONGS_EQUAL(0, view.changeClipLength(1, 96, action));
	CHECK_TRUE(action == nullptr);
	CHECK_TRUE(deluge::model::clip_length_resync_allowed());
	LONGS_EQUAL(96, clip.loopLength);
	LONGS_EQUAL(0, view.navigation_calls);
	LONGS_EQUAL(0, renders);
}
TEST(ClipViewLengthResult, allocation_selection_change_cannot_resize_replacement) {
	Clip replacement;
	for (int direction : {-1, 1}) {
		selected_clip = &clip;
		actionLogger.on_allocate = [&] { selected_clip = &replacement; };
		Action* action = nullptr;
		LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
		CHECK_TRUE(action == nullptr);
		LONGS_EQUAL(96, clip.loopLength);
		LONGS_EQUAL(96, replacement.loopLength);
		LONGS_EQUAL(0, view.navigation_calls);
	}
	selected_clip = &clip;
}
TEST(ClipViewLengthResult, implicit_undo_selection_change_stops_before_following_resize) {
	Clip replacement;
	saved_action.navigation_owner = deluge::gui::ui_session::current();
	saved_action.captured_song = &song;
	saved_action.captured_output = clip.output;
	actionLogger.firstAction[BEFORE] = &saved_action;
	actionLogger.revert_success = true;
	actionLogger.on_revert = [&] { selected_clip = &replacement; };
	Action* action = nullptr;
	LONGS_EQUAL(0, view.changeClipLength(1, 96, action));
	selected_clip = &clip;
	CHECK_TRUE(action == nullptr);
	LONGS_EQUAL(96, replacement.loopLength);
	LONGS_EQUAL(0, view.navigation_calls);
}
TEST(ClipViewLengthResult, removed_action_is_not_used_after_allocation_or_returned_after_resize) {
	for (int direction : {-1, 1}) {
		for (bool during_resize : {false, true}) {
			clip.loopLength = 96;
			actionLogger = {};
			auto* allocated_action = new Action;
			allocated_action->currentClip = &clip;
			allocated_action->captured_song = &song;
			allocated_action->captured_output = clip.output;
			allocated_action->navigation_owner = deluge::gui::ui_session::current();
			actionLogger.next = allocated_action;
			auto remove_action = [&] {
				actionLogger.firstAction[BEFORE] = nullptr;
				delete allocated_action;
			};
			if (during_resize)
				song.on_resize = remove_action;
			else
				actionLogger.on_allocate = remove_action;
			Action* action = nullptr;
			LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
			CHECK_TRUE(action == nullptr);
			LONGS_EQUAL(during_resize ? (direction > 0 ? 120 : 72) : 96, clip.loopLength);
			LONGS_EQUAL(0, renders);
			LONGS_EQUAL(0, view.navigation_calls);
			song.on_resize = {};
		}
	}
}
TEST(ClipViewLengthResult, successful_resize_returns_current_undo_action) {
	for (int direction : {-1, 1}) {
		clip.loopLength = 96;
		actionLogger = {};
		actionLogger.next = &saved_action;
		Action* action = nullptr;
		LONGS_EQUAL(direction > 0 ? 120 : 72, view.changeClipLength(direction, 96, action));
		CHECK_TRUE(action == &saved_action);
	}
}
TEST(ClipViewLengthResult, invalid_computed_lengths_do_not_resize_or_render) {
	for (int position : {INT32_MAX, kMaxSequenceLength, -24}) {
		view.square_position = position;
		Action* action = &saved_action;
		LONGS_EQUAL(0, view.changeClipLength(1, 96, action));
		CHECK_TRUE(action == nullptr);
	}
	for (int amount : {96, 97, INT32_MAX}) {
		view.chop_amount = amount;
		Action* action = &saved_action;
		LONGS_EQUAL(0, view.changeClipLength(-1, 96, action));
		CHECK_TRUE(action == nullptr);
	}
	LONGS_EQUAL(96, clip.loopLength);
	LONGS_EQUAL(0, view.navigation_calls);
	LONGS_EQUAL(0, renders);
}
TEST(ClipViewLengthResult, computed_lengths_at_valid_limits_are_accepted) {
	view.square_position = kMaxSequenceLength - 24;
	Action* action = nullptr;
	LONGS_EQUAL(kMaxSequenceLength, view.changeClipLength(1, 96, action));
	clip.loopLength = 96;
	view.chop_amount = 95;
	LONGS_EQUAL(1, view.changeClipLength(-1, 96, action));
}
TEST(ClipViewLengthResult, implicit_undo_preserves_callers_resync_policy_on_success_and_failure) {
	for (bool previous_resync : {false, true}) {
		for (bool undo_success : {false, true}) {
			clip.loopLength = 96;
			mode = {};
			playbackHandler.active = true;
			deluge::model::clip_length_resync_allowed() = previous_resync;
			saved_action.navigation_owner = deluge::gui::ui_session::current();
			saved_action.captured_song = &song;
			saved_action.captured_output = clip.output;
			actionLogger.firstAction[BEFORE] = &saved_action;
			actionLogger.revert_success = undo_success;
			actionLogger.on_revert = [&] {
				CHECK_FALSE(deluge::model::clip_length_resync_allowed());
				if (undo_success)
					clip.loopLength = 120;
			};
			Action* action = nullptr;
			const bool result = view.lengthenClip(120, action);
			CHECK_TRUE(result == undo_success);
			CHECK_TRUE(deluge::model::clip_length_resync_allowed() == previous_resync);
			LONGS_EQUAL(previous_resync && undo_success ? 1 : 0, mode.resyncs);
		}
	}
}
TEST(ClipViewLengthResult, remote_implicit_undo_does_not_suppress_nested_local_edit) {
	using namespace deluge::gui::ui_session;
	Scope remote(Id::Remote);
	deluge::model::clip_length_resync_allowed() = true;
	{
		Scope local(Id::Local);
		deluge::model::clip_length_resync_allowed() = true;
	}
	saved_action.navigation_owner = Id::Remote;
	saved_action.captured_song = &song;
	saved_action.captured_output = clip.output;
	actionLogger.firstAction[BEFORE] = &saved_action;
	actionLogger.revert_success = true;
	actionLogger.on_revert = [&] {
		CHECK_FALSE(deluge::model::clip_length_resync_allowed());
		{
			Scope local(Id::Local);
			CHECK_TRUE(deluge::model::clip_length_resync_allowed());
			Action* local_action = nullptr;
			CHECK_TRUE(view.shortenClip(72, local_action));
			CHECK_TRUE(deluge::model::clip_length_resync_allowed());
		}
		CHECK_FALSE(deluge::model::clip_length_resync_allowed());
		clip.loopLength = 120;
	};
	Action* action = nullptr;
	CHECK_TRUE(view.lengthenClip(120, action));
	CHECK_TRUE(deluge::model::clip_length_resync_allowed());
}
TEST(ClipViewLengthResult, manual_resync_changed_length_does_not_report_resize_success) {
	saved_action.navigation_owner = deluge::gui::ui_session::current();
	saved_action.captured_song = &song;
	saved_action.captured_output = clip.output;
	actionLogger.firstAction[BEFORE] = &saved_action;
	actionLogger.revert_success = true;
	actionLogger.on_revert = [&] { clip.loopLength = 120; };
	playbackHandler.active = true;
	mode.on_resync = [&] { clip.loopLength = 48; };
	Action* action = nullptr;
	LONGS_EQUAL(0, view.changeClipLength(1, 96, action));
	CHECK_TRUE(action == nullptr);
	LONGS_EQUAL(1, mode.resyncs);
	LONGS_EQUAL(0, renders);
	LONGS_EQUAL(0, view.navigation_calls);
}
TEST(ClipViewLengthResult, successful_resize_result_still_requires_requested_length) {
	for (int direction : {-1, 1}) {
		clip.loopLength = 96;
		song.on_resize = [&] { clip.loopLength = 48; };
		Action* action = nullptr;
		LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
		CHECK_TRUE(action == nullptr);
		LONGS_EQUAL(0, renders);
		LONGS_EQUAL(0, view.navigation_calls);
	}
}
TEST(ClipViewLengthResult, callback_metadata_changes_invalidate_both_resize_directions) {
	int replacement_output = 0;
	for (int direction : {-1, 1}) {
		for (bool during_resize : {false, true}) {
			for (int change = 0; change < 4; ++change) {
				clip = {};
				clip.output = &output;
				actionLogger = {};
				song.on_resize = {};
				auto change_context = [&] {
					if (change == 0)
						clip.type = 1;
					else if (change == 1)
						clip.output = &replacement_output;
					else {
						using namespace deluge::gui::ui_session;
						navigation.for_owner(change == 2 ? Id::Local : Id::Remote).structural_refresh.request();
					}
				};
				if (during_resize)
					song.on_resize = change_context;
				else
					actionLogger.on_allocate = change_context;
				Action* action = nullptr;
				LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
				CHECK_TRUE(action == nullptr);
				LONGS_EQUAL(during_resize ? (direction > 0 ? 120 : 72) : 96, clip.loopLength);
				LONGS_EQUAL(0, view.navigation_calls);
				LONGS_EQUAL(0, renders);
			}
		}
	}
}
TEST(ClipViewLengthResult, retained_action_changed_ownership_rejects_resize_result) {
	Song replacement_song;
	Clip replacement_clip;
	int replacement_output = 0;
	for (int direction : {-1, 1}) {
		for (int change = 0; change < 4; ++change) {
			clip = {};
			clip.output = &output;
			actionLogger = {};
			saved_action.currentClip = &clip;
			saved_action.captured_song = &song;
			saved_action.captured_output = clip.output;
			saved_action.navigation_owner = deluge::gui::ui_session::current();
			actionLogger.next = &saved_action;
			song.on_resize = [&] {
				if (change == 0)
					saved_action.currentClip = &replacement_clip;
				else if (change == 1)
					saved_action.captured_song = &replacement_song;
				else if (change == 2)
					saved_action.captured_output = &replacement_output;
				else
					saved_action.navigation_owner = saved_action.navigation_owner == deluge::gui::ui_session::Id::Local
					                                    ? deluge::gui::ui_session::Id::Remote
					                                    : deluge::gui::ui_session::Id::Local;
			};
			Action* action = nullptr;
			LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
			CHECK_TRUE(action == nullptr);
			LONGS_EQUAL(0, renders);
			LONGS_EQUAL(0, view.navigation_calls);
		}
	}
}
TEST(ClipViewLengthResult, action_for_wrong_song_is_rejected_before_mutation) {
	Song replacement_song;
	for (int direction : {-1, 1}) {
		actionLogger = {};
		saved_action.captured_song = &replacement_song;
		actionLogger.next = &saved_action;
		Action* action = nullptr;
		LONGS_EQUAL(0, view.changeClipLength(direction, 96, action));
		CHECK_TRUE(action == nullptr);
		LONGS_EQUAL(96, clip.loopLength);
		LONGS_EQUAL(0, renders);
	}
}
TEST(ClipViewLengthResult, invalid_resize_entry_preserves_history_without_callbacks) {
	for (bool lengthen : {false, true}) {
		for (int invalid_case = 0; invalid_case < 8; ++invalid_case) {
			currentSong = &song;
			selected_clip = &clip;
			clip.loopLength = 96;
			clip.output = &output;
			int32_t requested_length = lengthen ? 120 : 72;
			switch (invalid_case) {
			case 0:
				currentSong = nullptr;
				break;
			case 1:
				requested_length = 0;
				break;
			case 2:
				requested_length = -1;
				break;
			case 3:
				requested_length = kMaxSequenceLength + 1;
				break;
			case 4:
				clip.output = nullptr;
				break;
			case 5:
				clip.loopLength = 0;
				break;
			case 6:
				clip.loopLength = -1;
				break;
			case 7:
				selected_clip = nullptr;
				break;
			}
			const int32_t original_length = clip.loopLength;
			int callback_calls = 0;
			actionLogger = {};
			actionLogger.firstAction[BEFORE] = &saved_action;
			actionLogger.on_allocate = [&] { ++callback_calls; };
			actionLogger.on_revert = [&] { ++callback_calls; };
			song.on_resize = [&] { ++callback_calls; };
			view.selection_reads = 0;
			Action* action = &saved_action;
			CHECK_FALSE(lengthen ? view.lengthenClip(requested_length, action)
			                     : view.shortenClip(requested_length, action));
			POINTERS_EQUAL(nullptr, action);
			POINTERS_EQUAL(&saved_action, actionLogger.firstAction[BEFORE]);
			LONGS_EQUAL(0, callback_calls);
			LONGS_EQUAL(original_length, clip.loopLength);
			LONGS_EQUAL(invalid_case < 4 ? 0 : 1, view.selection_reads);
			CHECK_TRUE(deluge::model::clip_length_resync_allowed());
		}
	}
}
TEST(ClipViewLengthResult, allocation_length_changes_do_not_overwrite_callback_edit) {
	Clip replacement_clip;
	for (bool lengthen : {false, true}) {
		for (int allocation_path = 0; allocation_path < 3; ++allocation_path) {
			clip.loopLength = 96;
			actionLogger = {};
			saved_action.currentClip = &clip;
			saved_action.type = allocation_path == 2 ? ActionType::PATTERN_PASTE : ActionType::CLIP_LENGTH_DECREASE;
			actionLogger.next = &saved_action;
			if (allocation_path == 2)
				actionLogger.firstAction[BEFORE] = &saved_action;
			int allocation_calls = 0, resize_calls = 0;
			song.on_resize = [&] { ++resize_calls; };
			actionLogger.on_allocate = [&] {
				++allocation_calls;
				if (allocation_path == 1 && allocation_calls == 1)
					saved_action.currentClip = &replacement_clip;
				else {
					saved_action.currentClip = &clip;
					clip.loopLength = 123;
				}
			};
			Action* action = &saved_action;
			LONGS_EQUAL(0, view.changeClipLength(lengthen ? 1 : -1, 96, action));
			POINTERS_EQUAL(nullptr, action);
			LONGS_EQUAL(allocation_path == 1 ? 2 : 1, allocation_calls);
			LONGS_EQUAL(0, resize_calls);
			LONGS_EQUAL(123, clip.loopLength);
			LONGS_EQUAL(0, renders);
			LONGS_EQUAL(0, view.navigation_calls);
		}
	}
}
TEST(ClipViewLengthResult, invalid_implicit_undo_length_stops_before_new_history_or_resize) {
	for (bool previous_resync : {false, true}) {
		for (int32_t restored_length : {0, -1, kMaxSequenceLength + 1}) {
			clip.loopLength = 96;
			actionLogger = {};
			actionLogger.firstAction[BEFORE] = &saved_action;
			actionLogger.revert_success = true;
			int allocation_calls = 0, resize_calls = 0;
			actionLogger.on_revert = [&] { clip.loopLength = restored_length; };
			actionLogger.on_allocate = [&] { ++allocation_calls; };
			song.on_resize = [&] { ++resize_calls; };
			deluge::model::clip_length_resync_allowed() = previous_resync;
			Action* action = &saved_action;
			LONGS_EQUAL(0, view.changeClipLength(1, 96, action));
			POINTERS_EQUAL(nullptr, action);
			POINTERS_EQUAL(&saved_action, actionLogger.firstAction[BEFORE]);
			LONGS_EQUAL(restored_length, clip.loopLength);
			LONGS_EQUAL(0, allocation_calls);
			LONGS_EQUAL(0, resize_calls);
			LONGS_EQUAL(0, mode.resyncs);
			LONGS_EQUAL(0, renders);
			LONGS_EQUAL(0, view.navigation_calls);
			CHECK_TRUE(deluge::model::clip_length_resync_allowed() == previous_resync);
		}
	}
}
TEST(ClipViewLengthResult, valid_implicit_undo_length_allows_followup_resize) {
	actionLogger.firstAction[BEFORE] = &saved_action;
	actionLogger.revert_success = true;
	actionLogger.on_revert = [&] { clip.loopLength = 144; };
	actionLogger.next = &saved_action;
	int allocation_calls = 0, resize_calls = 0;
	actionLogger.on_allocate = [&] {
		++allocation_calls;
		LONGS_EQUAL(144, clip.loopLength);
	};
	song.on_resize = [&] { ++resize_calls; };
	Action* action = nullptr;
	LONGS_EQUAL(120, view.changeClipLength(1, 96, action));
	POINTERS_EQUAL(&saved_action, action);
	LONGS_EQUAL(120, clip.loopLength);
	LONGS_EQUAL(1, allocation_calls);
	LONGS_EQUAL(1, resize_calls);
	LONGS_EQUAL(1, renders);
}
} // namespace clip_view_length_tests
