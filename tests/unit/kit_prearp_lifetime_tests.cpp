#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
namespace kit_prearp_lifetime_test {
constexpr int kNumExpressionDimensions = 3;
constexpr int ARP_NOTE_NONE = -1;
enum class DrumType { SOUND, MIDI };
enum class ArpNoteStatus { OFF, PLAYING };
struct ModelStackWithSoundFlags {};
struct ModelStackWithThreeMainThings {
	ModelStackWithSoundFlags flags;
	ModelStackWithSoundFlags* addSoundFlags() { return &flags; }
};
struct ArpeggiatorSettings {
	bool includeInKitArp = true;
};
struct ArpNote {
	int noteCodeOnPostArp[1]{0};
	ArpNoteStatus noteStatus[1]{ArpNoteStatus::OFF};
	uint8_t velocity = 99;
	int16_t mpeValues[kNumExpressionDimensions]{};
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	bool invertReversed = false;
	int noteCodeOffPostArp[1]{ARP_NOTE_NONE};
};
std::function<void()> on_arp, on_tails, on_note;
int dispatched = 0;
struct Arpeggiator {
	bool invertReversedFromKitArp = false;
	ArpNote note;
	void noteOn(ArpeggiatorSettings*, int, uint8_t, ArpReturnInstruction* instruction, int32_t, const int16_t*) {
		instruction->arpNoteOn = &note;
		if (on_arp)
			on_arp();
	}
	void noteOff(ArpeggiatorSettings*, int, ArpReturnInstruction* instruction) {
		instruction->noteCodeOffPostArp[0] = 0;
		if (on_arp)
			on_arp();
	}
};
struct Drum {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	DrumType type = DrumType::SOUND;
	ArpeggiatorSettings arpSettings;
	Arpeggiator arpeggiator;
	bool tails = true;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	bool allowNoteTails(ModelStackWithSoundFlags*, bool) {
		bool result = tails;
		if (on_tails)
			on_tails();
		return result;
	}
	void noteOn(ModelStackWithThreeMainThings*, uint8_t, const int16_t*, int32_t, uint32_t, int32_t, uint32_t) {
		++dispatched;
		if (on_note)
			on_note();
	}
	void noteOff(ModelStackWithThreeMainThings*, int32_t) {
		++dispatched;
		if (on_note)
			on_note();
	}
};
using SoundDrum = Drum;
struct NoteRow {
	Drum* drum = nullptr;
	uint64_t undo_identity = 1;
};
struct Kit;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	Kit* output = nullptr;
	NoteRow* row = nullptr;
	ArpeggiatorSettings arpSettings;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	NoteRow* getNoteRowForDrum(Drum* drum, int32_t* index = nullptr) {
		if (index)
			*index = row ? 0 : -1;
		return row && row->drum == drum ? row : nullptr;
	}
};
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	InstrumentClip* activeClip = nullptr;
	Drum* member = nullptr;
	Arpeggiator arpeggiator;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	int getDrumIndex(Drum* drum) { return member == drum ? 0 : -1; }
	ArpeggiatorSettings* getArpSettings() { return activeClip ? &activeClip->arpSettings : nullptr; }
	void noteOnPreKitArp(ModelStackWithThreeMainThings*, Drum*, uint8_t, int16_t const*, int32_t, uint32_t, int32_t,
	                     uint32_t);
	void noteOffPreKitArp(ModelStackWithThreeMainThings*, Drum*, int32_t);
};
#include "kit_arp_dispatch.inc"
#include "kit_prearp_lifetime.inc"
} // namespace kit_prearp_lifetime_test
using namespace kit_prearp_lifetime_test;
TEST_GROUP(kit_prearp_lifetime) {
	std::unique_ptr<Kit> kit;
	std::unique_ptr<Drum> drum;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<NoteRow> row;
	ModelStackWithThreeMainThings stack;
	void reset() {
		on_arp = {};
		on_tails = {};
		on_note = {};
		kit = std::make_unique<Kit>();
		drum = std::make_unique<Drum>();
		clip = std::make_unique<InstrumentClip>();
		row = std::make_unique<NoteRow>();
		kit->activeClip = clip.get();
		kit->member = drum.get();
		clip->output = kit.get();
		clip->row = row.get();
		row->drum = drum.get();
		dispatched = 0;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_arp = {};
		on_tails = {};
		on_note = {};
	}
	void send(bool on) {
		int16_t mpe_values[kNumExpressionDimensions]{};
		if (on)
			kit->noteOnPreKitArp(&stack, drum.get(), 99, mpe_values, 0, 0, 0, 0);
		else
			kit->noteOffPreKitArp(&stack, drum.get(), 64);
	}
};
TEST(kit_prearp_lifetime, live_note_on_and_off_dispatch) {
	send(true);
	send(false);
	LONGS_EQUAL(2, dispatched);
}
TEST(kit_prearp_lifetime, generation_can_destroy_owners) {
	for (bool on : {false, true}) {
		reset();
		on_arp = [&] {
			drum.reset();
			row.reset();
			clip.reset();
			kit.reset();
		};
		send(on);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_prearp_lifetime, generation_can_remove_row) {
	for (bool on : {false, true}) {
		reset();
		on_arp = [&] {
			row.reset();
			clip->row = nullptr;
		};
		send(on);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_prearp_lifetime, generation_rejects_changed_row_identity) {
	for (bool on : {false, true}) {
		reset();
		on_arp = [&] { ++row->undo_identity; };
		send(on);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_prearp_lifetime, generation_rejects_detached_drum) {
	for (bool on : {false, true}) {
		reset();
		on_arp = [&] { kit->member = nullptr; };
		send(on);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_prearp_lifetime, generation_preserves_active_clip_retarget) {
	for (bool on : {false, true}) {
		reset();
		InstrumentClip replacement;
		on_arp = [&] { kit->activeClip = &replacement; };
		send(on);
		LONGS_EQUAL(0, dispatched);
		POINTERS_EQUAL(&replacement, kit->activeClip);
	}
}
TEST(kit_prearp_lifetime, tail_query_can_destroy_owners) {
	for (bool on : {false, true}) {
		reset();
		on_tails = [&] {
			drum.reset();
			row.reset();
			clip.reset();
			kit.reset();
		};
		send(on);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_prearp_lifetime, tail_query_can_remove_row) {
	for (bool on : {false, true}) {
		reset();
		on_tails = [&] {
			row.reset();
			clip->row = nullptr;
		};
		send(on);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_prearp_lifetime, retired_and_detached_entries_do_not_dispatch) {
	drum->lifetime_source.retire();
	send(true);
	send(false);
	LONGS_EQUAL(0, dispatched);
	reset();
	kit->member = nullptr;
	send(true);
	send(false);
	LONGS_EQUAL(0, dispatched);
	reset();
	kit->lifetime_source.retire();
	send(true);
	send(false);
	LONGS_EQUAL(0, dispatched);
	reset();
	clip->lifetime_source.retire();
	send(true);
	send(false);
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_prearp_lifetime, bypass_and_one_shot_dispatch_without_generation) {
	for (bool on : {false, true}) {
		reset();
		on_arp = [] { FAIL("Bypass must not generate kit arp instructions"); };
		drum->arpSettings.includeInKitArp = false;
		send(on);
		LONGS_EQUAL(1, dispatched);
		reset();
		on_arp = [] { FAIL("One-shot must not generate kit arp instructions"); };
		drum->tails = false;
		send(on);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_prearp_lifetime, dispatch_can_destroy_owners) {
	for (bool on : {false, true}) {
		reset();
		on_note = [&] {
			drum.reset();
			row.reset();
			clip.reset();
			kit.reset();
		};
		send(on);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_prearp_lifetime, absent_row_and_mismatched_output_do_not_dispatch) {
	clip->row = nullptr;
	send(true);
	send(false);
	LONGS_EQUAL(0, dispatched);
	reset();
	clip->output = nullptr;
	send(true);
	send(false);
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_prearp_lifetime, no_clip_dispatches_directly) {
	kit->activeClip = nullptr;
	send(true);
	send(false);
	LONGS_EQUAL(2, dispatched);
}
