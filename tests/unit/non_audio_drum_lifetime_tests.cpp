#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace non_audio_drum_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3;
constexpr int ARP_NOTE_NONE = -1;
constexpr int kNoteForDrum = 60;
enum class ArpNoteStatus { OFF, PENDING, PLAYING };
struct ModelStackWithThreeMainThings {};
struct ArpeggiatorSettings {};
struct ArpNote {
	int noteCodeOnPostArp[3]{60, 61, ARP_NOTE_NONE};
	ArpNoteStatus noteStatus[3]{ArpNoteStatus::PENDING, ArpNoteStatus::PENDING, ArpNoteStatus::OFF};
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	int glideNoteCodeOffPostArp[3]{60, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int noteCodeOffPostArp[3]{61, 62, ARP_NOTE_NONE};
};
std::function<void()> on_generation;
std::function<void(ArpNote*, int)> on_dispatch;
int calls = 0;
int resets = 0;
struct Arpeggiator {
	uint64_t revision = 0;
	uint64_t instruction_revision() const { return revision; }
	void reset() {
		++resets;
		++revision;
	}
	ArpNote active_note;
	void noteOn(ArpeggiatorSettings*, int, uint8_t, ArpReturnInstruction* instruction, int32_t, const int16_t*) {
		instruction->arpNoteOn = &active_note;
		if (on_generation)
			on_generation();
	}
	void noteOff(ArpeggiatorSettings*, int, ArpReturnInstruction*) {
		if (on_generation)
			on_generation();
	}
};
struct drum_base {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	ArpeggiatorSettings settings;
	Arpeggiator arpeggiator;
	int note = 60;
	bool active = true;
	bool hasActiveVoices() { return active; }
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	ArpeggiatorSettings* getArpSettings() { return &settings; }
	void noteOnPostArp(int, ArpNote* arp_note, int index) {
		++calls;
		CHECK(arp_note->noteStatus[index] == ArpNoteStatus::PLAYING);
		if (on_dispatch)
			on_dispatch(arp_note, index);
	}
	void noteOffPostArp(int) {
		++calls;
		if (on_dispatch)
			on_dispatch(nullptr, 0);
	}
};
struct MIDIDrum : drum_base {
	void noteOn(ModelStackWithThreeMainThings*, uint8_t, const int16_t*, int32_t, uint32_t, int32_t, uint32_t);
	void noteOff(ModelStackWithThreeMainThings*, int32_t = 64);
	void killAllVoices();
	bool stop_arp_notes();
};
struct GateDrum : drum_base {
	void noteOn(ModelStackWithThreeMainThings*, uint8_t, const int16_t*, int32_t, uint32_t, int32_t, uint32_t);
	void noteOff(ModelStackWithThreeMainThings*, int32_t = 64);
	void killAllVoices();
	bool stop_arp_notes();
};
#include "gate_drum_note_lifetime.inc"
#include "midi_drum_note_lifetime.inc"
template <class T>
void send(T* drum, bool on) {
	if (on)
		drum->noteOn(nullptr, 100, nullptr, 0, 0, 0, 0);
	else
		drum->noteOff(nullptr, 64);
}
template <class T>
void check_live() {
	T drum;
	calls = 0;
	send(&drum, true);
	LONGS_EQUAL(2, calls);
	calls = 0;
	send(&drum, false);
	LONGS_EQUAL(3, calls);
}
template <class T>
void check_generation_deletion() {
	for (bool on : {false, true}) {
		auto drum = std::make_unique<T>();
		calls = 0;
		on_generation = [&] { drum.reset(); };
		send(drum.get(), on);
		LONGS_EQUAL(0, calls);
		on_generation = {};
	}
}
template <class T>
void check_dispatch_deletion() {
	for (bool on : {false, true}) {
		auto drum = std::make_unique<T>();
		calls = 0;
		on_dispatch = [&](ArpNote*, int) { drum.reset(); };
		send(drum.get(), on);
		LONGS_EQUAL(1, calls);
		on_dispatch = {};
	}
}
template <class T>
void check_nested_reset() {
	T drum;
	calls = 0;
	on_dispatch = [&](ArpNote* note, int) {
		note->noteStatus[0] = ArpNoteStatus::OFF;
		note->noteCodeOnPostArp[1] = ARP_NOTE_NONE;
	};
	send(&drum, true);
	LONGS_EQUAL(1, calls);
	CHECK(drum.arpeggiator.active_note.noteStatus[0] == ArpNoteStatus::OFF);
	on_dispatch = {};
}
template <class T>
void check_retired() {
	T drum;
	drum.lifetime_source.retire();
	calls = 0;
	send(&drum, true);
	send(&drum, false);
	LONGS_EQUAL(0, calls);
}
} // namespace non_audio_drum_lifetime_test
using namespace non_audio_drum_lifetime_test;
TEST_GROUP(non_audio_drum_lifetime){void teardown() override{on_generation = {};
on_dispatch = {};
}
}
;
TEST(non_audio_drum_lifetime, midi_live_chord_and_glide) {
	check_live<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_live_chord_and_glide) {
	check_live<GateDrum>();
}
TEST(non_audio_drum_lifetime, midi_generation_deletion) {
	check_generation_deletion<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_generation_deletion) {
	check_generation_deletion<GateDrum>();
}
TEST(non_audio_drum_lifetime, midi_dispatch_deletion) {
	check_dispatch_deletion<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_dispatch_deletion) {
	check_dispatch_deletion<GateDrum>();
}
TEST(non_audio_drum_lifetime, midi_nested_reset) {
	check_nested_reset<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_nested_reset) {
	check_nested_reset<GateDrum>();
}
TEST(non_audio_drum_lifetime, midi_retired_entry) {
	check_retired<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_retired_entry) {
	check_retired<GateDrum>();
}

