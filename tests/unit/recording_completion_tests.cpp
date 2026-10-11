#include "CppUTest/TestHarness.h"
#include "gui/ui/recording_session.h"
#include <functional>
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
enum class AudioInputChannel { NONE, INPUT };
std::function<void()> on_discard, on_display;
int discards = 0, removals = 0;
struct SampleRecorder {
	bool pointerHeldElsewhere = true;
};
struct AudioRecorder {
	static SampleRecorder* recorder;
	static AudioInputChannel recordingSource;
	bool finishRecording();
};
SampleRecorder* AudioRecorder::recorder = nullptr;
AudioInputChannel AudioRecorder::recordingSource = AudioInputChannel::NONE;
struct Display {
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
} // namespace recording_completion_test
using namespace recording_completion_test;
TEST_GROUP(RecordingCompletion) {
	AudioRecorder ui;
	void setup() override {
		recording_session = {};
		on_discard = on_display = {};
		discards = removals = 0;
		currentSong = &song;
		active_ui = &screen;
		deluge::gui::ui_session::detail::active = Id::Local;
		CHECK_TRUE(recording_session.acquire());
		AudioRecorder::recorder = new SampleRecorder;
		AudioRecorder::recordingSource = AudioInputChannel::INPUT;
	}
	void teardown() override {
		on_discard = on_display = {};
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
