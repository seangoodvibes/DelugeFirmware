#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <optional>

namespace kit_audition_lifetime_test {
constexpr int MIDI_CHANNEL_NONE = -1;
enum class DrumType { SOUND, MIDI };
struct ParamManager {
	bool matches_type(int) { return true; }
};
struct ModControllable {
	int required_param_manager_type() { return 0; }
};
struct Drum : ModControllable {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	Drum* next = nullptr;
	DrumType type = DrumType::MIDI;
	bool auditioned = false, earlyNoteStillActive = false;
	int lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	ModControllable* toModControllable() { return this; }
};
using SoundDrum = Drum;
struct Song {
	ParamManager params;
	ParamManager* getBackedUpParamManagerPreferablyWithClip(SoundDrum*, void*) { return &params; }
};
struct NoteRow {
	ParamManager paramManager;
	bool droning = false, sequenced = false;
	bool isDroning(int) { return droning; }
};
struct ModelStackWithThreeMainThings {};
struct ModelStackWithNoteRow {
	Song* song;
	NoteRow* row = nullptr;
	ModelStackWithThreeMainThings target;
	NoteRow* getNoteRowAllowNull() { return row; }
	int getLoopLength() { return 96; }
	ModelStackWithThreeMainThings* addOtherTwoThings(ModControllable*, ParamManager*) { return &target; }
};
struct InstrumentClip;
struct ModelStackWithTimelineCounter {
	ModelStackWithNoteRow row_stack;
	ModelStackWithNoteRow* addNoteRow(int, NoteRow* row) {
		row_stack.row = row;
		return &row_stack;
	}
};
struct ModelStack {
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(InstrumentClip*) { return &timeline; }
};
std::function<void()> on_tails;
struct Kit;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	Kit* output = nullptr;
	bool tails = true;
	int events = 0;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	bool allowNoteTails(ModelStackWithNoteRow*) {
		bool result = tails;
		if (on_tails)
			on_tails();
		return result;
	}
	void expectEvent() { ++events; }
	NoteRow row;
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter* stack, Drum*) {
		return stack->addNoteRow(0, &row);
	}
};
std::function<void()> on_note;
int notes = 0;
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	InstrumentClip* activeClip = nullptr;
	Drum* member = nullptr;
	Drum* firstDrum = nullptr;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	int getDrumIndex(Drum* drum) {
		if (!firstDrum)
			return drum == member ? 0 : -1;
		int index = 0;
		for (auto* current = firstDrum; current; current = current->next, ++index)
			if (current == drum)
				return index;
		return -1;
	}
	void noteOnPreKitArp(ModelStackWithThreeMainThings*, Drum*, int32_t, int16_t const*, int32_t) {
		++notes;
		if (on_note)
			on_note();
	}
	void noteOffPreKitArp(ModelStackWithThreeMainThings*, Drum*) {
		++notes;
		if (on_note)
			on_note();
	}
	void stopAnyAuditioning(ModelStack*);
	void beginAuditioningforDrum(ModelStackWithNoteRow*, Drum*, int32_t, int16_t const*, int32_t);
	void endAuditioningForDrum(ModelStackWithNoteRow*, Drum*, int32_t = 64);
};
#define FREEZE_WITH_ERROR(message) FAIL(message)
#include "kit_audition_lifetime.inc"
#undef FREEZE_WITH_ERROR
} // namespace kit_audition_lifetime_test
using namespace kit_audition_lifetime_test;
TEST_GROUP(kit_audition_lifetime) {
	std::optional<Kit> kit;
	std::optional<Drum> drum;
	std::optional<InstrumentClip> clip;
	Song song;
	NoteRow row;
	ModelStackWithNoteRow stack{&song, &row};
	void setup() override {
		kit.emplace();
		drum.emplace();
		clip.emplace();
		kit->member = &*drum;
		kit->activeClip = &*clip;
		clip->output = &*kit;
		notes = 0;
	}
	void teardown() override {
		on_note = {};
		on_tails = {};
	}
	void begin() {
		kit->beginAuditioningforDrum(&stack, &*drum, 100, nullptr, 3);
	}
	void end() {
		kit->endAuditioningForDrum(&stack, &*drum);
	}
};
TEST(kit_audition_lifetime, note_on_publishes_state_before_callback) {
	on_note = [&] {
		CHECK(drum->auditioned);
		LONGS_EQUAL(3, drum->lastMIDIChannelAuditioned);
	};
	begin();
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, nested_note_off_state_is_preserved) {
	on_note = [&] {
		drum->auditioned = false;
		drum->lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE;
	};
	begin();
	CHECK_FALSE(drum->auditioned);
	LONGS_EQUAL(MIDI_CHANNEL_NONE, drum->lastMIDIChannelAuditioned);
}
TEST(kit_audition_lifetime, note_on_can_destroy_all_owners) {
	on_note = [&] {
		drum.reset();
		clip.reset();
		kit.reset();
	};
	begin();
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, note_off_can_destroy_all_owners) {
	on_note = [&] {
		drum.reset();
		clip.reset();
		kit.reset();
	};
	end();
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, note_off_clears_state_and_schedules_live_clip) {
	drum->auditioned = true;
	drum->earlyNoteStillActive = true;
	on_note = [&] {
		CHECK_FALSE(drum->auditioned);
		CHECK_FALSE(drum->earlyNoteStillActive);
		LONGS_EQUAL(MIDI_CHANNEL_NONE, drum->lastMIDIChannelAuditioned);
	};
	end();
	LONGS_EQUAL(1, clip->events);
}
TEST(kit_audition_lifetime, note_off_preserves_retargeted_clip) {
	InstrumentClip replacement;
	on_note = [&] { kit->activeClip = &replacement; };
	end();
	LONGS_EQUAL(0, clip->events);
	LONGS_EQUAL(0, replacement.events);
}
TEST(kit_audition_lifetime, note_off_rejects_reused_clip_address) {
	on_note = [&] {
		clip.reset();
		clip.emplace();
		clip->output = &*kit;
	};
	end();
	LONGS_EQUAL(0, clip->events);
}
TEST(kit_audition_lifetime, note_off_rejects_detached_drum) {
	on_note = [&] { kit->member = nullptr; };
	end();
	LONGS_EQUAL(0, clip->events);
}
TEST(kit_audition_lifetime, detached_and_retired_entries_do_not_dispatch) {
	kit->member = nullptr;
	begin();
	end();
	kit->member = &*drum;
	drum->lifetime_source.retire();
	begin();
	end();
	LONGS_EQUAL(0, notes);
}
TEST(kit_audition_lifetime, droning_sequenced_row_does_not_dispatch) {
	row.droning = row.sequenced = true;
	begin();
	end();
	LONGS_EQUAL(0, notes);
}
TEST(kit_audition_lifetime, one_shot_retains_existing_tail_behavior) {
	clip->tails = false;
	begin();
	CHECK_FALSE(drum->auditioned);
	LONGS_EQUAL(3, drum->lastMIDIChannelAuditioned);
}

