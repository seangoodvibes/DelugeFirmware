#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <string>

namespace audio_loading_context_test {
enum class ThingType { NONE, SONG, SYNTH };
enum class AlternateLoadDirStatus { NONE_SET, MIGHT_EXIST };
struct AudioFileManager {
	uint32_t loading_context_revision_ = 0;
	std::string alternateAudioFileLoadPath;
	AlternateLoadDirStatus alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	ThingType thingTypeBeingLoaded = ThingType::NONE;
	void thingBeginningLoading(ThingType);
	void thingFinishedLoading();
	bool finish_loading_if_current(uint32_t);
};
#include "audio_loading_context.inc"
} // namespace audio_loading_context_test
using namespace audio_loading_context_test;
TEST_GROUP(AudioLoadingContext) {
	AudioFileManager manager;
};
TEST(AudioLoadingContext, current_owner_finishes_once) {
	manager.alternateAudioFileLoadPath = "song";
	manager.thingBeginningLoading(ThingType::SONG);
	const auto revision = manager.loading_context_revision_;
	CHECK(manager.finish_loading_if_current(revision));
	CHECK(manager.alternateAudioFileLoadPath.empty());
	CHECK(manager.thingTypeBeingLoaded == ThingType::NONE);
	CHECK(manager.alternateLoadDirStatus == AlternateLoadDirStatus::NONE_SET);
	CHECK_FALSE(manager.finish_loading_if_current(revision));
}
TEST(AudioLoadingContext, stale_owner_preserves_new_directory_and_type) {
	manager.thingBeginningLoading(ThingType::SYNTH);
	const auto revision = manager.loading_context_revision_;
	manager.alternateAudioFileLoadPath = "new song";
	manager.thingBeginningLoading(ThingType::SONG);
	CHECK_FALSE(manager.finish_loading_if_current(revision));
	STRCMP_EQUAL("new song", manager.alternateAudioFileLoadPath.c_str());
	CHECK(manager.thingTypeBeingLoaded == ThingType::SONG);
	CHECK(manager.alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST);
}
TEST(AudioLoadingContext, finished_nested_context_does_not_revive_old_owner) {
	manager.thingBeginningLoading(ThingType::SYNTH);
	const auto revision = manager.loading_context_revision_;
	manager.thingBeginningLoading(ThingType::SONG);
	manager.thingFinishedLoading();
	CHECK_FALSE(manager.finish_loading_if_current(revision));
}
