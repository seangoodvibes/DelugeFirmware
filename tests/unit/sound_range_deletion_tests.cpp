#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <new>

namespace sound_range_deletion_test {
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
	alignas(MultiRange) unsigned char storage[sizeof(MultiRange)];
	RangeArray() { new (storage) MultiRange; }
	MultiRange* getElement(int32_t) { return reinterpret_cast<MultiRange*>(storage); }
	void deleteAtIndex(int32_t) {
		++deletions;
		locked_during_deletion = AudioEngine::audioRoutineLocked;
	}
};
struct Sound {
	struct {
		RangeArray ranges;
	} sources[1];
	void killAllVoices() { ++kills; }
	void deleteMultiRange(int32_t s, int32_t r);
};
#include "sound_range_deletion.inc"
} // namespace sound_range_deletion_test
using namespace sound_range_deletion_test;
TEST_GROUP(SoundRangeDeletion){void setup() override{kills = destructions = deletions = 0;
locked_during_destruction = locked_during_deletion = false;
AudioEngine::audioRoutineLocked = false;
}
void teardown() override {
	AudioEngine::audioRoutineLocked = false;
}
}
;
TEST(SoundRangeDeletion, preserves_existing_audio_lock_after_range_reclamation) {
	Sound sound;
	AudioEngine::audioRoutineLocked = true;
	sound.deleteMultiRange(0, 0);
	CHECK_TRUE(AudioEngine::audioRoutineLocked);
	CHECK_TRUE(locked_during_destruction);
	CHECK_TRUE(locked_during_deletion);
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(1, deletions);
}
TEST(SoundRangeDeletion, releases_its_own_audio_lock_after_range_reclamation) {
	Sound sound;
	sound.deleteMultiRange(0, 0);
	CHECK_FALSE(AudioEngine::audioRoutineLocked);
	CHECK_TRUE(locked_during_destruction);
	CHECK_TRUE(locked_during_deletion);
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(1, destructions);
	LONGS_EQUAL(1, deletions);
}
