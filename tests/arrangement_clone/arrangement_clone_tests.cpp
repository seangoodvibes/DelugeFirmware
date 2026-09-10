#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include <algorithm>
#include <functional>
#include <vector>

enum class Error { NONE, BUG, INSUFFICIENT_RAM };
enum class ClipType { INSTRUMENT, AUDIO };
enum class InstrumentRemoval { NONE };
struct Clip;
struct Song;
struct Output {
	bool has_instance = false;
	bool clipHasInstance(Clip*) { return has_instance; }
};
struct ModelStackWithTimelineCounter {
	Song* song;
	Clip* clip;
	Clip* getTimelineCounter() { return clip; }
	Clip* getTimelineCounterAllowNull() { return clip; }
};
struct Clip {
	ClipType type = ClipType::INSTRUMENT;
	Output* output = nullptr;
	int section = 0;
	bool activeIfNoSolo = true;
	Clip* clone_result = nullptr;
	Error clone_error = Error::NONE;
	int clone_calls = 0;
	std::function<void()> on_clone;
	virtual ~Clip() = default;
	Error clone(ModelStackWithTimelineCounter* stack, bool) {
		++clone_calls;
		if (clone_error == Error::NONE)
			stack->clip = clone_result;
		Error result = clone_error;
		auto callback = on_clone;
		if (callback)
			callback();
		return result;
	}
};
struct InstrumentClip : Clip {
	bool repeat_result = true;
	std::function<void()> on_repeat;
	int* destruction_count = nullptr;
	~InstrumentClip() override {
		if (destruction_count)
			++*destruction_count;
	}
	bool repeatOrChopToExactLength(ModelStackWithTimelineCounter*, int) {
		bool result = repeat_result;
		auto callback = on_repeat;
		if (callback)
			callback();
		return result;
	}
};
struct ClipArray {
	int32_t getIndexForClip(Clip* clip) {
		auto found = std::find(entries.begin(), entries.end(), clip);
		return found == entries.end() ? -1 : int32_t(found - entries.begin());
	}
	void deleteAtIndex(int32_t index) { entries.erase(entries.begin() + index); }
	bool capacity = true;
	int inserts = 0;
	std::vector<Clip*> entries;
	std::function<void()> on_reserve;
	bool ensureEnoughSpaceAllocated(int) {
		bool result = capacity;
		auto callback = on_reserve;
		if (callback)
			callback();
		return result;
	}
	Error insert_error = Error::NONE;
	std::function<void()> on_insert;
	Error insertClipAtIndex(Clip* clip, int) {
		++inserts;
		Error result = insert_error;
		if (result == Error::NONE)
			entries.push_back(clip);
		auto callback = on_insert;
		if (callback)
			callback();
		return result;
	}
};
struct Song {
	ClipArray arrangementOnlyClips;
	ClipArray sessionClips;
	int cleanup_calls = 0;
	bool contains_clip_for_undo(Clip* clip) {
		return std::find(arrangementOnlyClips.entries.begin(), arrangementOnlyClips.entries.end(), clip)
		       != arrangementOnlyClips.entries.end();
	}
	void deleteClipObject(Clip* clip, bool, InstrumentRemoval) {
		++cleanup_calls;
		delete clip;
	}
};
Song* currentSong = nullptr;
constexpr int MODEL_STACK_MAX_SIZE = 128;
struct Builder {
	ModelStackWithTimelineCounter stack;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip* clip) {
		stack.clip = clip;
		return &stack;
	}
};
Builder* setupModelStackWithSong(char*, Song* song) {
	static Builder builder;
	builder.stack.song = song;
	return &builder;
}
struct ClipInstance {
	Clip* clip;
	int pos = 0, length = 16;
};
struct Arrangement {
	int notifications = 0;
	std::function<void(int)> on_notification;
	void rowEdited(Output*, int, int, Clip*, ClipInstance*) {
		++notifications;
		auto callback = on_notification;
		if (callback)
			callback(notifications);
	}
	Error doUniqueCloneOnClipInstance(ClipInstance*, int, bool);
};
#include "arrangement_clone_method.inc"
TEST_GROUP(ArrangementClone) {
	Song song;
	Output output;
	InstrumentClip original;
	InstrumentClip* clone = nullptr;
	ClipInstance instance{&original};
	Arrangement arrangement;
	int destructions = 0;
	void setup() {
		currentSong = &song;
		original.output = &output;
		clone = new InstrumentClip;
		clone->output = &output;
		clone->destruction_count = &destructions;
		original.clone_result = clone;
	}
	void teardown() {
		if (!destructions)
			delete clone;
		currentSong = nullptr;
	}
	void unchanged() {
		CHECK_TRUE(instance.clip == &original);
		LONGS_EQUAL(16, instance.length);
		LONGS_EQUAL(0, arrangement.notifications);
	}
};
TEST(ArrangementClone, failed_repeat_cleans_unpublished_clone_and_does_not_install) {
	clone->repeat_result = false;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(1, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}
TEST(ArrangementClone, callback_released_clone_is_not_cleaned_again) {
	clone->repeat_result = false;
	clone->on_repeat = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
		delete clone;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}
TEST(ArrangementClone, callback_published_clone_is_not_deleted_or_installed_again) {
	clone->repeat_result = false;
	clone->on_repeat = [&] { song.arrangementOnlyClips.entries.push_back(clone); };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}
TEST(ArrangementClone, successful_repeat_installs_clone_and_notifies) {
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::NONE);
	CHECK_TRUE(instance.clip == clone);
	LONGS_EQUAL(32, instance.length);
	LONGS_EQUAL(1, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(2, arrangement.notifications);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(255, clone->section);
	CHECK_FALSE(clone->activeIfNoSolo);
}
TEST(ArrangementClone, capacity_and_clone_errors_do_not_install) {
	song.arrangementOnlyClips.capacity = false;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, original.clone_calls);
	song.arrangementOnlyClips.capacity = true;
	original.clone_error = Error::INSUFFICIENT_RAM;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}

