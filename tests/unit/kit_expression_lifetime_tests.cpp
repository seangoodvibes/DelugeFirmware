#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace kit_expression_lifetime_test {
static int song;
static auto* currentSong = &song;
static bool dispatch_success = true;
static int render_count = 0;
enum class UIType { INSTRUMENT_CLIP };
struct UI {
	UIType getUIContextType() { return UIType::INSTRUMENT_CLIP; }
};
static UI ui;
static UI* getCurrentUI() {
	return &ui;
}
static void uiNeedsRendering(UI*, int32_t, uint32_t) {
	++render_count;
}
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MASTER, MPE_MEMBER };
constexpr int MIDI_DIRECTION_INPUT_TO_DELUGE = 0, BEND_RANGE_FINGER_LEVEL = 1;
struct MIDICable {
	struct port {
		bool zone = true;
		bool isChannelPartOfAnMPEZone(uint8_t) { return zone; }
	} ports[1];
};
struct midi_input {
	bool equalsChannelOrZone(MIDICable*, int32_t) { return match != MIDIMatchType::NO_MATCH; }
	bool equalsNoteOrCCAllowMPE(MIDICable*, int32_t, int32_t) { return match != MIDIMatchType::NO_MATCH; }
	bool equalsNoteOrCC(MIDICable*, int32_t, int32_t) { return match != MIDIMatchType::NO_MATCH; }
	MIDIMatchType match = MIDIMatchType::MPE_MASTER;
	uint8_t noteOrCC = 255;
	MIDIMatchType checkMatch(MIDICable*, int32_t) { return match; }
	bool equalsNoteOrCCAllowMPEMasterChannels(MIDICable*, int32_t, int32_t) { return match != MIDIMatchType::NO_MATCH; }
};
struct Kit;
struct Drum;
void dispatch(Kit*, Drum*);
struct Drum {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Drum* next = nullptr;
	void killAllVoices() { dispatch(nullptr, this); }
	void choke(void*) { dispatch(nullptr, this); }
	midi_input midiInput;
	midi_input muteMIDICommand{MIDIMatchType::NO_MATCH};
	uint8_t lastMIDIChannelAuditioned = 0;
};
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* output = nullptr;
	Drum* drum = nullptr;
};
std::function<void()> on_expression;
int expression_requests = 0;
struct ExpressionParamSet {
	bool automated = false;
	int bendRanges[2]{2, 48};
	bool isAutomated(int) { return automated; }
};
struct ParamManager {
	ExpressionParamSet expression;
	ExpressionParamSet* current = &expression;
	ExpressionParamSet* getExpressionParamSet() { return current; }
	ExpressionParamSet* getOrCreateExpressionParamSet() {
		++expression_requests;
		auto* result = current;
		if (on_expression)
			on_expression();
		return result;
	}
};
struct NoteRow {
	uint64_t undo_identity = 1;
	ParamManager paramManager;
};
struct ModelStack {};
struct ModelStackWithTimelineCounter;
struct ModelStackWithNoteRow {
	NoteRow* row;
	NoteRow* getNoteRowAllowNull() { return row; }
};
struct InstrumentClip : Clip {
	NoteRow row;
	NoteRow* mapped_row = &row;
	NoteRow* getNoteRowForDrum(Drum*) { return mapped_row; }
	ModelStackWithNoteRow row_stack{&row};
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter*, Drum*) { return &row_stack; }
	void toggleNoteRowMute(ModelStackWithNoteRow*);
};
struct ModelStackWithTimelineCounter {
	InstrumentClip* clip = nullptr;
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
};
int dispatched = 0;
ModelStackWithTimelineCounter* dispatched_stack = nullptr;
Clip* last_routed_clip = nullptr;
std::function<void(Kit*, Drum*)> on_dispatch;
void dispatch(Kit* kit, Drum* drum) {
	++dispatched;
	auto callback = on_dispatch;
	if (callback)
		callback(kit, drum);
}
void InstrumentClip::toggleNoteRowMute(ModelStackWithNoteRow*) {
	dispatch(output, drum);
}
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Drum* firstDrum = nullptr;
	Clip* activeClip = nullptr;
	midi_input midiInput{MIDIMatchType::NO_MATCH};
	int32_t getDrumIndex(Drum*);
	void offerBendRangeUpdate(ModelStack*, MIDICable&, int32_t, int32_t, int32_t);
	void cutAllSound();
	void choke();
	bool receivedNoteForDrum(ModelStackWithTimelineCounter* stack, MIDICable&, bool, int32_t, int32_t, int32_t, bool,
	                         bool*, Drum* drum) {
		auto kit_watch = watch_lifetime();
		auto drum_watch = drum->watch_lifetime();
		auto clip_watch = stack->clip ? stack->clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
		const bool had_clip = stack->clip;
		dispatched_stack = stack;
		last_routed_clip = stack->clip;
		dispatch(this, drum);
		return kit_watch.alive() && drum_watch.alive() && (!had_clip || clip_watch.alive()) && dispatch_success;
	}
	void offerReceivedNote(ModelStackWithTimelineCounter*, MIDICable&, bool, int32_t, int32_t, int32_t, bool, bool*);
	void receivedNoteForKit(ModelStackWithTimelineCounter*, MIDICable&, bool, int32_t, int32_t, int32_t, bool, bool*,
	                        InstrumentClip*);
	Drum* getDrumFromIndex(int32_t) { return firstDrum; }
	Drum* getDrumFromNoteCode(InstrumentClip* clip, int32_t) { return clip->drum; }
	void receivedPitchBendForDrum(ModelStackWithTimelineCounter*, Drum* drum, uint8_t, uint8_t, MIDIMatchType, uint8_t,
	                              bool*) {
		dispatch(this, drum);
	}
	void receivedMPEYForDrum(ModelStackWithTimelineCounter*, Drum* drum, MIDIMatchType, uint8_t, uint8_t) {
		dispatch(this, drum);
	}
	void receivedAftertouchForDrum(ModelStackWithTimelineCounter*, Drum* drum, MIDIMatchType, uint8_t, uint8_t) {
		dispatch(this, drum);
	}
	void offerReceivedPitchBend(ModelStackWithTimelineCounter*, MIDICable&, uint8_t, uint8_t, uint8_t, bool*);
	void offerReceivedCC(ModelStackWithTimelineCounter*, MIDICable&, uint8_t, uint8_t, uint8_t, bool*);
	void offerReceivedAftertouch(ModelStackWithTimelineCounter*, MIDICable&, int32_t, int32_t, int32_t, bool*);
	void receivedPitchBendForKit(ModelStackWithTimelineCounter*, MIDICable&, MIDIMatchType, uint8_t, uint8_t, uint8_t,
	                             bool*);
	void receivedCCForKit(ModelStackWithTimelineCounter*, MIDICable&, MIDIMatchType, uint8_t, uint8_t, uint8_t, bool*,
	                      Clip*);
	void receivedAftertouchForKit(ModelStackWithTimelineCounter*, MIDICable&, MIDIMatchType, int32_t, int32_t, int32_t,
	                              bool*);
};
#include "kit_expression_lifetime.inc"
void send(int mode, Kit& kit, InstrumentClip* clip, int32_t note_code = -1) {
	ModelStackWithTimelineCounter stack{clip};
	MIDICable cable;
	bool thru = false;
	switch (mode) {
	case 8:
		kit.cutAllSound();
		break;
	case 9:
		kit.choke();
		break;
	case 0:
		kit.offerReceivedPitchBend(&stack, cable, 0, 0, 64, &thru);
		break;
	case 1:
		kit.offerReceivedCC(&stack, cable, 0, 74, 64, &thru);
		break;
	case 2:
		kit.offerReceivedAftertouch(&stack, cable, 0, 64, note_code, &thru);
		break;
	case 3:
		kit.receivedPitchBendForKit(&stack, cable, MIDIMatchType::MPE_MASTER, 0, 0, 64, &thru);
		break;
	case 4:
		kit.receivedCCForKit(&stack, cable, MIDIMatchType::MPE_MASTER, 0, 74, 64, &thru, clip);
		break;
	case 6:
		kit.offerReceivedNote(&stack, cable, true, 0, 60, 100, true, &thru);
		break;
	case 7:
		kit.receivedNoteForKit(&stack, cable, true, 0, 60, 100, true, &thru, clip);
		break;
	case 5:
		kit.receivedAftertouchForKit(&stack, cable, MIDIMatchType::MPE_MASTER, 0, 64, note_code, &thru);
		break;
	}
}
struct fixture {
	Kit kit;
	Drum first, second;
	InstrumentClip clip;
	fixture() {
		first.next = &second;
		kit.firstDrum = &first;
		kit.activeClip = &clip;
		clip.output = &kit;
		clip.drum = &first;
	}
};
} // namespace kit_expression_lifetime_test
using namespace kit_expression_lifetime_test;
// clang-format off
TEST_GROUP(kit_expression_lifetime) {
	void setup() override {
		dispatched = render_count = 0;
		on_dispatch = {};
 on_expression = {};
 expression_requests = 0;
		dispatch_success = true;
		dispatched_stack = nullptr;
		last_routed_clip = nullptr;
	}
	void teardown() override { on_dispatch = {}; on_expression = {}; }
};
// clang-format on
TEST(kit_expression_lifetime, live_routes_dispatch_to_all_drums) {
	for (int mode = 0; mode < 7; ++mode) {
		fixture f;
		dispatched = 0;
		send(mode, f.kit, &f.clip);
		LONGS_EQUAL(2, dispatched);
	}
}
TEST(kit_expression_lifetime, current_drum_deletion_stops_before_following_its_next_pointer) {
	for (int mode = 0; mode < 7; ++mode) {
		fixture f;
		auto first = std::make_unique<Drum>();
		first->next = &f.second;
		f.kit.firstDrum = first.get();
		dispatched = 0;
		on_dispatch = [&](Kit* kit, Drum*) {
			kit->firstDrum = &f.second;
			first.reset();
		};
		send(mode, f.kit, &f.clip);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, current_drum_address_reuse_does_not_continue_the_old_batch) {
	for (int mode = 0; mode < 7; ++mode) {
		fixture f;
		auto* first = new Drum;
		first->next = &f.second;
		f.kit.firstDrum = first;
		dispatched = 0;
		on_dispatch = [&](Kit*, Drum*) {
			std::destroy_at(first);
			first = std::construct_at(first);
			first->next = &f.second;
		};
		send(mode, f.kit, &f.clip);
		LONGS_EQUAL(1, dispatched);
		delete first;
	}
}
TEST(kit_expression_lifetime, detached_live_drum_stops_batch_before_traversing_old_links) {
	for (int mode = 0; mode < 7; ++mode) {
		fixture f;
		dispatched = 0;
		on_dispatch = [&](Kit* kit, Drum*) { kit->firstDrum = &f.second; };
		send(mode, f.kit, &f.clip);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, kit_destruction_stops_before_list_membership_or_next_dispatch) {
	for (int mode = 0; mode < 7; ++mode) {
		fixture f;
		auto kit = std::make_unique<Kit>();
		kit->firstDrum = &f.first;
		kit->activeClip = &f.clip;
		f.clip.output = kit.get();
		dispatched = 0;
		on_dispatch = [&](Kit*, Drum*) { kit.reset(); };
		send(mode, *kit, &f.clip);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, routed_clip_destruction_stops_batch) {
	for (int mode = 0; mode < 7; ++mode) {
		fixture f;
		auto clip = std::make_unique<InstrumentClip>();
		clip->output = &f.kit;
		f.kit.activeClip = clip.get();
		dispatched = 0;
		on_dispatch = [&](Kit*, Drum*) { clip.reset(); };
		send(mode, f.kit, clip.get());
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, retiring_targets_do_not_receive_expression) {
	for (int mode = 0; mode < 7; ++mode) {
		for (int target = 0; target < 3; ++target) {
			fixture f;
			if (target == 0)
				f.kit.lifetime.retire();
			if (target == 1)
				f.first.lifetime.retire();
			if (target == 2)
				f.clip.lifetime.retire();
			send(mode, f.kit, &f.clip);
			LONGS_EQUAL(0, dispatched);
		}
	}
}
TEST(kit_expression_lifetime, kit_cc_rejects_missing_or_mismatched_clip) {
	fixture f;
	send(4, f.kit, nullptr);
	Kit other;
	f.clip.output = &other;
	send(4, f.kit, &f.clip);
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_expression_lifetime, poly_aftertouch_rejects_missing_clip_detached_or_retiring_drum) {
	fixture f;
	f.kit.activeClip = nullptr;
	send(5, f.kit, &f.clip, 60);
	f.kit.activeClip = &f.clip;
	f.kit.firstDrum = &f.second;
	send(5, f.kit, &f.clip, 60);
	f.kit.firstDrum = &f.first;
	f.first.lifetime.retire();
	send(5, f.kit, &f.clip, 60);
	LONGS_EQUAL(0, dispatched);
}

TEST(kit_expression_lifetime, note_batch_honors_handler_cancellation_even_when_owners_survive) {
	fixture f;
	dispatch_success = false;
	send(6, f.kit, &f.clip);
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_expression_lifetime, successful_note_retarget_is_used_for_remaining_mapped_drums) {
	fixture f;
	InstrumentClip cloned;
	cloned.output = &f.kit;
	on_dispatch = [&](Kit*, Drum*) {
		if (dispatched == 1)
			dispatched_stack->clip = &cloned;
	};
	send(6, f.kit, &f.clip);
	LONGS_EQUAL(2, dispatched);
	POINTERS_EQUAL(&cloned, last_routed_clip);
}
TEST(kit_expression_lifetime, kit_note_mapping_rejects_missing_wrong_or_retiring_clip) {
	fixture f;
	send(7, f.kit, nullptr);
	Kit other;
	f.clip.output = &other;
	send(7, f.kit, &f.clip);
	f.clip.output = &f.kit;
	f.clip.lifetime.retire();
	send(7, f.kit, &f.clip);
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_expression_lifetime, live_kit_note_mapping_dispatches_only_selected_drum) {
	fixture f;
	send(7, f.kit, &f.clip);
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_expression_lifetime, mute_callback_drum_destruction_stops_before_render_or_next_drum) {
	fixture f;
	auto first = std::make_unique<Drum>();
	first->next = &f.second;
	first->midiInput.match = MIDIMatchType::NO_MATCH;
	first->muteMIDICommand.match = MIDIMatchType::CHANNEL;
	f.kit.firstDrum = first.get();
	f.clip.drum = first.get();
	on_dispatch = [&](Kit* kit, Drum*) {
		kit->firstDrum = &f.second;
		first.reset();
	};
	send(6, f.kit, &f.clip);
	LONGS_EQUAL(1, dispatched);
	LONGS_EQUAL(0, render_count);
}
TEST(kit_expression_lifetime, mute_callback_kit_destruction_stops_before_render_or_next_drum) {
	fixture f;
	auto kit = std::make_unique<Kit>();
	kit->firstDrum = &f.first;
	f.clip.output = kit.get();
	f.first.midiInput.match = MIDIMatchType::NO_MATCH;
	f.first.muteMIDICommand.match = MIDIMatchType::CHANNEL;
	on_dispatch = [&](Kit*, Drum*) { kit.reset(); };
	send(6, *kit, &f.clip);
	LONGS_EQUAL(1, dispatched);
	LONGS_EQUAL(0, render_count);
}

TEST(kit_expression_lifetime, live_mute_commands_render_and_continue_to_other_drums) {
	fixture f;
	f.first.midiInput.match = f.second.midiInput.match = MIDIMatchType::NO_MATCH;
	f.first.muteMIDICommand.match = f.second.muteMIDICommand.match = MIDIMatchType::CHANNEL;
	send(6, f.kit, &f.clip);
	LONGS_EQUAL(2, dispatched);
	LONGS_EQUAL(2, render_count);
}
TEST(kit_expression_lifetime, mute_callback_output_reassignment_stops_before_rendering) {
	fixture f;
	f.first.midiInput.match = MIDIMatchType::NO_MATCH;
	f.first.muteMIDICommand.match = MIDIMatchType::CHANNEL;
	on_dispatch = [&](Kit*, Drum*) { f.clip.output = nullptr; };
	send(6, f.kit, &f.clip);
	LONGS_EQUAL(1, dispatched);
	LONGS_EQUAL(0, render_count);
}
TEST(kit_expression_lifetime, note_offer_rejects_clip_assigned_to_another_output) {
	fixture f;
	Kit other;
	f.clip.output = &other;
	send(6, f.kit, &f.clip);
	LONGS_EQUAL(0, dispatched);
}

TEST(kit_expression_lifetime, cut_and_choke_reach_all_live_drums) {
	for (int mode : {8, 9}) {
		fixture f;
		dispatched = 0;
		send(mode, f.kit, nullptr);
		LONGS_EQUAL(2, dispatched);
	}
}
TEST(kit_expression_lifetime, cut_and_choke_stop_after_drum_deletion) {
	for (int mode : {8, 9}) {
		fixture f;
		dispatched = 0;
		auto first = std::make_unique<Drum>();
		first->next = &f.second;
		f.kit.firstDrum = first.get();
		on_dispatch = [&](Kit*, Drum*) { first.reset(); };
		send(mode, f.kit, nullptr);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, cut_and_choke_stop_after_kit_deletion) {
	for (int mode : {8, 9}) {
		fixture f;
		dispatched = 0;
		auto kit = std::make_unique<Kit>();
		kit->firstDrum = &f.first;
		on_dispatch = [&](Kit*, Drum*) { kit.reset(); };
		send(mode, *kit, nullptr);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, cut_and_choke_stop_after_live_detachment) {
	for (int mode : {8, 9}) {
		fixture f;
		dispatched = 0;
		on_dispatch = [&](Kit*, Drum*) { f.kit.firstDrum = &f.second; };
		send(mode, f.kit, nullptr);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, cut_and_choke_reject_same_address_replacement) {
	for (int mode : {8, 9}) {
		fixture f;
		dispatched = 0;
		on_dispatch = [&](Kit*, Drum*) {
			std::destroy_at(&f.first);
			std::construct_at(&f.first);
			f.first.next = &f.second;
		};
		send(mode, f.kit, nullptr);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, cut_and_choke_reject_retiring_owners) {
	for (int mode : {8, 9}) {
		for (bool retire_kit : {false, true}) {
			fixture f;
			dispatched = 0;
			if (retire_kit)
				f.kit.lifetime.retire();
			else
				f.first.lifetime.retire();
			send(mode, f.kit, nullptr);
			LONGS_EQUAL(0, dispatched);
		}
	}
}

static void send_bend_range(Kit& kit, int range = BEND_RANGE_FINGER_LEVEL) {
	MIDICable cable;
	kit.offerBendRangeUpdate(nullptr, cable, 0, range, 12);
}
TEST(kit_expression_lifetime, bend_range_updates_live_unautomated_rows) {
	fixture f;
	send_bend_range(f.kit);
	LONGS_EQUAL(2, expression_requests);
	LONGS_EQUAL(12, f.clip.row.paramManager.expression.bendRanges[1]);
	f.clip.row.paramManager.expression.automated = true;
	f.clip.row.paramManager.expression.bendRanges[1] = 24;
	send_bend_range(f.kit);
	LONGS_EQUAL(24, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_rejects_unsupported_indices) {
	fixture f;
	for (int range : {-1, 0, 2, 100})
		send_bend_range(f.kit, range);
	LONGS_EQUAL(0, expression_requests);
}
TEST(kit_expression_lifetime, bend_range_allocation_can_delete_kit) {
	fixture f;
	auto kit = std::make_unique<Kit>();
	kit->firstDrum = &f.first;
	kit->activeClip = &f.clip;
	f.clip.output = kit.get();
	on_expression = [&] { kit.reset(); };
	send_bend_range(*kit);
	LONGS_EQUAL(1, expression_requests);
}
TEST(kit_expression_lifetime, bend_range_allocation_can_delete_clip) {
	fixture f;
	auto clip = std::make_unique<InstrumentClip>();
	clip->output = &f.kit;
	f.kit.activeClip = clip.get();
	on_expression = [&] { clip.reset(); };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
}
TEST(kit_expression_lifetime, bend_range_allocation_can_delete_drum) {
	fixture f;
	auto drum = std::make_unique<Drum>();
	drum->next = &f.second;
	f.kit.firstDrum = drum.get();
	on_expression = [&] { drum.reset(); };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
}
TEST(kit_expression_lifetime, bend_range_allocation_can_delete_row) {
	fixture f;
	auto row = std::make_unique<NoteRow>();
	f.clip.mapped_row = row.get();
	on_expression = [&] {
		row.reset();
		f.clip.mapped_row = nullptr;
	};
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
}
TEST(kit_expression_lifetime, bend_range_rejects_reused_row_identity) {
	fixture f;
	on_expression = [&] { ++f.clip.row.undo_identity; };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_preserves_replacement_parameter_set) {
	fixture f;
	ExpressionParamSet replacement;
	on_expression = [&] { f.clip.row.paramManager.current = &replacement; };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, replacement.bendRanges[1]);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_preserves_clip_retarget) {
	fixture f;
	InstrumentClip replacement;
	on_expression = [&] { f.kit.activeClip = &replacement; };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_rejects_live_drum_detachment) {
	fixture f;
	on_expression = [&] { f.kit.firstDrum = &f.second; };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_allocation_failure_skips_write) {
	fixture f;
	f.clip.row.paramManager.current = nullptr;
	send_bend_range(f.kit);
	LONGS_EQUAL(2, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_rejects_song_change) {
	fixture f;
	int replacement_song;
	on_expression = [&] { currentSong = &replacement_song; };
	send_bend_range(f.kit);
	currentSong = &song;
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_rejects_panel_change) {
	fixture f;
	std::unique_ptr<deluge::gui::ui_session::Scope> changed_owner;
	const auto other_owner = deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local
	                             ? deluge::gui::ui_session::Id::Remote
	                             : deluge::gui::ui_session::Id::Local;
	on_expression = [&] { changed_owner = std::make_unique<deluge::gui::ui_session::Scope>(other_owner); };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_rejects_output_change) {
	fixture f;
	Kit replacement;
	on_expression = [&] { f.clip.output = &replacement; };
	send_bend_range(f.kit);
	LONGS_EQUAL(1, expression_requests);
	LONGS_EQUAL(48, f.clip.row.paramManager.expression.bendRanges[1]);
}
TEST(kit_expression_lifetime, bend_range_rejects_retiring_and_missing_context) {
	for (int target = 0; target < 5; ++target) {
		fixture f;
		expression_requests = 0;
		if (target == 0)
			f.kit.lifetime.retire();
		if (target == 1)
			f.first.lifetime.retire();
		if (target == 2)
			f.clip.lifetime.retire();
		if (target == 3)
			f.kit.activeClip = nullptr;
		if (target == 4)
			f.clip.output = nullptr;
		send_bend_range(f.kit);
		LONGS_EQUAL(0, expression_requests);
	}
}
