#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <vector>

namespace kit_sample_loading_test {
namespace session = deluge::gui::ui_session;
enum class Error { NONE, ABORTED_BY_USER, FILE_NOT_FOUND };
enum class AlternateLoadDirStatus { NONE_SET, MIGHT_EXIST };
std::function<void()> on_setup, on_abort, on_load;
int loads = 0, finishes = 0, setups = 0, publications = 0;
Error setup_error = Error::NONE, load_error = Error::NONE;
bool abort_requested = false;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct Song : Lifetime {};
Song* currentSong = nullptr;
struct Drum : Lifetime {
	Drum* next = nullptr;
	Error loadAllSamples(bool, const deluge::lifetime::callback_validation* validation = nullptr) {
		++loads;
		if (on_load)
			on_load();
		if (validation && !validation->valid())
			return Error::ABORTED_BY_USER;
		++publications;
		return load_error;
	}
};
struct NoteRow {
	Drum* drum = nullptr;
	bool muted = false, empty = false;
	bool hasNoNotes() { return empty; }
};
struct Kit;
struct InstrumentClip : Lifetime {
	Kit* output = nullptr;
	struct {
		std::vector<NoteRow*> rows;
		int32_t getNumElements() { return rows.size(); }
		NoteRow* getElement(int32_t index) { return rows.at(index); }
	} noteRows;
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
struct Kit : Lifetime {
	Drum* firstDrum = nullptr;
	InstrumentClip* activeClip = nullptr;
	Error setupDefaultAudioFileDir() {
		++setups;
		if (on_setup)
			on_setup();
		if (setup_error == Error::NONE) {
			++audioFileManager.revision;
			audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
		}
		return setup_error;
	}
	int32_t getDrumIndex(Drum* drum) {
		int32_t index = 0;
		for (auto* candidate = firstDrum; candidate; candidate = candidate->next, ++index)
			if (candidate == drum)
				return index;
		return -1;
	}
	Error loadAllAudioFiles(bool);
	void loadCrucialAudioFilesOnly();
};
bool shouldAbortLoading() {
	if (on_abort)
		on_abort();
	return abort_requested;
}
namespace AudioEngine {
void logAction(const char*) {
}
} // namespace AudioEngine
#include "kit_sample_loading.inc"
} // namespace kit_sample_loading_test
using namespace kit_sample_loading_test;

TEST_GROUP(KitSampleLoading) {
	Kit kit;
	Song song;
	Drum first, second;
	InstrumentClip clip;
	NoteRow row, other_row;
	void setup() override {
		currentSong = &song;
		session::detail::active = session::Id::Local;
		on_setup = on_abort = on_load = {};
		loads = finishes = setups = publications = 0;
		setup_error = load_error = Error::NONE;
		abort_requested = false;
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
		kit.firstDrum = &first;
		first.next = &second;
		kit.activeClip = &clip;
		clip.output = &kit;
		row.drum = &first;
		other_row.drum = &second;
		clip.noteRows.rows = {&row, &other_row};
	}
	void teardown() override {
		on_setup = on_abort = on_load = {};
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
	}
};
TEST(KitSampleLoading, normal_full_and_crucial_loads_finish_owned_directory) {
	CHECK(kit.loadAllAudioFiles(true) == Error::NONE);
	LONGS_EQUAL(2, loads);
	LONGS_EQUAL(1, finishes);
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(4, loads);
	LONGS_EQUAL(2, finishes);
}
TEST(KitSampleLoading, destroyed_kit_during_setup_does_not_begin_drum_scan) {
	auto owner = std::make_unique<Kit>();
	owner->firstDrum = &first;
	on_setup = [&] { owner.reset(); };
	CHECK(owner->loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
	LONGS_EQUAL(1, finishes);
}
TEST(KitSampleLoading, destroyed_drum_during_abort_check_is_not_loaded) {
	auto drum = std::make_unique<Drum>();
	kit.firstDrum = drum.get();
	on_abort = [&] {
		kit.firstDrum = &second;
		drum.reset();
	};
	CHECK(kit.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, loads);
	LONGS_EQUAL(1, finishes);
}
TEST(KitSampleLoading, destroyed_kit_during_drum_load_does_not_continue) {
	auto owner = std::make_unique<Kit>();
	owner->firstDrum = &first;
	on_load = [&] { owner.reset(); };
	CHECK(owner->loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(1, finishes);
}
TEST(KitSampleLoading, removed_drum_does_not_follow_stale_next_link) {
	on_load = [&] { kit.firstDrum = &second; };
	CHECK(kit.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
}
TEST(KitSampleLoading, retired_song_and_changed_owner_cancel_and_restore_owner) {
	on_load = [&] {
		song.lifetime.retire();
		session::detail::active = session::Id::Remote;
	};
	CHECK(kit.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	CHECK(session::current() == session::Id::Local);
}
TEST(KitSampleLoading, destroyed_active_clip_stops_crucial_row_scan) {
	auto selected = std::make_unique<InstrumentClip>();
	selected->output = &kit;
	selected->noteRows.rows = {&row, &other_row};
	kit.activeClip = selected.get();
	on_load = [&] {
		selected.reset();
		kit.activeClip = &clip;
	};
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(1, finishes);
}
TEST(KitSampleLoading, changed_rows_stop_crucial_scan) {
	on_load = [&] { clip.noteRows.rows = {&other_row}; };
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
}
TEST(KitSampleLoading, same_size_replaced_row_stops_crucial_scan) {
	on_load = [&] { clip.noteRows.rows[0] = &other_row; };
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
}
TEST(KitSampleLoading, destroyed_drum_stops_crucial_scan) {
	auto drum = std::make_unique<Drum>();
	drum->next = &second;
	kit.firstDrum = drum.get();
	row.drum = drum.get();
	on_load = [&] {
		kit.firstDrum = &second;
		drum.reset();
	};
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
}
TEST(KitSampleLoading, normal_filtering_and_existing_directory_remain_supported) {
	audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
	row.muted = true;
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	other_row.empty = true;
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, setups);
	LONGS_EQUAL(0, finishes);
}
TEST(KitSampleLoading, setup_and_load_errors_preserve_cleanup_contract) {
	setup_error = Error::FILE_NOT_FOUND;
	CHECK(kit.loadAllAudioFiles(true) == Error::FILE_NOT_FOUND);
	LONGS_EQUAL(0, loads);
	LONGS_EQUAL(0, finishes);
	setup_error = Error::NONE;
	load_error = Error::FILE_NOT_FOUND;
	CHECK(kit.loadAllAudioFiles(true) == Error::FILE_NOT_FOUND);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(1, finishes);
}
TEST(KitSampleLoading, invalid_entry_does_not_start_loading) {
	kit.lifetime.retire();
	CHECK(kit.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(0, loads);
	LONGS_EQUAL(0, setups);
}

TEST(KitSampleLoading, crucial_cancellation_does_not_continue_to_another_drum) {
	load_error = Error::ABORTED_BY_USER;
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(1, finishes);
}

TEST(KitSampleLoading, detached_drum_cancels_inner_publication) {
	on_load = [&] { kit.firstDrum = &second; };
	CHECK(kit.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}
TEST(KitSampleLoading, reassigned_row_cancels_inner_publication) {
	on_load = [&] { row.drum = &second; };
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}
TEST(KitSampleLoading, changed_active_clip_cancels_inner_publication) {
	on_load = [&] { kit.activeClip = nullptr; };
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
}

TEST(KitSampleLoading, newer_directory_context_survives_full_load_cancellation) {
	on_load = [&] { ++audioFileManager.revision; };
	CHECK(kit.loadAllAudioFiles(true) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
	LONGS_EQUAL(0, finishes);
	CHECK(audioFileManager.alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST);
}
TEST(KitSampleLoading, newer_directory_context_survives_crucial_load_cancellation) {
	on_load = [&] { ++audioFileManager.revision; };
	kit.loadCrucialAudioFilesOnly();
	LONGS_EQUAL(1, loads);
	LONGS_EQUAL(0, publications);
	LONGS_EQUAL(0, finishes);
}