TEST(ArrangementClone, reservation_invalidation_avoids_released_instance_access) {
	auto* transient = new ClipInstance{&original};
	song.arrangementOnlyClips.on_reserve = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local).structural_refresh.request();
		delete transient;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(transient, 32, true) == Error::BUG);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
}
TEST(ArrangementClone, clone_invalidation_avoids_released_clone_access) {
	original.on_clone = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
		delete clone;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}
TEST(ArrangementClone, reservation_changing_instance_stops_before_clone) {
	song.arrangementOnlyClips.on_reserve = [&] { instance.length = 24; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(24, instance.length);
}

TEST(ArrangementClone, successful_repeat_cannot_overwrite_changed_source_instance) {
	for (int change = 0; change < 3; ++change) {
		// Each iteration owns a fresh clone; the caller deletes rejected clones.
		if (change) {
			clone = new InstrumentClip;
			clone->output = &output;
			clone->destruction_count = &destructions;
			original.clone_result = clone;
		}
		instance = {&original};
		clone->on_repeat = [&] {
			if (change == 0)
				instance.length = 24;
			if (change == 1)
				instance.pos = 8;
			if (change == 2)
				instance.clip = nullptr;
		};
		CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
		LONGS_EQUAL(change + 1, destructions);
		LONGS_EQUAL(change + 1, song.cleanup_calls);
		LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
		LONGS_EQUAL(0, arrangement.notifications);
		if (change == 0)
			LONGS_EQUAL(24, instance.length);
		if (change == 1)
			LONGS_EQUAL(8, instance.pos);
		if (change == 2)
			CHECK_TRUE(instance.clip == nullptr);
	}
}
TEST(ArrangementClone, successful_repeat_cannot_install_published_clone_again) {
	clone->on_repeat = [&] { song.arrangementOnlyClips.entries.push_back(clone); };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
	unchanged();
}
TEST(ArrangementClone, successful_repeat_invalidation_avoids_released_instance_access) {
	auto* transient = new ClipInstance{&original};
	clone->on_repeat = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
		delete transient;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(transient, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
}

TEST(ArrangementClone, rejects_invalid_lengths_before_reservation) {
	int reserve_calls = 0;
	song.arrangementOnlyClips.on_reserve = [&] { ++reserve_calls; };
	for (int invalid_length : {0, -2, INT32_MIN}) {
		CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, invalid_length, true) == Error::BUG);
		CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, invalid_length, false) == Error::BUG);
	}
	LONGS_EQUAL(0, reserve_calls);
	LONGS_EQUAL(0, original.clone_calls);
	unchanged();
}
TEST(ArrangementClone, rejects_invalid_source_and_overflowing_ranges) {
	int reserve_calls = 0;
	song.arrangementOnlyClips.on_reserve = [&] { ++reserve_calls; };
	instance.pos = -1;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	instance.pos = 0;
	for (int invalid_length : {0, -1}) {
		instance.length = invalid_length;
		CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	}
	instance.length = 16;
	instance.pos = INT32_MAX;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, -1, true) == Error::BUG);
	instance.pos = INT32_MAX - 16;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, reserve_calls);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, arrangement.notifications);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
}
TEST(ArrangementClone, accepts_keep_length_at_signed_position_boundary) {
	instance.pos = INT32_MAX - 16;
	clone->repeat_result = false; // Keeping length must bypass repetition.
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, -1, true) == Error::NONE);
	CHECK_TRUE(instance.clip == clone);
	LONGS_EQUAL(16, instance.length);
	LONGS_EQUAL(2, arrangement.notifications);
}
TEST(ArrangementClone, accepts_requested_length_at_signed_position_boundary) {
	instance.pos = INT32_MAX - 32;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::NONE);
	CHECK_TRUE(instance.clip == clone);
	LONGS_EQUAL(32, instance.length);
	LONGS_EQUAL(2, arrangement.notifications);
}

