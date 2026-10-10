#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <array>
#include <functional>
#include <vector>
namespace playhead_buffer_routing_test {
namespace session = deluge::gui::ui_session;
constexpr int kDisplayWidth = 16, kDisplayHeight = 8;
constexpr int UI_MODE_EXPLODE_ANIMATION = 1, UI_MODE_IMPLODE_ANIMATION = 2;
static session::State<int> modes;
#define currentUIMode modes.active()
enum class RuntimeFeatureSettingType { EnableLaunchEventPlayhead };
enum class RuntimeFeatureStateToggle { Off, On };
struct Settings {
	RuntimeFeatureStateToggle enabled = RuntimeFeatureStateToggle::On;
	auto get(RuntimeFeatureSettingType) { return enabled; }
} runtimeFeatureSettings;
struct Clip {
	int lastProcessedPos = 0, loopLength = 16;
	bool active = true, linear = false;
	bool getCurrentlyRecordingLinearly() { return linear; }
};
static session::State<Clip> clips;
Clip* getCurrentClip() {
	return &clips.active();
}
struct Song {
	bool isClipActive(Clip* clip) { return clip->active; }
} song;
static Song* currentSong = &song;
struct Playback {
	bool clock = true, recording = true;
	int ticksLeftInCountIn = 0;
	bool isEitherClockActive() { return clock; }
	bool isCurrentlyRecording() { return recording; }
	int getNumSwungTicksInSinceLastActionedSwungTick() { return 0; }
} playbackHandler;
const uint8_t launchTickColours[kDisplayHeight]{};
const uint8_t keyboardTickColoursBasicRecording[kDisplayHeight]{};
const uint8_t keyboardTickColoursLinearRecording[kDisplayHeight] = {0, 0, 0, 0, 0, 0, 0, 2};
struct Output {
	std::array<uint8_t, kDisplayHeight> squares, colours;
	session::Id owner;
};
static std::vector<Output> outputs;
static std::function<void()> before_copy;
namespace PadLEDs {
void setTickSquares(const uint8_t* squares, const uint8_t* colours) {
	// Suspend the consumer before copying to expose shared scratch-buffer reuse.
	if (before_copy) {
		auto callback = std::move(before_copy);
		before_copy = {};
		callback();
	}
	Output output;
	std::copy_n(squares, kDisplayHeight, output.squares.begin());
	std::copy_n(colours, kDisplayHeight, output.colours.begin());
	output.owner = session::current();
	outputs.push_back(output);
}
} // namespace PadLEDs
struct SessionView {
	void potentiallyRenderClipLaunchPlayhead(bool, int32_t);
};
struct KeyboardScreen {
	void graphicsRoutine();
};
#include "keyboard_playhead_buffer.inc"
#include "launch_playhead_buffer.inc"
TEST_GROUP(PlayheadBufferRouting){void setup() override{outputs.clear();
before_copy = {};
clips = {};
modes = {};
playbackHandler = {};
runtimeFeatureSettings = {};
} // namespace playhead_buffer_routing_test
void teardown() override {
	before_copy = {};
}
}
;
TEST(PlayheadBufferRouting, nested_launch_render_preserves_outer_sessions_position) {
	before_copy = [] {
		session::Scope remote(session::Id::Remote);
		SessionView{}.potentiallyRenderClipLaunchPlayhead(false, 4);
	};
	session::Scope local(session::Id::Local);
	SessionView{}.potentiallyRenderClipLaunchPlayhead(false, 12);
	LONGS_EQUAL(2, outputs.size());
	LONGS_EQUAL(12, outputs[0].squares.back());
	LONGS_EQUAL(4, outputs[1].squares.back());
	for (auto& output : outputs)
		for (int row = 0; row < kDisplayHeight - 1; ++row)
			LONGS_EQUAL(255, output.squares[row]);
}
TEST(PlayheadBufferRouting, nested_keyboard_render_preserves_outer_position_and_recording_colour) {
	clips.for_owner(session::Id::Local).lastProcessedPos = 3;
	clips.for_owner(session::Id::Remote).lastProcessedPos = 11;
	clips.for_owner(session::Id::Remote).linear = true;
	before_copy = [] {
		session::Scope remote(session::Id::Remote);
		KeyboardScreen{}.graphicsRoutine();
	};
	session::Scope local(session::Id::Local);
	KeyboardScreen{}.graphicsRoutine();
	LONGS_EQUAL(2, outputs.size());
	LONGS_EQUAL(11, outputs[0].squares.back());
	LONGS_EQUAL(2, outputs[0].colours.back());
	LONGS_EQUAL(3, outputs[1].squares.back());
	LONGS_EQUAL(0, outputs[1].colours.back());
	CHECK(outputs[0].owner == session::Id::Remote);
	CHECK(outputs[1].owner == session::Id::Local);
}
TEST(PlayheadBufferRouting, suppressed_and_out_of_range_playheads_publish_no_tick) {
	session::Scope remote(session::Id::Remote);
	SessionView{}.potentiallyRenderClipLaunchPlayhead(true, 4);
	SessionView{}.potentiallyRenderClipLaunchPlayhead(false, 17);
	SessionView{}.potentiallyRenderClipLaunchPlayhead(false, 0);
	playbackHandler.clock = false;
	KeyboardScreen{}.graphicsRoutine();
	playbackHandler.clock = true;
	clips.active().lastProcessedPos = 16;
	KeyboardScreen{}.graphicsRoutine();
	for (auto& output : outputs)
		for (auto square : output.squares)
			LONGS_EQUAL(255, square);
}
#undef currentUIMode
} // namespace playhead_buffer_routing_test
