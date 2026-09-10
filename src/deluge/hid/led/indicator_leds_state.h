#pragma once
#include "hid/led/indicator_leds.h"
#include <array>

namespace indicator_leds {

struct IndicatorFrame {
	std::array<bool, NUM_LED_COLS * NUM_LED_ROWS> leds{};
	std::array<std::array<uint8_t, kNumGoldKnobIndicatorLEDs>, NUM_LEVEL_INDICATORS> knobs{};
	uint32_t revision = 0;
	void set_led(size_t led, bool on) {
		if (leds[led] != on) {
			leds[led] = on;
			++revision;
		}
	}
	void set_knob(size_t knob, const std::array<uint8_t, kNumGoldKnobIndicatorLEDs>& brightness) {
		if (knobs[knob] != brightness) {
			knobs[knob] = brightness;
			++revision;
		}
	}
};

struct IndicatorState {
	IndicatorFrame frame;
	std::array<LedBlinker, numLedBlinkers> ledBlinkers{};
	std::array<bool, NUM_LEVEL_INDICATORS> ledBlinkState{};
	std::array<uint8_t, NUM_LEVEL_INDICATORS> knobIndicatorLevels{};
	std::array<bool, NUM_LEVEL_INDICATORS> knobIndicatorBipolar{};
	uint8_t whichLevelIndicatorBlinking = 0;
	bool levelIndicatorBlinkOn = false;
	uint8_t levelIndicatorBlinksLeft = 0;
	bool levelIndicatorBipolar = false;
	uint8_t whichKnobMetering = 0;
};

} // namespace indicator_leds
