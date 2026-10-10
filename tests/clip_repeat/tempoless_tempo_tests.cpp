#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/action/action_identity.h"
#include <cstdint>
#include <functional>
#include <new>
namespace tempoless_tempo_tests {
constexpr uint32_t kMaxSequenceLength = 1000;
constexpr int BEFORE = 0;
struct ConsequenceTempoChange {
	uint64_t before, after;
	ConsequenceTempoChange(uint64_t old_tempo, uint64_t new_tempo) : before(old_tempo), after(new_tempo) {}
};
struct Action {
	uint64_t action_identity = deluge::model::next_action_identity();
	deluge::gui::ui_session::Id navigation_owner = deluge::gui::ui_session::Id::Local;
	void* captured_song = nullptr;
	ConsequenceTempoChange* consequence = nullptr;
	void addConsequence(ConsequenceTempoChange* value) { consequence = value; }
};
static struct {
	Action* firstAction[2]{};
} actionLogger;
struct GeneralMemoryAllocator {
	bool succeeds = false;
	int frees = 0;
	std::function<void()> on_allocate;
	int allocations = 0;
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocLowSpeed(size_t size) {
		++allocations;
		if (on_allocate)
			on_allocate();
		return succeeds ? ::operator new(size) : nullptr;
	}
};
void delugeDealloc(void* memory) {
	++GeneralMemoryAllocator::get().frees;
	::operator delete(memory);
}
struct Song {
	std::function<void()> on_tempo;
	uint32_t tick_duration = 100;
	uint64_t timePerTimerTickBig = 1234;
	int tempo_changes = 0;
	float assigned_duration = 0;
	uint32_t getTimePerTimerTickRounded() { return tick_duration; }
	void setTempoFromNumSamples(float duration, bool) {
		++tempo_changes;
		assigned_duration = duration;
		timePerTimerTickBig = 4321;
		auto callback = on_tempo;
		if (callback)
			callback();
	}
};
static Song* currentSong;
struct PlaybackHandler {
	int displays = 0;
	std::function<void()> on_display;
	void commandDisplayTempo() {
		++displays;
		if (on_display)
			on_display();
	}
	uint32_t setTempoFromAudioClipLength(uint64_t, Action*);
};
#include "tempoless_tempo_method.inc"
TEST_GROUP(TempolessTempo) {
	Song song;
	Action action;
	PlaybackHandler handler;
	void setup() override {
		currentSong = &song;
		GeneralMemoryAllocator::get() = {};
		actionLogger.firstAction[BEFORE] = &action;
		action.captured_song = &song;
		action.navigation_owner = deluge::gui::ui_session::current();
	}
	void teardown() override {
		delete action.consequence;
		GeneralMemoryAllocator::get() = {};
		actionLogger.firstAction[BEFORE] = nullptr;
		currentSong = nullptr;
	}
};
TEST(TempolessTempo, invalid_inputs_do_not_change_tempo_or_record_history) {
	currentSong = nullptr;
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, &action));
	currentSong = &song;
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(0, &action));
	song.tick_duration = 0;
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, &action));
	song.tick_duration = 100;
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(UINT64_MAX, &action));
	LONGS_EQUAL(0, song.tempo_changes);
	LONGS_EQUAL(0, handler.displays);
	LONGS_EQUAL(0, GeneralMemoryAllocator::get().allocations);
}
TEST(TempolessTempo, valid_duration_keeps_existing_power_of_two_tick_selection) {
	LONGS_EQUAL(6, handler.setTempoFromAudioClipLength(480, nullptr));
	DOUBLES_EQUAL(80.0, song.assigned_duration, 0.001);
	LONGS_EQUAL(1, song.tempo_changes);
	LONGS_EQUAL(1, handler.displays);
}
TEST(TempolessTempo, largest_supported_tick_count_succeeds_before_next_doubling_is_rejected) {
	LONGS_EQUAL(768, handler.setTempoFromAudioClipLength(76800, nullptr));
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(153600, &action));
	LONGS_EQUAL(1, song.tempo_changes);
	LONGS_EQUAL(1, handler.displays);
	LONGS_EQUAL(0, GeneralMemoryAllocator::get().allocations);
}
TEST(TempolessTempo, history_allocation_failure_preserves_tempo) {
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, &action));
	LONGS_EQUAL(1234, song.timePerTimerTickBig);
	LONGS_EQUAL(0, song.tempo_changes);
	LONGS_EQUAL(0, handler.displays);
}
TEST(TempolessTempo, successful_history_records_before_and_after_tempo) {
	GeneralMemoryAllocator::get().succeeds = true;
	LONGS_EQUAL(6, handler.setTempoFromAudioClipLength(480, &action));
	CHECK_TRUE(action.consequence != nullptr);
	LONGS_EQUAL(1234, action.consequence->before);
	LONGS_EQUAL(4321, action.consequence->after);
	LONGS_EQUAL(1, handler.displays);
}
TEST(TempolessTempo, allocation_callbacks_cannot_overwrite_tempo_or_publish_stale_history) {
	using namespace deluge::gui::ui_session;
	for (int change = 0; change < 6; ++change) {
		currentSong = &song;
		song.timePerTimerTickBig = 1234;
		actionLogger.firstAction[BEFORE] = &action;
		auto& allocator = GeneralMemoryAllocator::get();
		allocator = {};
		allocator.succeeds = true;
		allocator.on_allocate = [&] {
			switch (change) {
			case 0:
				currentSong = nullptr;
				break;
			case 1:
				song.timePerTimerTickBig = 5678;
				break;
			case 2:
				actionLogger.firstAction[BEFORE] = nullptr;
				break;
			case 3:
				navigation.for_owner(Id::Local).structural_refresh.request();
				break;
			case 4:
				navigation.for_owner(Id::Remote).structural_refresh.request();
				break;
			case 5:
				action.captured_song = nullptr;
				break;
			}
		};
		LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, &action));
		LONGS_EQUAL(0, song.tempo_changes);
		LONGS_EQUAL(0, handler.displays);
		LONGS_EQUAL(1, allocator.frees);
		POINTERS_EQUAL(nullptr, action.consequence);
	}
}
TEST(TempolessTempo, deleted_action_during_allocation_or_tempo_change_is_not_accessed) {
	for (bool during_tempo : {false, true}) {
		auto* target_action = new Action;
		target_action->captured_song = &song;
		actionLogger.firstAction[BEFORE] = target_action;
		auto& allocator = GeneralMemoryAllocator::get();
		allocator = {};
		allocator.succeeds = true;
		song.on_tempo = {};
		auto remove_action = [&] {
			actionLogger.firstAction[BEFORE] = nullptr;
			delete target_action;
		};
		if (during_tempo)
			song.on_tempo = remove_action;
		else
			allocator.on_allocate = remove_action;
		LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, target_action));
		LONGS_EQUAL(1, allocator.frees);
		LONGS_EQUAL(0, handler.displays);
	}
}
TEST(TempolessTempo, deleted_song_during_allocation_or_tempo_change_is_not_accessed) {
	for (bool during_tempo : {false, true}) {
		auto* target_song = new Song;
		currentSong = target_song;
		action.captured_song = target_song;
		auto& allocator = GeneralMemoryAllocator::get();
		allocator = {};
		allocator.succeeds = true;
		auto remove_song = [&] {
			currentSong = nullptr;
			delete target_song;
		};
		if (during_tempo)
			target_song->on_tempo = remove_song;
		else
			allocator.on_allocate = remove_song;
		LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, &action));
		LONGS_EQUAL(1, allocator.frees);
		POINTERS_EQUAL(nullptr, action.consequence);
		LONGS_EQUAL(0, handler.displays);
	}
}
TEST(TempolessTempo, display_tempo_change_does_not_report_stale_tick_result) {
	for (bool with_history : {false, true}) {
		GeneralMemoryAllocator::get().succeeds = true;
		handler.on_display = [&] { song.timePerTimerTickBig = 5678; };
		LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, with_history ? &action : nullptr));
		LONGS_EQUAL(5678, song.timePerTimerTickBig);
		if (with_history)
			LONGS_EQUAL(4321, action.consequence->after);
	}
}
TEST(TempolessTempo, display_callback_cannot_retain_deleted_song) {
	auto* target_song = new Song;
	currentSong = target_song;
	action.captured_song = target_song;
	GeneralMemoryAllocator::get().succeeds = true;
	handler.on_display = [&] {
		currentSong = nullptr;
		delete target_song;
	};
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, &action));
	CHECK_TRUE(action.consequence != nullptr);
}
TEST(TempolessTempo, display_callback_cannot_retain_deleted_action) {
	auto* target_action = new Action;
	target_action->captured_song = &song;
	actionLogger.firstAction[BEFORE] = target_action;
	GeneralMemoryAllocator::get().succeeds = true;
	handler.on_display = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action->consequence;
		delete target_action;
	};
	LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, target_action));
}
TEST(TempolessTempo, display_structural_invalidation_rejects_result) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		handler.on_display = [&] { navigation.for_owner(owner).structural_refresh.request(); };
		LONGS_EQUAL(0, handler.setTempoFromAudioClipLength(480, nullptr));
	}
}
TEST(TempolessTempo, nested_display_session_restores_valid_tempo_result) {
	using namespace deluge::gui::ui_session;
	handler.on_display = [] { Scope nested(Id::Remote); };
	LONGS_EQUAL(6, handler.setTempoFromAudioClipLength(480, nullptr));
	LONGS_EQUAL(4321, song.timePerTimerTickBig);
}
TEST(TempolessTempo, same_address_action_replacement_is_rejected_at_callback_boundaries) {
	for (int stage = 0; stage < 3; ++stage) {
		auto* target_action = new Action;
		target_action->captured_song = &song;
		actionLogger.firstAction[BEFORE] = target_action;
		auto& allocator = GeneralMemoryAllocator::get();
		allocator = {};
		allocator.succeeds = true;
		song.on_tempo = {};
		handler = {};
		auto replace_action = [&] {
			delete target_action->consequence;
			target_action->~Action();
			new (target_action) Action;
			target_action->captured_song = &song;
		};
		if (stage == 0)
			allocator.on_allocate = replace_action;
		if (stage == 1)
			song.on_tempo = replace_action;
		if (stage == 2)
			handler.on_display = replace_action;
		const uint32_t result = handler.setTempoFromAudioClipLength(480, target_action);
		const bool empty_history = target_action->consequence == nullptr;
		delete target_action;
		LONGS_EQUAL(0, result);
		CHECK_TRUE(empty_history);
		LONGS_EQUAL(stage < 2 ? 1 : 0, allocator.frees);
	}
}
} // namespace tempoless_tempo_tests
