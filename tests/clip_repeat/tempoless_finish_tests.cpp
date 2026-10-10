#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include <cstdint>
#include <functional>
#include <new>
#include <vector>

namespace tempoless_finish_tests {
constexpr int MODEL_STACK_MAX_SIZE = 64;
constexpr int BEFORE = 0;
constexpr uint32_t kMaxSequenceLength = 1000;
enum class ClipType { AUDIO, INSTRUMENT };
enum class ActionType { RECORD };
enum class ActionAddition { ALLOWED };
enum class RecordingMode { NORMAL, OFF };
struct ModelStackWithTimelineCounter {};
struct ModelStack {
	ModelStackWithTimelineCounter* addTimelineCounter(void*) { return nullptr; }
};
struct SampleHolder {
	void* audioFile = nullptr;
	uint64_t startPos = 0, endPos = 480;
	int64_t duration = 480;
	int64_t getDurationInSamples(bool) { return duration; }
};
struct Clip {
	ClipType type = ClipType::AUDIO;
	std::function<void()> on_finish, on_copy;
	int32_t loopLength = 96, originalLength = 96;
	void* output = nullptr;
	SampleHolder sampleHolder;
	int finish_calls = 0, copy_calls = 0;
	bool getCurrentlyRecordingLinearly() { return true; }
	void finishLinearRecording(ModelStackWithTimelineCounter*, void*, int32_t) {
		++finish_calls;
		auto callback = on_finish;
		if (callback)
			callback();
	}
	void copyBasicsFrom(Clip*) {
		++copy_calls;
		auto callback = on_copy;
		if (callback)
			callback();
	}
};
using AudioClip = Clip;
struct ConsequenceBeginPlayback {};
struct Action {
	deluge::gui::ui_session::Id navigation_owner = deluge::gui::ui_session::Id::Local;
	void* captured_song = nullptr;
	bool record_succeeds = true;
	int record_calls = 0;
	std::function<void()> on_record;
	bool recordClipLengthChange(Clip*, int32_t) {
		++record_calls;
		const bool result = record_succeeds;
		auto callback = on_record;
		if (callback)
			callback();
		return result;
	}
	ConsequenceBeginPlayback* playback_consequence = nullptr;
	void addConsequence(ConsequenceBeginPlayback* consequence) { playback_consequence = consequence; }
};
static struct Logger {
	Action* next = nullptr;
	Action* firstAction[2]{};
	std::function<void()> on_allocate;
	Action* getNewAction(ActionType, ActionAddition);
} actionLogger;
struct GeneralMemoryAllocator {
	bool succeeds = false;
	int allocations = 0, frees = 0;
	std::function<void()> on_allocate;
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocLowSpeed(size_t size) {
		if (on_allocate)
			on_allocate();
		if (!succeeds)
			return nullptr;
		++allocations;
		return ::operator new(size);
	}
};
void delugeDealloc(void* memory) {
	++GeneralMemoryAllocator::get().frees;
	::operator delete(memory);
}
struct Song {
	struct {
		std::vector<Clip*> clips;
		int getNumElements() { return clips.size(); }
		Clip* getClipAtIndex(int index) { return clips[index]; }
	} sessionClips;
	bool contains_clip_for_undo(Clip* clip) {
		for (auto* candidate : sessionClips.clips)
			if (candidate == clip)
				return true;
		return false;
	}
	Clip* pending = nullptr;
	Clip* getPendingOverdubWithOutput(void*) { return pending; }
};
static Song* currentSong;
Action* Logger::getNewAction(ActionType, ActionAddition) {
	Action* result = next;
	firstAction[BEFORE] = result;
	if (result) {
		result->navigation_owner = deluge::gui::ui_session::current();
		result->captured_song = currentSong;
	}
	if (on_allocate)
		on_allocate();
	return result;
}

ModelStack* setupModelStackWithSong(char*, Song*) {
	static ModelStack stack;
	return &stack;
}
struct PlaybackHandler {
	RecordingMode recording = RecordingMode::NORMAL;
	int end_calls = 0, restart_calls = 0, led_calls = 0;
	std::function<void()> on_tempo;
	uint32_t requested_ticks = 48;
	uint32_t setTempoFromAudioClipLength(uint64_t, Action*) {
		if (on_tempo)
			on_tempo();
		return requested_ticks;
	}
	std::function<void()> on_end, on_led;
	void endPlayback() {
		++end_calls;
		if (on_end)
			on_end();
	}
	void setLedStates() {
		++led_calls;
		if (on_led)
			on_led();
	}
	void setupPlaybackUsingInternalClock(int, bool) { ++restart_calls; }
	void finishTempolessRecording(bool, int32_t, bool);
};
#include "tempoless_finish_method.inc"

TEST_GROUP(TempolessFinish) {
	Song song;
	Clip clip, pending;
	Action action;
	PlaybackHandler handler;
	void setup() override {
		currentSong = &song;
		GeneralMemoryAllocator::get() = {};
		song.sessionClips.clips = {&clip};
		song.pending = &pending;
		clip.sampleHolder.audioFile = &song;
		actionLogger = {};
		actionLogger.next = &action;
	}
	void teardown() override {
		delete action.playback_consequence;
		GeneralMemoryAllocator::get() = {};
		currentSong = nullptr;
		actionLogger = {};
	}
};
TEST(TempolessFinish, missing_action_still_finishes_recording_without_dereference) {
	actionLogger.next = nullptr;
	handler.finishTempolessRecording(true, 0, true);
	LONGS_EQUAL(48, clip.loopLength);
	LONGS_EQUAL(48, clip.originalLength);
	LONGS_EQUAL(1, pending.copy_calls);
	LONGS_EQUAL(1, handler.restart_calls);
	LONGS_EQUAL(1, handler.led_calls);
	CHECK_TRUE(handler.recording == RecordingMode::OFF);
}
TEST(TempolessFinish, failed_history_stops_before_metadata_and_transport_updates) {
	action.record_succeeds = false;
	handler.finishTempolessRecording(true, 0, true);
	LONGS_EQUAL(1, action.record_calls);
	LONGS_EQUAL(48, clip.loopLength);
	LONGS_EQUAL(96, clip.originalLength);
	LONGS_EQUAL(0, pending.copy_calls);
	LONGS_EQUAL(0, handler.restart_calls);
	LONGS_EQUAL(0, handler.end_calls);
	LONGS_EQUAL(0, handler.led_calls);
}
TEST(TempolessFinish, failed_history_callback_may_delete_clip_without_followup_access) {
	auto* target_clip = new Clip;
	target_clip->sampleHolder.audioFile = &song;
	song.sessionClips.clips = {target_clip};
	action.record_succeeds = false;
	action.on_record = [&] {
		song.sessionClips.clips.clear();
		delete target_clip;
	};
	handler.finishTempolessRecording(false, 0, true);
	LONGS_EQUAL(0, pending.copy_calls);
	LONGS_EQUAL(0, handler.end_calls);
	LONGS_EQUAL(0, handler.led_calls);
}
TEST(TempolessFinish, successful_history_preserves_stop_and_record_exit) {
	handler.finishTempolessRecording(false, 0, true);
	LONGS_EQUAL(1, action.record_calls);
	LONGS_EQUAL(48, clip.originalLength);
	LONGS_EQUAL(1, pending.copy_calls);
	LONGS_EQUAL(1, handler.end_calls);
	LONGS_EQUAL(0, handler.restart_calls);
	LONGS_EQUAL(1, handler.led_calls);
}
TEST(TempolessFinish, unchanged_length_does_not_require_length_history) {
	clip.loopLength = 48;
	action.record_succeeds = false;
	handler.finishTempolessRecording(false, 0, true);
	LONGS_EQUAL(0, action.record_calls);
	LONGS_EQUAL(48, clip.originalLength);
	LONGS_EQUAL(1, pending.copy_calls);
	LONGS_EQUAL(1, handler.end_calls);
}
TEST(TempolessFinish, successful_callbacks_cannot_retain_deleted_clip) {
	for (int stage = 0; stage < 4; ++stage) {
		auto* target_clip = new Clip;
		target_clip->sampleHolder.audioFile = &song;
		song.sessionClips.clips = {target_clip};
		handler = {};
		action = {};
		actionLogger = {};
		actionLogger.next = &action;
		auto remove_clip = [&] {
			song.sessionClips.clips.clear();
			delete target_clip;
		};
		if (stage == 0)
			target_clip->on_finish = remove_clip;
		if (stage == 1)
			actionLogger.on_allocate = remove_clip;
		if (stage == 2)
			handler.on_tempo = remove_clip;
		if (stage == 3)
			action.on_record = remove_clip;
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(0, pending.copy_calls);
		LONGS_EQUAL(0, handler.restart_calls);
		LONGS_EQUAL(0, handler.led_calls);
	}
}
TEST(TempolessFinish, finish_callback_rejects_changed_song_type_output_or_structure) {
	using namespace deluge::gui::ui_session;
	Song replacement;
	for (int change = 0; change < 5; ++change) {
		currentSong = &song;
		clip.type = ClipType::AUDIO;
		clip.output = nullptr;
		clip.on_finish = [&] {
			switch (change) {
			case 0:
				currentSong = &replacement;
				break;
			case 1:
				clip.type = ClipType::INSTRUMENT;
				break;
			case 2:
				clip.output = &replacement;
				break;
			case 3:
				navigation.for_owner(Id::Local).structural_refresh.request();
				break;
			case 4:
				navigation.for_owner(Id::Remote).structural_refresh.request();
				break;
			}
		};
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(0, action.record_calls);
		LONGS_EQUAL(0, handler.restart_calls);
		LONGS_EQUAL(0, pending.copy_calls);
	}
}
TEST(TempolessFinish, pending_overdub_is_looked_up_after_recording_callback) {
	Clip replacement_pending;
	clip.on_finish = [&] { song.pending = &replacement_pending; };
	handler.finishTempolessRecording(false, 0, true);
	LONGS_EQUAL(0, pending.copy_calls);
	LONGS_EQUAL(1, replacement_pending.copy_calls);
}
TEST(TempolessFinish, callbacks_cannot_continue_with_deleted_action) {
	for (int stage = 0; stage < 3; ++stage) {
		clip.loopLength = clip.originalLength = 96;
		handler = {};
		actionLogger = {};
		auto* target_action = new Action;
		actionLogger.next = target_action;
		auto remove_action = [&] {
			actionLogger.firstAction[BEFORE] = nullptr;
			delete target_action;
		};
		if (stage == 0)
			actionLogger.on_allocate = remove_action;
		if (stage == 1)
			handler.on_tempo = remove_action;
		if (stage == 2)
			target_action->on_record = remove_action;
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(96, clip.originalLength);
		LONGS_EQUAL(0, pending.copy_calls);
		LONGS_EQUAL(0, handler.restart_calls);
	}
}
TEST(TempolessFinish, missing_song_returns_without_transport_work) {
	currentSong = nullptr;
	handler.finishTempolessRecording(true, 0, true);
	LONGS_EQUAL(0, handler.restart_calls);
	LONGS_EQUAL(0, handler.led_calls);
}
TEST(TempolessFinish, restart_requires_allocated_playback_history_when_action_exists) {
	handler.finishTempolessRecording(true, 0, true);
	LONGS_EQUAL(0, handler.restart_calls);
	POINTERS_EQUAL(nullptr, action.playback_consequence);
}
TEST(TempolessFinish, valid_restart_attaches_playback_history) {
	GeneralMemoryAllocator::get().succeeds = true;
	handler.finishTempolessRecording(true, 0, true);
	LONGS_EQUAL(1, handler.restart_calls);
	CHECK_TRUE(action.playback_consequence != nullptr);
	LONGS_EQUAL(1, GeneralMemoryAllocator::get().allocations);
	LONGS_EQUAL(0, GeneralMemoryAllocator::get().frees);
}
TEST(TempolessFinish, restart_allocation_cannot_use_deleted_action) {
	for (bool allocation_succeeds : {false, true}) {
		clip.loopLength = clip.originalLength = 96;
		handler = {};
		actionLogger = {};
		auto* target_action = new Action;
		actionLogger.next = target_action;
		auto& allocator = GeneralMemoryAllocator::get();
		allocator = {};
		allocator.succeeds = allocation_succeeds;
		allocator.on_allocate = [&] {
			actionLogger.firstAction[BEFORE] = nullptr;
			delete target_action;
		};
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(0, handler.restart_calls);
		LONGS_EQUAL(allocation_succeeds ? 1 : 0, allocator.frees);
	}
}
TEST(TempolessFinish, restart_allocation_releases_memory_after_session_change) {
	using namespace deluge::gui::ui_session;
	for (int change = 0; change < 3; ++change) {
		currentSong = &song;
		handler = {};
		auto& allocator = GeneralMemoryAllocator::get();
		allocator = {};
		allocator.succeeds = true;
		allocator.on_allocate = [&] {
			if (change == 0)
				currentSong = nullptr;
			if (change == 1)
				navigation.for_owner(Id::Local).structural_refresh.request();
			if (change == 2)
				navigation.for_owner(Id::Remote).structural_refresh.request();
		};
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(0, handler.restart_calls);
		LONGS_EQUAL(1, allocator.frees);
		POINTERS_EQUAL(nullptr, action.playback_consequence);
	}
}
TEST(TempolessFinish, transport_callbacks_cannot_continue_after_song_change) {
	for (bool restarting : {false, true}) {
		currentSong = &song;
		handler = {};
		if (restarting)
			handler.on_led = [] { currentSong = nullptr; };
		else
			handler.on_end = [] { currentSong = nullptr; };
		handler.finishTempolessRecording(restarting, 0, true);
		LONGS_EQUAL(restarting ? 1 : 0, handler.led_calls);
		LONGS_EQUAL(0, handler.restart_calls);
		LONGS_EQUAL(0, GeneralMemoryAllocator::get().allocations);
	}
}
TEST(TempolessFinish, callbacks_cannot_redirect_retained_history_ownership) {
	using namespace deluge::gui::ui_session;
	Song other_song;
	for (bool change_owner : {false, true}) {
		for (int stage = 0; stage < 4; ++stage) {
			clip.loopLength = clip.originalLength = 96;
			handler = {};
			action = {};
			actionLogger = {};
			actionLogger.next = &action;
			auto& allocator = GeneralMemoryAllocator::get();
			allocator = {};
			allocator.succeeds = true;
			auto redirect_history = [&] {
				if (change_owner)
					action.navigation_owner = current() == Id::Local ? Id::Remote : Id::Local;
				else
					action.captured_song = &other_song;
			};
			if (stage == 0)
				actionLogger.on_allocate = redirect_history;
			if (stage == 1)
				handler.on_tempo = redirect_history;
			if (stage == 2)
				action.on_record = redirect_history;
			if (stage == 3)
				allocator.on_allocate = redirect_history;
			handler.finishTempolessRecording(true, 0, true);
			POINTERS_EQUAL(&action, actionLogger.firstAction[BEFORE]);
			POINTERS_EQUAL(nullptr, action.playback_consequence);
			LONGS_EQUAL(0, handler.restart_calls);
			LONGS_EQUAL(stage == 3 ? 1 : 0, allocator.frees);
			if (stage < 3)
				LONGS_EQUAL(96, clip.originalLength);
		}
	}
}
TEST(TempolessFinish, matching_remote_history_owner_allows_restart) {
	using namespace deluge::gui::ui_session;
	Scope remote(Id::Remote);
	GeneralMemoryAllocator::get().succeeds = true;
	handler.finishTempolessRecording(true, 0, true);
	CHECK_TRUE(action.navigation_owner == Id::Remote);
	POINTERS_EQUAL(&song, action.captured_song);
	CHECK_TRUE(action.playback_consequence != nullptr);
	LONGS_EQUAL(1, handler.restart_calls);
}
TEST(TempolessFinish, invalid_tempo_length_does_not_publish_clip_length) {
	for (uint32_t invalid_length : {uint32_t(0), kMaxSequenceLength + 1, UINT32_MAX}) {
		handler = {};
		handler.requested_ticks = invalid_length;
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(96, clip.loopLength);
		LONGS_EQUAL(96, clip.originalLength);
		LONGS_EQUAL(0, action.record_calls);
		LONGS_EQUAL(0, pending.copy_calls);
		LONGS_EQUAL(0, handler.restart_calls);
		LONGS_EQUAL(0, handler.led_calls);
	}
}
TEST(TempolessFinish, callback_length_changes_stop_followup_finalization) {
	for (int stage = 0; stage < 3; ++stage) {
		clip.loopLength = clip.originalLength = 96;
		pending = {};
		action = {};
		handler = {};
		if (stage == 0)
			action.on_record = [&] { clip.loopLength = 123; };
		if (stage == 1)
			pending.on_copy = [&] { clip.loopLength = 123; };
		if (stage == 2)
			pending.on_copy = [&] { clip.originalLength = 123; };
		handler.finishTempolessRecording(false, 0, true);
		LONGS_EQUAL(stage == 2 ? 48 : 123, clip.loopLength);
		LONGS_EQUAL(stage == 0 ? 96 : (stage == 2 ? 123 : 48), clip.originalLength);
		LONGS_EQUAL(stage == 0 ? 0 : 1, pending.copy_calls);
		LONGS_EQUAL(0, handler.end_calls);
		LONGS_EQUAL(0, handler.led_calls);
	}
}
TEST(TempolessFinish, tempo_length_limits_remain_valid) {
	for (uint32_t valid_length : {uint32_t(1), kMaxSequenceLength}) {
		clip.loopLength = clip.originalLength = 96;
		handler = {};
		handler.requested_ticks = valid_length;
		handler.finishTempolessRecording(false, 0, true);
		LONGS_EQUAL(valid_length, clip.loopLength);
		LONGS_EQUAL(valid_length, clip.originalLength);
		LONGS_EQUAL(1, handler.end_calls);
	}
}
TEST(TempolessFinish, changed_tempo_source_is_not_overwritten_after_callback) {
	for (bool during_tempo : {false, true}) {
		for (int changed_field = 0; changed_field < 4; ++changed_field) {
			clip = {};
			clip.sampleHolder.audioFile = &song;
			action = {};
			actionLogger = {};
			actionLogger.next = &action;
			handler = {};
			auto change_source = [&] {
				switch (changed_field) {
				case 0:
					clip.loopLength = 123;
					break;
				case 1:
					clip.sampleHolder.audioFile = &action;
					break;
				case 2:
					clip.sampleHolder.startPos = 10;
					break;
				case 3:
					clip.sampleHolder.endPos = 240;
					break;
				}
			};
			if (during_tempo)
				handler.on_tempo = change_source;
			else
				actionLogger.on_allocate = change_source;
			handler.finishTempolessRecording(true, 0, true);
			LONGS_EQUAL(changed_field == 0 ? 123 : 96, clip.loopLength);
			LONGS_EQUAL(96, clip.originalLength);
			POINTERS_EQUAL(changed_field == 1 ? static_cast<void*>(&action) : static_cast<void*>(&song),
			               clip.sampleHolder.audioFile);
			LONGS_EQUAL(changed_field == 2 ? 10 : 0, clip.sampleHolder.startPos);
			LONGS_EQUAL(changed_field == 3 ? 240 : 480, clip.sampleHolder.endPos);
			LONGS_EQUAL(0, action.record_calls);
			LONGS_EQUAL(0, pending.copy_calls);
			LONGS_EQUAL(0, handler.restart_calls);
			LONGS_EQUAL(0, handler.led_calls);
		}
	}
}
TEST(TempolessFinish, nonpositive_sample_duration_stops_before_undo_and_tempo) {
	for (int64_t duration : {int64_t(0), int64_t(-1), INT64_MIN}) {
		clip.sampleHolder.duration = duration;
		int allocation_calls = 0, tempo_calls = 0;
		actionLogger.on_allocate = [&] { ++allocation_calls; };
		handler.on_tempo = [&] { ++tempo_calls; };
		handler.finishTempolessRecording(true, 0, true);
		LONGS_EQUAL(0, allocation_calls);
		LONGS_EQUAL(0, tempo_calls);
		LONGS_EQUAL(96, clip.loopLength);
		LONGS_EQUAL(0, handler.restart_calls);
	}
}
} // namespace tempoless_finish_tests
