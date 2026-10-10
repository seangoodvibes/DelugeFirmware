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
std::function<void()> on_audio, on_abort, on_load, on_setup, on_claim, on_detach;
int claims = 0, detachments = 0;
bool claim_result = true;
Error file_error = Error::NONE;
bool abort_requested = false;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct Song : Lifetime {};
Song* currentSong = nullptr;
struct Sample {
	uint64_t lengthInSamples = 100;
} sample;
struct Holder {
	Sample* audioFile = &sample;
	uint64_t endPos = 200;
	bool claimClusterReasons(bool, int, const callback_validation* validation) {
		++claims;
		if (on_claim)
			on_claim();
		if (!validation->valid())
			return false;
		++publications;
		return claim_result;
	}
	bool setAudioFile(Sample* file, bool, bool, int, const callback_validation* validation) {
		if (on_detach)
			on_detach();
		if (!validation->valid())
			return false;
		audioFile = file;
		++detachments;
		return true;
	}
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
using SampleHolder = Holder;
struct Range {
	Holder holder;
	Holder* getAudioFileHolder() { return &holder; }
};
struct Source : Lifetime {
	OscType oscType = OscType::SAMPLE;
	struct {
		bool reversed = false, invertReversed = false;
		bool isCurrentlyReversed() { return invertReversed ? !reversed : reversed; }
	} sampleControls;
	struct {
		std::vector<Range*> entries;
		int32_t getNumElements() { return entries.size(); }
		Range* getElement(int32_t index) { return entries.at(index); }
	} ranges;
	Error loadAllSamples(bool, const callback_validation* = nullptr);
	bool detachAllAudioFiles();
	void setReversed(bool);
};
struct Sound {
	Source sources[kNumSources];
	Error loadAllAudioFiles(bool, const callback_validation* = nullptr);
	void detachSourcesFromAudioFiles();
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
#include "sound_sample_detachment.inc"
#include "sound_sample_loading.inc"
#include "source_sample_loading.inc"
#include "source_sample_operations.inc"
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
		on_audio = on_abort = on_load = on_setup = on_claim = on_detach = {};
		loads = publications = yields = finishes = claims = detachments = 0;
		claim_result = true;
		file_error = Error::NONE;
		abort_requested = false;
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	}
	void teardown() override {
		on_audio = on_abort = on_load = on_setup = on_claim = on_detach = {};
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

TEST(SourceSampleLoading, direct_source_loading_cancels_after_source_destruction) {
	auto source = std::make_unique<Source>();
	source->ranges.entries = {&first};
	on_audio = [&] { source.reset(); };
	CHECK(source->loadAllSamples(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
}
TEST(SourceSampleLoading, direct_source_lookup_rejects_destroyed_source_publication) {
	auto source = std::make_unique<Source>();
	source->ranges.entries = {&first};
	on_load = [&] { source.reset(); };
	CHECK(source->loadAllSamples(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, retired_source_does_not_begin_loading) {
	instrument.sources[0].lifetime.retire();
	CHECK(instrument.sources[0].loadAllSamples(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, yields);
}

TEST(SourceSampleLoading, direct_source_loading_rejects_same_address_replacement) {
	alignas(Source) unsigned char storage[sizeof(Source)];
	auto* source = new (storage) Source;
	source->ranges.entries = {&first};
	on_audio = [&] {
		source->~Source();
		source = new (storage) Source;
		source->ranges.entries = {&second};
	};
	const auto error = source->loadAllSamples(true);
	source->~Source();
	CHECK(error == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
}

TEST(SourceSampleLoading, detach_cancels_after_source_destruction_during_audio_service) {
	auto source = std::make_unique<Source>();
	source->ranges.entries = {&first};
	on_audio = [&] { source.reset(); };
	CHECK_FALSE(source->detachAllAudioFiles());
	LONGS_EQUAL(0, detachments);
}
TEST(SourceSampleLoading, detach_cancels_after_range_replacement) {
	on_audio = [&] { instrument.sources[0].ranges.entries[0] = &second; };
	CHECK_FALSE(instrument.sources[0].detachAllAudioFiles());
	LONGS_EQUAL(0, detachments);
}
TEST(SourceSampleLoading, detachment_cancellation_stops_later_sound_sources) {
	auto owner = std::make_unique<SoundInstrument>();
	owner->sources[0].ranges.entries = {&first};
	owner->sources[1].ranges.entries = {&second};
	on_audio = [&] { owner.reset(); };
	owner->detachSourcesFromAudioFiles();
	LONGS_EQUAL(1, yields);
	LONGS_EQUAL(0, detachments);
}
TEST(SourceSampleLoading, normal_detachment_preserves_cadence_and_clears_all_files) {
	instrument.sources[0].ranges.entries = {&first, &first, &first, &first, &first, &first, &first, &first, &second};
	CHECK(instrument.sources[0].detachAllAudioFiles());
	LONGS_EQUAL(9, detachments);
	LONGS_EQUAL(2, yields);
	POINTERS_EQUAL(nullptr, first.holder.audioFile);
	POINTERS_EQUAL(nullptr, second.holder.audioFile);
}
TEST(SourceSampleLoading, reverse_rejects_destroyed_source_before_cluster_publication) {
	auto source = std::make_unique<Source>();
	source->ranges.entries = {&first};
	on_claim = [&] { source.reset(); };
	source->setReversed(true);
	LONGS_EQUAL(1, claims);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, reverse_rejects_nested_direction_change) {
	instrument.sources[0].ranges.entries = {&first, &second};
	on_claim = [&] { instrument.sources[0].sampleControls.reversed = false; };
	instrument.sources[0].setReversed(true);
	LONGS_EQUAL(1, claims);
	LONGS_EQUAL(0, publications);
	CHECK_FALSE(instrument.sources[0].sampleControls.reversed);
	LONGS_EQUAL(200, second.holder.endPos);
}
TEST(SourceSampleLoading, reverse_honors_cluster_cancellation_with_live_owner) {
	instrument.sources[0].ranges.entries = {&first, &second};
	claim_result = false;
	instrument.sources[0].setReversed(true);
	LONGS_EQUAL(1, claims);
	LONGS_EQUAL(200, second.holder.endPos);
}
TEST(SourceSampleLoading, reverse_normal_path_clamps_end_and_skips_empty_holders) {
	instrument.sources[0].ranges.entries = {&first, &second};
	second.holder.audioFile = nullptr;
	instrument.sources[0].setReversed(true);
	LONGS_EQUAL(1, claims);
	LONGS_EQUAL(1, publications);
	LONGS_EQUAL(100, first.holder.endPos);
	LONGS_EQUAL(200, second.holder.endPos);
}
TEST(SourceSampleLoading, changed_panel_cancels_reverse_and_restores_owner) {
	on_claim = [&] { session::detail::active = session::Id::Remote; };
	instrument.sources[0].setReversed(true);
	LONGS_EQUAL(0, publications);
	CHECK(session::current() == session::Id::Local);
}
TEST(SourceSampleLoading, retired_source_does_not_detach_or_reverse) {
	instrument.sources[0].lifetime.retire();
	CHECK_FALSE(instrument.sources[0].detachAllAudioFiles());
	instrument.sources[0].setReversed(true);
	CHECK_FALSE(instrument.sources[0].sampleControls.reversed);
	LONGS_EQUAL(0, yields);
	LONGS_EQUAL(0, claims);
}

TEST(SourceSampleLoading, changed_inversion_cancels_reverse_publication) {
	on_claim = [&] { instrument.sources[0].sampleControls.invertReversed = true; };
	instrument.sources[0].setReversed(true);
	LONGS_EQUAL(1, claims);
	LONGS_EQUAL(0, publications);
}
TEST(SourceSampleLoading, effective_reverse_direction_controls_end_clamping) {
	instrument.sources[0].setReversed(false);
	LONGS_EQUAL(200, first.holder.endPos);
	instrument.sources[0].sampleControls.invertReversed = true;
	instrument.sources[0].setReversed(false);
	LONGS_EQUAL(100, first.holder.endPos);
	LONGS_EQUAL(2, claims);
}
TEST(SourceSampleLoading, holder_detachment_cancellation_stops_later_sources) {
	on_detach = [&] { instrument.sources[0].ranges.entries.clear(); };
	instrument.detachSourcesFromAudioFiles();
	LONGS_EQUAL(0, detachments);
	LONGS_EQUAL(1, yields);
	POINTERS_EQUAL(&sample, second.holder.audioFile);
}
