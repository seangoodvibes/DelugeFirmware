#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace sound_expression_output_lifetime_test {
constexpr int kNumExpressionDimensions = 3, ARP_MAX_INSTRUCTION_NOTES = 3, ARP_NOTE_NONE = -1;
constexpr int MIDI_CHANNEL_NONE = 255, kMIDIOutputFilterNoMPE = -1, GREATER_OR_EQUAL = 0;
enum class MIDICharacteristic { NOTE, CHANNEL };
enum class PatchSource { X = 10 };
enum class ArpType { DRUM, SYNTH };
namespace util {
template <class T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
int song;
int* currentSong = &song;
bool expressionValueChangesMustBeDoneSmoothly = false;
struct ArpNote {
	int inputCharacteristics[2]{60, 2};
	int16_t mpeValues[3]{};
	std::array<int16_t, ARP_MAX_INSTRUCTION_NOTES> noteCodeOnPostArp{60, 64, ARP_NOTE_NONE};
};
struct ArpeggiatorBase {
	uint64_t revision = 0;
	ArpType type;
	uint64_t instruction_revision() const { return revision; }
	ArpType getArpType() const { return type; }
};
struct Arpeggiator : ArpeggiatorBase {
	Arpeggiator() { type = ArpType::SYNTH; }
	struct {
		std::vector<ArpNote*> entries;
		int getNumElements() const { return entries.size(); }
		void* getElementAddress(int index) { return entries.at(index); }
		int search(int note, int) {
			int index = 0;
			while (index < getNumElements() && entries[index]->inputCharacteristics[0] < note)
				++index;
			return index;
		}
	} notes;
};
struct ArpeggiatorForDrum : ArpeggiatorBase {
	ArpeggiatorForDrum() { type = ArpType::DRUM; }
	ArpNote active_note;
};
struct Sound;
int smooth_updates = 0, immediate_updates = 0;
struct Voice {
	int inputCharacteristics[2]{60, 2};
	void expressionEventSmooth(int, int) { ++smooth_updates; }
	void expressionEventImmediate(Sound&, int, int) { ++immediate_updates; }
};
std::function<void()> on_send;
std::vector<int> notes_sent;
int channel_messages = 0;
struct {
	void sendChannelAftertouch(Sound*, int, int, int) {
		++channel_messages;
		if (on_send)
			on_send();
	}
	void sendPolyphonicAftertouch(Sound*, int, int, int note, int) {
		notes_sent.push_back(note);
		if (on_send)
			on_send();
	}
} midiEngine;
struct Sound {
	using ActiveVoice = std::unique_ptr<Voice>;
	std::vector<ActiveVoice> voice_list;
	const auto& voices() const { return voice_list; }
	ArpeggiatorBase* arp = nullptr;
	ArpeggiatorBase* getArp() { return arp; }
	int outputMidiChannel = 0;
	void send_polyphonic_expression_midi(int32_t, int32_t, int32_t, MIDICharacteristic,
	                                     const deluge::lifetime::callback_validation&);
};
struct SoundInstrument;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	SoundInstrument* output = nullptr;
};
struct SoundInstrument : Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Clip* activeClip = nullptr;
	Arpeggiator arpeggiator;
	SoundInstrument() { arp = &arpeggiator; }
	void polyphonicExpressionEventOnChannelOrNote(int32_t, int32_t, int32_t, MIDICharacteristic);
};
struct SoundDrum;
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	SoundDrum* member = nullptr;
	int getDrumIndex(SoundDrum* drum) { return member == drum ? 0 : -1; }
};
struct SoundDrum : Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* kit = nullptr;
	ArpeggiatorForDrum arpeggiator;
	SoundDrum() { arp = &arpeggiator; }
	void expressionEvent(int32_t, int32_t);
	void polyphonicExpressionEventOnChannelOrNote(int32_t, int32_t, int32_t, MIDICharacteristic);
};
#include "sound_drum_expression_lifetime.inc"
#include "sound_expression_output_lifetime.inc"
#include "sound_instrument_expression_lifetime.inc"
} // namespace sound_expression_output_lifetime_test
using namespace sound_expression_output_lifetime_test;
TEST_GROUP(sound_expression_output_lifetime) {
	std::unique_ptr<SoundInstrument> instrument;
	std::unique_ptr<SoundDrum> drum;
	std::unique_ptr<Clip> clip;
	std::unique_ptr<Kit> kit;
	std::unique_ptr<ArpNote> note;
	void reset() {
		on_send = {};
		notes_sent.clear();
		channel_messages = smooth_updates = immediate_updates = 0;
		currentSong = &song;
		expressionValueChangesMustBeDoneSmoothly = false;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
		instrument = std::make_unique<SoundInstrument>();
		drum = std::make_unique<SoundDrum>();
		clip = std::make_unique<Clip>();
		kit = std::make_unique<Kit>();
		note = std::make_unique<ArpNote>();
		clip->output = instrument.get();
		instrument->activeClip = clip.get();
		instrument->arpeggiator.notes.entries = {note.get()};
		instrument->voice_list.push_back(std::make_unique<Voice>());
		drum->voice_list.push_back(std::make_unique<Voice>());
		drum->kit = kit.get();
		kit->member = drum.get();
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_send = {};
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void send(bool to_drum = false, int note_number = 60,
	          MIDICharacteristic characteristic = MIDICharacteristic::NOTE) {
		if (to_drum)
			drum->polyphonicExpressionEventOnChannelOrNote(123 << 16, 2, note_number, characteristic);
		else
			instrument->polyphonicExpressionEventOnChannelOrNote(123 << 16, 2, note_number, characteristic);
	}
};
TEST(sound_expression_output_lifetime, live_synth_and_drum_update_voices_arp_and_midi) {
	for (bool to_drum : {false, true}) {
		reset();
		send(to_drum);
		LONGS_EQUAL(2, notes_sent.size());
		LONGS_EQUAL(60, notes_sent[0]);
		LONGS_EQUAL(64, notes_sent[1]);
		LONGS_EQUAL(1, immediate_updates);
		LONGS_EQUAL(123, to_drum ? drum->arpeggiator.active_note.mpeValues[2] : note->mpeValues[2]);
	}
}
TEST(sound_expression_output_lifetime, missing_synth_note_does_not_send_next_higher_note) {
	for (int note_number : {59, 61, 99})
		send(false, note_number);
	LONGS_EQUAL(0, notes_sent.size());
	LONGS_EQUAL(0, immediate_updates);
}
TEST(sound_expression_output_lifetime, channel_pressure_and_smoothing_remain_supported) {
	expressionValueChangesMustBeDoneSmoothly = true;
	send(false, 2, MIDICharacteristic::CHANNEL);
	LONGS_EQUAL(1, smooth_updates);
	LONGS_EQUAL(123, note->mpeValues[2]);
	LONGS_EQUAL(1, channel_messages);
	LONGS_EQUAL(0, notes_sent.size());
}
TEST(sound_expression_output_lifetime, deletion_during_first_midi_output_stops_chord) {
	for (bool to_drum : {false, true}) {
		reset();
		on_send = [&] {
			instrument.reset();
			drum.reset();
			clip.reset();
			kit.reset();
			note.reset();
		};
		send(to_drum);
		LONGS_EQUAL(1, notes_sent.size());
	}
}
TEST(sound_expression_output_lifetime, replacement_arp_can_free_note_without_reusing_it) {
	on_send = [&] {
		++instrument->arpeggiator.revision;
		instrument->arpeggiator.notes.entries.clear();
		note.reset();
	};
	send();
	LONGS_EQUAL(1, notes_sent.size());
}
TEST(sound_expression_output_lifetime, routing_changes_cancel_remaining_chord) {
	for (int mutation = 0; mutation < 5; ++mutation) {
		reset();
		on_send = [&] {
			switch (mutation) {
			case 0:
				instrument->outputMidiChannel = 1;
				break;
			case 1:
				instrument->activeClip = nullptr;
				break;
			case 2:
				currentSong = nullptr;
				break;
			case 3:
				deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote;
				break;
			case 4:
				clip->output = nullptr;
				break;
			}
		};
		send();
		LONGS_EQUAL(1, notes_sent.size());
	}
}
TEST(sound_expression_output_lifetime, kit_deletion_or_drum_detachment_stops_remaining_chord) {
	for (bool delete_kit : {false, true}) {
		reset();
		on_send = [&] {
			if (delete_kit)
				kit.reset();
			else
				kit->member = nullptr;
		};
		send(true);
		LONGS_EQUAL(1, notes_sent.size());
	}
}
TEST(sound_expression_output_lifetime, invalid_dimensions_characteristics_and_retired_owners_do_no_work) {
	for (int dimension : {-1, 3}) {
		instrument->polyphonicExpressionEventOnChannelOrNote(0, dimension, 60, MIDICharacteristic::NOTE);
		drum->polyphonicExpressionEventOnChannelOrNote(0, dimension, 60, MIDICharacteristic::NOTE);
		drum->expressionEvent(0, dimension);
	}
	instrument->polyphonicExpressionEventOnChannelOrNote(0, 2, 60, static_cast<MIDICharacteristic>(2));
	drum->polyphonicExpressionEventOnChannelOrNote(0, 2, 60, static_cast<MIDICharacteristic>(2));
	instrument->lifetime.retire();
	drum->lifetime.retire();
	send();
	send(true);
	LONGS_EQUAL(0, notes_sent.size());
	LONGS_EQUAL(0, immediate_updates);
}
TEST(sound_expression_output_lifetime, disabled_midi_still_updates_internal_expression) {
	instrument->outputMidiChannel = MIDI_CHANNEL_NONE;
	send();
	LONGS_EQUAL(1, immediate_updates);
	LONGS_EQUAL(123, note->mpeValues[2]);
	LONGS_EQUAL(0, notes_sent.size());
}
