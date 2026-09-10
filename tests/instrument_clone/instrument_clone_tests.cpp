#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <new>
#include <vector>

enum class Error { NONE, BUG, INSUFFICIENT_RAM };
enum class SequenceDirection { FORWARD, REVERSE, PINGPONG };
namespace deluge::model {
uint64_t next_note_row_identity() {
	static uint64_t next_id = 100;
	return next_id++;
}
} // namespace deluge::model
static int allocated = 0, freed = 0, rows_processed = 0, unsafe_destructions = 0;
static bool fail_allocation = false, fail_row_copy = false;
static Error parameter_error = Error::NONE;
struct GeneralMemoryAllocator {
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t size) {
		if (fail_allocation)
			return nullptr;
		++allocated;
		return ::operator new(size);
	}
};
void delugeDealloc(void* memory) {
	++freed;
	::operator delete(memory);
}
struct ModelStackWithNoteRow {};
struct InstrumentClip;
struct ModelStackWithTimelineCounter {
	InstrumentClip* clip = nullptr;
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithNoteRow row_stack;
	void setTimelineCounter(InstrumentClip* target) { clip = target; }
	ModelStackWithNoteRow* addNoteRow(int, void*) { return &row_stack; }
};
struct NoteRow {
	uint64_t undo_identity = 1;
	Error clone_error = Error::NONE;
	bool borrowed = false;
	Error beenCloned(ModelStackWithNoteRow*, bool) {
		++rows_processed;
		borrowed = false;
		return clone_error;
	}
};
struct Rows {
	std::vector<NoteRow> entries;
	bool cloneFrom(const Rows* source) {
		if (fail_row_copy)
			return false;
		entries = source->entries;
		for (auto& row : entries)
			row.borrowed = true;
		return true;
	}
	int getNumElements() { return entries.size(); }
	NoteRow* getElement(int index) { return &entries.at(index); }
};
struct Params {
	Error cloneParamCollectionsFrom(const Params*, bool, bool, int32_t) { return parameter_error; }
};
struct InstrumentClip {
	Rows noteRows;
	Params paramManager;
	SequenceDirection sequenceDirectionMode = SequenceDirection::FORWARD;
	int32_t loopLength = 16;
	void* output = nullptr;
	bool activeIfNoSolo = true, soloingInSessionMode = true;
	~InstrumentClip() {
		for (auto& row : noteRows.entries)
			if (row.borrowed)
				++unsafe_destructions;
	}
	void copyBasicsFrom(const InstrumentClip* source) {
		sequenceDirectionMode = source->sequenceDirectionMode;
		loopLength = source->loopLength;
	}
	int getNoteRowId(NoteRow*, int index) { return index; }
	Error clone(ModelStackWithTimelineCounter*, bool) const;
};
#include "instrument_clone_method.inc"
TEST_GROUP(InstrumentClone) {
	InstrumentClip source;
	ModelStackWithTimelineCounter stack{&source};
	void setup() {
		allocated = freed = rows_processed = unsafe_destructions = 0;
		fail_allocation = fail_row_copy = false;
		parameter_error = Error::NONE;
		source.noteRows.entries.resize(3);
	}
	void teardown() {
		if (stack.clip != &source) {
			stack.clip->~InstrumentClip();
			delugeDealloc(stack.clip);
		}
		LONGS_EQUAL(allocated, freed);
		LONGS_EQUAL(0, unsafe_destructions);
	}
};
TEST(InstrumentClone, row_failure_finishes_all_rows_and_discards_clone) {
	source.noteRows.entries[0].clone_error = Error::INSUFFICIENT_RAM;
	source.noteRows.entries[1].clone_error = Error::BUG;
	CHECK_TRUE(source.clone(&stack, true) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(stack.clip == &source);
	LONGS_EQUAL(3, rows_processed);
	LONGS_EQUAL(1, freed);
	LONGS_EQUAL(0, unsafe_destructions);
	for (auto& row : source.noteRows.entries) {
		CHECK_FALSE(row.borrowed);
		LONGS_EQUAL(1, row.undo_identity);
	}
}
TEST(InstrumentClone, final_row_failure_restores_original_stack) {
	source.noteRows.entries[2].clone_error = Error::BUG;
	CHECK_TRUE(source.clone(&stack, false) == Error::BUG);
	CHECK_TRUE(stack.clip == &source);
	LONGS_EQUAL(3, rows_processed);
	LONGS_EQUAL(1, freed);
}
TEST(InstrumentClone, successful_clone_is_retained_with_fresh_row_identities) {
	source.sequenceDirectionMode = SequenceDirection::REVERSE;
	CHECK_TRUE(source.clone(&stack, true) == Error::NONE);
	CHECK_TRUE(stack.clip != &source);
	CHECK_TRUE(stack.clip->sequenceDirectionMode == SequenceDirection::FORWARD);
	LONGS_EQUAL(3, rows_processed);
	LONGS_EQUAL(0, freed);
	for (auto& row : stack.clip->noteRows.entries) {
		CHECK_FALSE(row.borrowed);
		CHECK_TRUE(row.undo_identity != 1);
	}
}

TEST(InstrumentClone, missing_or_mismatched_stack_is_rejected_before_allocation) {
	CHECK_TRUE(source.clone(nullptr, true) == Error::BUG);
	ModelStackWithTimelineCounter missing_stack;
	CHECK_TRUE(source.clone(&missing_stack, true) == Error::BUG);
	InstrumentClip other;
	ModelStackWithTimelineCounter other_stack{&other};
	CHECK_TRUE(source.clone(&other_stack, true) == Error::BUG);
	CHECK_TRUE(other_stack.clip == &other);
	LONGS_EQUAL(0, allocated);
	LONGS_EQUAL(0, rows_processed);
}
TEST(InstrumentClone, allocation_failure_leaves_original_stack_untouched) {
	fail_allocation = true;
	CHECK_TRUE(source.clone(&stack, true) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(stack.clip == &source);
	LONGS_EQUAL(0, allocated);
	LONGS_EQUAL(0, rows_processed);
}
TEST(InstrumentClone, invalid_clip_length_is_rejected_before_allocation) {
	for (int32_t invalid_length : {0, -1, INT32_MIN}) {
		for (bool flatten_reversing : {false, true}) {
			source.loopLength = invalid_length;
			source.sequenceDirectionMode = SequenceDirection::REVERSE;
			CHECK_TRUE(source.clone(&stack, flatten_reversing) == Error::BUG);
			CHECK_TRUE(stack.clip == &source);
			LONGS_EQUAL(invalid_length, source.loopLength);
		}
	}
	LONGS_EQUAL(0, allocated);
	LONGS_EQUAL(0, rows_processed);
}
TEST(InstrumentClone, failed_clone_can_be_retried_without_changing_source) {
	for (int failure_stage = 0; failure_stage < 6; ++failure_stage) {
		fail_allocation = failure_stage == 0;
		parameter_error = failure_stage == 1 ? Error::INSUFFICIENT_RAM : Error::NONE;
		fail_row_copy = failure_stage == 2;
		if (failure_stage >= 3)
			source.noteRows.entries[failure_stage - 3].clone_error = Error::INSUFFICIENT_RAM;
		CHECK_TRUE(source.clone(&stack, true) == Error::INSUFFICIENT_RAM);
		CHECK_TRUE(stack.clip == &source);
		LONGS_EQUAL(allocated, freed);
		fail_allocation = fail_row_copy = false;
		parameter_error = Error::NONE;
		for (auto& row : source.noteRows.entries) {
			CHECK_FALSE(row.borrowed);
			LONGS_EQUAL(1, row.undo_identity);
			row.clone_error = Error::NONE;
		}
		CHECK_TRUE(source.clone(&stack, true) == Error::NONE);
		CHECK_TRUE(stack.clip != &source);
		LONGS_EQUAL(3, stack.clip->noteRows.getNumElements());
		stack.clip->~InstrumentClip();
		delugeDealloc(stack.clip);
		stack.clip = &source;
		LONGS_EQUAL(allocated, freed);
		LONGS_EQUAL(0, unsafe_destructions);
	}
}
TEST(InstrumentClone, parameter_failure_discards_clone_before_copying_rows) {
	parameter_error = Error::BUG;
	CHECK_TRUE(source.clone(&stack, true) == Error::BUG);
	CHECK_TRUE(stack.clip == &source);
	LONGS_EQUAL(1, allocated);
	LONGS_EQUAL(1, freed);
	LONGS_EQUAL(0, rows_processed);
}
TEST(InstrumentClone, row_array_failure_discards_clone_without_borrowed_storage) {
	fail_row_copy = true;
	CHECK_TRUE(source.clone(&stack, true) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(stack.clip == &source);
	LONGS_EQUAL(1, allocated);
	LONGS_EQUAL(1, freed);
	LONGS_EQUAL(0, rows_processed);
	LONGS_EQUAL(0, unsafe_destructions);
}
