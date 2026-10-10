#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace kit_learned_midi_routing_test {
struct MIDICable {};
enum class DrumType { SOUND, MIDI, GATE };
struct Owner {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
};
struct Kit;
struct Drum : Owner {
	DrumType type = DrumType::SOUND;
	virtual ~Drum() = default;
};
struct NoteRow {
	Drum* drum = nullptr;
	uint64_t undo_identity = 1;
};
struct InstrumentClip : Owner {
	Kit* output = nullptr;
	struct {
		std::vector<NoteRow*> rows;
		int getNumElements() { return rows.size(); }
		NoteRow* getElement(int index) { return rows.at(index); }
	} noteRows;
	NoteRow* find_note_row_from_id(int index) {
		return index >= 0 && index < noteRows.getNumElements() ? noteRows.getElement(index) : nullptr;
	}
};
int song;
int* currentSong = &song;
struct ModelStackWithTimelineCounter {
	int* song = currentSong;
	InstrumentClip* clip = nullptr;
	bool timelineCounterIsSet() { return clip != nullptr; }
	InstrumentClip* getTimelineCounter() { return clip; }
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
};
std::function<void()> on_whole, on_drum;
std::vector<int> rows_received;
std::vector<InstrumentClip*> clips_received;
int whole_calls;
bool whole_used, row_used;
struct ModControllableAudio {
	bool offerReceivedPitchBendToLearnedParams(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*,
	                                           int = -1,
	                                           const deluge::lifetime::callback_validation* validation = nullptr) {
		CHECK(validation);
		CHECK(validation->valid());
		++whole_calls;
		if (on_whole)
			on_whole();
		return whole_used;
	}
	bool offerReceivedCCToLearnedParamsForClip(MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value,
	                                           ModelStackWithTimelineCounter* stack, int row,
	                                           const deluge::lifetime::callback_validation* validation) {
		return offerReceivedPitchBendToLearnedParams(cable, channel, cc, value, stack, row, validation);
	}
};

