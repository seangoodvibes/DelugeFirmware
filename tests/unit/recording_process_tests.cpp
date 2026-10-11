#include "CppUTest/TestHarness.h"
#include "gui/ui/recording_session.h"
#include <functional>
namespace recording_process_test {
namespace deluge {
namespace gui {
namespace ui_session = ::deluge::gui::ui_session;
}
namespace l10n {
enum class String { STRING_FOR_CLIPPING_OCCURRED };
String get(String value) {
	return value;
}
} // namespace l10n
namespace hid {
namespace mirror {
void transport_routine();
}
namespace display {
struct Canvas {
	template <typename... Args>
	void drawStringCentred(Args...) {}
};
struct OLED {
	static void clearMainImage() {}
	static Canvas& main_for_session() {
		static Canvas canvas;
		return canvas;
	}
	static void sendMainImage() {}
};
} // namespace display
} // namespace hid
} // namespace deluge
using ::deluge::gui::ui_session::Id;
using ::deluge::gui::ui_session::RecordingSession;
using ::deluge::gui::ui_session::RecordingTarget;
RecordingSession recording_session;
struct Navigation {
	struct State {
		struct Refresh {
			unsigned revision() const { return 0; }
		} structural_refresh;
	} state;
	State& active() { return state; }
} navigation;
int song, screen;
void* currentSong = &song;
void* active_ui = &screen;
void* getCurrentUI() {
	return active_ui;
}
struct Editor {
	void* currentSound = nullptr;
	void* currentSource = nullptr;
	void* currentMultiRange = nullptr;
} editor;
Editor& sound_editor_for_session() {
	return editor;
}
enum class RecorderStatus { CAPTURING_DATA, CAPTURING_DATA_WAITING_TO_STOP, COMPLETE, ABORTED };
std::function<void()> on_stop;
int stop_requests = 0;
struct SampleRecorder {
	RecorderStatus status = RecorderStatus::COMPLETE;
	bool hadCardError = false, recordingClippedRecently = false;
	int numSamplesCaptured = 0, minThresholdMargin = 1;
	int* sample = &song;
	void endSyncedRecording(int latency) {
		LONGS_EQUAL(0, latency);
		++stop_requests;
		status = RecorderStatus::COMPLETE;
		if (on_stop)
			on_stop();
	}
};
SampleRecorder* recorder = nullptr;
std::function<void()> on_service, on_attachment, on_finish;
int services = 0, attachments = 0, finishes = 0, closes = 0, animation_cleanups = 0;
void service() {
	++services;
	if (on_service)
		on_service();
}
struct AudioRecorder {
	bool updatedRecordingStatus = false;
	bool target_valid = true;
	RecordingTarget target;
	bool recording_target_current() const {
		return target_valid
		       && target
		              == RecordingTarget{currentSong, editor.currentSound, editor.currentSource,
		                                 editor.currentMultiRange, 0};
	}
	bool try_assign_recorded_sample(int*) {
		++attachments;
		if (on_attachment)
			on_attachment();
		return true;
	}
	bool finishRecording(bool remove_animation = true) {
		++finishes;
		animation_cleanups += remove_animation;
		delete recorder;
		recorder = nullptr;
		recording_session.release();
		if (on_finish)
			on_finish();
		return !recording_session.active();
	}
	void close() { ++closes; }
	void process();
};
AudioRecorder* active_recorder_ui = nullptr;
AudioRecorder& audio_recorder_for_session() {
	return *active_recorder_ui;
}
namespace AudioEngine {
void routineWithClusterLoading() {
	service();
}
void slowRoutine() {
	service();
}
} // namespace AudioEngine
struct TimerManager {
	void routine() { service(); }
} uiTimerManager;
struct Display {
	bool haveOLED() const { return true; }
	bool have7SEG() const { return false; }
	bool hasPopup() const { return false; }
	void displayPopup(deluge::l10n::String) {}
	void setText(const char*, bool, int, bool) {}
} display_instance;
auto* display = &display_instance;
constexpr int kTextBigSpacingX = 1, kTextBigSizeY = 1;
void oledRoutine() {
	service();
}
namespace PIC {
void flush() {
	service();
}
} // namespace PIC
void readButtonsAndPads() {
	service();
}
void deluge::hid::mirror::transport_routine() {
	service();
}
#include "recording_process.inc"
} // namespace recording_process_test
using namespace recording_process_test;
TEST_GROUP(RecordingProcess) {
	AudioRecorder ui;
	void setup() override {
		on_service = on_attachment = on_finish = on_stop = {};
		services = attachments = finishes = closes = animation_cleanups = stop_requests = 0;
		recording_session = {};
		::deluge::gui::ui_session::detail::active = Id::Local;
		CHECK_TRUE(recording_session.acquire());
		recorder = new SampleRecorder;
		active_recorder_ui = &ui;
		currentSong = &song;
		active_ui = &ui;
		editor = {};
		ui.target = {currentSong, nullptr, nullptr, nullptr, 0};
	}
	void teardown() override {
		on_service = on_attachment = on_finish = on_stop = {};
		delete recorder;
		recorder = nullptr;
		recording_session.release();
		::deluge::gui::ui_session::detail::active = Id::Local;
	}
};
TEST(RecordingProcess, completed_recording_attaches_finishes_and_closes) {
	ui.process();
	LONGS_EQUAL(7, services);
	LONGS_EQUAL(1, attachments);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(1, closes);
}
TEST(RecordingProcess, each_service_boundary_stops_after_recorder_destruction) {
	for (int boundary = 1; boundary <= 7; ++boundary) {
		services = 0;
		if (!recording_session.active())
			CHECK_TRUE(recording_session.acquire());
		if (!recorder)
			recorder = new SampleRecorder;
		on_service = [&] {
			if (services == boundary) {
				delete recorder;
				recorder = nullptr;
				recording_session.release();
			}
		};
		ui.process();
		LONGS_EQUAL(boundary, services);
		LONGS_EQUAL(0, attachments);
		LONGS_EQUAL(0, finishes);
		LONGS_EQUAL(0, closes);
	}
}
TEST(RecordingProcess, replacement_session_at_same_recorder_address_is_not_processed) {
	on_service = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	ui.process();
	LONGS_EQUAL(1, services);
	LONGS_EQUAL(0, finishes);
	CHECK_TRUE(recording_session.active());
}
TEST(RecordingProcess, attachment_callback_cannot_finish_a_replacement_recording) {
	on_attachment = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	ui.process();
	LONGS_EQUAL(1, attachments);
	LONGS_EQUAL(0, finishes);
	LONGS_EQUAL(0, closes);
}
TEST(RecordingProcess, completed_recording_on_departed_ui_is_not_attached_or_closed) {
	on_service = [&] { active_ui = &screen; };
	ui.process();
	LONGS_EQUAL(0, attachments);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, closes);
}
TEST(RecordingProcess, completion_callback_navigation_is_not_closed) {
	on_finish = [&] { active_ui = &screen; };
	ui.process();
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, closes);
}
TEST(RecordingProcess, completion_callback_new_recording_is_not_closed) {
	on_finish = [&] {
		CHECK_TRUE(recording_session.acquire());
		recorder = new SampleRecorder;
	};
	ui.process();
	LONGS_EQUAL(0, closes);
	CHECK_TRUE(recording_session.active());
}
TEST(RecordingProcess, remote_timer_completion_stops_before_recorder_access) {
	recording_session.release();
	::deluge::gui::ui_session::detail::active = Id::Remote;
	CHECK_TRUE(recording_session.acquire());
	on_service = [&] {
		if (services == 8) {
			delete recorder;
			recorder = nullptr;
			recording_session.release();
		}
	};
	ui.process();
	LONGS_EQUAL(8, services);
	LONGS_EQUAL(0, finishes);
	CHECK(::deluge::gui::ui_session::current() == Id::Remote);
}

