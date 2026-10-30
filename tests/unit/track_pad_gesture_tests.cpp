#include "CppUTest/TestHarness.h"
#include "gui/views/session/track_pad_gesture.h"

TEST_GROUP(TrackPadGestureTest){};

TEST(TrackPadGestureTest, TapMutesWithoutAudition) {
	TrackPadGesture gesture;
	gesture.press(100, false);
	CHECK(gesture.shouldMute(199, 100));
	CHECK_FALSE(gesture.shouldAudition(199, 100));
}

TEST(TrackPadGestureTest, HoldAuditionsWithoutMutingAtThreshold) {
	TrackPadGesture gesture;
	gesture.press(100, false);
	CHECK_FALSE(gesture.shouldMute(200, 100));
	CHECK(gesture.shouldAudition(200, 100));
}

TEST(TrackPadGestureTest, ShiftLockNeverAuditionsOrMutes) {
	TrackPadGesture gesture;
	gesture.press(100, true);
	CHECK_FALSE(gesture.shouldMute(101, 100));
	CHECK_FALSE(gesture.shouldAudition(101, 100));
	CHECK_FALSE(gesture.shouldMute(10000, 100));
	CHECK_FALSE(gesture.shouldAudition(10000, 100));
}

TEST(TrackPadGestureTest, HardwareOrMidiEditCancelsTap) {
	TrackPadGesture gesture;
	gesture.press(100, false);
	gesture.markUsed();
	CHECK_FALSE(gesture.shouldMute(101, 100));
	gesture.press(200, false);
	CHECK(gesture.shouldMute(201, 100));
}

TEST(TrackPadGestureTest, AudioTimerWrapDoesNotTurnTapIntoHold) {
	TrackPadGesture gesture;
	gesture.press(UINT32_MAX - 49, false);
	CHECK(gesture.shouldMute(0, 100));
	CHECK_FALSE(gesture.shouldAudition(0, 100));
	CHECK_FALSE(gesture.shouldMute(50, 100));
	CHECK(gesture.shouldAudition(50, 100));
}
