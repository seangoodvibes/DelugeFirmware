#include "CppUTest/TestHarness.h"
#include "gui/ui/recording_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
namespace recording_setup_test {
using deluge::gui::ui_session::Id;
deluge::gui::ui_session::RecordingSession recording_session;
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
Song* currentSong = nullptr;
int screen;
void* active_ui = &screen;
void* getCurrentUI() {
	return active_ui;
}
enum class AudioInputChannel { NONE, INPUT, OUTPUT };
enum class AudioRecordingFolder { RECORD };
enum class Error { INSUFFICIENT_RAM };
constexpr int kInternalButtonPressLatency = 1;
struct SampleRecorder {
	bool pointerHeldElsewhere = true, allowFileAlterationAfter = false, allowNormalization = false;
};
struct RecorderConfig {
	bool value;
};
std::function<void()> on_allocate, on_discard;
int allocations = 0, discards = 0, errors = 0;
bool fail_allocation = false;
namespace AudioEngine {
SampleRecorder* getNewRecorder(int, AudioRecordingFolder, AudioInputChannel, bool, bool, int, bool, void*,
                               RecorderConfig) {
	++allocations;
	if (on_allocate)
		on_allocate();
	return fail_allocation ? nullptr : new SampleRecorder;
}
void discardRecorder(SampleRecorder* recorder) {
	++discards;
	CHECK_FALSE(recorder->pointerHeldElsewhere);
	delete recorder;
	if (on_discard)
		on_discard();
}
} // namespace AudioEngine
struct Display {
	void displayError(Error) { ++errors; }
} display_instance;
auto* display = &display_instance;
struct AudioRecorder {
	static SampleRecorder* recorder;
	static AudioInputChannel recordingSource;
	bool setupRecordingToFile(AudioInputChannel, int32_t, AudioRecordingFolder, bool, bool);
};
SampleRecorder* AudioRecorder::recorder = nullptr;
AudioInputChannel AudioRecorder::recordingSource = AudioInputChannel::NONE;
#include "recording_setup.inc"
} // namespace recording_setup_test
using namespace recording_setup_test;
TEST_GROUP(RecordingSetup) {
	Song song;
	AudioRecorder ui;
	bool start(bool normalize = true) {
		return ui.setupRecordingToFile(AudioInputChannel::INPUT, 2, AudioRecordingFolder::RECORD, false, normalize);
	}
	void setup() override {
		recording_session = {};
		on_allocate = on_discard = {};
		allocations = discards = errors = 0;
		fail_allocation = false;
		currentSong = &song;
		active_ui = &screen;
		AudioRecorder::recorder = nullptr;
		AudioRecorder::recordingSource = AudioInputChannel::NONE;
		deluge::gui::ui_session::detail::active = Id::Local;
	}
	void teardown() override {
		on_allocate = on_discard = {};
		delete AudioRecorder::recorder;
		AudioRecorder::recorder = nullptr;
		recording_session.release();
		deluge::gui::ui_session::detail::active = Id::Local;
	}
};
TEST(RecordingSetup, successful_allocation_publishes_configured_recorder) {
	CHECK_TRUE(start());
	CHECK_TRUE(recording_session.active());
	CHECK(AudioRecorder::recordingSource == AudioInputChannel::INPUT);
	CHECK_TRUE(AudioRecorder::recorder->allowFileAlterationAfter);
	CHECK_TRUE(AudioRecorder::recorder->allowNormalization);
	LONGS_EQUAL(0, discards);
}
TEST(RecordingSetup, allocation_failure_releases_reservation_and_reports_error) {
	fail_allocation = true;
	CHECK_FALSE(start());
	CHECK_FALSE(recording_session.active());
	POINTERS_EQUAL(nullptr, AudioRecorder::recorder);
	LONGS_EQUAL(1, errors);
}
TEST(RecordingSetup, changed_ui_discards_only_unpublished_recorder) {
	int replacement;
	on_allocate = [&] { active_ui = &replacement; };
	CHECK_FALSE(start());
	LONGS_EQUAL(1, discards);
	LONGS_EQUAL(0, errors);
	CHECK_FALSE(recording_session.active());
	POINTERS_EQUAL(nullptr, AudioRecorder::recorder);
}
TEST(RecordingSetup, destroyed_song_cancels_allocation_publication) {
	auto owner = std::make_unique<Song>();
	currentSong = owner.get();
	on_allocate = [&] { owner.reset(); };
	CHECK_FALSE(start());
	LONGS_EQUAL(1, discards);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingSetup, replacement_session_recorder_is_not_overwritten_or_released) {
	SampleRecorder* replacement = nullptr;
	on_allocate = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
		replacement = new SampleRecorder;
		AudioRecorder::recorder = replacement;
		AudioRecorder::recordingSource = AudioInputChannel::OUTPUT;
	};
	CHECK_FALSE(start());
	POINTERS_EQUAL(replacement, AudioRecorder::recorder);
	CHECK(AudioRecorder::recordingSource == AudioInputChannel::OUTPUT);
	CHECK_TRUE(recording_session.active());
	LONGS_EQUAL(1, discards);
}
TEST(RecordingSetup, cancelled_allocation_cleanup_preserves_session_started_during_discard) {
	on_allocate = [&] { active_ui = nullptr; };
	on_discard = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	CHECK_FALSE(start());
	CHECK_TRUE(recording_session.active());
	LONGS_EQUAL(0, errors);
}
TEST(RecordingSetup, competing_entry_cannot_allocate_while_outer_request_is_pending) {
	on_allocate = [&] { CHECK_FALSE(start()); };
	CHECK_TRUE(start());
	LONGS_EQUAL(1, allocations);
}
TEST(RecordingSetup, source_panel_change_cancels_and_restores_owner) {
	on_allocate = [&] { deluge::gui::ui_session::detail::active = Id::Remote; };
	CHECK_FALSE(start());
	LONGS_EQUAL(1, discards);
	CHECK(deluge::gui::ui_session::current() == Id::Local);
}
TEST(RecordingSetup, live_recorder_is_preserved_even_if_reservation_is_missing) {
	AudioRecorder::recorder = new SampleRecorder;
	auto* original = AudioRecorder::recorder;
	CHECK_FALSE(start());
	POINTERS_EQUAL(original, AudioRecorder::recorder);
	LONGS_EQUAL(0, allocations);
}
