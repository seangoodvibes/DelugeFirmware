#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
namespace kit_arp_dispatch_test {
constexpr int kNumExpressionDimensions = 3;
enum class ArpNoteStatus { PENDING, PLAYING, OFF };
struct ModelStackWithThreeMainThings {};
struct ArpNote {
	uint8_t velocity = 101;
	int16_t mpeValues[kNumExpressionDimensions]{12, -34, 56};
	ArpNoteStatus noteStatus[1]{ArpNoteStatus::PENDING};
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn;
	bool invertReversed = true;
};
std::function<void()> on_note;
int calls = 0;
struct Drum {
	struct {
		bool invertReversedFromKitArp = false;
	} arpeggiator;
	void noteOn(ModelStackWithThreeMainThings* stack, uint8_t velocity, const int16_t* mpe_values, int channel,
	            uint32_t sync_length, int32_t ticks_late, uint32_t samples_late) {
		++calls;
		CHECK(stack);
		LONGS_EQUAL(101, velocity);
		LONGS_EQUAL(0, channel);
		LONGS_EQUAL(96, sync_length);
		LONGS_EQUAL(-2, ticks_late);
		LONGS_EQUAL(3, samples_late);
		if (on_note)
			on_note();
		// A nested reset must not invalidate the expression data still being consumed.
		LONGS_EQUAL(12, mpe_values[0]);
		LONGS_EQUAL(-34, mpe_values[1]);
		LONGS_EQUAL(56, mpe_values[2]);
	}
};
#include "kit_arp_dispatch.inc"
} // namespace kit_arp_dispatch_test
using namespace kit_arp_dispatch_test;
TEST_GROUP(kit_arp_dispatch) {
	std::optional<Drum> drum;
	std::optional<ArpNote> note;
	ModelStackWithThreeMainThings stack;
	void setup() override {
		drum.emplace();
		note.emplace();
		calls = 0;
	}
	void teardown() override {
		on_note = {};
	}
	void dispatch() {
		ArpReturnInstruction instruction{&*note};
		dispatch_kit_arp_note_on(&stack, &*drum, instruction, 96, -2, 3);
	}
};
TEST(kit_arp_dispatch, publishes_status_and_reverse_before_dispatch) {
	on_note = [&] {
		CHECK(note->noteStatus[0] == ArpNoteStatus::PLAYING);
		CHECK(drum->arpeggiator.invertReversedFromKitArp);
	};
	dispatch();
	LONGS_EQUAL(1, calls);
}
TEST(kit_arp_dispatch, preserves_nested_note_off_status) {
	on_note = [&] { note->noteStatus[0] = ArpNoteStatus::OFF; };
	dispatch();
	CHECK(note->noteStatus[0] == ArpNoteStatus::OFF);
}
TEST(kit_arp_dispatch, note_and_drum_can_be_destroyed_during_dispatch) {
	on_note = [&] {
		note.reset();
		drum.reset();
	};
	dispatch();
	LONGS_EQUAL(1, calls);
}
TEST(kit_arp_dispatch, reused_note_storage_is_not_written_after_dispatch) {
	on_note = [&] {
		note.reset();
		note.emplace();
		note->mpeValues[0] = 99;
	};
	dispatch();
	CHECK(note->noteStatus[0] == ArpNoteStatus::PENDING);
	LONGS_EQUAL(99, note->mpeValues[0]);
}
TEST(kit_arp_dispatch, expression_snapshot_survives_nested_edits) {
	on_note = [&] {
		note->mpeValues[0] = 99;
		note->mpeValues[1] = 99;
	};
	dispatch();
	LONGS_EQUAL(99, note->mpeValues[0]);
}
TEST(kit_arp_dispatch, heap_note_and_drum_can_be_freed_during_dispatch) {
	auto* heap_note = new ArpNote;
	auto* heap_drum = new Drum;
	ArpReturnInstruction instruction{heap_note};
	on_note = [&] {
		delete heap_note;
		delete heap_drum;
	};
	dispatch_kit_arp_note_on(&stack, heap_drum, instruction, 96, -2, 3);
	LONGS_EQUAL(1, calls);
}
