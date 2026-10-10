#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
namespace midi_mpe_output_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, kNumExpressionDimensions = 3;
constexpr int MIDI_CHANNEL_MPE_LOWER_ZONE = 16, MIDI_CHANNEL_NONE = 255, kMIDIOutputFilterNoMPE = -1;
constexpr int X_PITCH_BEND = 0, Y_SLIDE_TIMBRE = 1, Z_PRESSURE = 2;
enum class ArpMode { OFF, ON };
enum class MIDICharacteristic { NOTE, CHANNEL };
namespace util {
template <class T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
namespace MIDIDeviceManager {
int highestLastMemberChannelOfUpperZoneOnConnectedOutput = 14;
int lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 1;
} // namespace MIDIDeviceManager
int song;
int* currentSong = &song;
uint16_t lastNoteOffOrder = 1;
struct ArpNote {
	int outputMemberChannel[3]{MIDI_CHANNEL_NONE, MIDI_CHANNEL_NONE, MIDI_CHANNEL_NONE};
	int16_t inputCharacteristics[2]{60, 2};
	int16_t mpeValues[3]{400, 512, 768};
	uint8_t velocity = 99;
};
struct MIDIInstrument;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	MIDIInstrument* output = nullptr;
	struct {
		ArpMode mode = ArpMode::OFF;
	} arpSettings;
};
using ArpeggiatorSettings = decltype(InstrumentClip::arpSettings);
std::function<void()> on_output;
int outputs = 0, notes = 0, combines = 0, last_note_velocity = 0;
int pitch_value = 0, slide_value = 0, pressure_value = 0;
struct {
	void sendPitchBend(MIDIInstrument*, int, int value, int) {
		pitch_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendCC(MIDIInstrument*, int, int, int value, int) {
		slide_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendChannelAftertouch(MIDIInstrument*, int, int value, int) {
		pressure_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendNote(MIDIInstrument*, bool, int, int velocity, int, int) {
		last_note_velocity = velocity;
		++notes;
		++outputs;
		if (on_output)
			on_output();
	}
} midiEngine;
struct MIDIInstrument {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	InstrumentClip* activeClip = nullptr;
	int channel = MIDI_CHANNEL_MPE_LOWER_ZONE, outputMPEY = 74;
	bool mpe = true, internal = false, collapseAftertouch = false, collapseMPE = false;
	int getChannel() const { return channel; }
	bool sendsToMPE() const { return mpe; }
	bool sendsToInternal() const { return internal; }
	struct {
		uint64_t revision = 0;
		uint64_t instruction_revision() const { return revision; }
		struct {
			std::vector<ArpNote*> entries;
			int getNumElements() const { return entries.size(); }
			void* getElementAddress(int index) { return entries.at(index); }
		} notes;
	} arpeggiator;
	struct member {
		int lastNoteCode = -1;
		uint16_t noteOffOrder = 0;
		int lastXValueSent = 0, lastYAndZValuesSent[2]{};
	} mpeOutputMemberChannels[16];
	void sendNoteToInternal(bool, int, int, int) {
		++notes;
		++outputs;
		if (on_output)
			on_output();
	}
	void combineMPEtoMono(int, int) {
		++combines;
		if (on_output)
			on_output();
	}
	bool outputAllMPEValuesOnMemberChannel(const int16_t*, int32_t);
	void noteOnPostArp(int32_t, ArpNote*, int32_t);
	void noteOffPostArp(int32_t, int32_t, int32_t, int32_t);
};
#include "midi_mpe_output_lifetime.inc"
} // namespace midi_mpe_output_lifetime_test
using namespace midi_mpe_output_lifetime_test;
TEST_GROUP(midi_mpe_output_lifetime) {
	std::unique_ptr<MIDIInstrument> instrument;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<ArpNote> note;
	void reset() {
		instrument = std::make_unique<MIDIInstrument>();
		clip = std::make_unique<InstrumentClip>();
		note = std::make_unique<ArpNote>();
		instrument->activeClip = clip.get();
		clip->output = instrument.get();
		instrument->arpeggiator.notes.entries = {note.get()};
		on_output = {};
		outputs = notes = combines = 0;
		pitch_value = slide_value = pressure_value = 0;
		currentSong = &song;
		MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 1;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_output = {};
		currentSong = &song;
	}
	void send() {
		instrument->noteOnPostArp(60, note.get(), 0);
	}
};
TEST(midi_mpe_output_lifetime, live_mpe_values_precede_note_output) {
	send();
	LONGS_EQUAL(4, outputs);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(8292, pitch_value);
	LONGS_EQUAL(65, slide_value);
	LONGS_EQUAL(3, pressure_value);
	LONGS_EQUAL(1, note->outputMemberChannel[0]);
}
TEST(midi_mpe_output_lifetime, every_expression_output_can_destroy_owners_and_cancel_note) {
	for (int stage = 1; stage <= 3; ++stage) {
		reset();
		on_output = [&] {
			if (outputs == stage) {
				note.reset();
				clip.reset();
				instrument.reset();
			}
		};
		send();
		LONGS_EQUAL(stage, outputs);
		LONGS_EQUAL(0, notes);
	}
}
TEST(midi_mpe_output_lifetime, revision_change_can_free_note_without_destroying_output) {
	for (int stage = 1; stage <= 3; ++stage) {
		reset();
		on_output = [&] {
			if (outputs == stage) {
				++instrument->arpeggiator.revision;
				instrument->arpeggiator.notes.entries.clear();
				note.reset();
			}
		};
		send();
		LONGS_EQUAL(stage, outputs);
		LONGS_EQUAL(0, notes);
	}
}
TEST(midi_mpe_output_lifetime, expression_sequence_uses_stable_input_values) {
	on_output = [&] {
		note->mpeValues[1] = 2048;
		note->mpeValues[2] = 4096;
	};
	CHECK(instrument->outputAllMPEValuesOnMemberChannel(note->mpeValues, 1));
	LONGS_EQUAL(65, slide_value);
	LONGS_EQUAL(3, pressure_value);
}
TEST(midi_mpe_output_lifetime, channel_and_y_assignment_changes_cancel_expression_sequence) {
	for (bool change_channel : {false, true}) {
		reset();
		on_output = [&] {
			if (change_channel)
				++instrument->channel;
			else
				++instrument->outputMPEY;
		};
		send();
		LONGS_EQUAL(1, outputs);
		LONGS_EQUAL(0, notes);
	}
}
TEST(midi_mpe_output_lifetime, invalid_member_channels_and_zone_bounds_do_not_access_arrays) {
	CHECK_FALSE(instrument->outputAllMPEValuesOnMemberChannel(note->mpeValues, -1));
	CHECK_FALSE(instrument->outputAllMPEValuesOnMemberChannel(note->mpeValues, 16));
	MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 0;
	send();
	LONGS_EQUAL(0, outputs);
}
TEST(midi_mpe_output_lifetime, non_mpe_and_internal_notes_keep_single_output_behavior) {
	for (bool internal : {false, true}) {
		reset();
		instrument->mpe = false;
		instrument->internal = internal;
		instrument->channel = 1;
		send();
		LONGS_EQUAL(1, outputs);
		LONGS_EQUAL(1, notes);
	}
}
TEST(midi_mpe_output_lifetime, note_off_destruction_stops_mpe_traversal_and_mono_collapse) {
	for (bool mpe : {false, true}) {
		reset();
		instrument->mpe = mpe;
		instrument->collapseAftertouch = instrument->collapseMPE = true;
		on_output = [&] {
			note.reset();
			clip.reset();
			instrument.reset();
		};
		instrument->noteOffPostArp(60, 1, 64, 0);
		LONGS_EQUAL(1, notes);
		LONGS_EQUAL(0, combines);
	}
}
TEST(midi_mpe_output_lifetime, nested_reset_stops_mono_collapse_sequence) {
	instrument->mpe = false;
	instrument->collapseAftertouch = instrument->collapseMPE = true;
	on_output = [&] {
		if (combines == 1)
			++instrument->arpeggiator.revision;
	};
	instrument->noteOffPostArp(60, 1, 64, 0);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(1, combines);
}
TEST(midi_mpe_output_lifetime, shared_member_note_off_recomputes_expression_values) {
	note->outputMemberChannel[0] = 1;
	instrument->noteOffPostArp(61, 1, 64, 0);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(4, outputs);
	LONGS_EQUAL(8292, pitch_value);
	LONGS_EQUAL(65, slide_value);
	LONGS_EQUAL(3, pressure_value);
}
TEST(midi_mpe_output_lifetime, shared_member_note_on_preserves_mpe_averaging) {
	ArpNote existing;
	existing.outputMemberChannel[0] = 1;
	existing.mpeValues[0] = 800;
	existing.mpeValues[1] = 1536;
	existing.mpeValues[2] = 1280;
	instrument->arpeggiator.notes.entries.push_back(&existing);
	send();
	LONGS_EQUAL(4, outputs);
	LONGS_EQUAL(8342, pitch_value);
	LONGS_EQUAL(66, slide_value);
	LONGS_EQUAL(4, pressure_value);
}

TEST(midi_mpe_output_lifetime, note_velocity_is_snapshotted_before_expression_callbacks) {
	on_output = [&] { note->velocity = 17; };
	send();
	LONGS_EQUAL(99, last_note_velocity);
}
TEST(midi_mpe_output_lifetime, zone_reconfiguration_cancels_expression_sequence) {
	on_output = [&] { ++MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput; };
	send();
	LONGS_EQUAL(1, outputs);
	LONGS_EQUAL(0, notes);
}
TEST(midi_mpe_output_lifetime, collapse_setting_change_does_not_start_new_cleanup_sequence) {
	instrument->mpe = false;
	on_output = [&] { instrument->collapseMPE = true; };
	instrument->noteOffPostArp(60, 1, 64, 0);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(0, combines);
}
