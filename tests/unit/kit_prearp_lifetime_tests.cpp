#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
namespace deluge::modulation::params {
constexpr int GLOBAL_ARP_RATE = 0;
}
namespace kit_prearp_lifetime_test {
struct InstrumentClip;
struct Song {
	bool isClipActive(InstrumentClip*) { return true; }
} song;
Song* currentSong = &song;
int paramNeutralValues[1]{};
int cableToExpParamShortcut(int value) {
	return value;
}
int getFinalParameterValueExp(int, int value) {
	return value;
}
namespace params {
constexpr int UNPATCHED_ARP_GATE = 0, UNPATCHED_ARP_RATE = 1;
}
struct StereoSample {};
struct UnpatchedParamSet {
	int getValue(int) { return 0; }
} unpatched;
struct ParamManager {
	UnpatchedParamSet* getUnpatchedParamSet() { return &unpatched; }
} manager;
enum class ArpMode { OFF, ON };
constexpr int kNumExpressionDimensions = 3;
constexpr int ARP_NOTE_NONE = -1;
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3;
enum class DrumType { SOUND, MIDI, GATE };
enum class ArpNoteStatus { OFF, PENDING, PLAYING };
struct ModelStackWithSoundFlags {};
struct ModelStackWithThreeMainThings {
	ModelStackWithSoundFlags flags;
	ModelStackWithSoundFlags* addSoundFlags() { return &flags; }
};
struct ArpeggiatorSettings {
	bool includeInKitArp = true;
	ArpMode mode = ArpMode::ON;
	int chordPolyphony = 0, chordProbability = 0, spreadOctave = 0;
	void updateParamsFromUnpatchedParamSet(UnpatchedParamSet*) {}
	int getPhaseIncrement(int value) { return value; }
};
struct ArpNote {
	int noteCodeOnPostArp[3]{0, ARP_NOTE_NONE, ARP_NOTE_NONE};
	ArpNoteStatus noteStatus[3]{ArpNoteStatus::PENDING, ArpNoteStatus::OFF, ArpNoteStatus::OFF};
	uint8_t velocity = 99;
	int16_t mpeValues[kNumExpressionDimensions]{};
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	bool invertReversed = false;
	int noteCodeOffPostArp[3]{ARP_NOTE_NONE, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int glideNoteCodeOffPostArp[3]{ARP_NOTE_NONE, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int sampleSyncLengthOn = 0;
};
std::function<void()> on_arp, on_tails, on_note;
int dispatched = 0, generated = 0;
struct Arpeggiator {
	uint64_t revision = 0;
	uint64_t instruction_revision() const { return revision; }
	std::unique_ptr<ArpNote> pending_note;
	bool invertReversedFromKitArp = false;
	ArpNote note;
	int off_index = 0, glide_index = 0;
	int32_t doTickForward(ArpeggiatorSettings*, ArpReturnInstruction* instruction, int32_t, bool) {
		++generated;
		instruction->arpNoteOn = pending_note ? pending_note.get() : &note;
		instruction->noteCodeOffPostArp[0] = off_index;
		instruction->glideNoteCodeOffPostArp[0] = glide_index;
		if (on_arp)
			on_arp();
		return 7;
	}
	void render(ArpeggiatorSettings*, ArpReturnInstruction* instruction, size_t, uint32_t, uint32_t) {
		instruction->arpNoteOn = pending_note ? pending_note.get() : &note;
		instruction->noteCodeOffPostArp[0] = off_index;
		instruction->glideNoteCodeOffPostArp[0] = glide_index;
		if (on_arp)
			on_arp();
	}
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
	void noteOffPostArpeggiator(ModelStackWithSoundFlags*, int) {
		++dispatched;
		if (on_note)
			on_note();
	}
	void process_postarp_notes(ModelStackWithSoundFlags*, ArpeggiatorSettings*, ArpReturnInstruction&,
	                           const deluge::lifetime::callback_validation* = nullptr) {
		++dispatched;
		if (on_note)
			on_note();
	}
	void noteOffPostArp(int) {
		++dispatched;
		if (on_note)
			on_note();
	}
	void noteOnPostArp(int, ArpNote* note, int index) {
		CHECK(note->noteStatus[index] == ArpNoteStatus::PLAYING);
		++dispatched;
		if (on_note)
			on_note();
	}

	Drum* toModControllable() { return this; }
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
	void noteOff(ModelStackWithThreeMainThings*, int32_t = 64) {
		++dispatched;
		if (on_note)
			on_note();
	}
};
using SoundDrum = Drum;
using NonAudioDrum = Drum;
struct NoteRow {
	bool hasIndependentPlayPos() { return false; }
	int lastProcessedPosIfIndependent = 0;
	ParamManager paramManager;
	Drum* drum = nullptr;
	uint64_t undo_identity = 1;
};
struct Kit;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	Kit* output = nullptr;
	bool currentlyPlayingReversed = false;
	NoteRow* row = nullptr;
	ParamManager paramManager;
	struct row_view {
		InstrumentClip* clip;
		int getNumElements() { return clip->row ? 1 : 0; }
		NoteRow* getElement(int index) {
			CHECK(index == 0);
			return clip->row;
		}
	} noteRows{this};
	NoteRow* find_note_row_from_id(int index) { return index == 0 ? row : nullptr; }
	ArpeggiatorSettings arpSettings;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	NoteRow* getNoteRowForDrum(Drum* drum, int32_t* index = nullptr) {
		if (index)
			*index = row ? 0 : -1;
		return row && row->drum == drum ? row : nullptr;
	}
};
struct ModelStackWithTimelineCounter {
	Song* song = &kit_prearp_lifetime_test::song;
	bool isCurrentlyPlayingReversed() { return false; }
	InstrumentClip* clip = nullptr;
	ModelStackWithThreeMainThings main;
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithTimelineCounter* addNoteRow(int, NoteRow*) { return this; }
	ModelStackWithThreeMainThings* addOtherTwoThings(Drum*, ParamManager*) { return &main; }
};
using ModelStackWithNoteRow = ModelStackWithTimelineCounter;
struct ModelStack {
	Song* song = &kit_prearp_lifetime_test::song;
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(InstrumentClip* clip) {
		timeline.clip = clip;
		timeline.song = song;
		return &timeline;
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
	void setupAndRenderArpPreOutput(ModelStackWithTimelineCounter*, ParamManager*, std::span<StereoSample>);
	int32_t doTickForwardForArp(ModelStack*, int32_t);
	ParamManager* getParamManager(Song*) { return &activeClip->paramManager; }
};
#include "kit_arp_dispatch.inc"
#include "kit_prearp_lifetime.inc"
#include "kit_render_prearp_lifetime.inc"
#include "kit_tick_arp_lifetime.inc"
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
		dispatched = generated = 0;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_arp = {};
		on_tails = {};
		on_note = {};
	}
	int32_t tick() {
		ModelStack tick_stack;
		return kit->doTickForwardForArp(&tick_stack, 0);
	}
	void render() {
		ModelStackWithTimelineCounter render_stack;
		render_stack.clip = clip.get();
		StereoSample samples[1];
		kit->setupAndRenderArpPreOutput(&render_stack, &clip->paramManager, samples);
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

TEST(kit_prearp_lifetime, render_dispatches_glide_off_note_off_and_note_on) {
	render();
	LONGS_EQUAL(3, dispatched);
}
TEST(kit_prearp_lifetime, render_generation_can_destroy_owners) {
	on_arp = [&] {
		row.reset();
		drum.reset();
		clip.reset();
		kit.reset();
	};
	render();
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_prearp_lifetime, render_off_callbacks_can_destroy_owners) {
	for (int stop_after : {1, 2}) {
		reset();
		on_note = [&] {
			if (dispatched == stop_after) {
				row.reset();
				drum.reset();
				clip.reset();
				kit.reset();
			}
		};
		render();
		LONGS_EQUAL(stop_after, dispatched);
	}
}
TEST(kit_prearp_lifetime, render_off_row_removal_stops_remaining_events) {
	on_note = [&] {
		clip->row = nullptr;
		row.reset();
	};
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_prearp_lifetime, render_off_drum_deletion_stops_remaining_events) {
	on_note = [&] {
		row->drum = nullptr;
		kit->member = nullptr;
		drum.reset();
	};
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_prearp_lifetime, render_off_row_identity_change_stops_remaining_events) {
	on_note = [&] { ++row->undo_identity; };
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_prearp_lifetime, render_rejects_out_of_range_row_indices) {
	for (int index : {-2, 1, 100}) {
		reset();
		kit->arpeggiator.off_index = kit->arpeggiator.glide_index = index;
		kit->arpeggiator.note.noteCodeOnPostArp[0] = index;
		render();
		LONGS_EQUAL(0, dispatched);
	}
}

TEST(kit_prearp_lifetime, nested_arp_reset_cancels_remaining_render_instruction) {
	for (int stop_after : {1, 2}) {
		reset();
		kit->arpeggiator.pending_note = std::make_unique<ArpNote>();
		on_note = [&] {
			if (dispatched == stop_after) {
				++kit->arpeggiator.revision;
				kit->arpeggiator.pending_note.reset();
			}
		};
		render();
		LONGS_EQUAL(stop_after, dispatched);
	}
}

TEST(kit_prearp_lifetime, tick_live_sound_midi_and_gate_routes_keep_next_tick) {
	for (auto type : {DrumType::SOUND, DrumType::MIDI, DrumType::GATE}) {
		reset();
		drum->type = type;
		LONGS_EQUAL(7, tick());
		LONGS_EQUAL(6, dispatched);
	}
}
TEST(kit_prearp_lifetime, tick_generation_can_destroy_owners) {
	for (int stop_after : {1, 2}) {
		reset();
		on_arp = [&] {
			if (generated == stop_after) {
				row.reset();
				drum.reset();
				clip.reset();
				kit.reset();
			}
		};
		LONGS_EQUAL(2147483647, tick());
		LONGS_EQUAL(stop_after == 1 ? 0 : 3, dispatched);
	}
}
TEST(kit_prearp_lifetime, tick_destruction_at_every_dispatch_cancels_all_routes) {
	for (auto type : {DrumType::SOUND, DrumType::MIDI, DrumType::GATE}) {
		for (int stop_after = 1; stop_after <= 6; ++stop_after) {
			reset();
			drum->type = type;
			on_note = [&] {
				if (dispatched == stop_after) {
					row.reset();
					drum.reset();
					clip.reset();
					kit.reset();
				}
			};
			LONGS_EQUAL(2147483647, tick());
			LONGS_EQUAL(stop_after, dispatched);
		}
	}
}
TEST(kit_prearp_lifetime, tick_event_replacement_cancels_all_routes) {
	for (auto type : {DrumType::SOUND, DrumType::MIDI, DrumType::GATE}) {
		for (int stop_after = 1; stop_after <= 6; ++stop_after) {
			reset();
			drum->type = type;
			on_note = [&] {
				if (dispatched == stop_after) {
					if (stop_after <= 3)
						++kit->arpeggiator.revision;
					else
						++drum->arpeggiator.revision;
				}
			};
			LONGS_EQUAL(2147483647, tick());
			LONGS_EQUAL(stop_after, dispatched);
		}
	}
}
TEST(kit_prearp_lifetime, tick_row_deletion_after_drum_generation_cancels_dispatch) {
	on_arp = [&] {
		if (generated == 2) {
			clip->row = nullptr;
			row.reset();
		}
	};
	LONGS_EQUAL(2147483647, tick());
	LONGS_EQUAL(3, dispatched);
}
TEST(kit_prearp_lifetime, tick_note_publication_does_not_overwrite_nested_reset) {
	drum->type = DrumType::MIDI;
	on_note = [&] {
		if (dispatched == 6) {
			++drum->arpeggiator.revision;
			drum->arpeggiator.note.noteStatus[0] = ArpNoteStatus::OFF;
		}
	};
	LONGS_EQUAL(2147483647, tick());
	CHECK(drum->arpeggiator.note.noteStatus[0] == ArpNoteStatus::OFF);
}
TEST(kit_prearp_lifetime, tick_rejects_invalid_entry_without_generation) {
	LONGS_EQUAL(2147483647, kit->doTickForwardForArp(nullptr, 0));
	clip->output = nullptr;
	LONGS_EQUAL(2147483647, tick());
	clip->output = kit.get();
	clip->lifetime_source.retire();
	LONGS_EQUAL(2147483647, tick());
	LONGS_EQUAL(0, generated);
}
TEST(kit_prearp_lifetime, tick_row_identity_and_membership_changes_cancel_each_phase) {
	for (int stop_after : {1, 4}) {
		for (bool detach : {false, true}) {
			reset();
			on_note = [&] {
				if (dispatched == stop_after) {
					if (detach)
						kit->member = nullptr;
					else
						++row->undo_identity;
				}
			};
			LONGS_EQUAL(2147483647, tick());
			LONGS_EQUAL(stop_after, dispatched);
		}
	}
}
TEST(kit_prearp_lifetime, tick_changed_model_stack_cancels_dispatch) {
	ModelStack tick_stack;
	on_note = [&] { tick_stack.timeline.clip = nullptr; };
	LONGS_EQUAL(2147483647, kit->doTickForwardForArp(&tick_stack, 0));
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_prearp_lifetime, tick_out_of_range_kit_indices_leave_per_drum_tick_intact) {
	for (int index : {-2, 1, 100}) {
		reset();
		kit->arpeggiator.off_index = kit->arpeggiator.glide_index = index;
		kit->arpeggiator.note.noteCodeOnPostArp[0] = index;
		LONGS_EQUAL(7, tick());
		LONGS_EQUAL(3, dispatched);
	}
}
