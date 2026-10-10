#include "CppUTest/TestHarness.h"
#include <cmath>
#include <cstdint>

namespace sample_pitch_tuning_test {
struct Sample {
	float midiNote = 60;
	int detections = 0;
	void workOutMIDINote(bool) { ++detections; }
};
struct SampleHolderForVoice {
	Sample* audioFile = nullptr;
	int32_t transpose = 0, cents = 0;
	void setCents(int32_t value) { cents = value; }
	void setTransposeAccordingToSamplePitch(bool, bool, bool, int32_t);
};
#include "sample_pitch_tuning.inc"
} // namespace sample_pitch_tuning_test
using namespace sample_pitch_tuning_test;
TEST_GROUP(SamplePitchTuning) {
	Sample sample;
	SampleHolderForVoice holder;
	void setup() override {
		holder.audioFile = &sample;
	}
};
TEST(SamplePitchTuning, single_note_range_discards_small_detuning_at_its_actual_midi_note) {
	sample.midiNote = 72.03f;
	holder.setTransposeAccordingToSamplePitch(false, false, true, 72);
	LONGS_EQUAL(-12, holder.transpose);
	LONGS_EQUAL(0, holder.cents);
	LONGS_EQUAL(1, sample.detections);
}
TEST(SamplePitchTuning, single_note_range_preserves_detuning_outside_the_tolerance) {
	sample.midiNote = 72.05f;
	holder.setTransposeAccordingToSamplePitch(false, false, true, 72);
	LONGS_EQUAL(-12, holder.transpose);
	LONGS_EQUAL(-5, holder.cents);
}
TEST(SamplePitchTuning, different_target_note_does_not_discard_detuning) {
	sample.midiNote = 72.03f;
	holder.setTransposeAccordingToSamplePitch(false, false, true, 71);
	LONGS_EQUAL(-3, holder.cents);
}
TEST(SamplePitchTuning, octave_minimization_preserves_existing_tuning_behavior) {
	sample.midiNote = 73.03f;
	holder.setTransposeAccordingToSamplePitch(true, false, false, 0);
	LONGS_EQUAL(-1, holder.transpose);
	LONGS_EQUAL(-3, holder.cents);
}
TEST(SamplePitchTuning, invalid_pitch_preserves_existing_holder_tuning) {
	sample.midiNote = -1000;
	holder.transpose = 5;
	holder.cents = 17;
	holder.setTransposeAccordingToSamplePitch(false, false, true, 72);
	LONGS_EQUAL(5, holder.transpose);
	LONGS_EQUAL(17, holder.cents);
}
