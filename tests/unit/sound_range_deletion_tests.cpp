#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <new>

namespace sound_range_deletion_test {
namespace session = deluge::gui::ui_session;
constexpr int32_t kNumSources = 1;
std::function<void()> on_kill;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct Song : Lifetime {};
Song* currentSong = nullptr;
namespace AudioEngine {
bool audioRoutineLocked = false;
}
int kills = 0, destructions = 0, deletions = 0;
bool locked_during_destruction = false, locked_during_deletion = false;
struct MultiRange {
	~MultiRange() {
		++destructions;
		locked_during_destruction = AudioEngine::audioRoutineLocked;
	}
};
struct RangeArray {
	uint32_t revision = 0;
	int32_t count = 1;
	uint32_t layout_revision() const { return revision; }
	int32_t getNumElements() const { return count; }
	alignas(MultiRange) unsigned char storage[sizeof(MultiRange)];
	RangeArray() { new (storage) MultiRange; }
	MultiRange* getElement(int32_t) { return reinterpret_cast<MultiRange*>(storage); }
	void deleteAtIndex(int32_t) {
		++deletions;
		locked_during_deletion = AudioEngine::audioRoutineLocked;
	}
};
struct Source : Lifetime {
	RangeArray ranges;
	int oscType = 0;
};
struct Sound : Lifetime {
	Source sources[kNumSources];
	bool active_voices = false;
	bool hasActiveVoices() const { return active_voices; }
	auto watch_sound_lifetime() { return watch_lifetime(); }
	void killAllVoices() {
		++kills;
		if (on_kill)
			on_kill();
	}
	bool try_delete_multi_range(int32_t, int32_t, const deluge::lifetime::callback_validation* = nullptr);
};
#include "sound_range_deletion.inc"
} // namespace sound_range_deletion_test
using namespace sound_range_deletion_test;
TEST_GROUP(SoundRangeDeletion) {
	Song song;
	void setup() override {
		currentSong = &song;
		on_kill = {};
		session::detail::active = session::Id::Local;
		kills = destructions = deletions = 0;
		locked_during_destruction = locked_during_deletion = false;
		AudioEngine::audioRoutineLocked = false;
	}
	void teardown() override {
		AudioEngine::audioRoutineLocked = false;
		on_kill = {};
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
	}
};
TEST(SoundRangeDeletion, preserves_existing_audio_lock_after_range_reclamation) {
	Sound sound;
	AudioEngine::audioRoutineLocked = true;
	CHECK_TRUE(sound.try_delete_multi_range(0, 0));
	CHECK_TRUE(AudioEngine::audioRoutineLocked);
	CHECK_TRUE(locked_during_destruction);
	CHECK_TRUE(locked_during_deletion);
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(1, deletions);
}
TEST(SoundRangeDeletion, releases_its_own_audio_lock_after_range_reclamation) {
	Sound sound;
	CHECK_TRUE(sound.try_delete_multi_range(0, 0));
	CHECK_FALSE(AudioEngine::audioRoutineLocked);
	CHECK_TRUE(locked_during_destruction);
	CHECK_TRUE(locked_during_deletion);
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(1, deletions);
}

TEST(SoundRangeDeletion, destroyed_owner_during_voice_cleanup_cancels_reclamation) {
	auto sound = std::make_unique<Sound>();
	on_kill = [&] { sound.reset(); };
	CHECK_FALSE(sound->try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, deletions);
}
TEST(SoundRangeDeletion, replaced_layout_at_same_address_cancels_reclamation) {
	Sound sound;
	on_kill = [&] { ++sound.sources[0].ranges.revision; };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
	LONGS_EQUAL(0, deletions);
}
TEST(SoundRangeDeletion, retired_source_cancels_reclamation) {
	Sound sound;
	on_kill = [&] { sound.sources[0].lifetime.retire(); };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, changed_source_type_cancels_reclamation) {
	Sound sound;
	on_kill = [&] { ++sound.sources[0].oscType; };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, changed_panel_cancels_reclamation_and_restores_owner) {
	Sound sound;
	on_kill = [] { session::detail::active = session::Id::Remote; };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	CHECK_TRUE(session::current() == session::Id::Local);
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, retired_song_cancels_reclamation) {
	Sound sound;
	on_kill = [&] { song.lifetime.retire(); };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, replaced_song_cancels_reclamation) {
	Sound sound;
	Song replacement;
	on_kill = [&] { currentSong = &replacement; };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, nested_voice_cancels_reclamation) {
	Sound sound;
	on_kill = [&] { sound.active_voices = true; };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, caller_selection_change_cancels_reclamation) {
	Sound sound;
	bool selected = true;
	const auto selection_valid = [&] { return selected; };
	deluge::lifetime::callback_validation validation(selection_valid);
	on_kill = [&] { selected = false; };
	CHECK_FALSE(sound.try_delete_multi_range(0, 0, &validation));
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, invalid_indices_do_not_start_voice_cleanup) {
	Sound sound;
	CHECK_FALSE(sound.try_delete_multi_range(-1, 0));
	CHECK_FALSE(sound.try_delete_multi_range(kNumSources, 0));
	CHECK_FALSE(sound.try_delete_multi_range(0, -1));
	CHECK_FALSE(sound.try_delete_multi_range(0, 1));
	LONGS_EQUAL(0, kills);
	LONGS_EQUAL(0, destructions);
}
TEST(SoundRangeDeletion, retired_owner_does_not_start_voice_cleanup) {
	Sound sound;
	sound.lifetime.retire();
	CHECK_FALSE(sound.try_delete_multi_range(0, 0));
	LONGS_EQUAL(0, kills);
}
TEST(SoundRangeDeletion, cancelled_entry_does_not_start_voice_cleanup) {
	Sound sound;
	const auto cancelled = [] { return false; };
	deluge::lifetime::callback_validation validation(cancelled);
	CHECK_FALSE(sound.try_delete_multi_range(0, 0, &validation));
	LONGS_EQUAL(0, kills);
}
