#include "CppUTest/TestHarness.h"
#include "gui/ui/recording_session.h"
#include <functional>
namespace recording_completion_test {
extern bool mirror_client;
}
namespace deluge::hid::mirror {
bool is_client() {
	return recording_completion_test::mirror_client;
}
} // namespace deluge::hid::mirror
namespace recording_completion_test {
using deluge::gui::ui_session::Id;
using deluge::gui::ui_session::RecordingSession;
RecordingSession recording_session;
int song;
void* currentSong = &song;
int screen;
void* active_ui = &screen;
void* getCurrentUI() {
	return active_ui;
}
enum class AudioInputChannel { NONE, INPUT, INTERNAL };
constexpr auto AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION = AudioInputChannel::INTERNAL;
bool sdRoutineLock = false, currentlyAccessingCard = false, mirror_client = false;
int freezes = 0, led_updates = 0;
#define FREEZE_WITH_ERROR(code) (++freezes)
enum class IndicatorLED { RECORD };
enum class RecordingMode { OFF, NORMAL };
struct PlaybackHandler {
	RecordingMode recording = RecordingMode::OFF;
} playbackHandler;
std::function<void()> on_led;
namespace indicator_leds {
void setLedState(IndicatorLED, bool) {
	++led_updates;
	if (on_led)
		on_led();
}
} // namespace indicator_leds
std::function<void()> on_discard, on_display;
int discards = 0, removals = 0, stop_requests = 0, stop_latency = 0, animations = 0;
enum class RecorderStatus { CAPTURING_DATA, COMPLETE };
struct SampleRecorder {
	bool pointerHeldElsewhere = true;
	RecorderStatus status = RecorderStatus::CAPTURING_DATA;
	void endSyncedRecording(int latency) {
		++stop_requests;
		stop_latency = latency;
		status = RecorderStatus::COMPLETE;
	}
};
struct AudioRecorder {
	static SampleRecorder* recorder;
	static AudioInputChannel recordingSource;
	bool finishRecording(bool remove_animation = true);
	void endRecordingSoon(int32_t buttonLatency);
	void slowRoutine();
};
SampleRecorder* AudioRecorder::recorder = nullptr;
AudioInputChannel AudioRecorder::recordingSource = AudioInputChannel::NONE;
struct Display {
	void displayLoadingAnimationText(const char*) {
		++animations;
		if (on_display)
			on_display();
	}
	void removeLoadingAnimation() {
		++removals;
		if (on_display)
			on_display();
	}
} display_instance;
auto* display = &display_instance;
namespace AudioEngine {
void discardRecorder(SampleRecorder* recorder) {
	++discards;
	CHECK_FALSE(recorder->pointerHeldElsewhere);
	delete recorder;
	if (on_discard)
		on_discard();
}
} // namespace AudioEngine
#include "recording_completion.inc"

#include "recording_stop.inc"

#include "recording_slow.inc"
#undef FREEZE_WITH_ERROR
} // namespace recording_completion_test
using namespace recording_completion_test;
TEST_GROUP(RecordingCompletion) {
	AudioRecorder ui;
	void setup() override {
		recording_session = {};
		sdRoutineLock = currentlyAccessingCard = mirror_client = false;
		freezes = led_updates = 0;
		on_discard = on_display = on_led = {};
		discards = removals = stop_requests = stop_latency = animations = 0;
		currentSong = &song;
		active_ui = &screen;
		deluge::gui::ui_session::detail::active = Id::Local;
		CHECK_TRUE(recording_session.acquire());
		AudioRecorder::recorder = new SampleRecorder;
		AudioRecorder::recordingSource = AudioInputChannel::INPUT;
	}
	void teardown() override {
		on_discard = on_display = on_led = {};
		delete AudioRecorder::recorder;
		AudioRecorder::recorder = nullptr;
		recording_session.release();
		deluge::gui::ui_session::detail::active = Id::Local;
	}
};
TEST(RecordingCompletion, completion_detaches_recorder_and_releases_session_once) {
	CHECK_TRUE(ui.finishRecording());
	POINTERS_EQUAL(nullptr, AudioRecorder::recorder);
	CHECK(AudioRecorder::recordingSource == AudioInputChannel::NONE);
	CHECK_FALSE(recording_session.active());
	LONGS_EQUAL(1, discards);
	LONGS_EQUAL(1, removals);
	CHECK_FALSE(ui.finishRecording());
	LONGS_EQUAL(1, discards);
	LONGS_EQUAL(1, removals);
}
TEST(RecordingCompletion, destructor_callbacks_see_detached_state_and_reserved_session) {
	on_discard = [&] {
		POINTERS_EQUAL(nullptr, AudioRecorder::recorder);
		CHECK(AudioRecorder::recordingSource == AudioInputChannel::NONE);
		CHECK_TRUE(recording_session.finishing());
		CHECK_FALSE(recording_session.acquire());
		ui.finishRecording();
	};
	ui.finishRecording();
	LONGS_EQUAL(1, discards);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingCompletion, changed_ui_during_discard_preserves_new_animation) {
	int replacement;
	on_discard = [&] { active_ui = &replacement; };
	ui.finishRecording();
	LONGS_EQUAL(0, removals);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingCompletion, changed_song_during_discard_preserves_new_animation) {
	int replacement;
	on_discard = [&] { currentSong = &replacement; };
	ui.finishRecording();
	LONGS_EQUAL(0, removals);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingCompletion, display_callback_cannot_start_competing_recording) {
	on_display = [&] {
		CHECK_FALSE(recording_session.acquire());
		ui.finishRecording();
	};
	ui.finishRecording();
	LONGS_EQUAL(1, removals);
	LONGS_EQUAL(1, discards);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingCompletion, replacement_session_is_not_released_by_old_cleanup) {
	on_discard = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
		AudioRecorder::recorder = new SampleRecorder;
		AudioRecorder::recordingSource = AudioInputChannel::INPUT;
	};
	CHECK_FALSE(ui.finishRecording());
	CHECK_TRUE(recording_session.active());
	CHECK(AudioRecorder::recorder != nullptr);
	LONGS_EQUAL(0, removals);
}
TEST(RecordingCompletion, null_recorder_during_allocation_does_not_release_reservation) {
	delete AudioRecorder::recorder;
	AudioRecorder::recorder = nullptr;
	ui.finishRecording();
	CHECK_TRUE(recording_session.active());
	CHECK_FALSE(recording_session.finishing());
	LONGS_EQUAL(0, discards);
}
TEST(RecordingCompletion, session_revision_distinguishes_reused_owner_and_finish_transition) {
	const auto first_revision = recording_session.revision();
	CHECK_TRUE(recording_session.begin_finish());
	CHECK(first_revision != recording_session.revision());
	CHECK_FALSE(recording_session.begin_finish());
	recording_session.release();
	CHECK_TRUE(recording_session.acquire());
	CHECK_FALSE(recording_session.finishing());
	CHECK(first_revision != recording_session.revision());
}

