#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>

namespace {
enum class Error { NONE, BUG, INSUFFICIENT_RAM };
enum class SequenceDirection { FORWARD, REVERSE, PINGPONG, OBEY_PARENT };
std::function<void()> on_parameters, on_notes;
int unsafe_destructions = 0, tail_checks = 0;
bool fail_notes = false;
struct Note {
	int32_t pos = 4, length = 2, velocity = 80, lift = 64, probability = 20, iterance = 0, fill = 0;
	int32_t getLength() { return length; }
	void setLength(int32_t value) { length = value; }
	int32_t getVelocity() { return velocity; }
	void setVelocity(int32_t value) { velocity = value; }
	int32_t getLift() { return lift; }
	void setLift(int32_t value) { lift = value; }
	int32_t getProbability() { return probability; }
	void setProbability(int32_t value) { probability = value; }
	int32_t getIterance() { return iterance; }
	void setIterance(int32_t value) { iterance = value; }
	int32_t getFill() { return fill; }
	void setFill(int32_t value) { fill = value; }
};
struct NoteVector {
	Note* data = nullptr;
	int32_t count = 0;
	~NoteVector() { empty(); }
	void init() {
		data = nullptr;
		count = 0;
	}
	void empty() {
		delete[] data;
		init();
	}
	int32_t getNumElements() { return count; }
	void* getElementAddress(int32_t index) { return data + index; }
	Error insertAtIndex(int32_t, int32_t size) {
		if (!fail_notes) {
			data = new Note[size];
			count = size;
		}
		auto callback = on_notes;
		if (callback)
			callback();
		return fail_notes ? Error::INSUFFICIENT_RAM : Error::NONE;
	}
	Error beenCloned(const deluge::lifetime::lifetime_watch* watch) {
		auto* source = data;
		const int32_t source_count = count;
		init();
		if (watch && !watch->alive())
			return Error::BUG;
		auto error = insertAtIndex(0, source_count);
		if (watch && !watch->alive()) {
			empty();
			return Error::BUG;
		}
		if (error == Error::NONE)
			std::copy_n(source, source_count, data);
		return error;
	}
};
struct Params {
	bool borrowed = false;
	~Params() {
		if (borrowed)
			++unsafe_destructions;
	}
	Error beenCloned(int32_t, const deluge::lifetime::lifetime_watch* watch) {
		borrowed = false;
		if (watch && !watch->alive())
			return Error::BUG;
		auto callback = on_parameters;
		if (callback)
			callback();
		return watch && !watch->alive() ? Error::BUG : Error::NONE;
	}
};
struct ModelStackWithNoteRow;
struct InstrumentClip {
	bool tails = true;
	bool allowNoteTails(ModelStackWithNoteRow*) {
		++tail_checks;
		return tails;
	}
};
struct ModelStackWithNoteRow {
	InstrumentClip* clip;
	int32_t getLoopLength() { return 32; }
	InstrumentClip* getTimelineCounter() { return clip; }
};
struct NoteRow {
	NoteVector notes;
	Params paramManager;
	void* firstOldDrumName = nullptr;
	int32_t ignoreNoteOnsBefore_ = 0;
	SequenceDirection sequenceDirectionMode = SequenceDirection::REVERSE;
	SequenceDirection getEffectiveSequenceDirectionMode(ModelStackWithNoteRow*) { return sequenceDirectionMode; }
	Error beenCloned(ModelStackWithNoteRow*, bool, const deluge::lifetime::lifetime_watch*,
	                 const deluge::lifetime::lifetime_watch*);
};
#include "note_row_clone_lifetime.inc"
} // namespace

TEST_GROUP(note_row_clone_lifetime) {
	InstrumentClip clip;
	ModelStackWithNoteRow stack{&clip};
	void setup() {
		on_parameters = on_notes = {};
		unsafe_destructions = tail_checks = 0;
		fail_notes = false;
	}
	void teardown() {
		on_parameters = on_notes = {};
		LONGS_EQUAL(0, unsafe_destructions);
	}
};