TEST(kit_audition_lifetime, tail_query_can_destroy_owners_without_dispatch) {
	on_tails = [&] {
		drum.reset();
		clip.reset();
		kit.reset();
	};
	begin();
	LONGS_EQUAL(0, notes);
}
TEST(kit_audition_lifetime, note_off_rejects_changed_output) {
	Kit replacement;
	on_note = [&] { clip->output = &replacement; };
	end();
	LONGS_EQUAL(0, clip->events);
}

TEST(kit_audition_lifetime, stop_all_stops_after_current_drum_destruction) {
	ModelStack all{{{&song}}};
	kit->firstDrum = &*drum;
	drum->auditioned = true;
	on_note = [&] { drum.reset(); };
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, stop_all_stops_after_kit_destruction) {
	ModelStack all{{{&song}}};
	kit->firstDrum = &*drum;
	drum->auditioned = true;
	on_note = [&] { kit.reset(); };
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, stop_all_stops_after_clip_destruction) {
	ModelStack all{{{&song}}};
	kit->firstDrum = &*drum;
	drum->auditioned = true;
	on_note = [&] { clip.reset(); };
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, stop_all_preserves_callback_retarget) {
	ModelStack all{{{&song}}};
	InstrumentClip replacement;
	kit->firstDrum = &*drum;
	drum->auditioned = true;
	on_note = [&] { kit->activeClip = &replacement; };
	kit->stopAnyAuditioning(&all);
	POINTERS_EQUAL(&replacement, kit->activeClip);
	LONGS_EQUAL(0, replacement.events);
}
TEST(kit_audition_lifetime, stop_all_handles_live_list_and_skips_idle_drums) {
	ModelStack all{{{&song}}};
	Drum second, idle;
	kit->firstDrum = &*drum;
	drum->next = &second;
	second.next = &idle;
	drum->auditioned = second.auditioned = true;
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(2, notes);
	CHECK_FALSE(second.auditioned);
	CHECK_FALSE(drum->auditioned);
}
TEST(kit_audition_lifetime, stop_all_stops_after_live_drum_detachment) {
	ModelStack all{{{&song}}};
	Drum second;
	kit->firstDrum = &*drum;
	drum->next = &second;
	drum->auditioned = second.auditioned = true;
	on_note = [&] { kit->firstDrum = &second; };
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(1, notes);
	CHECK(second.auditioned);
}
TEST(kit_audition_lifetime, stop_all_rejects_reused_drum_address) {
	ModelStack all{{{&song}}};
	kit->firstDrum = &*drum;
	drum->auditioned = true;
	on_note = [&] {
		drum.reset();
		drum.emplace();
		drum->next = &*drum;
	};
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(1, notes);
}
TEST(kit_audition_lifetime, stop_all_without_active_clip) {
	ModelStack all{{{&song}}};
	kit->activeClip = nullptr;
	kit->firstDrum = &*drum;
	drum->auditioned = true;
	kit->stopAnyAuditioning(&all);
	LONGS_EQUAL(1, notes);
	CHECK_FALSE(drum->auditioned);
}
