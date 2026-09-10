#include "CppUTest/TestHarness.h"
#include "model/consequence/consequence_note_existence.h"
#include "undo_model.h"

// Note fields/accessors and constructor are production code; storage is doubled.
#include "note_constructor.inc"

TEST_GROUP(NoteExistence) {
	Song song;
	InstrumentClip clip;
	NoteRow row;
	ModelStack stack;
	Note original;
	void setup() override {
		clip.row = &row;
		song.registered = {&clip};
		stack.song = &song;
		currentSong = &song;
		original.pos = 12;
		original.setLength(6);
		original.setVelocity(101);
		original.setProbability(17);
		original.setLift(45);
		original.setIterance({3, 5});
		original.setFill(1);
	}
	void teardown() override {
		currentSong = nullptr;
	}
};

TEST(NoteExistence, deleted_clip_rejected_before_dereference) {
	auto* dead = new InstrumentClip;
	dead->row = &row;
	ConsequenceNoteExistence change(dead, 7, &original, ExistenceChangeType::CREATE);
	delete dead;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, row.notes.insert_calls);
	LONGS_EQUAL(0, row.notes.delete_calls);
}

TEST(NoteExistence, missing_song_wrong_type_and_missing_row_reject_before_mutation) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::CREATE);
	CHECK_TRUE(change.revert(BEFORE, nullptr) == Error::BUG);
	stack.song = nullptr;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	stack.song = &song;
	clip.type = ClipType::AUDIO;
	clip.row_lookups = 0;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, clip.row_lookups);
	clip.type = ClipType::INSTRUMENT;
	clip.row = nullptr;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, row.notes.insert_calls);
	LONGS_EQUAL(0, row.notes.delete_calls);
}

TEST(NoteExistence, same_address_replacement_cannot_receive_or_lose_notes) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::CREATE);
	row.~NoteRow();
	new (&row) NoteRow();
	row.notes.entries.push_back(original);
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	CHECK_TRUE(change.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(1, row.notes.getNumElements());
	LONGS_EQUAL(0, row.notes.insert_calls);
	LONGS_EQUAL(0, row.notes.delete_calls);
}

TEST(NoteExistence, create_and_delete_round_trip_every_note_attribute) {
	for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
		ConsequenceNoteExistence change(&clip, 7, &original, type);
		auto insertTime = type == ExistenceChangeType::CREATE ? AFTER : BEFORE;
		auto deleteTime = type == ExistenceChangeType::CREATE ? BEFORE : AFTER;
		CHECK_TRUE(change.revert(insertTime, &stack) == Error::NONE);
		LONGS_EQUAL(1, row.notes.getNumElements());
		auto* restored = row.notes.getElement(0);
		LONGS_EQUAL(original.pos, restored->pos);
		LONGS_EQUAL(original.getLength(), restored->getLength());
		LONGS_EQUAL(original.getVelocity(), restored->getVelocity());
		LONGS_EQUAL(original.getProbability(), restored->getProbability());
		LONGS_EQUAL(original.getLift(), restored->getLift());
		CHECK_TRUE(original.getIterance() == restored->getIterance());
		LONGS_EQUAL(original.getFill(), restored->getFill());
		CHECK_TRUE(change.revert(deleteTime, &stack) == Error::NONE);
		LONGS_EQUAL(0, row.notes.getNumElements());
	}
}

TEST(NoteExistence, allocation_failure_leaves_other_notes_intact_and_can_be_retried) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	Note neighbour = original;
	neighbour.pos = 30;
	row.notes.entries.push_back(neighbour);
	row.notes.fail_insert = true;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, row.notes.getNumElements());
	LONGS_EQUAL(30, row.notes.getElement(0)->pos);
	row.notes.fail_insert = false;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(2, row.notes.getNumElements());
	LONGS_EQUAL(12, row.notes.getElement(0)->pos);
}

TEST(NoteExistence, missing_delete_remains_no_op_for_iteration_dependent_redo) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	Note neighbour = original;
	neighbour.pos = 30;
	row.notes.entries.push_back(neighbour);
	CHECK_TRUE(change.revert(AFTER, &stack) == Error::NONE);
	LONGS_EQUAL(0, row.notes.delete_calls);
	LONGS_EQUAL(1, row.notes.getNumElements());
}

TEST(NoteExistence, reservation_that_changes_song_aborts_without_insertion) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	row.notes.on_reserve = [] { currentSong = nullptr; };
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, row.notes.insert_calls);
}

TEST(NoteExistence, reservation_that_detaches_clip_aborts_without_insertion) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	row.notes.on_reserve = [&] { song.registered.clear(); };
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, row.notes.insert_calls);
}

TEST(NoteExistence, reservation_that_replaces_row_aborts_without_insertion) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	NoteRow replacement;
	row.notes.on_reserve = [&] { clip.row = &replacement; };
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, row.notes.insert_calls);
	LONGS_EQUAL(0, replacement.notes.insert_calls);
}

TEST(NoteExistence, reservation_that_relocates_row_resolves_new_storage) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	NoteRow relocated;
	relocated.undo_identity = row.undo_identity;
	row.notes.on_reserve = [&] { clip.row = &relocated; };
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(0, row.notes.insert_calls);
	LONGS_EQUAL(1, relocated.notes.insert_calls);
	LONGS_EQUAL(original.pos, relocated.notes.getElement(0)->pos);
}

TEST(NoteExistence, reservation_that_introduces_conflicting_note_does_not_overwrite_it) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	row.notes.on_reserve = [&] {
		Note conflict = original;
		conflict.setVelocity(33);
		row.notes.entries.push_back(conflict);
	};
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(0, row.notes.insert_calls);
	LONGS_EQUAL(1, row.notes.getNumElements());
	LONGS_EQUAL(33, row.notes.getElement(0)->getVelocity());
}

TEST(NoteExistence, commit_failure_propagates_without_initializing_a_note) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	row.notes.fail_commit = true;
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, row.notes.getNumElements());
}

TEST(NoteExistence, reservation_uses_captured_metadata_rather_than_changed_history_fields) {
	ConsequenceNoteExistence change(&clip, 7, &original, ExistenceChangeType::DELETE);
	row.notes.on_reserve = [&] {
		change.pos = 80;
		change.velocity = 1;
	};
	CHECK_TRUE(change.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(original.pos, row.notes.getElement(0)->pos);
	LONGS_EQUAL(original.getVelocity(), row.notes.getElement(0)->getVelocity());
}
