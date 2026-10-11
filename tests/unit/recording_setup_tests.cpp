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
enum class AudioInputChannel { NONE, INPUT, OUTPUT, STEREO, LEFT };
enum class AudioRecordingFolder { RECORD };
enum class Error { INSUFFICIENT_RAM };
constexpr int kInternalButtonPressLatency = 1;
struct SampleRecorder {
	bool pointerHeldElsewhere = true, allowFileAlterationAfter = false, allowNormalization = false;
};
struct RecorderConfig {
	bool value;
};
std::function<void()> on_allocate, on_discard, on_blink, on_clear;
enum class IndicatorLED { RECORD };
namespace indicator_leds {
void blinkLed(IndicatorLED, int, int) {
	if (on_blink)
		on_blink();
}
} // namespace indicator_leds
int allocations = 0, discards = 0, errors = 0;
bool fail_allocation = false;
namespace PadLEDs {
void clearTickSquares(bool) {
	if (on_clear)
		on_clear();
}
} // namespace PadLEDs
namespace AudioEngine {
bool micPluggedIn = false, lineInPluggedIn = false;
bool bypassCulling = false;
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
	bool target_valid = true;
	bool recording_target_current() const { return target_valid; }
	bool start_prepared_recording();
	bool finishRecording() {
		recorder->pointerHeldElsewhere = false;
		AudioEngine::discardRecorder(recorder);
		recorder = nullptr;
		recordingSource = AudioInputChannel::NONE;
		recording_session.release();
		return true;
	}
	bool setupRecordingToFile(AudioInputChannel, int32_t, AudioRecordingFolder, bool = false, bool = false);
	bool beginOutputRecording(AudioRecordingFolder, AudioInputChannel, bool, bool);
};
SampleRecorder* AudioRecorder::recorder = nullptr;
AudioInputChannel AudioRecorder::recordingSource = AudioInputChannel::NONE;
#include "recording_setup.inc"

#include "output_recording_start.inc"

#include "recording_prepared_start.inc"
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
		on_allocate = on_discard = on_blink = on_clear = {};
		allocations = discards = errors = 0;
		fail_allocation = false;
		AudioEngine::bypassCulling = false;
		currentSong = &song;
		active_ui = &screen;
		AudioRecorder::recorder = nullptr;
		AudioRecorder::recordingSource = AudioInputChannel::NONE;
		deluge::gui::ui_session::detail::active = Id::Local;
	}
	void teardown() override {
		on_allocate = on_discard = on_blink = on_clear = {};
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

TEST(RecordingSetup, failed_output_start_does_not_change_culling_state) {
	fail_allocation = true;
	CHECK_FALSE(ui.beginOutputRecording(AudioRecordingFolder::RECORD, AudioInputChannel::OUTPUT, false, true));
	CHECK_FALSE(AudioEngine::bypassCulling);
}
TEST(RecordingSetup, output_start_publishes_audio_state_before_led_callback) {
	on_blink = [&] { CHECK_TRUE(AudioEngine::bypassCulling); };
	CHECK_TRUE(ui.beginOutputRecording(AudioRecordingFolder::RECORD, AudioInputChannel::OUTPUT, false, false));
	CHECK_FALSE(AudioRecorder::recorder->allowNormalization);
	CHECK(AudioRecorder::recordingSource == AudioInputChannel::OUTPUT);
}
TEST(RecordingSetup, output_start_preserves_culling_change_from_led_callback) {
	on_blink = [&] { AudioEngine::bypassCulling = false; };
	CHECK_TRUE(ui.beginOutputRecording(AudioRecordingFolder::RECORD, AudioInputChannel::OUTPUT, false, true));
	CHECK_FALSE(AudioEngine::bypassCulling);
}
TEST(RecordingSetup, output_start_reports_cancellation_when_led_callback_finishes_recorder) {
	on_blink = [&] {
		delete AudioRecorder::recorder;
		AudioRecorder::recorder = nullptr;
		AudioRecorder::recordingSource = AudioInputChannel::NONE;
		recording_session.release();
	};
	CHECK_FALSE(ui.beginOutputRecording(AudioRecordingFolder::RECORD, AudioInputChannel::OUTPUT, false, true));
}
TEST(RecordingSetup, output_start_reports_replacement_even_when_recorder_address_is_unchanged) {
	on_blink = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	CHECK_FALSE(ui.beginOutputRecording(AudioRecordingFolder::RECORD, AudioInputChannel::OUTPUT, false, true));
	CHECK_TRUE(recording_session.active());
}

TEST(RecordingSetup, prepared_recording_starts_for_original_target) {
	CHECK_TRUE(ui.start_prepared_recording());
	LONGS_EQUAL(1, allocations);
	CHECK(AudioRecorder::recordingSource == AudioInputChannel::LEFT);
}
TEST(RecordingSetup, pad_callback_target_change_prevents_allocation) {
	on_clear = [&] { ui.target_valid = false; };
	CHECK_FALSE(ui.start_prepared_recording());
	LONGS_EQUAL(0, allocations);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingSetup, pad_callback_navigation_prevents_allocation) {
	int replacement;
	on_clear = [&] { active_ui = &replacement; };
	CHECK_FALSE(ui.start_prepared_recording());
	LONGS_EQUAL(0, allocations);
}
TEST(RecordingSetup, pad_callback_transient_session_prevents_allocation) {
	on_clear = [&] {
		CHECK_TRUE(recording_session.acquire());
		recording_session.release();
	};
	CHECK_FALSE(ui.start_prepared_recording());
	LONGS_EQUAL(0, allocations);
}
TEST(RecordingSetup, target_change_during_allocation_discards_new_recorder) {
	on_allocate = [&] { ui.target_valid = false; };
	CHECK_FALSE(ui.start_prepared_recording());
	LONGS_EQUAL(1, allocations);
	LONGS_EQUAL(1, discards);
	POINTERS_EQUAL(nullptr, AudioRecorder::recorder);
	CHECK_FALSE(recording_session.active());
	CHECK(AudioRecorder::recordingSource == AudioInputChannel::NONE);
}
