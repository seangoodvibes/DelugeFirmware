#include "CppUTest/TestHarness.h"
#include "mirror_environment.h"

// Compile production implementation after the hardware collaborators.
#include "hid/led/indicator_leds.cpp"

namespace us = deluge::gui::ui_session;
using namespace indicator_leds;
// clang-format off
TEST_GROUP(IndicatorRuntime) {
	void setup() override {
		us::detail::active = us::Id::Local;
		states.for_owner(us::Id::Local) = {};
		states.for_owner(us::Id::Remote) = {};
		uiTimerManager = {};
		fixture::leds.clear();
		fixture::knobs.clear();
	}
};
// clang-format on

TEST(IndicatorRuntime, remote_led_updates_software_frame_without_writing_pic) {
	setLedState(LED::PLAY, true);
	LONGS_EQUAL(1, fixture::leds.size());
	{
		us::Scope remote(us::Id::Remote);
		setLedState(LED::PLAY, false);
		setLedState(LED::RECORD, true);
		CHECK_FALSE(frame_for_session().leds[static_cast<size_t>(LED::PLAY)]);
		CHECK_TRUE(frame_for_session().leds[static_cast<size_t>(LED::RECORD)]);
	}
	LONGS_EQUAL(1, fixture::leds.size());
	CHECK_TRUE(frame_for_session().leds[static_cast<size_t>(LED::PLAY)]);
	CHECK_FALSE(frame_for_session().leds[static_cast<size_t>(LED::RECORD)]);
}
TEST(IndicatorRuntime, same_led_can_blink_independently_on_both_panels) {
	blinkLed(LED::PLAY, 255, 0, true);
	{
		us::Scope remote(us::Id::Remote);
		blinkLed(LED::PLAY, 255, 0, false);
		CHECK_FALSE(blink_state_for_session(0));
		ledBlinkTimeout(0);
		CHECK_TRUE(blink_state_for_session(0));
		stopLedBlinking(LED::PLAY);
		LONGS_EQUAL(255, getLedBlinkerIndex(LED::PLAY));
	}
	CHECK_TRUE(getLedBlinkerIndex(LED::PLAY) != 255);
	CHECK_TRUE(blink_state_for_session(0));
	ledBlinkTimeout(0);
	CHECK_FALSE(blink_state_for_session(0));
}
TEST(IndicatorRuntime, every_knob_level_publishes_same_brightness_but_only_local_writes_hardware) {
	for (bool bipolar : {false, true}) {
		for (uint8_t knob = 0; knob < 2; ++knob) {
			for (int level = 0; level <= 128; ++level) {
				const auto before_writes = fixture::knobs.size();
				actuallySetKnobIndicatorLevel(knob, level, bipolar);
				const auto expected = frame_for_session().knobs[knob];
				const auto physical_writes = fixture::knobs.size();
				{
					us::Scope remote(us::Id::Remote);
					actuallySetKnobIndicatorLevel(knob, level, bipolar);
					CHECK_TRUE(expected == frame_for_session().knobs[knob]);
				}
				LONGS_EQUAL(physical_writes, fixture::knobs.size());
				if (fixture::knobs.size() > before_writes)
					CHECK_TRUE(expected == fixture::knobs.back().second);
			}
		}
	}
}
TEST(IndicatorRuntime, peer_knob_blink_cannot_cancel_local_timer_or_blink_state) {
	blinkKnobIndicator(0, true);
	CHECK_TRUE(isKnobIndicatorBlinking(0));
	CHECK_TRUE(uiTimerManager.isTimerSet(TimerName::LEVEL_INDICATOR_BLINK));
	{
		us::Scope remote(us::Id::Remote);
		CHECK_FALSE(isKnobIndicatorBlinking(0));
		blinkKnobIndicator(0, false);
		stopBlinkingKnobIndicator(0);
		CHECK_FALSE(uiTimerManager.isTimerSet(TimerName::LEVEL_INDICATOR_BLINK));
	}
	CHECK_TRUE(uiTimerManager.isTimerSet(TimerName::LEVEL_INDICATOR_BLINK));
	CHECK_TRUE(isKnobIndicatorBlinking(0));
}
TEST(IndicatorRuntime, local_meter_holdoff_does_not_block_peer_meter) {
	setMeterLevel(0, 12);
	setKnobIndicatorLevel(0, 100, false);
	setMeterLevel(0, 30);
	const auto held = frame_for_session().knobs[0];
	{
		us::Scope remote(us::Id::Remote);
		setMeterLevel(0, 30);
		CHECK_TRUE(frame_for_session().knobs[0] != held);
	}
	CHECK_TRUE(held == frame_for_session().knobs[0]);
}
