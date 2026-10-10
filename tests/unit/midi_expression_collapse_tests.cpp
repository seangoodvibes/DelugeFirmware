#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace midi_expression_collapse_test {
constexpr int kNumExpressionDimensions = 3, BEND_RANGE_MAIN = 0, BEND_RANGE_FINGER_LEVEL = 1;
constexpr int X_PITCH_BEND = 0, Y_SLIDE_TIMBRE = 1, Z_PRESSURE = 2, CC_EXTERNAL_MOD_WHEEL = 1;
constexpr int32_t ONE_Q31 = INT32_MAX;
int32_t add_saturate(int32_t a, int32_t b) {
	return std::clamp<int64_t>(int64_t(a) + b, INT32_MIN, INT32_MAX);
}
enum class ArpMode { OFF, ON };
struct ArpeggiatorSettings {
	ArpMode mode = ArpMode::OFF;
};
struct ArpNote {
	int16_t mpeValues[3]{};
};
struct ExpressionParamSet {
	int bendRanges[2]{2, 48};
};
struct ParamManager {
	ExpressionParamSet expression;
	bool present = true;
	ExpressionParamSet* getExpressionParamSet() { return present ? &expression : nullptr; }
};
struct MIDIInstrument;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	MIDIInstrument* output = nullptr;
	ParamManager paramManager;
	ArpeggiatorSettings settings;
};
std::function<void()> on_send;
int sends = 0, last_value = 0, last_dimension = -1;
struct {
	void send(int value, int dimension) {
		++sends;
		last_value = value;
		last_dimension = dimension;
		if (on_send)
			on_send();
	}
	void sendPitchBend(MIDIInstrument*, int, int value, int) { send(value, 0); }
	void sendCC(MIDIInstrument*, int, int cc, int value, int) {
		LONGS_EQUAL(1, cc);
		send(value, 1);
	}
	void sendChannelAftertouch(MIDIInstrument*, int, int value, int) { send(value, 2); }
} midiEngine;
struct MIDIInstrument {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Clip* activeClip = nullptr;
	int32_t lastCombinedPolyExpression[3]{}, lastMonoExpression[3]{};
	int cachedBendRanges[2]{2, 48};
	float ratio = 24;
	struct {
		struct {
			std::vector<ArpNote*> entries;
			int getNumElements() const { return entries.size(); }
			void* getElementAddress(int index) { return entries.at(index); }
		} notes;
	} arpeggiator;
	ParamManager* getParamManager(void*) { return activeClip ? &activeClip->paramManager : nullptr; }
	ArpeggiatorSettings* getArpSettings() { return activeClip ? &activeClip->settings : nullptr; }
	int getChannel() { return 16; }
	int getOutputMasterChannel() { return 0; }
	void monophonicExpressionEvent(int32_t, int32_t);
	void sendMonophonicExpressionEvent(int32_t);
	void combineMPEtoMono(int32_t, int32_t);
};
#include "midi_expression_collapse.inc"
} // namespace midi_expression_collapse_test
using namespace midi_expression_collapse_test;
TEST_GROUP(midi_expression_collapse) {
	std::unique_ptr<MIDIInstrument> instrument;
	std::unique_ptr<Clip> clip;
	void setup() override {
		instrument = std::make_unique<MIDIInstrument>();
		clip = std::make_unique<Clip>();
		instrument->activeClip = clip.get();
		clip->output = instrument.get();
		on_send = {};
		sends = last_value = 0;
		last_dimension = -1;
	}
	void teardown() override {
		on_send = {};
	}
};
TEST(midi_expression_collapse, rejects_invalid_dimensions_before_indexing) {
	for (int dimension : {-1, 3, 255}) {
		instrument->monophonicExpressionEvent(123, dimension);
		instrument->sendMonophonicExpressionEvent(dimension);
		instrument->combineMPEtoMono(123, dimension);
	}
	LONGS_EQUAL(0, sends);
}
TEST(midi_expression_collapse, rejects_reassigned_and_retired_owners) {
	clip->output = nullptr;
	instrument->monophonicExpressionEvent(123, 0);
	instrument->combineMPEtoMono(123, 0);
	clip->output = instrument.get();
	clip->lifetime.retire();
	instrument->monophonicExpressionEvent(123, 0);
	instrument->combineMPEtoMono(123, 0);
	instrument->activeClip = nullptr;
	instrument->lifetime.retire();
	instrument->sendMonophonicExpressionEvent(0);
	LONGS_EQUAL(0, sends);
	LONGS_EQUAL(0, instrument->lastMonoExpression[0]);
}
TEST(midi_expression_collapse, pitch_collapse_saturates_both_limits) {
	instrument->combineMPEtoMono(INT32_MAX, 0);
	LONGS_EQUAL(INT32_MAX, instrument->lastCombinedPolyExpression[0]);
	LONGS_EQUAL(16383, last_value);
	instrument->combineMPEtoMono(INT32_MIN, 0);
	LONGS_EQUAL(INT32_MIN, instrument->lastCombinedPolyExpression[0]);
	LONGS_EQUAL(0, last_value);
}
TEST(midi_expression_collapse, zero_main_range_clears_poly_pitch_without_division) {
	instrument->lastCombinedPolyExpression[0] = 123;
	clip->paramManager.expression.bendRanges[0] = 0;
	instrument->combineMPEtoMono(INT32_MAX, 0);
	DOUBLES_EQUAL(0, instrument->ratio, 0);
	LONGS_EQUAL(0, instrument->lastCombinedPolyExpression[0]);
	LONGS_EQUAL(8192, last_value);
}
TEST(midi_expression_collapse, note_pitch_averages_and_other_dimensions_take_maximum) {
	ArpNote first{{-100, 100, 300}}, second{{-300, 200, 100}};
	instrument->arpeggiator.notes.entries = {&first, &second};
	clip->paramManager.expression.bendRanges[1] = 2;
	instrument->combineMPEtoMono(0, 0);
	LONGS_EQUAL(-200 * 65536, instrument->lastCombinedPolyExpression[0]);
	instrument->combineMPEtoMono(0, 1);
	LONGS_EQUAL(200 * 65536, instrument->lastCombinedPolyExpression[1]);
	instrument->combineMPEtoMono(0, 2);
	LONGS_EQUAL(300 * 65536, instrument->lastCombinedPolyExpression[2]);
	LONGS_EQUAL(3, sends);
	instrument->combineMPEtoMono(0, 2);
	LONGS_EQUAL(3, sends);
}
TEST(midi_expression_collapse, arpeggiating_skips_collapse_and_clipless_uses_cached_ratio) {
	clip->settings.mode = ArpMode::ON;
	instrument->combineMPEtoMono(65536, 0);
	LONGS_EQUAL(0, sends);
	instrument->activeClip = nullptr;
	instrument->ratio = 0.5f;
	instrument->combineMPEtoMono(-65536, 0);
	LONGS_EQUAL(-32768, instrument->lastCombinedPolyExpression[0]);
	LONGS_EQUAL(1, sends);
}
TEST(midi_expression_collapse, mono_output_saturates_pitch_pressure_and_clamps_modulation) {
	instrument->lastCombinedPolyExpression[0] = INT32_MAX;
	instrument->monophonicExpressionEvent(INT32_MAX, 0);
	LONGS_EQUAL(16383, last_value);
	instrument->lastCombinedPolyExpression[2] = INT32_MAX;
	instrument->monophonicExpressionEvent(INT32_MAX, 2);
	LONGS_EQUAL(127, last_value);
	instrument->lastCombinedPolyExpression[1] = INT32_MIN;
	instrument->monophonicExpressionEvent(0, 1);
	LONGS_EQUAL(0, last_value);
	instrument->lastCombinedPolyExpression[1] = INT32_MAX;
	instrument->monophonicExpressionEvent(INT32_MAX, 1);
	LONGS_EQUAL(127, last_value);
}
TEST(midi_expression_collapse, final_output_can_delete_instrument_and_clip) {
	on_send = [&] {
		instrument.reset();
		clip.reset();
	};
	instrument->combineMPEtoMono(65536, 0);
	LONGS_EQUAL(1, sends);
}