TEST(RecordingProcess, retired_target_with_matching_addresses_is_not_attached) {
	ui.target_valid = false;
	ui.process();
	LONGS_EQUAL(0, attachments);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(1, closes);
}

TEST(RecordingProcess, departed_ui_completion_preserves_unrelated_loading_animation) {
	on_service = [&] { active_ui = &screen; };
	ui.process();
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, animation_cleanups);
}
TEST(RecordingProcess, attachment_navigation_preserves_destination_loading_animation) {
	on_attachment = [&] { active_ui = &screen; };
	ui.process();
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, animation_cleanups);
}
TEST(RecordingProcess, original_ui_completion_requests_animation_cleanup) {
	ui.process();
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(1, animation_cleanups);
}

TEST(RecordingProcess, navigation_before_process_does_not_adopt_destination_ui) {
	active_ui = &screen;
	ui.process();
	LONGS_EQUAL(0, attachments);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, closes);
	LONGS_EQUAL(0, animation_cleanups);
}
TEST(RecordingProcess, navigation_stops_capture_and_finishes_without_attaching) {
	recorder->status = RecorderStatus::CAPTURING_DATA;
	on_service = [&] { active_ui = &screen; };
	ui.process();
	LONGS_EQUAL(1, stop_requests);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, attachments);
	LONGS_EQUAL(0, closes);
	LONGS_EQUAL(0, animation_cleanups);
}
TEST(RecordingProcess, lost_target_stops_capture_and_releases_recording) {
	recorder->status = RecorderStatus::CAPTURING_DATA;
	ui.target_valid = false;
	ui.process();
	LONGS_EQUAL(1, stop_requests);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, attachments);
	LONGS_EQUAL(1, closes);
}
TEST(RecordingProcess, stop_callback_replacement_session_is_not_finished) {
	recorder->status = RecorderStatus::CAPTURING_DATA;
	ui.target_valid = false;
	on_stop = [&] {
		recording_session.release();
		CHECK_TRUE(recording_session.acquire());
	};
	ui.process();
	LONGS_EQUAL(1, stop_requests);
	LONGS_EQUAL(0, finishes);
	CHECK_TRUE(recording_session.active());
}

TEST(RecordingProcess, navigation_stop_waits_for_completion_without_repeating_stop) {
	recorder->status = RecorderStatus::CAPTURING_DATA;
	ui.target_valid = false;
	on_stop = [&] { recorder->status = RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP; };
	on_service = [&] {
		if (services == 14)
			recorder->status = RecorderStatus::COMPLETE;
	};
	ui.process();
	LONGS_EQUAL(1, stop_requests);
	LONGS_EQUAL(14, services);
	LONGS_EQUAL(1, finishes);
	LONGS_EQUAL(0, attachments);
}
