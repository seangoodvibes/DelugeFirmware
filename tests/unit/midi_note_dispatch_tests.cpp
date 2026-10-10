#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include <array>
#include <climits>
namespace midi_note_dispatch_test {
enum class MIDIMatchType { CHANNEL };
struct MIDICable {};
struct Clip;
struct ModelStackWithTimelineCounter {};
struct ModelStack {
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip*) { return &timeline; }
};
struct Output {
	OutputType type = OutputType::SYNTH;
};
struct Clip {
	Output* output = nullptr;
	ClipType type = ClipType::INSTRUMENT;
};
using InstrumentClip = Clip;
static int sends = 0, last_note = -1;
static bool last_on = false, last_record = false;
struct Kit : Output {
	void receivedNoteForKit(ModelStackWithTimelineCounter*, MIDICable&, bool on, int, int note, int, bool record, bool*,
	                        InstrumentClip*) {
		++sends;
		last_note = note;
		last_on = on;
		last_record = record;
	}
};
struct MelodicInstrument : Output {
	void receivedNote(ModelStackWithTimelineCounter*, MIDICable&, bool on, int, MIDIMatchType, int note, int,
	                  bool record, bool*) {
		++sends;
		last_note = note;
		last_on = on;
		last_record = record;
	}
};
static struct {
	int midiFollowKitRootNote = 36;
} midiEngine;
struct song_fixture {
	bool active = true;
	bool isOutputActiveInArrangement(Output*) { return active; }
};
static song_fixture song;
static song_fixture* currentSong = &song;
static Clip* clipForLastNoteReceived[kMaxMIDIValue + 1]{};
struct MidiFollow {
	Output* sendNoteToClip(MIDICable&, Clip*, MIDIMatchType, bool, int32_t, int32_t, int32_t, bool*, bool, ModelStack*,
	                       bool);
};
#include "midi_note_dispatch.inc"
} // namespace midi_note_dispatch_test
using namespace midi_note_dispatch_test;
TEST_GROUP(MidiNoteDispatch) {
	MidiFollow follow;
	Clip clip;
	MelodicInstrument output;
	ModelStack stack;
	MIDICable cable;
	bool thru = true;
	Output* send(bool on, int note = 60, bool remember = true) {
		return follow.sendNoteToClip(cable, &clip, MIDIMatchType::CHANNEL, on, 0, note, 100, &thru, true, &stack,
		                             remember);
	}
	void setup() override {
		clip.output = &output;
		song.active = true;
		currentSong = &song;
		sends = 0;
		last_note = -1;
		last_on = last_record = false;
		for (auto& target : clipForLastNoteReceived)
			target = nullptr;
	}
};
TEST(MidiNoteDispatch, invalid_notes_and_missing_inputs_do_not_send) {
	for (int note : {-1, INT_MIN, 128, INT_MAX})
		POINTERS_EQUAL(nullptr, send(true, note));
	clip.output = nullptr;
	POINTERS_EQUAL(nullptr, send(true));
	clip.output = &output;
	currentSong = nullptr;
	POINTERS_EQUAL(nullptr, send(false));
	currentSong = &song;
	POINTERS_EQUAL(nullptr, follow.sendNoteToClip(cable, &clip, MIDIMatchType::CHANNEL, true, 0, 60, 100, &thru, true,
	                                              nullptr, true));
	POINTERS_EQUAL(nullptr, follow.sendNoteToClip(cable, nullptr, MIDIMatchType::CHANNEL, true, 0, 60, 100, &thru, true,
	                                              &stack, true));
	LONGS_EQUAL(0, sends);
}
TEST(MidiNoteDispatch, muted_note_off_is_delivered_without_recording) {
	POINTERS_EQUAL(&output, send(true));
	POINTERS_EQUAL(&clip, clipForLastNoteReceived[60]);
	CHECK(last_record);
	song.active = false;
	POINTERS_EQUAL(nullptr, send(true));
	LONGS_EQUAL(1, sends);
	POINTERS_EQUAL(&output, send(false));
	LONGS_EQUAL(2, sends);
	CHECK_FALSE(last_on);
	CHECK_FALSE(last_record);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}
TEST(MidiNoteDispatch, kit_translation_and_specific_track_delivery_preserve_retained_note) {
	Kit kit;
	kit.type = OutputType::KIT;
	clip.output = &kit;
	POINTERS_EQUAL(&kit, send(true, 38, false));
	LONGS_EQUAL(2, last_note);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[38]);
	CHECK(last_on);
	CHECK(last_record);
}
TEST(MidiNoteDispatch, audio_clip_returns_output_without_instrument_dispatch) {
	clip.type = ClipType::AUDIO;
	POINTERS_EQUAL(&output, send(true));
	LONGS_EQUAL(0, sends);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}

TEST(MidiNoteDispatch, incompatible_output_types_are_not_cast_to_instruments) {
	for (auto type : {OutputType::AUDIO, OutputType::NONE}) {
		output.type = type;
		POINTERS_EQUAL(&output, send(true));
		LONGS_EQUAL(0, sends);
		POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
	}
}
TEST(MidiNoteDispatch, supported_melodic_types_deliver_boundary_notes_and_release_retention) {
	for (auto type : {OutputType::SYNTH, OutputType::MIDI_OUT, OutputType::CV}) {
		output.type = type;
		for (int note : {0, kMaxMIDIValue}) {
			POINTERS_EQUAL(&output, send(true, note));
			LONGS_EQUAL(note, last_note);
			POINTERS_EQUAL(&clip, clipForLastNoteReceived[note]);
			POINTERS_EQUAL(&output, send(false, note));
			POINTERS_EQUAL(nullptr, clipForLastNoteReceived[note]);
		}
	}
	LONGS_EQUAL(12, sends);
}
