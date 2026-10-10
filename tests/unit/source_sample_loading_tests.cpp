#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <vector>

namespace source_sample_loading_test {
namespace session = deluge::gui::ui_session;
using deluge::lifetime::callback_validation;
enum class Error { NONE, ABORTED_BY_USER, FILE_NOT_FOUND };
enum class OscType { SAMPLE, WAVETABLE, SQUARE };
enum class AlternateLoadDirStatus { NONE_SET, MIGHT_EXIST };
constexpr int kNumSources = 2, CLUSTER_ENQUEUE = 1;
int loads = 0, publications = 0, yields = 0, finishes = 0;
std::function<void()> on_audio, on_abort, on_load, on_setup;
Error file_error = Error::NONE;
bool abort_requested = false;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct Song : Lifetime {};
Song* currentSong = nullptr;
struct Holder {
	Error loadFile(bool, bool, bool, int, void*, bool, const callback_validation* validation) {
		++loads;
		if (on_load)
			on_load();
		if (!validation->valid())
			return Error::ABORTED_BY_USER;
		++publications;
		return file_error;
	}
};
struct Range {
	Holder holder;
	Holder* getAudioFileHolder() { return &holder; }
};
struct Source {
	OscType oscType = OscType::SAMPLE;
	struct {
		bool reversed = false;
		bool isCurrentlyReversed() { return reversed; }
	} sampleControls;
	struct {
		std::vector<Range*> entries;
		int32_t getNumElements() { return entries.size(); }
		Range* getElement(int32_t index) { return entries.at(index); }
	} ranges;
	Error loadAllSamples(bool, const callback_validation* = nullptr);
};
struct Sound {
	Source sources[kNumSources];
	Error loadAllAudioFiles(bool, const callback_validation* = nullptr);
};
struct {
	AlternateLoadDirStatus alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	uint32_t revision = 0;
	uint32_t loading_context_revision() const { return revision; }
	bool finish_loading_if_current(uint32_t expected) {
		if (expected != revision)
			return false;
		thingFinishedLoading();
		return true;
	}
	void thingFinishedLoading() {
		++revision;
		++finishes;
		alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	}
} audioFileManager;
struct SoundDrum : Sound, Lifetime {
	Error loadAllSamples(bool, const callback_validation* = nullptr);
};
struct SoundInstrument : Sound, Lifetime {
	Error loadAllAudioFiles(bool);
	Error setupDefaultAudioFileDir() {
		if (on_setup)
			on_setup();
		++audioFileManager.revision;
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
		return Error::NONE;
	}
};
bool shouldAbortLoading() {
	if (on_abort)
		on_abort();
	return abort_requested;
}
namespace AudioEngine {
void logAction(const char*) {
}
void routineWithClusterLoading() {
	++yields;
	if (on_audio)
		on_audio();
}
} // namespace AudioEngine
#include "sound_drum_sample_loading.inc"
#include "sound_instrument_sample_loading.inc"
#include "sound_sample_loading.inc"
#include "source_sample_loading.inc"
} // namespace source_sample_loading_test
using namespace source_sample_loading_test;
TEST_GROUP(SourceSampleLoading) {
	Song song;
	SoundInstrument instrument;
	Range first, second;
	void setup() override {
		currentSong = &song;
		session::detail::active = session::Id::Local;
		instrument.sources[0].ranges.entries = {&first};
		instrument.sources[1].ranges.entries = {&second};
		on_audio = on_abort = on_load = on_setup = {};
		loads = publications = yields = finishes = 0;
		file_error = Error::NONE;
		abort_requested = false;
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	}
	void teardown() override {
		on_audio = on_abort = on_load = on_setup = {};
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
	}
};
TEST(SourceSampleLoading, normal_loads_preserve_cadence_and_non_sample_filter) {
	instrument.sources[0].ranges.entries = {&first, &first, &first, &first, &first};
	instrument.sources[1].oscType = OscType::SQUARE;
	CHECK(instrument.loadAllAudioFiles(true) == Error::NONE);
	LONGS_EQUAL(5, publications);
	LONGS_EQUAL(2, yields);
	LONGS_EQUAL(1, finishes);
}
TEST(SourceSampleLoading, destroyed_instrument_during_audio_service_cancels_before_range_use) {
	auto owner = std::make_unique<SoundInstrument>();
	owner->sources[0].ranges.entries = {&first};
	on_audio = [&] { owner.reset(); };
	CHECK(owner->loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
	LONGS_EQUAL(1, finishes);
}
TEST(SourceSampleLoading, destroyed_drum_during_holder_lookup_cancels_publication) {
	auto owner = std::make_unique<SoundDrum>();
	owner->sources[0].ranges.entries = {&first};
	on_load = [&] { owner.reset(); };
	CHECK(owner->loadAllSamples(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, destroyed_instrument_during_setup_cancels_scan) {
	auto owner = std::make_unique<SoundInstrument>();
	on_setup = [&] { owner.reset(); };
	CHECK(owner->loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, yields);
	LONGS_EQUAL(1, finishes);
}
TEST(SourceSampleLoading, retired_song_stops_next_source) {
	on_load = [&] { song.lifetime.retire(); };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, changed_panel_stops_loading_and_restores_caller) {
	on_abort = [&] { session::detail::active = session::Id::Remote; };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
	CHECK(session::current() == session::Id::Local);
}
TEST(SourceSampleLoading, resized_ranges_cancel_before_dereference) {
	on_audio = [&] { instrument.sources[0].ranges.entries.clear(); };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
}
TEST(SourceSampleLoading, replaced_range_during_lookup_cancels_publication) {
	on_load = [&] { instrument.sources[0].ranges.entries[0] = &second; };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, changed_reverse_setting_cancels_publication) {
	on_load = [&] { instrument.sources[0].sampleControls.reversed = true; };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, changed_oscillator_type_cancels_before_lookup) {
	on_audio = [&] { instrument.sources[0].oscType = OscType::SQUARE; };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
}
TEST(SourceSampleLoading, cancellation_return_stops_next_source_even_if_owner_survives) {
	file_error = Error::ABORTED_BY_USER;
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
}
TEST(SourceSampleLoading, missing_files_keep_existing_continue_loading_policy) {
	file_error = Error::FILE_NOT_FOUND;
	CHECK(instrument.loadAllAudioFiles(true) == Error::NONE);
	LONGS_EQUAL(2, loads);
}
TEST(SourceSampleLoading, invalid_entry_does_not_service_audio) {
	instrument.lifetime.retire();
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, yields);
	bool valid = false;
	auto check = [&] { return valid; };
	callback_validation validation(check);
	CHECK(instrument.sources[0].loadAllSamples(true, &validation) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, yields);
}
TEST(SourceSampleLoading, no_song_cached_loading_preserves_existing_directory) {
	currentSong = nullptr;
	audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
	abort_requested = true; // Cached-only loading does not poll the abort control.
	CHECK(instrument.loadAllAudioFiles(false) == Error::NONE);
	LONGS_EQUAL(2, loads);
	LONGS_EQUAL(0, finishes);
}

TEST(SourceSampleLoading, caller_cancellation_reaches_holder_before_publication) {
	SoundDrum drum;
	drum.sources[0].ranges.entries = {&first};
	bool valid = true;
	auto check = [&] { return valid; };
	callback_validation validation(check);
	on_load = [&] { valid = false; };
	CHECK(drum.loadAllSamples(true, &validation) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, invalid_caller_does_not_start_drum_loading) {
	SoundDrum drum;
	drum.sources[0].ranges.entries = {&first};
	bool valid = false;
	auto check = [&] { return valid; };
	callback_validation validation(check);
	CHECK(drum.loadAllSamples(true, &validation) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, yields);
	LONGS_EQUAL(0, loads);
}

TEST(SourceSampleLoading, newer_directory_context_survives_instrument_cancellation) {
	on_load = [&] { ++audioFileManager.revision; };
	CHECK(instrument.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
	LONGS_EQUAL(0, finishes);
	CHECK(audioFileManager.alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST);
}
TEST(SourceSampleLoading, standalone_drum_rejects_newer_directory_context) {
	SoundDrum drum;
	drum.sources[0].ranges.entries = {&first};
	on_load = [&] { ++audioFileManager.revision; };
	CHECK(drum.loadAllSamples(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, publications);
}
