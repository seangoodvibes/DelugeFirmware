#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace kit_expression_lifetime_test {
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MASTER, MPE_MEMBER };
constexpr int MIDI_DIRECTION_INPUT_TO_DELUGE = 0, BEND_RANGE_FINGER_LEVEL = 1;
struct MIDICable {
	struct port {
		bool zone = true;
		bool isChannelPartOfAnMPEZone(uint8_t) { return zone; }
	} ports[1];
};
struct midi_input {
	MIDIMatchType match = MIDIMatchType::MPE_MASTER;
	MIDIMatchType checkMatch(MIDICable*, int32_t) { return match; }
	bool equalsNoteOrCCAllowMPEMasterChannels(MIDICable*, int32_t, int32_t) { return match != MIDIMatchType::NO_MATCH; }
};
struct Kit;
struct Drum {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Drum* next = nullptr;
	midi_input midiInput;
	uint8_t lastMIDIChannelAuditioned = 0;
};
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* output = nullptr;
	Drum* drum = nullptr;
};
struct InstrumentClip : Clip {};
struct ModelStackWithTimelineCounter {
	InstrumentClip* clip = nullptr;
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
};
int dispatched = 0;
std::function<void(Kit*, Drum*)> on_dispatch;
void dispatch(Kit* kit, Drum* drum) {
	++dispatched;
	auto callback = on_dispatch;
	if (callback)
		callback(kit, drum);
}
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Drum* firstDrum = nullptr;
	Clip* activeClip = nullptr;
	midi_input midiInput{MIDIMatchType::NO_MATCH};
	int32_t getDrumIndex(Drum*);
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
TEST_GROUP(kit_expression_lifetime){void setup() override{dispatched = 0;
on_dispatch = {};
}
void teardown() override {
	on_dispatch = {};
}
}
;
TEST(kit_expression_lifetime, live_routes_dispatch_to_all_drums) {
	for (int mode = 0; mode < 6; ++mode) {
		fixture f;
		dispatched = 0;
		send(mode, f.kit, &f.clip);
		LONGS_EQUAL(2, dispatched);
	}
}
TEST(kit_expression_lifetime, current_drum_deletion_stops_before_following_its_next_pointer) {
	for (int mode = 0; mode < 6; ++mode) {
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
	for (int mode = 0; mode < 6; ++mode) {
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
	for (int mode = 0; mode < 6; ++mode) {
		fixture f;
		dispatched = 0;
		on_dispatch = [&](Kit* kit, Drum*) { kit->firstDrum = &f.second; };
		send(mode, f.kit, &f.clip);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(kit_expression_lifetime, kit_destruction_stops_before_list_membership_or_next_dispatch) {
	for (int mode = 0; mode < 6; ++mode) {
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
	for (int mode = 0; mode < 6; ++mode) {
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
	for (int mode = 0; mode < 6; ++mode) {
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