TEST(ArrangementClone, removal_notification_cannot_overwrite_changed_instance) {
	arrangement.on_notification = [&](int notification) {
		if (notification == 1)
			instance.length = 24;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	CHECK_TRUE(instance.clip == &original);
	LONGS_EQUAL(24, instance.length);
	LONGS_EQUAL(1, arrangement.notifications);
	LONGS_EQUAL(1, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(1, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.arrangementOnlyClips.entries.size());
}
TEST(ArrangementClone, notifications_can_release_instance_without_later_access) {
	for (int release_at : {1, 2}) {
		auto* transient = new ClipInstance{&original};
		arrangement.notifications = 0;
		song.arrangementOnlyClips.entries.clear();
		arrangement.on_notification = [&](int notification) {
			if (notification == release_at) {
				deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
				    .structural_refresh.request();
				delete transient;
			}
		};
		CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(transient, 32, true) == Error::BUG);
		LONGS_EQUAL(release_at, arrangement.notifications);
		LONGS_EQUAL(0, song.cleanup_calls);
	}
}

TEST(ArrangementClone, insertion_failure_cleans_unpublished_clone_and_preserves_error) {
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.arrangementOnlyClips.entries.size());
	unchanged();
}
TEST(ArrangementClone, insertion_failure_after_clone_release_does_not_clean_twice) {
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	song.arrangementOnlyClips.on_insert = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
		delete clone;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	unchanged();
}
TEST(ArrangementClone, successful_insertion_callback_can_release_source_instance) {
	auto* transient = new ClipInstance{&original};
	song.arrangementOnlyClips.on_insert = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local).structural_refresh.request();
		delete transient;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(transient, 32, true) == Error::BUG);
	LONGS_EQUAL(0, arrangement.notifications);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
}