TEST(RecordingCompletion, stop_request_preserves_latency_and_ignores_repeated_stop) {
	ui.endRecordingSoon(123);
	LONGS_EQUAL(1, stop_requests);
	LONGS_EQUAL(123, stop_latency);
	ui.endRecordingSoon(456);
	LONGS_EQUAL(1, stop_requests);
	LONGS_EQUAL(1, animations);
}
TEST(RecordingCompletion, stop_display_callback_can_destroy_recorder_without_stale_access) {
	on_display = [&] {
		delete AudioRecorder::recorder;
		AudioRecorder::recorder = nullptr;
		recording_session.release();
	};
	ui.endRecordingSoon(123);
	LONGS_EQUAL(0, stop_requests);
}
TEST(RecordingCompletion, stop_display_callback_does_not_stop_replacement_at_same_address) {
	on_display = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	ui.endRecordingSoon(123);
	LONGS_EQUAL(0, stop_requests);
	CHECK(AudioRecorder::recorder->status == RecorderStatus::CAPTURING_DATA);
}
TEST(RecordingCompletion, stop_display_callback_completion_is_not_stopped_again) {
	on_display = [&] { AudioRecorder::recorder->status = RecorderStatus::COMPLETE; };
	ui.endRecordingSoon(123);
	LONGS_EQUAL(0, stop_requests);
}
TEST(RecordingCompletion, finishing_session_rejects_stop_before_display) {
	CHECK_TRUE(recording_session.begin_finish());
	ui.endRecordingSoon(123);
	LONGS_EQUAL(0, animations);
	LONGS_EQUAL(0, stop_requests);
}
TEST(RecordingCompletion, stop_display_changed_panel_cancels_and_restores_owner) {
	on_display = [&] { deluge::gui::ui_session::detail::active = Id::Remote; };
	ui.endRecordingSoon(123);
	LONGS_EQUAL(0, stop_requests);
	CHECK(deluge::gui::ui_session::current() == Id::Local);
}

TEST(RecordingCompletion, slow_routine_finishes_completed_internal_recording) {
	AudioRecorder::recordingSource = AudioInputChannel::INTERNAL;
	AudioRecorder::recorder->status = RecorderStatus::COMPLETE;
	ui.slowRoutine();
	LONGS_EQUAL(1, led_updates);
	LONGS_EQUAL(1, discards);
	CHECK_FALSE(recording_session.active());
}
TEST(RecordingCompletion, slow_routine_preserves_replacement_started_by_led_callback) {
	AudioRecorder::recordingSource = AudioInputChannel::INTERNAL;
	AudioRecorder::recorder->status = RecorderStatus::COMPLETE;
	on_led = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	ui.slowRoutine();
	LONGS_EQUAL(0, discards);
	CHECK_TRUE(recording_session.active());
	CHECK(AudioRecorder::recorder != nullptr);
}
TEST(RecordingCompletion, slow_routine_stops_after_led_callback_destroys_recorder) {
	AudioRecorder::recordingSource = AudioInputChannel::INTERNAL;
	AudioRecorder::recorder->status = RecorderStatus::COMPLETE;
	on_led = [&] { ui.finishRecording(); };
	ui.slowRoutine();
	LONGS_EQUAL(1, discards);
	LONGS_EQUAL(1, removals);
}
TEST(RecordingCompletion, slow_routine_defers_cleanup_under_storage_locks_or_mirror_client) {
	AudioRecorder::recordingSource = AudioInputChannel::INTERNAL;
	AudioRecorder::recorder->status = RecorderStatus::COMPLETE;
	sdRoutineLock = true;
	ui.slowRoutine();
	sdRoutineLock = false;
	currentlyAccessingCard = true;
	ui.slowRoutine();
	currentlyAccessingCard = false;
	mirror_client = true;
	ui.slowRoutine();
	LONGS_EQUAL(0, led_updates);
	LONGS_EQUAL(0, discards);
}

TEST(RecordingCompletion, silent_completion_releases_recorder_without_touching_animation) {
	CHECK_TRUE(ui.finishRecording(false));
	LONGS_EQUAL(1, discards);
	LONGS_EQUAL(0, removals);
	CHECK_FALSE(recording_session.active());
	POINTERS_EQUAL(nullptr, AudioRecorder::recorder);
}
