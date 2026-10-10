#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>
namespace sound_note_off_midi_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, ARP_NOTE_NONE = -1, ALL_NOTES_OFF = -32768;
constexpr int MIDI_CHANNEL_NONE = 255, MIDI_NOTE_NONE = 255, kNoteForDrum = 60, kDefaultNoteOffVelocity = 64,
              kMIDIOutputFilterNoMPE = 0;
enum class ArpNoteStatus { OFF, PLAYING };
struct ModelStackWithSoundFlags {};
struct ArpNote {
	std::array<int16_t, 3> noteCodeOnPostArp{60, 64, ARP_NOTE_NONE};
	std::array<ArpNoteStatus, 3> noteStatus{ArpNoteStatus::PLAYING, ArpNoteStatus::PLAYING, ArpNoteStatus::OFF};
};
struct Arpeggiator {
	std::array<int16_t, 3> glideNoteCodeCurrentlyOnPostArp{55, ARP_NOTE_NONE, ARP_NOTE_NONE};
	ArpNote active_note;
	uint64_t revision = 0;
	uint64_t instruction_revision() const { return revision; }
};
std::function<void()> on_send, on_tails;
std::vector<int> sent;
int all_off;
struct Sound;
struct {
	void sendNote(Sound*, bool on, int note, int velocity, int channel, int) {
		CHECK_FALSE(on);
		LONGS_EQUAL(kDefaultNoteOffVelocity, velocity);
		LONGS_EQUAL(2, channel);
		sent.push_back(note);
		if (on_send)
			on_send();
	}
	void sendAllNotesOff(Sound*, int channel, int) {
		LONGS_EQUAL(2, channel);
		++all_off;
		if (on_send)
			on_send();
	}
} midiEngine;
struct Sound {
	deluge::lifetime::lifetime_source lifetime;
	Arpeggiator arp;
	Arpeggiator* selected_arp = &arp;
	int outputMidiChannel = 2, outputMidiNoteForDrum = MIDI_NOTE_NONE;
	bool tails = true;
	Arpeggiator* getArp() { return selected_arp; }
	bool allowNoteTails(ModelStackWithSoundFlags*, bool) {
		const bool result = tails;
		if (on_tails)
			on_tails();
		return result;
	}
	bool send_note_off_midi(ModelStackWithSoundFlags*, int32_t, const deluge::lifetime::callback_validation*);
};
#include "sound_note_off_midi.inc"
} // namespace sound_note_off_midi_test
using namespace sound_note_off_midi_test;
TEST_GROUP(sound_note_off_midi) {
	std::unique_ptr<Sound> sound;
	ModelStackWithSoundFlags stack;
	bool context_valid = true;
	void reset() {
		sound = std::make_unique<Sound>();
		sent.clear();
		all_off = 0;
		on_send = on_tails = {};
		context_valid = true;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_send = on_tails = {};
	}
	bool send(int note = ALL_NOTES_OFF) {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive() && context_valid; };
		const deluge::lifetime::callback_validation validation{valid};
		return sound->send_note_off_midi(&stack, note, &validation);
	}
};
TEST(sound_note_off_midi, sends_glide_chord_and_fallback_in_order) {
	CHECK(send());
	LONGS_EQUAL(3, sent.size());
	LONGS_EQUAL(55, sent[0]);
	LONGS_EQUAL(60, sent[1]);
	LONGS_EQUAL(64, sent[2]);
	LONGS_EQUAL(1, all_off);
	CHECK(sound->arp.active_note.noteStatus[0] == ArpNoteStatus::OFF);
}
TEST(sound_note_off_midi, each_send_boundary_can_delete_the_sound) {
	for (int boundary = 1; boundary <= 4; ++boundary) {
		reset();
		on_send = [&] {
			if (sent.size() + all_off == boundary)
				sound.reset();
		};
		CHECK_FALSE(send());
		LONGS_EQUAL(boundary, sent.size() + all_off);
	}
}
TEST(sound_note_off_midi, replacement_event_survives_each_send_boundary) {
	for (int boundary = 1; boundary <= 4; ++boundary) {
		reset();
		on_send = [&] {
			if (sent.size() + all_off == boundary) {
				++sound->arp.revision;
				sound->arp.active_note.noteCodeOnPostArp[0] = 77;
				sound->arp.active_note.noteStatus[0] = ArpNoteStatus::PLAYING;
			}
		};
		CHECK_FALSE(send());
		LONGS_EQUAL(boundary, sent.size() + all_off);
		LONGS_EQUAL(77, sound->arp.active_note.noteCodeOnPostArp[0]);
		CHECK(sound->arp.active_note.noteStatus[0] == ArpNoteStatus::PLAYING);
	}
}
TEST(sound_note_off_midi, routing_changes_cancel_remaining_sends) {
	for (int change = 0; change < 3; ++change) {
		reset();
		on_send = [&] {
			if (change == 0)
				sound->outputMidiChannel = 3;
			else if (change == 1)
				sound->outputMidiNoteForDrum = 36;
			else
				sound->selected_arp = nullptr;
		};
		CHECK_FALSE(send());
		LONGS_EQUAL(1, sent.size());
		LONGS_EQUAL(0, all_off);
	}
}
TEST(sound_note_off_midi, completed_slots_are_published_before_callback) {
	on_send = [&] {
		if (sent.size() == 1)
			LONGS_EQUAL(ARP_NOTE_NONE, sound->arp.glideNoteCodeCurrentlyOnPostArp[0]);
		if (sent.size() == 2) {
			LONGS_EQUAL(ARP_NOTE_NONE, sound->arp.active_note.noteCodeOnPostArp[0]);
			CHECK(sound->arp.active_note.noteStatus[0] == ArpNoteStatus::OFF);
		}
	};
	CHECK(send());
}
TEST(sound_note_off_midi, resumed_sweep_reaches_slots_after_cancelled_prefix) {
	on_send = [&] {
		if (sent.size() == 2)
			context_valid = false;
	};
	CHECK_FALSE(send());
	context_valid = true;
	on_send = {};
	CHECK(send());
	LONGS_EQUAL(3, sent.size());
	LONGS_EQUAL(64, sent[2]);
	LONGS_EQUAL(1, all_off);
}
TEST(sound_note_off_midi, one_shot_only_sends_all_notes_off_fallback) {
	sound->tails = false;
	CHECK(send());
	LONGS_EQUAL(0, sent.size());
	LONGS_EQUAL(1, all_off);
	CHECK(send(60));
	LONGS_EQUAL(0, sent.size());
	LONGS_EQUAL(1, all_off);
}
TEST(sound_note_off_midi, drum_relative_notes_are_clamped) {
	sound->outputMidiNoteForDrum = 36;
	CHECK(send(64));
	LONGS_EQUAL(40, sent.back());
	CHECK(send(-100));
	LONGS_EQUAL(0, sent.back());
	CHECK(send(200));
	LONGS_EQUAL(127, sent.back());
}
TEST(sound_note_off_midi, rejected_and_disabled_output_do_not_send) {
	context_valid = false;
	CHECK_FALSE(send());
	context_valid = true;
	sound->outputMidiChannel = MIDI_CHANNEL_NONE;
	CHECK(send());
	LONGS_EQUAL(0, sent.size());
	LONGS_EQUAL(0, all_off);
}
TEST(sound_note_off_midi, tails_check_deletion_cancels_before_sends) {
	on_tails = [&] { sound.reset(); };
	CHECK_FALSE(send());
	LONGS_EQUAL(0, sent.size());
	LONGS_EQUAL(0, all_off);
}