TEST(ArrangementClone, insertion_source_change_discards_unused_published_clone) {
	song.arrangementOnlyClips.on_insert = [&] { instance.length = 24; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(24, instance.length);
	CHECK_TRUE(instance.clip == &original);
	LONGS_EQUAL(1, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.arrangementOnlyClips.entries.size());
	LONGS_EQUAL(0, arrangement.notifications);
}
TEST(ArrangementClone, removal_notification_preserves_clone_adopted_by_arrangement) {
	arrangement.on_notification = [&](int) {
		instance.length = 24;
		output.has_instance = true;
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
}
TEST(ArrangementClone, removal_notification_preserves_clone_adopted_by_session) {
	arrangement.on_notification = [&](int) {
		instance.length = 24;
		song.sessionClips.entries.push_back(clone);
	};
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
}

TEST(ArrangementClone, removal_notification_preserves_clone_adopted_by_source) {
	arrangement.on_notification = [&](int) { instance.clip = clone; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, destructions);
	CHECK_TRUE(instance.clip == clone);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
}

TEST(ArrangementClone, failed_repeat_preserves_clone_adopted_by_source) {
	clone->repeat_result = false;
	clone->on_repeat = [&] { instance.clip = clone; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	CHECK_TRUE(instance.clip == clone);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
}
TEST(ArrangementClone, failed_repeat_preserves_clone_referenced_by_output) {
	clone->repeat_result = false;
	clone->on_repeat = [&] { output.has_instance = true; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
	unchanged();
}
TEST(ArrangementClone, failed_insertion_preserves_clone_adopted_by_source) {
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	song.arrangementOnlyClips.on_insert = [&] { instance.clip = clone; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(instance.clip == clone);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
}
TEST(ArrangementClone, failed_insertion_preserves_clone_referenced_by_output) {
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	song.arrangementOnlyClips.on_insert = [&] { output.has_instance = true; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, song.cleanup_calls);
	unchanged();
}

TEST(ArrangementClone, repeat_output_replacement_rejects_installation_without_cleanup) {
	Output replacement_output;
	clone->on_repeat = [&] { clone->output = &replacement_output; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}
TEST(ArrangementClone, insertion_failure_output_replacement_skips_unsafe_cleanup) {
	Output replacement_output;
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	song.arrangementOnlyClips.on_insert = [&] { clone->output = &replacement_output; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, destructions);
	unchanged();
}
TEST(ArrangementClone, notification_output_replacement_preserves_published_clone) {
	Output replacement_output;
	arrangement.on_notification = [&](int) { clone->output = &replacement_output; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(1, arrangement.notifications);
	CHECK_TRUE(instance.clip == &original);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
}

TEST(ArrangementClone, reservation_source_output_change_stops_before_clone) {
	Output replacement_output;
	song.arrangementOnlyClips.on_reserve = [&] { original.output = &replacement_output; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(0, arrangement.notifications);
	CHECK_TRUE(original.output == &replacement_output);
}
TEST(ArrangementClone, repeat_source_output_change_discards_unused_clone) {
	Output replacement_output;
	clone->on_repeat = [&] { original.output = &replacement_output; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(1, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	CHECK_TRUE(original.output == &replacement_output);
	unchanged();
}
TEST(ArrangementClone, notification_source_output_change_prevents_second_notification) {
	Output replacement_output;
	arrangement.on_notification = [&](int) { original.output = &replacement_output; };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(1, arrangement.notifications);
	LONGS_EQUAL(1, song.cleanup_calls);
	LONGS_EQUAL(1, destructions);
	CHECK_TRUE(instance.clip == &original);
	CHECK_TRUE(original.output == &replacement_output);
}

TEST(ArrangementClone, successful_clone_with_null_result_is_rejected) {
	original.clone_result = nullptr;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(0, song.cleanup_calls);
	unchanged();
}
TEST(ArrangementClone, successful_clone_returning_source_does_not_mutate_source) {
	original.clone_result = &original;
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, original.section);
	CHECK_TRUE(original.activeIfNoSolo);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	unchanged();
}
TEST(ArrangementClone, clone_already_published_by_callback_is_not_modified) {
	original.on_clone = [&] { song.arrangementOnlyClips.entries.push_back(clone); };
	CHECK_TRUE(arrangement.doUniqueCloneOnClipInstance(&instance, 32, true) == Error::BUG);
	LONGS_EQUAL(0, clone->section);
	CHECK_TRUE(clone->activeIfNoSolo);
	LONGS_EQUAL(0, song.cleanup_calls);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(1, song.arrangementOnlyClips.entries.size());
	unchanged();
}