template <class T>
void check_kill_deletion() {
	auto drum = std::make_unique<T>();
	calls = resets = 0;
	on_dispatch = [&](ArpNote*, int) { drum.reset(); };
	drum->killAllVoices();
	LONGS_EQUAL(1, calls);
	LONGS_EQUAL(0, resets);
	on_dispatch = {};
}
template <class T>
void check_kill_live() {
	T drum;
	calls = resets = 0;
	drum.killAllVoices();
	LONGS_EQUAL(3, calls);
	LONGS_EQUAL(1, resets);
	drum.active = false;
	drum.killAllVoices();
	LONGS_EQUAL(3, calls);
	LONGS_EQUAL(2, resets);
	drum.lifetime_source.retire();
	drum.killAllVoices();
	LONGS_EQUAL(2, resets);
}
TEST(non_audio_drum_lifetime, midi_kill_deletion) {
	check_kill_deletion<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_kill_deletion) {
	check_kill_deletion<GateDrum>();
}
TEST(non_audio_drum_lifetime, midi_kill_live_and_retired) {
	check_kill_live<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_kill_live_and_retired) {
	check_kill_live<GateDrum>();
}

template <class T>
void check_replaced_instruction() {
	for (bool on : {false, true}) {
		T drum;
		calls = 0;
		on_dispatch = [&](ArpNote*, int) {
			drum.arpeggiator.reset();
			drum.arpeggiator.active_note.noteCodeOnPostArp[1] = 90;
		};
		send(&drum, on);
		LONGS_EQUAL(1, calls);
		on_dispatch = {};
	}
}
TEST(non_audio_drum_lifetime, midi_replaced_instruction_cancels_old_batch) {
	check_replaced_instruction<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_replaced_instruction_cancels_old_batch) {
	check_replaced_instruction<GateDrum>();
}

template <class T>
void check_kill_preserves_replacement() {
	T drum;
	calls = resets = 0;
	on_dispatch = [&](ArpNote*, int) {
		drum.arpeggiator.reset();
		drum.arpeggiator.active_note.noteCodeOnPostArp[0] = 90;
	};
	drum.killAllVoices();
	LONGS_EQUAL(1, calls);
	LONGS_EQUAL(1, resets);
	LONGS_EQUAL(90, drum.arpeggiator.active_note.noteCodeOnPostArp[0]);
	on_dispatch = {};
}
TEST(non_audio_drum_lifetime, midi_kill_does_not_reset_replacement_event) {
	check_kill_preserves_replacement<MIDIDrum>();
}
TEST(non_audio_drum_lifetime, gate_kill_does_not_reset_replacement_event) {
	check_kill_preserves_replacement<GateDrum>();
}