struct SoundDrum : Drum {
	bool offerReceivedPitchBendToLearnedParams(MIDICable&, uint8_t channel, uint8_t low, uint8_t high,
	                                           ModelStackWithTimelineCounter* stack, int row = -1,
	                                           const deluge::lifetime::callback_validation* validation = nullptr) {
		CHECK(validation);
		CHECK(validation->valid());
		LONGS_EQUAL(2, channel);
		LONGS_EQUAL(3, low);
		LONGS_EQUAL(64, high);
		rows_received.push_back(row);
		clips_received.push_back(stack->clip);
		if (on_drum)
			on_drum();
		return row_used;
	}
	void offerReceivedCCToLearnedParamsForClip(MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value,
	                                           ModelStackWithTimelineCounter* stack, int row,
	                                           const deluge::lifetime::callback_validation* validation) {
		offerReceivedPitchBendToLearnedParams(cable, channel, cc, value, stack, row, validation);
	}
};
struct Kit : Owner, ModControllableAudio {
	std::vector<Drum*> members;
	int getDrumIndex(Drum* drum) {
		for (size_t i = 0; i < members.size(); ++i)
			if (members[i] == drum)
				return i;
		return -1;
	}
	bool offerReceivedPitchBendToLearnedParams(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*);
	void offerReceivedCCToModControllable(MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value,
	                                      ModelStackWithTimelineCounter* stack) {
		ModControllableAudio::offerReceivedPitchBendToLearnedParams(cable, channel, cc, value, stack);
	}
	void offerReceivedCCToLearnedParams(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*);
	bool dispatch_learned_midi(MIDICable&, uint8_t, uint8_t, uint8_t, ModelStackWithTimelineCounter*, bool);
};
#include "kit_learned_midi_routing.inc"
} // namespace kit_learned_midi_routing_test
using namespace kit_learned_midi_routing_test;
TEST_GROUP(kit_learned_midi_routing) {
	std::unique_ptr<Kit> kit;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<SoundDrum> first, second;
	NoteRow first_row, second_row;
	ModelStackWithTimelineCounter stack;
	MIDICable cable;
	void setup() override {
		kit = std::make_unique<Kit>();
		clip = std::make_unique<InstrumentClip>();
		first = std::make_unique<SoundDrum>();
		second = std::make_unique<SoundDrum>();
		first_row.drum = first.get();
		second_row.drum = second.get();
		clip->output = kit.get();
		clip->noteRows.rows = {&first_row, &second_row};
		kit->members = {first.get(), second.get()};
		currentSong = &song;
		stack = {};
		stack.clip = clip.get();
		whole_calls = 0;
		whole_used = row_used = false;
		rows_received.clear();
		clips_received.clear();
		on_whole = on_drum = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void teardown() override {
		on_whole = on_drum = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	bool bend() {
		return kit->offerReceivedPitchBendToLearnedParams(cable, 2, 3, 64, &stack);
	}
	void send(bool pitch) {
		if (pitch)
			bend();
		else
			kit->offerReceivedCCToLearnedParams(cable, 2, 3, 64, &stack);
	}
};
TEST(kit_learned_midi_routing, pitch_bend_supplies_each_sound_drums_row_index) {
	row_used = true;
	CHECK(bend());
	LONGS_EQUAL(1, whole_calls);
	LONGS_EQUAL(2, rows_received.size());
	LONGS_EQUAL(0, rows_received[0]);
	LONGS_EQUAL(1, rows_received[1]);
	POINTERS_EQUAL(clip.get(), clips_received[1]);
}
TEST(kit_learned_midi_routing, pitch_bend_skips_non_sound_and_missing_drums) {
	first->type = DrumType::MIDI;
	second_row.drum = nullptr;
	CHECK_FALSE(bend());
	LONGS_EQUAL(0, rows_received.size());
}
TEST(kit_learned_midi_routing, whole_kit_usage_is_preserved_when_rows_ignore_message) {
	whole_used = true;
	CHECK(bend());
	LONGS_EQUAL(2, rows_received.size());
}
TEST(kit_learned_midi_routing, whole_kit_clone_retarget_routes_to_new_clip) {
	InstrumentClip clone;
	clone.output = kit.get();
	clone.noteRows.rows = {&second_row};
	on_whole = [&] { stack.clip = &clone; };
	CHECK_FALSE(bend());
	LONGS_EQUAL(1, rows_received.size());
	LONGS_EQUAL(0, rows_received[0]);
	POINTERS_EQUAL(&clone, clips_received[0]);
}

TEST(kit_learned_midi_routing, both_routes_deliver_row_indices) {
	for (bool pitch : {false, true}) {
		rows_received.clear();
		send(pitch);
		LONGS_EQUAL(2, rows_received.size());
		LONGS_EQUAL(0, rows_received[0]);
		LONGS_EQUAL(1, rows_received[1]);
	}
}
TEST(kit_learned_midi_routing, whole_callback_owner_deletion_stops_fanout) {
	on_whole = [&] {
		kit.reset();
		clip.reset();
		first.reset();
		second.reset();
	};
	send(false);
	LONGS_EQUAL(0, rows_received.size());
}
TEST(kit_learned_midi_routing, row_callback_owner_deletion_stops_fanout) {
	on_drum = [&] {
		kit.reset();
		clip.reset();
		first.reset();
		second.reset();
	};
	send(true);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, row_replacement_cancels_next_delivery) {
	on_drum = [&] { ++first_row.undo_identity; };
	send(false);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, drum_detachment_cancels_next_delivery) {
	on_drum = [&] { kit->members.clear(); };
	send(true);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, row_count_change_cancels_next_delivery) {
	on_drum = [&] { clip->noteRows.rows.pop_back(); };
	send(false);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, session_change_cancels_next_delivery) {
	on_drum = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	send(true);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, song_change_in_whole_handler_cancels_fanout) {
	int replacement;
	on_whole = [&] { currentSong = &replacement; };
	send(false);
	LONGS_EQUAL(0, rows_received.size());
}
TEST(kit_learned_midi_routing, row_clone_retarget_routes_remaining_rows_to_clone) {
	InstrumentClip clone;
	clone.output = kit.get();
	clone.noteRows.rows = {&first_row, &second_row};
	on_drum = [&] { stack.clip = &clone; };
	send(true);
	LONGS_EQUAL(2, rows_received.size());
	POINTERS_EQUAL(clip.get(), clips_received[0]);
	POINTERS_EQUAL(&clone, clips_received[1]);
}
TEST(kit_learned_midi_routing, unrelated_output_retarget_is_rejected) {
	InstrumentClip replacement;
	on_drum = [&] { stack.clip = &replacement; };
	send(false);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, used_prefix_is_preserved_on_cancellation) {
	row_used = true;
	on_drum = [&] { clip.reset(); };
	CHECK(bend());
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, retired_source_and_null_stack_are_rejected) {
	CHECK_FALSE(kit->offerReceivedPitchBendToLearnedParams(cable, 2, 3, 64, nullptr));
	clip->lifetime.retire();
	send(false);
	LONGS_EQUAL(0, whole_calls);
}

TEST(kit_learned_midi_routing, whole_callback_clip_deletion_stops_before_reacquisition) {
	on_whole = [&] { clip.reset(); };
	send(true);
	LONGS_EQUAL(0, rows_received.size());
}
TEST(kit_learned_midi_routing, row_callback_drum_deletion_stops_before_row_access) {
	on_drum = [&] { first.reset(); };
	send(false);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, same_address_drum_replacement_stops_fanout) {
	on_drum = [&] {
		auto* drum = first.get();
		drum->~SoundDrum();
		new (drum) SoundDrum;
	};
	send(true);
	LONGS_EQUAL(1, rows_received.size());
}
TEST(kit_learned_midi_routing, clipless_whole_kit_route_does_not_visit_rows) {
	stack.clip = nullptr;
	whole_used = true;
	CHECK(bend());
	LONGS_EQUAL(1, whole_calls);
	LONGS_EQUAL(0, rows_received.size());
}