TEST(note_row_clone_lifetime, source_deletion_during_parameters_detaches_borrowed_notes) {
	auto source = std::make_unique<NoteRow>();
	CHECK(source->notes.insertAtIndex(0, 1) == Error::NONE);
	NoteRow destination = *source;
	destination.paramManager.borrowed = true;
	deluge::lifetime::lifetime_source lifetime;
	deluge::lifetime::lifetime_watch watch{lifetime};
	on_parameters = [&] {
		lifetime.retire();
		source.reset();
	};
	CHECK(destination.beenCloned(&stack, true, &watch, nullptr) == Error::BUG);
	LONGS_EQUAL(0, destination.notes.count);
	LONGS_EQUAL(0, tail_checks);
}
TEST(note_row_clone_lifetime, source_deletion_during_normal_and_reverse_note_allocation_cancels) {
	for (bool reverse : {false, true}) {
		on_notes = {};
		auto source = std::make_unique<NoteRow>();
		CHECK(source->notes.insertAtIndex(0, 1) == Error::NONE);
		NoteRow destination = *source;
		destination.paramManager.borrowed = true;
		deluge::lifetime::lifetime_source lifetime;
		deluge::lifetime::lifetime_watch watch{lifetime};
		on_notes = [&] {
			lifetime.retire();
			source.reset();
		};
		CHECK(destination.beenCloned(&stack, reverse, &watch, nullptr) == Error::BUG);
		LONGS_EQUAL(0, destination.notes.count);
		LONGS_EQUAL(0, tail_checks);
	}
}
TEST(note_row_clone_lifetime, retired_output_cancels_at_entry_and_after_each_allocation) {
	for (int stage : {0, 1, 2}) {
		on_parameters = on_notes = {};
		NoteRow source;
		CHECK(source.notes.insertAtIndex(0, 1) == Error::NONE);
		NoteRow destination = source;
		destination.paramManager.borrowed = true;
		destination.firstOldDrumName = &source;
		deluge::lifetime::lifetime_source lifetime, output;
		deluge::lifetime::lifetime_watch watch{lifetime}, output_watch{output};
		if (stage == 0)
			output.retire();
		else if (stage == 1)
			on_parameters = [&] { output.retire(); };
		else
			on_notes = [&] { output.retire(); };
		CHECK(destination.beenCloned(&stack, true, &watch, &output_watch) == Error::BUG);
		POINTERS_EQUAL(nullptr, destination.firstOldDrumName);
		LONGS_EQUAL(0, destination.notes.count);
		LONGS_EQUAL(1, source.notes.count);
		LONGS_EQUAL(0, tail_checks);
	}
}
TEST(note_row_clone_lifetime, expired_source_clears_all_borrowed_fields_before_cleanup) {
	auto source = std::make_unique<NoteRow>();
	CHECK(source->notes.insertAtIndex(0, 1) == Error::NONE);
	NoteRow destination = *source;
	destination.paramManager.borrowed = true;
	destination.firstOldDrumName = source.get();
	deluge::lifetime::lifetime_source lifetime;
	deluge::lifetime::lifetime_watch watch{lifetime};
	lifetime.retire();
	source.reset();
	CHECK(destination.beenCloned(&stack, true, &watch, nullptr) == Error::BUG);
	POINTERS_EQUAL(nullptr, destination.firstOldDrumName);
	LONGS_EQUAL(0, destination.notes.count);
}
TEST(note_row_clone_lifetime, live_guards_preserve_normal_reverse_and_one_shot_positions) {
	for (bool reverse : {false, true}) {
		for (bool tails : {false, true}) {
			clip.tails = tails;
			NoteRow source;
			CHECK(source.notes.insertAtIndex(0, 1) == Error::NONE);
			NoteRow destination = source;
			destination.paramManager.borrowed = true;
			deluge::lifetime::lifetime_source lifetime, output;
			deluge::lifetime::lifetime_watch watch{lifetime}, output_watch{output};
			CHECK(destination.beenCloned(&stack, reverse, &watch, &output_watch) == Error::NONE);
			source.notes.empty();
			LONGS_EQUAL(1, destination.notes.count);
			LONGS_EQUAL(reverse ? (tails ? 26 : 28) : 4, destination.notes.data[0].pos);
			LONGS_EQUAL(reverse && !tails ? 1 : 2, destination.notes.data[0].length);
		}
	}
}
TEST(note_row_clone_lifetime, failed_note_allocation_preserves_original) {
	NoteRow source;
	CHECK(source.notes.insertAtIndex(0, 1) == Error::NONE);
	NoteRow destination = source;
	destination.paramManager.borrowed = true;
	deluge::lifetime::lifetime_source lifetime;
	deluge::lifetime::lifetime_watch watch{lifetime};
	fail_notes = true;
	CHECK(destination.beenCloned(&stack, true, &watch, nullptr) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, source.notes.count);
	LONGS_EQUAL(4, source.notes.data[0].pos);
	LONGS_EQUAL(0, destination.notes.count);
}
