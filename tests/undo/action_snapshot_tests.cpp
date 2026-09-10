#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/action/action_identity.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_existence.h"
#include "model/note/note_row_edit_context.h"
#include "undo_model.h"

// Production Note deliberately leaves fields uninitialized. Every test input
// must supply all values before snapshot validation compares the note payload.
static Note note_for_test(int32_t pos = 0, int32_t length = 1, uint8_t velocity = 64) {
	Note note;
	note.pos = pos;
	note.length = length;
	note.velocity = velocity;
	note.lift = 64;
	note.probability = 20;
	note.iterance = Iterance{};
	note.fill = 0;
	return note;
}

extern "C" void freezeWithError(const char* message) {
	FAIL(message);
}

// Only these symbolic action kinds are needed by the extracted methods.
enum class ActionType { MISC, ARRANGEMENT_RECORD };
using ClipArray = Song::ClipArray;
constexpr size_t MODEL_STACK_MAX_SIZE = sizeof(ModelStack);

// Clip deletion and allocation are injected collaborators for recording tests.
// The Action method itself is extracted verbatim below.
namespace recording {
inline Error result = Error::NONE;
inline bool fail_allocation = false;
inline bool fail_working_allocation = false;
inline std::function<void()> on_working_allocate;
inline std::function<void()> on_allocate;
inline int allocated = 0, freed = 0, destroyed = 0, reverted = 0;
} // namespace recording
class ConsequenceClipExistence : public Consequence {
public:
	Clip* clip;
	ClipArray* clipArray;
	int32_t clipIndex = 0;
	bool owns_detached_clip = false;
	ConsequenceClipExistence(Clip* c, ClipArray* a, ExistenceChangeType) : clip(c), clipArray(a) {}
	void prepareForDestruction(int32_t, Song*) override;
	bool can_recreate(Song*);
	Error reserve_for_recreation(Song*);
	Error reattach_for_recreation(ModelStackWithTimelineCounter* modelStack);
	Error commit_recreation(Song*);
	~ConsequenceClipExistence() override { ++recording::destroyed; }
	Error revert(TimeType, ModelStack*) override {
		++recording::reverted;
		return recording::result;
	}
};
class GeneralMemoryAllocator {
public:
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t size) {
		if (recording::on_working_allocate)
			recording::on_working_allocate();
		if (recording::fail_working_allocation)
			return nullptr;
		++recording::allocated;
		return ::operator new(size);
	}
	void* allocLowSpeed(size_t size) {
		if (recording::on_allocate)
			recording::on_allocate();
		if (recording::fail_allocation)
			return nullptr;
		++recording::allocated;
		return ::operator new(size);
	}
};
static void delugeDealloc(void* memory) {
	++recording::freed;
	::operator delete(memory);
}
static ModelStack* setupModelStackWithSong(char*, Song* song) {
	static ModelStack stack;
	stack.song = song;
	return &stack;
}

// Only Action storage is doubled. Lookup/list mutation and consequences are
// production implementations; the full allocator/action logger is not linked.
class Action {
public:
	uint64_t action_identity = deluge::model::next_action_identity();
	Consequence* firstConsequence = nullptr;
	ActionType type = ActionType::MISC;
	int32_t posToClearArrangementFrom = 0;
	Error revert(TimeType, ModelStack*);
	Error recordNoteArrayChangeIfNotAlreadySnapshotted(InstrumentClip*, int32_t, NoteVector*, bool, bool = false);
	Error recordNoteArrayChangeDefinitely(InstrumentClip*, int32_t, NoteVector*, bool);
	Error recordNoteExistenceChange(InstrumentClip*, int32_t, Note*, ExistenceChangeType, Note** = nullptr);
	int32_t xScrollClip[2] = {5, 9};
	void addConsequence(Consequence* c) {
		c->next = firstConsequence;
		firstConsequence = c;
	}
	bool recordClipExistenceChange(Song*, ClipArray*, Clip*, ExistenceChangeType);
	bool recordClipLengthChange(Clip*, int32_t);
	bool containsConsequenceNoteArrayChange(InstrumentClip*, int32_t, bool = false);
};

static struct {
	Action* firstAction[2]{};
} actionLogger;
#include "action_snapshot_method.inc"
#include "clip_recreate_method.inc"
enum {
	CORRESPONDING_NOTES_ADJUST_VELOCITY = 0,
	CORRESPONDING_NOTES_SET_PROBABILITY = 1,
	CORRESPONDING_NOTES_SET_VELOCITY = 2,
	CORRESPONDING_NOTES_SET_ITERANCE = 3,
	CORRESPONDING_NOTES_SET_FILL = 4
};
#define D_PRINTLN(...) ((void)0)
#include "iterance_method.inc"
#include "note_delete_method.inc"

TEST_GROUP(ActionSnapshot) {
	InstrumentClip clip;
	NoteRow row;
	NoteVector snapshot;
	Action action;
	void setup() override {
		clip.row = &row;
		snapshot.values = {1, 2};
		NoteVector::fail_clone = false;
		NoteVector::fail_any_insert = false;
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
		recording::fail_working_allocation = false;
	}
	void teardown() override {
		NoteVector::fail_clone = false;
		NoteVector::fail_any_insert = false;
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
		recording::fail_working_allocation = false;
	}
};

TEST(ActionSnapshot, replacement_identity_does_not_suppress_fresh_snapshot_or_reorder_history) {
	ConsequenceNoteArrayChange old(&clip, 7, &snapshot, false);
	old.next = nullptr;
	action.firstConsequence = &old;
	row.undo_identity = deluge::model::next_note_row_identity();
	CHECK_FALSE(action.containsConsequenceNoteArrayChange(&clip, 7, true));
	POINTERS_EQUAL(&old, action.firstConsequence);
	POINTERS_EQUAL(nullptr, old.next);
}

TEST(ActionSnapshot, matching_live_snapshot_moves_to_front_without_losing_other_consequences) {
	ConsequenceNoteArrayChange old(&clip, 7, &snapshot, false);
	row.undo_identity = deluge::model::next_note_row_identity();
	ConsequenceNoteArrayChange live(&clip, 7, &snapshot, false);
	live.next = nullptr;
	old.next = &live;
	action.firstConsequence = &old;
	CHECK_TRUE(action.containsConsequenceNoteArrayChange(&clip, 7));
	POINTERS_EQUAL(&old, action.firstConsequence);
	CHECK_TRUE(action.containsConsequenceNoteArrayChange(&clip, 7, true));
	POINTERS_EQUAL(&live, action.firstConsequence);
	POINTERS_EQUAL(&old, live.next);
	POINTERS_EQUAL(nullptr, old.next);
	CHECK_TRUE(action.containsConsequenceNoteArrayChange(&clip, 7, true));
	POINTERS_EQUAL(&old, live.next);
}

TEST(ActionSnapshot, missing_or_invalid_target_does_not_match_or_reorder) {
	ConsequenceNoteArrayChange saved(&clip, 7, &snapshot, false);
	saved.next = nullptr;
	action.firstConsequence = &saved;
	CHECK_FALSE(action.containsConsequenceNoteArrayChange(nullptr, 7, true));
	CHECK_FALSE(action.containsConsequenceNoteArrayChange(&clip, 8, true));
	clip.row = nullptr;
	CHECK_FALSE(action.containsConsequenceNoteArrayChange(&clip, 7, true));
	clip.row = &row;
	clip.type = ClipType::AUDIO;
	CHECK_FALSE(action.containsConsequenceNoteArrayChange(&clip, 7, true));
	POINTERS_EQUAL(&saved, action.firstConsequence);
	POINTERS_EQUAL(nullptr, saved.next);
}

TEST(ActionSnapshot, failed_snapshot_does_not_suppress_new_capture) {
	NoteVector::fail_clone = true;
	ConsequenceNoteArrayChange failed(&clip, 7, &snapshot, false);
	failed.next = nullptr;
	action.firstConsequence = &failed;
	CHECK_FALSE(action.containsConsequenceNoteArrayChange(&clip, 7, true));
	POINTERS_EQUAL(&failed, action.firstConsequence);
}

TEST_GROUP(ClipExistenceRecording) {
	Action action;
	Song song;
	Clip clip;
	void setup() override {
		recording::result = Error::NONE;
		recording::fail_allocation = false;
		recording::allocated = recording::freed = recording::destroyed = recording::reverted = 0;
	}
	void teardown() override {
		while (action.firstConsequence) {
			auto* c = action.firstConsequence;
			action.firstConsequence = c->next;
			c->~Consequence();
			delugeDealloc(c);
		}
		LONGS_EQUAL(recording::allocated, recording::freed);
	}
};

TEST(ClipExistenceRecording, failed_deletion_does_not_record_history_or_reset_scroll) {
	recording::result = Error::BUG;
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(1, recording::reverted);
	LONGS_EQUAL(1, recording::destroyed);
	LONGS_EQUAL(1, recording::freed);
	LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
	LONGS_EQUAL(9, action.xScrollClip[AFTER]);
}

TEST(ClipExistenceRecording, allocation_failure_does_not_attempt_deletion) {
	recording::fail_allocation = true;
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(ClipExistenceRecording, successful_deletion_is_recorded_once) {
	CHECK_TRUE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	CHECK_TRUE(action.firstConsequence != nullptr);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next);
	LONGS_EQUAL(1, recording::reverted);
	LONGS_EQUAL(0, recording::destroyed);
	LONGS_EQUAL(0, action.xScrollClip[BEFORE]);
	LONGS_EQUAL(0, action.xScrollClip[AFTER]);
}

TEST(ClipExistenceRecording, creation_is_recorded_without_executing_deletion) {
	CHECK_TRUE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	CHECK_TRUE(action.firstConsequence != nullptr);
	LONGS_EQUAL(0, recording::reverted);
}

TEST(ClipExistenceRecording, failed_deletion_preserves_existing_history) {
	CHECK_TRUE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	auto* previous = action.firstConsequence;
	recording::result = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	POINTERS_EQUAL(previous, action.firstConsequence);
	POINTERS_EQUAL(nullptr, previous->next);
	LONGS_EQUAL(1, recording::destroyed);
	LONGS_EQUAL(1, recording::freed);
}

namespace action_revert {
struct Result {
	int calls = 0, cleanups = 0;
	TimeType time = AFTER;
	Error error = Error::NONE;
	std::function<void()> on_revert;
	std::function<void()> on_cleanup;
};
class ConsequenceProbe : public Consequence {
public:
	Result& result;
	explicit ConsequenceProbe(Result& r) : result(r) { next = nullptr; }
	Error revert(TimeType time, ModelStack*) override {
		++result.calls;
		result.time = time;
		if (result.on_revert)
			result.on_revert();
		return result.error;
	}
	void prepareForDestruction(int32_t, Song*) override {
		++result.cleanups;
		if (result.on_cleanup)
			result.on_cleanup();
	}
};
} // namespace action_revert

TEST_GROUP(ActionRevert) {
	Action action;
	Song song;
	ModelStack stack;
	action_revert::Result first, second, third;
	void setup() override {
		stack.song = &song;
		currentSong = &song;
		recording::fail_allocation = false;
	}
	void add(action_revert::Result & result, uint8_t type = 0) {
		auto* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(action_revert::ConsequenceProbe));
		auto* c = new (memory) action_revert::ConsequenceProbe(result);
		c->type = type;
		action.addConsequence(c);
	}
	void teardown() override {
		currentSong = nullptr;
		while (action.firstConsequence) {
			auto* c = action.firstConsequence;
			action.firstConsequence = c->next;
			c->~Consequence();
			delugeDealloc(c);
		}
	}
};

TEST(ActionRevert, missing_context_leaves_history_untouched) {
	add(first);
	auto* head = action.firstConsequence;
	CHECK_TRUE(action.revert(BEFORE, nullptr) == Error::BUG);
	stack.song = nullptr;
	CHECK_TRUE(action.revert(BEFORE, &stack) == Error::BUG);
	POINTERS_EQUAL(head, action.firstConsequence);
	LONGS_EQUAL(0, first.calls);
}

TEST(ActionRevert, ordinary_failure_stops_dispatch_and_retains_all_consequences_for_cleanup) {
	add(third);
	add(second);
	add(first);
	auto* original_head = action.firstConsequence;
	second.error = Error::INSUFFICIENT_RAM;
	CHECK_TRUE(action.revert(BEFORE, &stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, first.calls);
	LONGS_EQUAL(1, second.calls);
	LONGS_EQUAL(0, third.calls);
	POINTERS_EQUAL(original_head, action.firstConsequence->next->next);
	POINTERS_EQUAL(nullptr, original_head->next);
	LONGS_EQUAL(0, first.cleanups);
}

TEST(ActionRevert, successful_ordinary_reversion_round_trips_list_and_direction) {
	add(second);
	add(first);
	auto* head = action.firstConsequence;
	CHECK_TRUE(action.revert(BEFORE, &stack) == Error::NONE);
	POINTERS_EQUAL(head, action.firstConsequence->next);
	CHECK_TRUE(first.time == BEFORE);
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::NONE);
	POINTERS_EQUAL(head, action.firstConsequence);
	CHECK_TRUE(first.time == AFTER);
	LONGS_EQUAL(2, second.calls);
}

TEST(ActionRevert, failed_arrangement_clear_preserves_new_and_old_chains_without_reverting_old) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(first);
	auto* old = action.firstConsequence;
	song.on_clear_arrangement = [&](Action*) {
		add(second);
		return Error::INSUFFICIENT_RAM;
	};
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(old, action.firstConsequence->next);
	LONGS_EQUAL(0, first.calls);
	LONGS_EQUAL(0, second.calls);
	LONGS_EQUAL(0, first.cleanups);
}

TEST(ActionRevert, arrangement_failure_cleans_every_old_consequence_and_preserves_new_history) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(second);
	add(first);
	first.error = Error::BUG;
	song.on_clear_arrangement = [&](Action*) {
		add(third);
		return Error::NONE;
	};
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(1, first.calls);
	CHECK_TRUE(first.time == BEFORE);
	LONGS_EQUAL(0, second.calls);
	LONGS_EQUAL(1, first.cleanups);
	LONGS_EQUAL(1, second.cleanups);
	LONGS_EQUAL(0, third.cleanups);
	CHECK_TRUE(action.firstConsequence != nullptr);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next);
}

TEST(ActionRevert, arrangement_skips_parameter_replay_but_still_cleans_it) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(first, Consequence::PARAM_CHANGE);
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::NONE);
	LONGS_EQUAL(0, first.calls);
	LONGS_EQUAL(1, first.cleanups);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(ClipExistenceRecording, restoration_requires_actual_detached_ownership) {
	ConsequenceClipExistence consequence(&clip, &song.sessionClips, ExistenceChangeType::DELETE);
	CHECK_FALSE(consequence.can_recreate(&song));
	consequence.owns_detached_clip = true;
	CHECK_TRUE(consequence.can_recreate(&song));
	consequence.owns_detached_clip = false;
	CHECK_FALSE(consequence.can_recreate(&song));
}

TEST(ClipExistenceRecording, restoration_rejects_clip_already_in_either_song_array) {
	ConsequenceClipExistence consequence(&clip, &song.sessionClips, ExistenceChangeType::DELETE);
	consequence.owns_detached_clip = true;
	song.sessionClips.values = {&clip};
	CHECK_FALSE(consequence.can_recreate(&song));
	CHECK_FALSE(consequence.owns_detached_clip);
	consequence.owns_detached_clip = true;
	song.sessionClips.values.clear();
	song.arrangementOnlyClips.values = {&clip};
	CHECK_FALSE(consequence.can_recreate(&song));
	CHECK_FALSE(consequence.owns_detached_clip);
}

TEST(ClipExistenceRecording, restoration_rejects_invalid_index_but_allows_appending) {
	ConsequenceClipExistence consequence(&clip, &song.sessionClips, ExistenceChangeType::DELETE);
	consequence.owns_detached_clip = true;
	Clip other;
	song.sessionClips.values = {&other};
	for (auto index : {-1, 2, INT32_MAX}) {
		consequence.clipIndex = index;
		CHECK_FALSE(consequence.can_recreate(&song));
	}
	for (auto index : {0, 1}) {
		consequence.clipIndex = index;
		CHECK_TRUE(consequence.can_recreate(&song));
	}
}

TEST(ClipExistenceRecording, restoration_rejects_foreign_or_freed_array_without_dereferencing_it) {
	auto* array = new ClipArray;
	ConsequenceClipExistence consequence(&clip, array, ExistenceChangeType::DELETE);
	consequence.owns_detached_clip = true;
	delete array;
	CHECK_FALSE(consequence.can_recreate(&song));
	CHECK_FALSE(consequence.can_recreate(nullptr));
	consequence.clipArray = &song.sessionClips;
	consequence.clip = nullptr;
	CHECK_FALSE(consequence.can_recreate(&song));
}

class ReattachmentClip : public Clip {
public:
	Error undoDetachmentFromOutput(ModelStackWithTimelineCounter*) override {
		return on_reattach ? on_reattach() : Error::NONE;
	}
};
TEST_GROUP(ClipRestorationReservation) {
	Song song;
	ReattachmentClip clip, other;
	ConsequenceClipExistence consequence{&clip, &song.sessionClips, ExistenceChangeType::DELETE};
	void setup() override {
		currentSong = &song;
		consequence.owns_detached_clip = true;
	}
	void teardown() override {
		currentSong = nullptr;
	}
};

TEST(ClipRestorationReservation, success_and_allocation_failure_keep_detached_ownership) {
	CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::NONE);
	song.sessionClips.reserve_succeeds = false;
	CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(consequence.owns_detached_clip);
}

TEST(ClipRestorationReservation, invalid_initial_target_does_not_reserve) {
	consequence.owns_detached_clip = false;
	CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::BUG);
	LONGS_EQUAL(0, song.sessionClips.reserve_calls);
}

TEST(ClipRestorationReservation, song_change_during_reservation_aborts) {
	song.sessionClips.on_reserve = [] { currentSong = nullptr; };
	CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::BUG);
}

TEST(ClipRestorationReservation, ownership_reconciled_even_when_allocation_fails) {
	song.sessionClips.reserve_succeeds = false;
	song.sessionClips.on_reserve = [&] { song.arrangementOnlyClips.values = {&clip}; };
	CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
}

TEST(ClipRestorationReservation, structural_change_on_either_panel_invalidates_reservation) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		song.sessionClips.on_reserve = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::BUG);
	}
}

TEST(ClipRestorationReservation, valid_but_changed_insertion_index_is_rejected) {
	song.sessionClips.values = {&other};
	song.sessionClips.on_reserve = [&] { consequence.clipIndex = 1; };
	CHECK_TRUE(consequence.reserve_for_recreation(&song) == Error::BUG);
}

TEST(ClipRestorationReservation, successful_commit_transfers_ownership_only_after_pointer_insertion) {
	song.sessionClips.values = {&other};
	consequence.clipIndex = 1;
	CHECK_TRUE(consequence.commit_recreation(&song) == Error::NONE);
	LONGS_EQUAL(2, song.sessionClips.getNumElements());
	POINTERS_EQUAL(&clip, song.sessionClips.getClipAtIndex(1));
	POINTERS_EQUAL(&other, song.sessionClips.getClipAtIndex(0));
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.reserve_calls);
	LONGS_EQUAL(1, song.sessionClips.pointer_writes);
}

TEST(ClipRestorationReservation, commit_failure_retains_detached_ownership_and_array_contents) {
	song.sessionClips.values = {&other};
	for (auto error : {Error::INSUFFICIENT_RAM, Error::BUG}) {
		song.sessionClips.commit_error = error;
		CHECK_TRUE(consequence.commit_recreation(&song) == error);
		CHECK_TRUE(consequence.owns_detached_clip);
		LONGS_EQUAL(1, song.sessionClips.getNumElements());
		POINTERS_EQUAL(&other, song.sessionClips.getClipAtIndex(0));
		LONGS_EQUAL(0, song.sessionClips.pointer_writes);
	}
	LONGS_EQUAL(0, song.sessionClips.reserve_calls);
}

TEST(ClipRestorationReservation, commit_rechecks_ownership_and_rejects_duplicate_insertion) {
	song.arrangementOnlyClips.values = {&clip};
	CHECK_TRUE(consequence.commit_recreation(&song) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);
}

TEST(ClipRestorationReservation, changed_song_or_index_rejects_commit_before_mutation) {
	currentSong = nullptr;
	CHECK_TRUE(consequence.commit_recreation(&song) == Error::BUG);
	currentSong = &song;
	consequence.clipIndex = 1;
	CHECK_TRUE(consequence.commit_recreation(&song) == Error::BUG);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);
	CHECK_TRUE(consequence.owns_detached_clip);
}

TEST(ClipRestorationReservation, reattachment_preserves_ordinary_errors) {
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	for (auto error : {Error::NONE, Error::INSUFFICIENT_RAM}) {
		clip.on_reattach = [error] { return error; };
		CHECK_TRUE(consequence.reattach_for_recreation(&stack) == error);
		CHECK_TRUE(consequence.owns_detached_clip);
	}
}

TEST(ClipRestorationReservation, failed_reattachment_reconciles_returned_ownership) {
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [&] {
		song.sessionClips.values.push_back(&clip);
		return Error::INSUFFICIENT_RAM;
	};
	CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
}

TEST(ClipRestorationReservation, reattachment_rejects_either_panels_structural_change) {
	using namespace deluge::gui::ui_session;
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	for (auto owner : {Id::Local, Id::Remote}) {
		clip.on_reattach = [owner] {
			navigation.for_owner(owner).structural_refresh.request();
			return Error::NONE;
		};
		CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	}
}

TEST(ClipRestorationReservation, reattachment_rejects_changed_song_and_index) {
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [&] {
		++consequence.clipIndex;
		return Error::NONE;
	};
	CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	consequence.clipIndex = 0;
	clip.on_reattach = [] {
		currentSong = nullptr;
		return Error::NONE;
	};
	CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_TRUE(consequence.reattach_for_recreation(nullptr) == Error::BUG);
}

TEST(ClipRestorationReservation, reattachment_rejects_missing_or_foreign_timeline_before_mutation) {
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	int calls = 0;
	clip.on_reattach = [&] {
		++calls;
		return Error::NONE;
	};
	for (auto* target : {static_cast<Clip*>(nullptr), static_cast<Clip*>(&other)}) {
		stack.clip = target;
		CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	}
	LONGS_EQUAL(0, calls);
}

TEST(ClipRestorationReservation, reattachment_rejects_stack_song_changed_by_callback) {
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [&] {
		stack.song = nullptr;
		return Error::NONE;
	};
	CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_TRUE(currentSong == &song);
}

TEST(ClipRestorationReservation, reattachment_rejects_stack_timeline_changed_by_callback) {
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [&] {
		stack.clip = &other;
		return Error::NONE;
	};
	CHECK_TRUE(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_TRUE(consequence.clip == &clip);
}

TEST(ActionRevert, song_change_stops_remaining_consequences_and_retains_processed_history) {
	add(third);
	add(second);
	add(first);
	second.on_revert = [] { currentSong = nullptr; };
	CHECK_TRUE(action.revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(1, first.calls);
	LONGS_EQUAL(1, second.calls);
	LONGS_EQUAL(0, third.calls);
	int retained = 0;
	for (auto* c = action.firstConsequence; c; c = c->next)
		++retained;
	LONGS_EQUAL(3, retained);
}

TEST(ActionRevert, arrangement_clear_context_change_retains_both_chains) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(first);
	song.on_clear_arrangement = [&](Action*) {
		add(second);
		stack.song = nullptr;
		return Error::NONE;
	};
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(0, first.calls);
	CHECK_TRUE(action.firstConsequence->next != nullptr);
	LONGS_EQUAL(0, first.cleanups);
}

TEST(ActionRevert, arrangement_replay_context_change_defers_old_consequence_cleanup) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(second);
	add(first);
	first.on_revert = [] { currentSong = nullptr; };
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(0, second.calls);
	LONGS_EQUAL(0, first.cleanups);
	LONGS_EQUAL(0, second.cleanups);
	CHECK_TRUE(action.firstConsequence->next != nullptr);
}

TEST(ActionRevert, arrangement_cleanup_song_change_stops_next_dispatch) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(second);
	add(first);
	first.on_cleanup = [] { currentSong = nullptr; };
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(1, first.calls);
	LONGS_EQUAL(1, first.cleanups);
	LONGS_EQUAL(0, second.calls);
	LONGS_EQUAL(0, second.cleanups);
	CHECK_TRUE(action.firstConsequence != nullptr);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next);
}

TEST(ActionRevert, arrangement_cleanup_stack_change_preserves_generated_history) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(second);
	add(first);
	song.on_clear_arrangement = [&](Action*) {
		add(third);
		return Error::NONE;
	};
	first.on_cleanup = [&] { stack.song = nullptr; };
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(0, second.calls);
	LONGS_EQUAL(0, third.calls);
	CHECK_TRUE(action.firstConsequence != nullptr);
	CHECK_TRUE(action.firstConsequence->next != nullptr);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next->next);
}

TEST(ActionRevert, final_arrangement_cleanup_context_change_still_reports_failure) {
	action.type = ActionType::ARRANGEMENT_RECORD;
	add(first);
	first.on_cleanup = [] { currentSong = nullptr; };
	CHECK_TRUE(action.revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(1, first.calls);
	LONGS_EQUAL(1, first.cleanups);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(ClipRestorationReservation, cleanup_preserves_clip_returned_to_either_song_array) {
	for (auto* array : {&song.sessionClips, &song.arrangementOnlyClips}) {
		array->values.push_back(&clip);
		consequence.owns_detached_clip = true;
		const int freed_before = recording::freed;
		consequence.prepareForDestruction(BEFORE, &song);
		CHECK_FALSE(consequence.owns_detached_clip);
		LONGS_EQUAL(freed_before, recording::freed);
		CHECK_TRUE(song.contains_clip_for_undo(&clip));
		array->values.clear();
	}
}

TEST(ClipRestorationReservation, cleanup_destroys_detached_clip_and_its_backups_once) {
	auto* detached = new Clip;
	ModControllableAudio output;
	song.backedUpParamManagers.values = {{&output, detached, {11, 12}}, {&output, &other, {21, 22}}};
	consequence.clip = detached;
	const int freed_before = recording::freed;
	consequence.prepareForDestruction(BEFORE, &song);
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(freed_before + 1, recording::freed);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	POINTERS_EQUAL(&other, song.backedUpParamManagers.values[0].clip);
	consequence.prepareForDestruction(BEFORE, &song);
	LONGS_EQUAL(freed_before + 1, recording::freed);
}

TEST_GROUP(NoteSnapshotRecording) {
	Song song;
	InstrumentClip clip;
	NoteRow row;
	Action action;
	void setup() override {
		currentSong = &song;
		clip.row = &row;
		row.sequenced = true;
		recording::fail_allocation = false;
		recording::on_allocate = {};
		NoteVector::on_clone = {};
		NoteVector::fail_clone = false;
		NoteVector::fail_any_insert = false;
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
		recording::fail_working_allocation = false;
	}
	void teardown() override {
		recording::on_allocate = {};
		NoteVector::on_clone = {};
		NoteVector::fail_clone = false;
		NoteVector::fail_any_insert = false;
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
		recording::fail_working_allocation = false;
		currentSong = nullptr;
		while (action.firstConsequence) {
			auto* node = action.firstConsequence;
			action.firstConsequence = node->next;
			node->~Consequence();
			delugeDealloc(node);
		}
	}
};

TEST(NoteSnapshotRecording, changed_song_during_allocation_does_not_steal_notes) {
	row.notes.values = {1, 2};
	recording::on_allocate = [] { currentSong = nullptr; };
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, true) == Error::BUG);
	LONGS_EQUAL(2, row.notes.values.size());
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, either_panel_invalidation_releases_unused_allocation) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		const int before = recording::freed;
		recording::on_allocate = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, true) == Error::BUG);
		LONGS_EQUAL(before + 1, recording::freed);
	}
}

TEST(NoteSnapshotRecording, replaced_row_rejects_snapshot_but_stable_row_succeeds) {
	recording::on_allocate = [&] { row.undo_identity = deluge::model::next_note_row_identity(); };
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
	recording::on_allocate = {};
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	CHECK_TRUE(action.firstConsequence != nullptr);
}

TEST(NoteSnapshotRecording, clone_invalidation_does_not_publish_snapshot) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		const int before = recording::freed;
		NoteVector::on_clone = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(before + 1, recording::freed);
	}
}

TEST(NoteSnapshotRecording, clone_row_replacement_rejects_snapshot) {
	NoteVector::on_clone = [&] { row.undo_identity = deluge::model::next_note_row_identity(); };
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, clone_callback_can_delete_target_without_later_access) {
	using namespace deluge::gui::ui_session;
	auto* target = new InstrumentClip;
	target->row = &row;
	NoteVector::on_clone = [target] {
		navigation.for_owner(Id::Remote).structural_refresh.request();
		delete target;
	};
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(target, 7, &row.notes, false) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, clone_allocation_failure_keeps_existing_history) {
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	auto* original = action.firstConsequence;
	NoteVector::fail_clone = true;
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(original, action.firstConsequence);
	POINTERS_EQUAL(nullptr, original->next);
}

TEST(NoteSnapshotRecording, single_note_recording_does_not_read_source_after_allocation) {
	auto* note = new Note(note_for_test());
	note->pos = 17;
	note->setLength(9);
	note->setVelocity(73);
	recording::on_allocate = [note] { delete note; };
	action.recordNoteExistenceChange(&clip, 7, note, ExistenceChangeType::DELETE);
	CHECK_TRUE(action.firstConsequence != nullptr);
	auto* saved = static_cast<ConsequenceNoteExistence*>(action.firstConsequence);
	LONGS_EQUAL(17, saved->pos);
	LONGS_EQUAL(9, saved->length);
	LONGS_EQUAL(73, saved->velocity);
}

TEST(NoteSnapshotRecording, single_note_recording_rejects_allocation_time_invalidation) {
	using namespace deluge::gui::ui_session;
	Note note = note_for_test();
	for (auto owner : {Id::Local, Id::Remote}) {
		int before = recording::freed;
		recording::on_allocate = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		action.recordNoteExistenceChange(&clip, 7, &note, ExistenceChangeType::DELETE);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(before + 1, recording::freed);
	}
}

TEST(NoteSnapshotRecording, single_note_recording_rejects_replaced_row) {
	Note note = note_for_test();
	recording::on_allocate = [&] { row.undo_identity = deluge::model::next_note_row_identity(); };
	action.recordNoteExistenceChange(&clip, 7, &note, ExistenceChangeType::DELETE);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, note_deletion_leaves_model_untouched_when_history_allocation_fails) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries[0].pos = 17;
	recording::fail_allocation = true;
	CHECK_TRUE(row.deleteNoteByIndex(0, &action, 7, &clip) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(0, row.notes.delete_calls);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, note_deletion_stops_after_target_invalidation) {
	row.notes.entries.push_back(note_for_test());
	recording::on_allocate = [&] { row.undo_identity = deluge::model::next_note_row_identity(); };
	CHECK_TRUE(row.deleteNoteByIndex(0, &action, 7, &clip) == Error::BUG);
	LONGS_EQUAL(0, row.notes.delete_calls);
	LONGS_EQUAL(1, row.notes.entries.size());
}

TEST(NoteSnapshotRecording, note_deletion_records_before_removing_note) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries[0].pos = 17;
	CHECK_TRUE(row.deleteNoteByIndex(0, &action, 7, &clip) == Error::NONE);
	LONGS_EQUAL(1, row.notes.delete_calls);
	LONGS_EQUAL(0, row.notes.entries.size());
	CHECK_TRUE(action.firstConsequence != nullptr);
	LONGS_EQUAL(17, static_cast<ConsequenceNoteExistence*>(action.firstConsequence)->pos);
}

TEST(NoteSnapshotRecording, failed_single_note_allocation_still_checks_both_panels) {
	using namespace deluge::gui::ui_session;
	Note note = note_for_test();
	recording::fail_allocation = true;
	int before = recording::freed;
	for (auto owner : {Id::Local, Id::Remote}) {
		recording::on_allocate = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, &note, ExistenceChangeType::CREATE) == Error::BUG);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
	}
	LONGS_EQUAL(before, recording::freed);
}

TEST(NoteSnapshotRecording, failed_single_note_allocation_rejects_row_replacement) {
	Note note = note_for_test();
	recording::fail_allocation = true;
	recording::on_allocate = [&] { row.undo_identity = deluge::model::next_note_row_identity(); };
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, &note, ExistenceChangeType::CREATE) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, failed_single_note_allocation_does_not_access_deleted_clip) {
	Note note = note_for_test();
	auto* target = new InstrumentClip;
	target->row = &row;
	recording::fail_allocation = true;
	recording::on_allocate = [target] {
		currentSong = nullptr;
		delete target;
	};
	CHECK_TRUE(action.recordNoteExistenceChange(target, 7, &note, ExistenceChangeType::CREATE) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, failed_creation_recording_removes_inserted_note_only) {
	for (int pos : {3, 17, 29}) {
		row.notes.entries.push_back(note_for_test());
		row.notes.entries.back().pos = pos;
	}
	recording::fail_allocation = true;
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, row.notes.getElement(1), ExistenceChangeType::CREATE)
	           == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(3, row.notes.entries[0].pos);
	LONGS_EQUAL(29, row.notes.entries[1].pos);
	LONGS_EQUAL(1, row.notes.delete_calls);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, failed_creation_recording_finds_note_after_vector_relocation) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries.back().pos = 17;
	recording::fail_allocation = true;
	recording::on_allocate = [&] {
		row.notes.entries.reserve(128);
		row.notes.insertAtKey(3);
	};
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, row.notes.getElement(0), ExistenceChangeType::CREATE)
	           == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(3, row.notes.entries[0].pos);
}

TEST(NoteSnapshotRecording, failed_creation_recording_preserves_changed_note) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries.back().pos = 17;
	recording::fail_allocation = true;
	recording::on_allocate = [&] { row.notes.entries[0].setVelocity(42); };
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, row.notes.getElement(0), ExistenceChangeType::CREATE)
	           == Error::BUG);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(42, row.notes.entries[0].velocity);
	LONGS_EQUAL(0, row.notes.delete_calls);
}

TEST(NoteSnapshotRecording, successful_creation_recording_refreshes_relocated_note_pointer) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries.back().pos = 17;
	Note* recorded = row.notes.getElement(0);
	recording::on_allocate = [&] {
		row.notes.entries.reserve(128);
		row.notes.insertAtKey(3);
	};
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, recorded, ExistenceChangeType::CREATE, &recorded)
	           == Error::NONE);
	POINTERS_EQUAL(row.notes.getElement(1), recorded);
	LONGS_EQUAL(17, recorded->pos);
	CHECK_TRUE(action.firstConsequence != nullptr);
}

TEST(NoteSnapshotRecording, successful_allocation_rejects_changed_creation_target) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries.back().pos = 17;
	Note* recorded = row.notes.getElement(0);
	int before = recording::freed;
	recording::on_allocate = [&] { row.notes.entries[0].setVelocity(42); };
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, recorded, ExistenceChangeType::CREATE, &recorded)
	           == Error::BUG);
	POINTERS_EQUAL(nullptr, recorded);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(before + 1, recording::freed);
	LONGS_EQUAL(0, row.notes.delete_calls);
}

TEST(NoteSnapshotRecording, existing_snapshot_keeps_creation_pointer_without_allocation) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries.back().pos = 17;
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	Note* recorded = row.notes.getElement(0);
	Note* original = recorded;
	bool allocated = false;
	recording::on_allocate = [&] { allocated = true; };
	CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, recorded, ExistenceChangeType::CREATE, &recorded)
	           == Error::NONE);
	CHECK_FALSE(allocated);
	POINTERS_EQUAL(original, recorded);
}

TEST(NoteSnapshotRecording, creation_recording_rejects_released_relocated_row) {
	for (bool fail_allocation : {false, true}) {
		auto* original = new NoteRow;
		original->notes.entries.push_back(note_for_test());
		original->notes.entries[0].pos = 17;
		NoteRow relocated;
		relocated.undo_identity = original->undo_identity;
		relocated.notes.entries = original->notes.entries;
		clip.row = original;
		Note* recorded = original->notes.getElement(0);
		int before = recording::freed;
		recording::fail_allocation = fail_allocation;
		recording::on_allocate = [&] {
			clip.row = &relocated;
			delete original;
		};
		CHECK_TRUE(action.recordNoteExistenceChange(&clip, 7, recorded, ExistenceChangeType::CREATE, &recorded)
		           == Error::BUG);
		POINTERS_EQUAL(nullptr, recorded);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(1, relocated.notes.entries.size());
		LONGS_EQUAL(0, relocated.notes.delete_calls);
		LONGS_EQUAL(before + !fail_allocation, recording::freed);
		clip.row = &row;
		recording::on_allocate = {};
	}
}

TEST(NoteSnapshotRecording, deletion_stops_when_allocation_releases_original_row) {
	auto* original = new NoteRow;
	original->notes.entries.push_back(note_for_test());
	original->notes.entries[0].pos = 17;
	NoteRow relocated;
	relocated.undo_identity = original->undo_identity;
	relocated.notes.entries = original->notes.entries;
	clip.row = original;
	recording::on_allocate = [&] {
		clip.row = &relocated;
		delete original;
	};
	CHECK_TRUE(original->deleteNoteByIndex(0, &action, 7, &clip) == Error::BUG);
	LONGS_EQUAL(1, relocated.notes.entries.size());
	LONGS_EQUAL(0, relocated.notes.delete_calls);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	clip.row = &row;
	recording::on_allocate = {};
}

TEST(NoteSnapshotRecording, deletion_reacquires_index_after_allocation_inserts_earlier_note) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries[0].pos = 17;
	recording::on_allocate = [&] {
		row.notes.entries.reserve(128);
		row.notes.insertAtKey(3);
	};
	CHECK_TRUE(row.deleteNoteByIndex(0, &action, 7, &clip) == Error::NONE);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(3, row.notes.entries[0].pos);
	LONGS_EQUAL(1, row.notes.delete_calls);
	CHECK_TRUE(action.firstConsequence != nullptr);
	LONGS_EQUAL(17, static_cast<ConsequenceNoteExistence*>(action.firstConsequence)->pos);
}

TEST(NoteSnapshotRecording, deletion_preserves_note_changed_during_history_allocation) {
	row.notes.entries.push_back(note_for_test());
	row.notes.entries[0].pos = 17;
	recording::on_allocate = [&] { row.notes.entries[0].setVelocity(42); };
	CHECK_TRUE(row.deleteNoteByIndex(0, &action, 7, &clip) == Error::BUG);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(42, row.notes.entries[0].velocity);
	LONGS_EQUAL(0, row.notes.delete_calls);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, array_snapshot_rejects_released_relocated_row_before_reading_vector) {
	for (bool fail_allocation : {false, true}) {
		auto* original = new NoteRow;
		original->notes.values = {1, 2};
		NoteRow relocated;
		relocated.undo_identity = original->undo_identity;
		relocated.notes.values = original->notes.values;
		clip.row = original;
		recording::fail_allocation = fail_allocation;
		recording::on_allocate = [&] {
			clip.row = &relocated;
			delete original;
		};
		CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &original->notes, true) == Error::BUG);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(2, relocated.notes.values.size());
		clip.row = &row;
		recording::on_allocate = {};
	}
}

TEST(NoteSnapshotRecording, array_snapshot_rejects_row_relocation_after_cloning) {
	NoteRow relocated;
	relocated.undo_identity = row.undo_identity;
	int before = recording::freed;
	NoteVector::on_clone = [&] { clip.row = &relocated; };
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(before + 1, recording::freed);
	clip.row = &row;
}

TEST(NoteSnapshotRecording, failed_array_snapshot_allocation_checks_context_before_memory_error) {
	using namespace deluge::gui::ui_session;
	recording::fail_allocation = true;
	for (auto owner : {Id::Local, Id::Remote}) {
		recording::on_allocate = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
	}
	recording::on_allocate = {};
	CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::INSUFFICIENT_RAM);
}

TEST(NoteSnapshotRecording, bulk_edits_stop_on_history_failure_and_release_working_memory) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	clip.loopLength = 64;
	std::function<Error()> operations[] = {
	    [&] { return row.addCorrespondingNotes(8, 2, 90, &stack, true, &action); },
	    [&] { return row.clearArea(0, 4, &stack, &action, 16, false); },
	    [&] { return row.editNoteRepeatAcrossAllScreens(0, 8, &stack, &action, 16, 2); },
	    [&] { return row.nudgeNotesAcrossAllScreens(0, &stack, &action, 16, 1); },
	    [&] { return row.changeNotesAcrossAllScreens(0, &stack, &action, CORRESPONDING_NOTES_SET_VELOCITY, 42); },
	    [&] { return row.trimNoteDataToNewClipLength(24, &clip, &action, 7); },
	    [&] { return row.trimNoteDataToNewClipLength(0, &clip, &action, 7); },
	    [&] { return row.complexSetNoteLength(row.notes.getElement(0), 2, &stack, &action); },
	};
	for (bool invalidate : {false, true}) {
		for (auto& operation : operations) {
			row.notes.entries.clear();
			for (int pos : {0, 16, 32, 48}) {
				Note note = note_for_test();
				note.pos = pos;
				note.length = 4;
				note.velocity = 90;
				row.notes.entries.push_back(note);
			}
			int outstanding_before = recording::allocated - recording::freed;
			recording::fail_allocation = !invalidate;
			recording::on_allocate = [&] {
				if (invalidate)
					deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
					    .structural_refresh.request();
			};
			CHECK_TRUE(operation() == (invalidate ? Error::BUG : Error::INSUFFICIENT_RAM));
			LONGS_EQUAL(4, row.notes.entries.size());
			for (int i = 0; i < 4; ++i) {
				LONGS_EQUAL(i * 16, row.notes.entries[i].pos);
				LONGS_EQUAL(4, row.notes.entries[i].length);
				LONGS_EQUAL(90, row.notes.entries[i].velocity);
			}
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			LONGS_EQUAL(0, clip.expected_events);
			LONGS_EQUAL(outstanding_before, recording::allocated - recording::freed);
		}
	}
}

TEST(NoteSnapshotRecording, bulk_edits_succeed_and_keep_undo_snapshot) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	clip.loopLength = 64;
	std::function<Error()> operations[] = {
	    [&] { return row.addCorrespondingNotes(8, 2, 90, &stack, true, &action); },
	    [&] { return row.clearArea(0, 4, &stack, &action, 16, false); },
	    [&] { return row.editNoteRepeatAcrossAllScreens(0, 8, &stack, &action, 16, 2); },
	    [&] { return row.nudgeNotesAcrossAllScreens(0, &stack, &action, 16, 1); },
	    [&] { return row.changeNotesAcrossAllScreens(0, &stack, &action, CORRESPONDING_NOTES_SET_VELOCITY, 42); },
	    [&] { return row.trimNoteDataToNewClipLength(24, &clip, &action, 7); },
	    [&] { return row.trimNoteDataToNewClipLength(0, &clip, &action, 7); },
	    [&] { return row.complexSetNoteLength(row.notes.getElement(0), 2, &stack, &action); },
	};
	const int expected_counts[] = {8, 0, 8, 4, 4, 2, 0, 4};
	for (int op = 0; op < 8; ++op) {
		row.notes.entries.clear();
		for (int pos : {0, 16, 32, 48}) {
			Note note = note_for_test();
			note.pos = pos;
			note.length = 4;
			note.velocity = 90;
			row.notes.entries.push_back(note);
		}
		CHECK_TRUE(operations[op]() == Error::NONE);
		LONGS_EQUAL(expected_counts[op], row.notes.entries.size());
		CHECK_TRUE(action.firstConsequence != nullptr);
		auto* snapshot = static_cast<ConsequenceNoteArrayChange*>(action.firstConsequence);
		LONGS_EQUAL(4, snapshot->backedUpNoteVector.entries.size());
		LONGS_EQUAL(90, snapshot->backedUpNoteVector.entries[0].velocity);
		if (op == 3)
			LONGS_EQUAL(1, row.notes.entries[0].pos);
		if (op == 4)
			LONGS_EQUAL(42, row.notes.entries[0].velocity);
		if (op == 7)
			LONGS_EQUAL(2, row.notes.entries[0].length);
		action.firstConsequence = nullptr;
		snapshot->~ConsequenceNoteArrayChange();
		delugeDealloc(snapshot);
	}
}

TEST(NoteSnapshotRecording, trimming_does_not_fall_back_to_unrecorded_mutation) {
	row.notes.entries.assign(2, note_for_test());
	row.notes.entries[0].pos = 0;
	row.notes.entries[0].length = 32;
	row.notes.entries[1].pos = 48;
	NoteVector::fail_any_insert = true;
	CHECK_TRUE(row.trimNoteDataToNewClipLength(24, &clip, &action, 7) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(32, row.notes.entries[0].length);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, clear_secures_history_before_automation_or_playback_mutation) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	ParamCollection automation;
	ExpressionParamSet mpe;
	row.paramManager.summaries[0].paramCollection = &automation;
	row.paramManager.summaries[1].paramCollection = &mpe;
	row.notes.entries.push_back(note_for_test());
	recording::fail_allocation = true;
	row.clear(&action, &stack, true, true);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(0, automation.clears);
	LONGS_EQUAL(0, mpe.clears);
	LONGS_EQUAL(0, row.note_stops);
	recording::fail_allocation = false;
	row.clear(&action, &stack, true, true);
	LONGS_EQUAL(0, row.notes.entries.size());
	LONGS_EQUAL(1, automation.clears);
	LONGS_EQUAL(1, mpe.clears);
	LONGS_EQUAL(1, row.note_stops);
	CHECK_TRUE(action.firstConsequence != nullptr);
}

TEST(NoteSnapshotRecording, corresponding_edits_skip_missing_and_empty_rows) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int before = recording::allocated;
	CHECK_TRUE(row.changeNotesAcrossAllScreens(0, &stack, &action, CORRESPONDING_NOTES_SET_VELOCITY, 42)
	           == Error::NONE);
	LONGS_EQUAL(before, recording::allocated);
	row.notes.entries.push_back(note_for_test());
	row.notes.entries[0].pos = 2;
	CHECK_TRUE(row.changeNotesAcrossAllScreens(8, &stack, &action, CORRESPONDING_NOTES_SET_VELOCITY, 42)
	           == Error::NONE);
	LONGS_EQUAL(2, row.notes.entries[0].pos);
}

TEST(NoteSnapshotRecording, snapshot_rejects_source_vector_changes_before_and_during_cloning) {
	for (bool during_clone : {false, true}) {
		for (bool grow : {false, true}) {
			row.notes.entries.clear();
			row.notes.entries.push_back(note_for_test());
			row.notes.entries[0].pos = 17;
			auto mutate = [&] {
				if (grow)
					row.notes.entries.push_back(note_for_test());
				else
					row.notes.entries.reserve(row.notes.entries.capacity() + 128);
			};
			if (during_clone)
				NoteVector::on_clone = mutate;
			else
				recording::on_allocate = mutate;
			CHECK_TRUE(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			NoteVector::on_clone = {};
			recording::on_allocate = {};
		}
	}
}

TEST(NoteSnapshotRecording, trim_failure_does_not_trim_parameters_or_schedule_playback) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(0, 32), note_for_test(48, 4)};
	bool trimmed_parameters = false;
	row.paramManager.on_trim = [&] { trimmed_parameters = true; };
	recording::fail_allocation = true;
	CHECK_TRUE(row.trimToLength(24, &stack, &action) == Error::INSUFFICIENT_RAM);
	CHECK_FALSE(trimmed_parameters);
	LONGS_EQUAL(0, clip.expected_events);
	LONGS_EQUAL(2, row.notes.entries.size());
	recording::fail_allocation = false;
	CHECK_TRUE(row.trimToLength(24, &stack, &action) == Error::NONE);
	CHECK_TRUE(trimmed_parameters);
	LONGS_EQUAL(1, clip.expected_events);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(24, row.notes.entries[0].length);
}

TEST(NoteSnapshotRecording, note_off_stops_before_length_and_lift_changes_on_history_failure) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(4, 8)};
	recording::fail_allocation = true;
	row.recordNoteOff(8, &stack, &action, 99);
	LONGS_EQUAL(8, row.notes.entries[0].length);
	LONGS_EQUAL(64, row.notes.entries[0].lift);
	LONGS_EQUAL(0, clip.expected_events);
	recording::fail_allocation = false;
	row.recordNoteOff(8, &stack, &action, 99);
	LONGS_EQUAL(4, row.notes.entries[0].length);
	LONGS_EQUAL(99, row.notes.entries[0].lift);
	LONGS_EQUAL(1, clip.expected_events);
}

TEST(NoteSnapshotRecording, wrapped_length_edit_propagates_clear_area_failure) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	clip.wrapEditing = true;
	row.notes.entries = {note_for_test(0, 8)};
	recording::fail_allocation = true;
	CHECK_TRUE(row.complexSetNoteLength(row.notes.getElement(0), 4, &stack, &action) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(8, row.notes.entries[0].length);
	LONGS_EQUAL(0, clip.expected_events);
}

static Error bulk_edit_at_allocation(int operation, NoteRow* row, ModelStackWithNoteRow* stack, Action* action) {
	switch (operation) {
	case 0:
		return row->addCorrespondingNotes(8, 2, 90, stack, true, action);
	case 1:
		return row->clearArea(0, 4, stack, action, 16, false);
	case 2:
		return row->editNoteRepeatAcrossAllScreens(0, 8, stack, action, 16, 2);
	case 3:
		return row->nudgeNotesAcrossAllScreens(0, stack, action, 16, 1);
	case 4:
		return row->changeNotesAcrossAllScreens(0, stack, action, CORRESPONDING_NOTES_SET_VELOCITY, 42);
	default:
		return row->trimNoteDataToNewClipLength(24, static_cast<InstrumentClip*>(stack->clip), action, 7);
	}
}

TEST(NoteSnapshotRecording, working_allocations_stop_before_reading_released_row) {
	clip.loopLength = 64;
	for (bool temporary_vector : {false, true}) {
		for (bool fail_allocation : {false, true}) {
			for (int operation = 0; operation < 6; ++operation) {
				if ((!temporary_vector && operation == 5) || (temporary_vector && operation == 4))
					continue;
				auto* original = new NoteRow;
				original->notes.entries = {note_for_test(0, 4), note_for_test(16, 4), note_for_test(32, 4),
				                           note_for_test(48, 4)};
				NoteRow relocated;
				relocated.undo_identity = original->undo_identity;
				relocated.notes.entries = original->notes.entries;
				clip.row = original;
				ModelStackWithNoteRow stack{&song, &clip, original};
				auto release = [&] {
					clip.row = &relocated;
					delete original;
				};
				if (temporary_vector) {
					NoteVector::on_insert = release;
					NoteVector::fail_any_insert = fail_allocation;
				}
				else {
					recording::on_working_allocate = release;
					recording::fail_working_allocation = fail_allocation;
				}
				int outstanding = recording::allocated - recording::freed;
				CHECK_TRUE(bulk_edit_at_allocation(operation, original, &stack, &action) == Error::BUG);
				LONGS_EQUAL(4, relocated.notes.entries.size());
				LONGS_EQUAL(0, relocated.notes.delete_calls);
				LONGS_EQUAL(outstanding, recording::allocated - recording::freed);
				POINTERS_EQUAL(nullptr, action.firstConsequence);
				LONGS_EQUAL(0, clip.expected_events);
				clip.row = &row;
				NoteVector::on_insert = {};
				NoteVector::fail_any_insert = false;
				recording::on_working_allocate = {};
				recording::fail_working_allocation = false;
			}
		}
	}
}

TEST(NoteSnapshotRecording, working_allocation_rejects_loop_and_source_changes) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	for (int change = 0; change < 4; ++change) {
		clip.loopLength = 64;
		row.loopLengthIfIndependent = 0;
		row.notes.entries = {note_for_test(0, 4), note_for_test(16, 4)};
		recording::on_working_allocate = [&] {
			switch (change) {
			case 0:
				clip.loopLength = 32;
				break;
			case 1:
				row.loopLengthIfIndependent = 32;
				break;
			case 2:
				row.notes.entries.reserve(row.notes.entries.capacity() + 128);
				break;
			case 3:
				row.notes.entries.push_back(note_for_test(32, 4));
				break;
			}
		};
		CHECK_TRUE(row.clearArea(0, 4, &stack, &action, 16, false) == Error::BUG);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(0, row.notes.delete_calls);
	}
}

TEST(NoteSnapshotRecording, working_allocation_detects_song_replacement_before_accessing_clip) {
	auto* target = new InstrumentClip;
	target->row = &row;
	row.notes.entries = {note_for_test(0, 4)};
	ModelStackWithNoteRow stack{&song, target, &row};
	recording::on_working_allocate = [target] {
		currentSong = nullptr;
		delete target;
	};
	CHECK_TRUE(row.clearArea(0, 4, &stack, &action, 16, false) == Error::BUG);
	LONGS_EQUAL(1, row.notes.entries.size());
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, clearing_stops_when_parameter_callback_releases_row) {
	auto* target = new NoteRow;
	target->notes.entries = {note_for_test(0, 4)};
	clip.row = target;
	ModelStackWithNoteRow stack{&song, &clip, target};
	ParamCollection automation;
	ExpressionParamSet mpe;
	target->paramManager.summaries[0].paramCollection = &automation;
	target->paramManager.summaries[1].paramCollection = &mpe;
	automation.on_clear = [&] {
		clip.row = &row;
		delete target;
	};
	target->clear(nullptr, &stack, true, true);
	LONGS_EQUAL(1, automation.clears);
	LONGS_EQUAL(0, mpe.clears);
	LONGS_EQUAL(0, row.note_stops);
}

TEST(NoteSnapshotRecording, clearing_stops_when_active_parameter_collection_is_replaced) {
	row.notes.entries = {note_for_test(0, 4)};
	ModelStackWithNoteRow stack{&song, &clip, &row};
	ParamCollection automation, replacement;
	ExpressionParamSet mpe;
	row.paramManager.summaries[0].paramCollection = &automation;
	row.paramManager.summaries[1].paramCollection = &mpe;
	automation.on_clear = [&] { row.paramManager.summaries[0].paramCollection = &replacement; };
	row.clear(nullptr, &stack, true, true);
	LONGS_EQUAL(0, mpe.clears);
	LONGS_EQUAL(0, replacement.clears);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(0, row.note_stops);
}

TEST(NoteSnapshotRecording, clearing_stops_after_playback_callback_releases_row) {
	auto* target = new NoteRow;
	target->sequenced = true;
	target->notes.entries = {note_for_test(0, 4)};
	clip.row = target;
	ModelStackWithNoteRow stack{&song, &clip, target};
	target->on_note_stop = [&] {
		clip.row = &row;
		delete target;
	};
	target->clear(nullptr, &stack, false, true);
	POINTERS_EQUAL(&row, clip.row);
	LONGS_EQUAL(0, row.notes.delete_calls);
}

TEST(NoteSnapshotRecording, trimming_stops_after_parameter_callback_releases_row) {
	auto* target = new NoteRow;
	target->notes.entries = {note_for_test(0, 8)};
	clip.row = target;
	ModelStackWithNoteRow stack{&song, &clip, target};
	target->paramManager.on_trim = [&] {
		clip.row = &row;
		delete target;
	};
	CHECK_TRUE(target->trimToLength(4, &stack, nullptr) == Error::BUG);
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, trimming_stops_before_using_clip_destroyed_by_parameter_callback) {
	auto* target = new InstrumentClip;
	target->row = &row;
	row.notes.entries = {note_for_test(0, 8)};
	ModelStackWithNoteRow stack{&song, target, &row};
	row.paramManager.on_trim = [target] {
		currentSong = nullptr;
		delete target;
	};
	CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::BUG);
	LONGS_EQUAL(4, row.notes.entries[0].length);
}

TEST(NoteSnapshotRecording, note_stop_publishes_state_before_reentry_and_preserves_new_playback) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.on_note_stop = [&] {
		CHECK_FALSE(row.sequenced);
		row.stopCurrentlyPlayingNote(&stack);
		row.sequenced = true; // A newly started note must not be cleared on return.
	};
	row.stopCurrentlyPlayingNote(&stack);
	LONGS_EQUAL(1, row.note_stops);
	CHECK_TRUE(row.sequenced);
}

TEST(NoteSnapshotRecording, silent_note_stop_and_already_stopped_rows_do_not_dispatch) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.stopCurrentlyPlayingNote(&stack, false);
	CHECK_FALSE(row.sequenced);
	LONGS_EQUAL(0, row.note_stops);
	row.stopCurrentlyPlayingNote(&stack);
	LONGS_EQUAL(0, row.note_stops);
}

TEST(NoteSnapshotRecording, clearing_does_not_visit_replaced_later_collection) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(0, 4)};
	ParamCollection automation;
	ExpressionParamSet original_mpe, replacement_mpe;
	row.paramManager.summaries[0].paramCollection = &automation;
	row.paramManager.summaries[1].paramCollection = &original_mpe;
	automation.on_clear = [&] { row.paramManager.summaries[1].paramCollection = &replacement_mpe; };
	row.clear(nullptr, &stack, true, true);
	LONGS_EQUAL(1, automation.clears);
	LONGS_EQUAL(0, original_mpe.clears);
	LONGS_EQUAL(0, replacement_mpe.clears);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(0, row.note_stops);
}

TEST(NoteSnapshotRecording, clearing_stops_when_expression_classification_changes) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(0, 4)};
	ParamCollection automation;
	ExpressionParamSet mpe;
	row.paramManager.summaries[0].paramCollection = &automation;
	row.paramManager.summaries[1].paramCollection = &mpe;
	automation.on_clear = [&] { row.paramManager.expressionParamSetOffset = 0; };
	row.clear(nullptr, &stack, true, true);
	LONGS_EQUAL(0, mpe.clears);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(0, row.note_stops);
}

TEST(NoteSnapshotRecording, trimming_stops_when_callback_replaces_output_or_collections) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	Output original_output, replacement_output;
	ParamCollection original_params, replacement_params;
	for (bool replaceOutput : {false, true}) {
		clip.output = &original_output;
		row.notes.entries = {note_for_test(0, 8)};
		row.paramManager.summaries[0].paramCollection = &original_params;
		row.paramManager.on_trim = [&] {
			if (replaceOutput)
				clip.output = &replacement_output;
			else
				row.paramManager.summaries[0].paramCollection = &replacement_params;
		};
		CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::BUG);
		LONGS_EQUAL(0, clip.expected_events);
		LONGS_EQUAL(4, row.notes.entries[0].length);
	}
}

TEST(NoteSnapshotRecording, repeat_generation_stops_when_parameter_callback_releases_row) {
	auto* target = new NoteRow;
	target->notes.entries = {note_for_test(2, 4)};
	clip.row = target;
	ModelStackWithNoteRow stack{&song, &clip, target};
	target->paramManager.on_repeat = [&] {
		clip.row = &row;
		delete target;
	};
	CHECK_FALSE(target->generateRepeats(&stack, 16, 32, 2, nullptr));
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_generation_does_not_flatten_direction_after_invalidation) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 4)};
	row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
	row.paramManager.on_repeat = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
	};
	CHECK_FALSE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
	CHECK_TRUE(row.sequenceDirectionMode == SequenceDirection::PINGPONG);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_generation_propagates_vector_failure) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 14)};
	row.notes.fail_repeats = true;
	CHECK_FALSE(row.generateRepeats(&stack, 16, 20, 1, nullptr));
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(14, row.notes.entries[0].length);
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_generation_preserves_successful_forward_and_pingpong_edits) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	for (bool pingpong : {false, true}) {
		row.notes.entries = {note_for_test(2, 4)};
		row.direction = row.sequenceDirectionMode = pingpong ? SequenceDirection::PINGPONG : SequenceDirection::FORWARD;
		CHECK_TRUE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
		LONGS_EQUAL(2, row.notes.entries.size());
		LONGS_EQUAL(2, row.notes.entries[0].pos);
		LONGS_EQUAL(pingpong ? 26 : 18, row.notes.entries[1].pos);
		LONGS_EQUAL(4, row.notes.entries[1].length);
	}
	LONGS_EQUAL(2, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_generation_rejects_zero_lengths_before_parameter_work) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	bool repeated = false;
	row.paramManager.on_repeat = [&] { repeated = true; };
	CHECK_FALSE(row.generateRepeats(&stack, 0, 32, 2, nullptr));
	CHECK_FALSE(row.generateRepeats(&stack, 16, 0, 2, nullptr));
	CHECK_FALSE(repeated);
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, pingpong_resize_rejects_invalidated_target_before_note_processing) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 4)};
	row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
	NoteVector::on_insert = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local).structural_refresh.request();
	};
	CHECK_FALSE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
	LONGS_EQUAL(0, clip.expected_events);
	LONGS_EQUAL(2, row.notes.entries[0].pos);
}

TEST(NoteSnapshotRecording, repeat_generation_handles_all_source_notes_discarded) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(20, 4)};
	CHECK_TRUE(row.generateRepeats(&stack, 16, 20, 1, nullptr));
	LONGS_EQUAL(0, row.notes.entries.size());
	LONGS_EQUAL(1, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_generation_uses_retained_source_count_after_truncation) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 4), note_for_test(20, 4), note_for_test(24, 4), note_for_test(28, 4)};
	CHECK_TRUE(row.generateRepeats(&stack, 16, 20, 1, nullptr));
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(2, row.notes.entries[0].pos);
	LONGS_EQUAL(18, row.notes.entries[1].pos);
	LONGS_EQUAL(2, row.notes.entries[1].length);
	LONGS_EQUAL(1, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_generation_preserves_first_last_conditions) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	for (bool pingpong : {false, true}) {
		for (int repeats : {0, 2}) {
			row.notes.entries = {note_for_test(2, 3), note_for_test(8, 1)};
			row.notes.entries[0].iterance = Iterance{0, 1};
			row.notes.entries[1].iterance = Iterance{0, 2};
			row.direction = row.sequenceDirectionMode =
			    pingpong ? SequenceDirection::PINGPONG : SequenceDirection::FORWARD;
			CHECK_TRUE(row.generateRepeats(&stack, 16, 32, repeats, nullptr));
			LONGS_EQUAL(4, row.notes.entries.size());
			CHECK_TRUE(row.notes.entries[0].iterance == (Iterance{0, 1}));
			CHECK_TRUE(row.notes.entries[1].iterance == (Iterance{0, 2}));
			CHECK_TRUE(row.notes.entries[2].iterance == (Iterance{0, pingpong ? 2 : 1}));
			CHECK_TRUE(row.notes.entries[3].iterance == (Iterance{0, pingpong ? 1 : 2}));
		}
	}
}

TEST(NoteSnapshotRecording, pingpong_iteration_uses_saved_source_length_after_deleting_original) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 3), note_for_test(8, 1)};
	row.notes.entries[0].iterance = Iterance{2, 2};
	row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
	CHECK_TRUE(row.generateRepeats(&stack, 16, 64, 4, nullptr));
	const int positions[] = {8, 23, 27, 40, 55, 59};
	const int lengths[] = {1, 1, 3, 1, 1, 3};
	LONGS_EQUAL(6, row.notes.entries.size());
	for (int i = 0; i < 6; ++i) {
		LONGS_EQUAL(positions[i], row.notes.entries[i].pos);
		LONGS_EQUAL(lengths[i], row.notes.entries[i].length);
		CHECK_TRUE(row.notes.entries[i].iterance == kDefaultIteranceValue);
	}
}

TEST(NoteSnapshotRecording, repeat_iteration_fails_instead_of_editing_neighbor_of_missing_copy) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 4)};
	row.notes.entries[0].iterance = Iterance{2, 1};
	row.notes.on_repeats = [&] { row.notes.entries[1].pos = 19; };
	CHECK_FALSE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(19, row.notes.entries[1].pos);
	CHECK_TRUE(row.notes.entries[1].iterance == (Iterance{2, 1}));
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, repeat_iteration_uses_wide_loop_divisor_product) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(1, 4)};
	row.notes.entries[0].iterance = Iterance{8, 1};
	const uint32_t oldLength = uint32_t{1} << 29;
	CHECK_TRUE(row.generateRepeats(&stack, oldLength, oldLength + 16, 2, nullptr));
	LONGS_EQUAL(1, row.notes.entries.size());
	CHECK_TRUE(row.notes.entries[0].iterance == (Iterance{4, 1}));
}

TEST(NoteSnapshotRecording, repeat_generation_rejects_unrepresentable_inputs_before_side_effects) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(0, 1), note_for_test(1, 1)};
	bool repeated = false;
	row.paramManager.on_repeat = [&] { repeated = true; };
	CHECK_FALSE(row.generateRepeats(&stack, uint32_t{INT32_MAX} + 1, 32, 2, &action));
	CHECK_FALSE(row.generateRepeats(&stack, 16, uint32_t{INT32_MAX} + 1, 2, &action));
	CHECK_FALSE(row.generateRepeats(&stack, 16, 32, -1, &action));
	row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
	// One case overflows the byte count; the other overflows the element count.
	CHECK_FALSE(row.generateRepeats(&stack, 2, INT32_MAX / 2, 2, &action));
	CHECK_FALSE(row.generateRepeats(&stack, 2, INT32_MAX, 2, &action));
	CHECK_FALSE(repeated);
	CHECK_TRUE(action.firstConsequence == nullptr);
	LONGS_EQUAL(0, recording::allocated);
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(1, row.notes.entries[1].pos);
	CHECK_TRUE(row.sequenceDirectionMode == SequenceDirection::PINGPONG);
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, pingpong_allocation_failure_preserves_direction_and_notes) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(2, 4)};
	row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
	NoteVector::fail_any_insert = true;
	CHECK_FALSE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
	CHECK_TRUE(row.sequenceDirectionMode == SequenceDirection::PINGPONG);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(2, row.notes.entries[0].pos);
	LONGS_EQUAL(4, row.notes.entries[0].length);
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, successful_pingpong_repeat_commits_direction_for_both_parent_modes) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	for (bool empty : {false, true}) {
		for (bool reverse_parent : {false, true}) {
			row.notes.entries.clear();
			if (!empty)
				row.notes.entries = {note_for_test(2, 4)};
			row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
			clip.sequenceDirectionMode = reverse_parent ? SequenceDirection::REVERSE : SequenceDirection::FORWARD;
			CHECK_TRUE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
			CHECK_TRUE(row.sequenceDirectionMode
			           == (reverse_parent ? SequenceDirection::FORWARD : SequenceDirection::OBEY_PARENT));
		}
	}
}

TEST(NoteSnapshotRecording, loop_length_conditional_note_does_not_use_drone_shortcut) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	SoundInstrument output;
	clip.output = &output;
	for (bool pingpong : {false, true}) {
		for (int repeats : {0, 2}) {
			row.notes.entries = {note_for_test(0, 16)};
			row.notes.entries[0].iterance = Iterance{2, 2};
			row.direction = row.sequenceDirectionMode =
			    pingpong ? SequenceDirection::PINGPONG : SequenceDirection::FORWARD;
			CHECK_TRUE(row.generateRepeats(&stack, 16, 32, repeats, nullptr));
			LONGS_EQUAL(1, row.notes.entries.size());
			LONGS_EQUAL(16, row.notes.entries[0].pos);
			LONGS_EQUAL(16, row.notes.entries[0].length);
			CHECK_TRUE(row.notes.entries[0].iterance == kDefaultIteranceValue);
		}
		for (int condition : {1, 2}) {
			row.notes.entries = {note_for_test(0, 16)};
			row.notes.entries[0].iterance = Iterance{0, condition};
			row.direction = row.sequenceDirectionMode =
			    pingpong ? SequenceDirection::PINGPONG : SequenceDirection::FORWARD;
			CHECK_TRUE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
			LONGS_EQUAL(2, row.notes.entries.size());
			for (int i = 0; i < 2; ++i) {
				LONGS_EQUAL(i * 16, row.notes.entries[i].pos);
				LONGS_EQUAL(16, row.notes.entries[i].length);
				CHECK_TRUE(row.notes.entries[i].iterance == (Iterance{0, condition}));
			}
		}
	}
}

TEST(NoteSnapshotRecording, unconditional_drone_still_stretches_as_one_note) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	SoundInstrument output;
	clip.output = &output;
	for (bool pingpong : {false, true}) {
		row.notes.entries = {note_for_test(0, 16)};
		row.direction = row.sequenceDirectionMode = pingpong ? SequenceDirection::PINGPONG : SequenceDirection::FORWARD;
		CHECK_TRUE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
		LONGS_EQUAL(1, row.notes.entries.size());
		LONGS_EQUAL(0, row.notes.entries[0].pos);
		LONGS_EQUAL(32, row.notes.entries[0].length);
		CHECK_TRUE(row.sequenceDirectionMode
		           == (pingpong ? SequenceDirection::OBEY_PARENT : SequenceDirection::FORWARD));
	}
}

TEST(NoteSnapshotRecording, pingpong_rejects_out_of_loop_source_before_side_effects) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	bool repeated = false;
	row.paramManager.on_repeat = [&] { repeated = true; };
	for (int pos : {-1, 16, 20}) {
		row.notes.entries = {note_for_test(pos, 4)};
		row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
		CHECK_FALSE(row.generateRepeats(&stack, 16, 32, 2, &action));
		LONGS_EQUAL(1, row.notes.entries.size());
		LONGS_EQUAL(pos, row.notes.entries[0].pos);
		LONGS_EQUAL(4, row.notes.entries[0].length);
		CHECK_TRUE(row.sequenceDirectionMode == SequenceDirection::PINGPONG);
	}
	CHECK_FALSE(repeated);
	CHECK_TRUE(action.firstConsequence == nullptr);
	LONGS_EQUAL(0, recording::allocated);
	LONGS_EQUAL(0, clip.expected_events);
}

TEST(NoteSnapshotRecording, pingpong_accepts_wrapped_tail_with_onset_inside_loop) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.notes.entries = {note_for_test(14, 4)};
	row.direction = row.sequenceDirectionMode = SequenceDirection::PINGPONG;
	CHECK_TRUE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(14, row.notes.entries[0].pos);
	LONGS_EQUAL(4, row.notes.entries[0].length);
	LONGS_EQUAL(30, row.notes.entries[1].pos);
	LONGS_EQUAL(4, row.notes.entries[1].length);
}

TEST(NoteSnapshotRecording, trimming_rejects_stack_redirect_before_event_notification) {
	InstrumentClip other_clip;
	row.notes.entries = {note_for_test(0, 8)};
	ModelStackWithNoteRow stack{&song, &clip, &row};
	row.paramManager.on_trim = [&] { stack.clip = &other_clip; };
	CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::BUG);
	LONGS_EQUAL(0, clip.expected_events);
	LONGS_EQUAL(0, other_clip.expected_events);
}
TEST(NoteSnapshotRecording, trimming_rejects_row_deleted_by_event_notification) {
	auto* target = new NoteRow;
	target->notes.entries = {note_for_test(0, 8)};
	clip.row = target;
	ModelStackWithNoteRow stack{&song, &clip, target};
	clip.on_expect_event = [&] {
		clip.row = &row;
		delete target;
	};
	CHECK_TRUE(target->trimToLength(4, &stack, nullptr) == Error::BUG);
	LONGS_EQUAL(1, clip.expected_events);
}
TEST(NoteSnapshotRecording, trimming_rejects_clip_deleted_by_event_notification) {
	auto* target = new InstrumentClip;
	target->row = &row;
	row.notes.entries = {note_for_test(0, 8)};
	ModelStackWithNoteRow stack{&song, target, &row};
	target->on_expect_event = [&] {
		currentSong = nullptr;
		delete target;
	};
	CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::BUG);
}

TEST(NoteSnapshotRecording, row_edit_context_rejects_registered_clip_removal_without_refresh) {
	song.registered.push_back(&clip);
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	CHECK_TRUE(context.valid());
	song.registered.clear();
	CHECK_FALSE(context.target_valid());
	CHECK_FALSE(context.valid());
}
TEST(NoteSnapshotRecording, row_edit_context_rejects_freed_registered_clip_without_refresh) {
	auto* target = new InstrumentClip;
	target->row = &row;
	song.registered.push_back(target);
	deluge::model::NoteRowEditContext context(target, 7, &row);
	song.registered.clear();
	delete target;
	CHECK_FALSE(context.target_valid());
	CHECK_FALSE(context.valid());
}
TEST(NoteSnapshotRecording, row_edit_context_accepts_unpublished_clip_and_publication) {
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	CHECK_TRUE(context.valid());
	song.registered.push_back(&clip);
	CHECK_TRUE(context.target_valid());
	CHECK_TRUE(context.valid());
}
TEST(NoteSnapshotRecording, trimming_rejects_registered_clip_deleted_without_song_change) {
	auto* target = new InstrumentClip;
	target->row = &row;
	song.registered.push_back(target);
	row.notes.entries = {note_for_test(0, 8)};
	ModelStackWithNoteRow stack{&song, target, &row};
	row.paramManager.on_trim = [&] {
		song.registered.clear();
		delete target;
	};
	CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::BUG);
	POINTERS_EQUAL(&song, currentSong);
}

TEST(NoteSnapshotRecording, row_edit_context_rejects_missing_snapshot_inputs) {
	deluge::model::NoteRowEditContext missing_clip(nullptr, 7, &row);
	deluge::model::NoteRowEditContext missing_row(&clip, 7, nullptr);
	currentSong = nullptr;
	deluge::model::NoteRowEditContext missing_song(&clip, 7, &row);
	currentSong = &song;
	for (auto* context : {&missing_clip, &missing_row, &missing_song}) {
		CHECK_FALSE(context->valid());
		CHECK_FALSE(context->target_valid());
	}
}
TEST(NoteSnapshotRecording, row_edit_context_rejects_mismatched_row_even_after_repair) {
	NoteRow other_row;
	clip.row = &other_row;
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	CHECK_FALSE(context.valid());
	clip.row = &row;
	CHECK_FALSE(context.target_valid());
	CHECK_FALSE(context.valid());
}
TEST(NoteSnapshotRecording, row_edit_context_rejects_wrong_clip_type_even_after_repair) {
	clip.type = ClipType::AUDIO;
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	clip.type = ClipType::INSTRUMENT;
	CHECK_FALSE(context.target_valid());
	CHECK_FALSE(context.valid());
}

TEST(NoteSnapshotRecording, row_edit_context_remembers_observed_publication_before_deletion) {
	for (bool check_full_snapshot : {false, true}) {
		auto* target = new InstrumentClip;
		target->row = &row;
		deluge::model::NoteRowEditContext context(target, 7, &row);
		song.registered.push_back(target);
		CHECK_TRUE(check_full_snapshot ? context.valid() : context.target_valid());
		song.registered.clear();
		delete target;
		CHECK_FALSE(context.target_valid());
		CHECK_FALSE(context.valid());
	}
}
TEST(NoteSnapshotRecording, row_edit_context_does_not_revive_after_observed_ownership_loss) {
	song.registered.push_back(&clip);
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	song.registered.clear();
	CHECK_FALSE(context.target_valid());
	song.registered.push_back(&clip);
	CHECK_FALSE(context.target_valid());
	CHECK_FALSE(context.valid());
}
TEST(NoteSnapshotRecording, row_edit_context_allows_continuous_ownership_between_song_arrays) {
	song.registered.push_back(&clip);
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	song.arrangementOnlyClips.values.push_back(&clip);
	song.registered.clear();
	CHECK_TRUE(context.target_valid());
	CHECK_TRUE(context.valid());
}

TEST(NoteSnapshotRecording, row_edit_context_does_not_revive_after_target_changes) {
	NoteRow replacement_row;
	Song other_song;
	for (int change = 0; change < 5; ++change) {
		const auto original_identity = row.undo_identity;
		const auto original_length = row.loopLengthIfIndependent;
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		switch (change) {
		case 0:
			clip.row = &replacement_row;
			break;
		case 1:
			++row.undo_identity;
			break;
		case 2:
			++row.loopLengthIfIndependent;
			break;
		case 3:
			currentSong = &other_song;
			break;
		case 4:
			clip.type = ClipType::AUDIO;
			break;
		}
		CHECK_FALSE(context.target_valid());
		clip.row = &row;
		row.undo_identity = original_identity;
		row.loopLengthIfIndependent = original_length;
		currentSong = &song;
		clip.type = ClipType::INSTRUMENT;
		CHECK_FALSE(context.target_valid());
		CHECK_FALSE(context.valid());
	}
}
TEST(NoteSnapshotRecording, row_edit_context_preserves_target_validation_after_note_resize) {
	row.notes.entries = {note_for_test(0, 4)};
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	row.notes.entries.push_back(note_for_test(8, 4));
	CHECK_FALSE(context.valid());
	CHECK_TRUE(context.target_valid());
}
TEST(NoteSnapshotRecording, invalidated_row_edit_context_does_not_read_later_freed_unpublished_clip) {
	auto* target = new InstrumentClip;
	target->row = &row;
	deluge::model::NoteRowEditContext context(target, 7, &row);
	++row.undo_identity;
	CHECK_FALSE(context.target_valid());
	--row.undo_identity;
	delete target;
	CHECK_FALSE(context.target_valid());
	CHECK_FALSE(context.valid());
}

TEST(NoteSnapshotRecording, row_edit_context_keeps_metadata_invalidation_after_restoration) {
	Output original_output, replacement_output;
	clip.output = &original_output;
	for (int change = 0; change < 3; ++change) {
		const auto original_length = clip.loopLength;
		const auto original_offset = row.paramManager.expressionParamSetOffset;
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		if (change == 0)
			++clip.loopLength;
		else if (change == 1)
			clip.output = &replacement_output;
		else
			++row.paramManager.expressionParamSetOffset;
		CHECK_FALSE(context.valid());
		clip.loopLength = original_length;
		clip.output = &original_output;
		row.paramManager.expressionParamSetOffset = original_offset;
		CHECK_FALSE(context.target_valid());
		deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
		CHECK_TRUE(fresh_context.valid());
	}
}
TEST(NoteSnapshotRecording, row_edit_context_keeps_every_collection_replacement_invalid) {
	ParamCollection original_collection, replacement_collection;
	for (auto& summary : row.paramManager.summaries) {
		summary.paramCollection = &original_collection;
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		summary.paramCollection = &replacement_collection;
		CHECK_FALSE(context.valid());
		summary.paramCollection = &original_collection;
		CHECK_FALSE(context.target_valid());
		deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
		CHECK_TRUE(fresh_context.valid());
	}
}
TEST(NoteSnapshotRecording, consuming_either_panel_refresh_does_not_revive_row_context) {
	using deluge::gui::ui_session::Id;
	for (auto panel : {Id::Local, Id::Remote}) {
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		auto& refresh = deluge::gui::ui_session::navigation.for_owner(panel).structural_refresh;
		refresh.request();
		CHECK_FALSE(context.valid());
		CHECK_TRUE(refresh.consume(false, true, true));
		CHECK_FALSE(context.target_valid());
		deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
		CHECK_TRUE(fresh_context.valid());
	}
}

TEST_GROUP(LengthHistoryRecording) {
	Song song;
	Clip clip;
	Action action;
	void setup() override {
		currentSong = &song;
		song.registered = {&clip};
		actionLogger.firstAction[BEFORE] = &action;
		recording::on_allocate = {};
		recording::fail_allocation = false;
		recording::allocated = recording::freed = 0;
	}
	void teardown() override {
		recording::on_allocate = {};
		recording::fail_allocation = false;
		while (action.firstConsequence) {
			auto* consequence = action.firstConsequence;
			action.firstConsequence = consequence->next;
			delete consequence;
		}
		actionLogger.firstAction[BEFORE] = nullptr;
		recording::allocated = recording::freed = 0;
		currentSong = nullptr;
	}
};
TEST(LengthHistoryRecording, allocation_failure_can_retry_and_preserves_first_snapshot) {
	recording::fail_allocation = true;
	CHECK_FALSE(action.recordClipLengthChange(&clip, 48));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	recording::fail_allocation = false;
	CHECK_TRUE(action.recordClipLengthChange(&clip, 48));
	CHECK_TRUE(action.recordClipLengthChange(&clip, 72));
	LONGS_EQUAL(1, recording::allocated);
	LONGS_EQUAL(48, static_cast<ConsequenceClipLength*>(action.firstConsequence)->lengthToRevertTo);
}
TEST(LengthHistoryRecording, allocation_changes_do_not_publish_stale_history) {
	using namespace deluge::gui::ui_session;
	Output replacement_output;
	for (int change = 0; change < 8; ++change) {
		currentSong = &song;
		song.registered = {&clip};
		clip.output = nullptr;
		clip.type = ClipType::INSTRUMENT;
		clip.loopLength = 96;
		actionLogger.firstAction[BEFORE] = &action;
		recording::on_allocate = [&] {
			switch (change) {
			case 0:
				currentSong = nullptr;
				break;
			case 1:
				song.registered.clear();
				break;
			case 2:
				clip.output = &replacement_output;
				break;
			case 3:
				clip.loopLength = 123;
				break;
			case 4:
				clip.type = ClipType::AUDIO;
				break;
			case 5:
				navigation.for_owner(Id::Remote).structural_refresh.request();
				break;
			case 6:
				actionLogger.firstAction[BEFORE] = nullptr;
				break;
			case 7:
				navigation.for_owner(Id::Local).structural_refresh.request();
				break;
			}
		};
		CHECK_FALSE(action.recordClipLengthChange(&clip, 48));
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(change + 1, recording::freed);
	}
}
TEST(LengthHistoryRecording, registered_clip_deleted_during_allocation_is_not_accessed) {
	auto* target_clip = new Clip;
	song.registered = {target_clip};
	recording::on_allocate = [&] {
		song.registered.clear();
		delete target_clip;
	};
	CHECK_FALSE(action.recordClipLengthChange(target_clip, 48));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(1, recording::freed);
}
TEST(LengthHistoryRecording, retained_action_deleted_during_allocation_is_not_accessed) {
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	CHECK_FALSE(target_action->recordClipLengthChange(&clip, 48));
	LONGS_EQUAL(1, recording::freed);
}
TEST(LengthHistoryRecording, nested_recording_keeps_one_consequence_and_first_backup) {
	bool nested = false;
	recording::on_allocate = [&] {
		if (!nested) {
			nested = true;
			CHECK_TRUE(action.recordClipLengthChange(&clip, 24));
		}
	};
	CHECK_TRUE(action.recordClipLengthChange(&clip, 48));
	LONGS_EQUAL(2, recording::allocated);
	LONGS_EQUAL(1, recording::freed);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next);
	LONGS_EQUAL(24, static_cast<ConsequenceClipLength*>(action.firstConsequence)->lengthToRevertTo);
}

TEST(LengthHistoryRecording, changed_ui_owner_releases_unused_storage) {
	using namespace deluge::gui::ui_session;
	const auto original_owner = current();
	recording::on_allocate = [&] { detail::active = original_owner == Id::Local ? Id::Remote : Id::Local; };
	CHECK_FALSE(action.recordClipLengthChange(&clip, 48));
	detail::active = original_owner;
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(1, recording::freed);
}
TEST(LengthHistoryRecording, stable_unpublished_clip_and_unlisted_action_remain_supported) {
	song.registered.clear();
	actionLogger.firstAction[BEFORE] = nullptr;
	CHECK_TRUE(action.recordClipLengthChange(&clip, 48));
	CHECK_TRUE(action.firstConsequence != nullptr);
	LONGS_EQUAL(48, static_cast<ConsequenceClipLength*>(action.firstConsequence)->lengthToRevertTo);
}

TEST(LengthHistoryRecording, same_address_action_replacement_rejects_pending_history) {
	auto* target_action = new Action;
	const uint64_t original_identity = target_action->action_identity;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		target_action->~Action();
		new (target_action) Action;
	};
	const bool result = target_action->recordClipLengthChange(&clip, 48);
	const uint64_t replacement_identity = target_action->action_identity;
	const bool empty_history = target_action->firstConsequence == nullptr;
	delete target_action;
	CHECK_FALSE(result);
	CHECK_TRUE(original_identity != replacement_identity);
	CHECK_TRUE(empty_history);
	LONGS_EQUAL(1, recording::freed);
}

TEST(NoteSnapshotRecording, row_context_owner_mismatch_stays_invalid_after_owner_restored) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		CHECK_TRUE(context.valid());
		{
			Scope other(owner == Id::Local ? Id::Remote : Id::Local);
			CHECK_FALSE(context.target_valid());
		}
		CHECK_FALSE(context.valid());
		deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
		CHECK_TRUE(fresh_context.valid());
	}
}
TEST(NoteSnapshotRecording, row_context_accepts_restored_nested_owner_without_intermediate_validation) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		{ Scope other(owner == Id::Local ? Id::Remote : Id::Local); }
		CHECK_TRUE(context.target_valid());
		CHECK_TRUE(context.valid());
	}
}
TEST(NoteSnapshotRecording, trimming_stops_on_owner_change_at_parameter_and_event_callbacks) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		for (bool during_event : {false, true}) {
			Scope initiating(owner);
			std::optional<Scope> switched_owner;
			row.notes.entries = {note_for_test(0, 8)};
			clip.expected_events = 0;
			ModelStackWithNoteRow stack{&song, &clip, &row};
			auto switch_owner = [&] { switched_owner.emplace(owner == Id::Local ? Id::Remote : Id::Local); };
			row.paramManager.on_trim = during_event ? std::function<void()>{} : switch_owner;
			clip.on_expect_event = during_event ? switch_owner : std::function<void()>{};
			CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::BUG);
			LONGS_EQUAL(during_event ? 1 : 0, clip.expected_events);
			row.paramManager.on_trim = {};
			clip.on_expect_event = {};
		}
	}
}
TEST(NoteSnapshotRecording, trimming_accepts_nested_ui_callbacks_for_both_owners) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		row.notes.entries = {note_for_test(0, 8)};
		clip.expected_events = 0;
		ModelStackWithNoteRow stack{&song, &clip, &row};
		auto nested = [&] { Scope other(owner == Id::Local ? Id::Remote : Id::Local); };
		row.paramManager.on_trim = nested;
		clip.on_expect_event = nested;
		CHECK_TRUE(row.trimToLength(4, &stack, nullptr) == Error::NONE);
		LONGS_EQUAL(1, clip.expected_events);
		row.paramManager.on_trim = {};
		clip.on_expect_event = {};
	}
}

TEST(NoteSnapshotRecording, corresponding_notes_populate_empty_row_and_clamp_final_wrap) {
	clip.loopLength = 36;
	clip.wrapEditLevel = 16;
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, &clip, &row};
	CHECK_TRUE(row.addCorrespondingNotes(0, 16, 90, &stack, true, nullptr) == Error::NONE);
	LONGS_EQUAL(3, row.notes.entries.size());
	for (int index = 0; index < 3; ++index) {
		LONGS_EQUAL(index * 16, row.notes.entries[index].pos);
		LONGS_EQUAL(index == 2 ? 4 : 16, row.notes.entries[index].length);
	}
}
TEST(NoteSnapshotRecording, corresponding_notes_use_independent_empty_row_length) {
	clip.loopLength = 64;
	clip.wrapEditLevel = 16;
	row.loopLengthIfIndependent = 20;
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, &clip, &row};
	CHECK_TRUE(row.addCorrespondingNotes(8, 4, 90, &stack, true, nullptr) == Error::NONE);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(8, row.notes.entries[0].pos);
	LONGS_EQUAL(4, row.notes.entries[0].length);
}
TEST(NoteSnapshotRecording, empty_corresponding_notes_allocation_failure_can_retry) {
	clip.loopLength = 32;
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, &clip, &row};
	recording::fail_working_allocation = true;
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(row.notes.entries.empty());
	LONGS_EQUAL(0, clip.expected_events);
	recording::fail_working_allocation = false;
	NoteVector::fail_any_insert = true;
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::INSUFFICIENT_RAM);
	CHECK_TRUE(row.notes.entries.empty());
	LONGS_EQUAL(0, clip.expected_events);
	NoteVector::fail_any_insert = false;
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::NONE);
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(8, row.notes.entries[0].pos);
	LONGS_EQUAL(24, row.notes.entries[1].pos);
}

TEST(NoteSnapshotRecording, corresponding_notes_reject_invalid_inputs_before_allocating) {
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	CHECK_TRUE(row.addCorrespondingNotes(0, 1, 90, nullptr, true, nullptr) == Error::BUG);
	for (int value : {0, -1, INT32_MIN}) {
		CHECK_TRUE(row.addCorrespondingNotes(0, value, 90, &stack, true, nullptr) == Error::BUG);
		clip.loopLength = value;
		CHECK_TRUE(row.addCorrespondingNotes(0, 1, 90, &stack, true, nullptr) == Error::BUG);
		clip.loopLength = 32;
	}
	CHECK_TRUE(row.addCorrespondingNotes(-1, 1, 90, &stack, true, nullptr) == Error::BUG);
	for (uint32_t level : {0u, 0x80000000u, 0xffffffffu}) {
		clip.wrapEditLevel = level;
		CHECK_TRUE(row.addCorrespondingNotes(0, 1, 90, &stack, true, nullptr) == Error::BUG);
	}
	LONGS_EQUAL(0, allocations);
	LONGS_EQUAL(0, clip.expected_events);
}
TEST(NoteSnapshotRecording, corresponding_notes_outside_short_row_are_noop) {
	clip.loopLength = 4;
	clip.wrapEditLevel = 16;
	row.notes.entries = {note_for_test(0, 2)};
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::NONE);
	LONGS_EQUAL(0, allocations);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(2, row.notes.entries[0].length);
	LONGS_EQUAL(0, clip.expected_events);
}
TEST(NoteSnapshotRecording, corresponding_notes_support_large_wrap_without_search_overflow) {
	clip.loopLength = INT32_MAX;
	clip.wrapEditLevel = INT32_MAX;
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, &clip, &row};
	CHECK_TRUE(row.addCorrespondingNotes(INT32_MAX - 1, 2, 90, &stack, true, nullptr) == Error::NONE);
	LONGS_EQUAL(1, row.notes.entries.size());
	LONGS_EQUAL(INT32_MAX - 1, row.notes.entries[0].pos);
	LONGS_EQUAL(2, row.notes.entries[0].length);
}

TEST(NoteSnapshotRecording, corresponding_notes_reject_changed_wrap_or_stack_during_allocations) {
	for (bool vector_allocation : {false, true}) {
		for (int field = 0; field < 5; ++field) {
			clip.loopLength = 32;
			clip.wrapEditLevel = 16;
			clip.expected_events = 0;
			row.notes.entries = {note_for_test(0, 2)};
			ModelStackWithNoteRow stack{&song, &clip, &row};
			Song other_song;
			InstrumentClip other_clip;
			NoteRow other_row;
			auto change_source = [&] {
				switch (field) {
				case 0:
					clip.wrapEditLevel = 8;
					break;
				case 1:
					stack.song = &other_song;
					break;
				case 2:
					stack.clip = &other_clip;
					break;
				case 3:
					stack.row = &other_row;
					break;
				case 4:
					++stack.noteRowId;
					break;
				}
			};
			if (vector_allocation)
				NoteVector::on_insert = change_source;
			else
				recording::on_working_allocate = change_source;
			CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
			LONGS_EQUAL(1, row.notes.entries.size());
			LONGS_EQUAL(0, row.notes.entries[0].pos);
			LONGS_EQUAL(2, row.notes.entries[0].length);
			LONGS_EQUAL(0, clip.expected_events);
			if (field == 0)
				LONGS_EQUAL(8, clip.wrapEditLevel);
			NoteVector::on_insert = {};
			recording::on_working_allocate = {};
		}
	}
}
TEST(NoteSnapshotRecording, corresponding_notes_reject_foreign_song_before_allocation) {
	Song other_song;
	ModelStackWithNoteRow stack{&other_song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
	LONGS_EQUAL(0, allocations);
}

TEST(NoteSnapshotRecording, corresponding_notes_reject_event_context_changes) {
	for (int field = 0; field < 6; ++field) {
		clip.loopLength = 32;
		clip.wrapEditLevel = 16;
		row.notes.entries.clear();
		ModelStackWithNoteRow stack{&song, &clip, &row};
		Song other_song;
		InstrumentClip other_clip;
		NoteRow other_row;
		clip.on_expect_event = [&] {
			switch (field) {
			case 0:
				clip.wrapEditLevel = 8;
				break;
			case 1:
				stack.song = &other_song;
				break;
			case 2:
				stack.clip = &other_clip;
				break;
			case 3:
				stack.row = &other_row;
				break;
			case 4:
				++stack.noteRowId;
				break;
			case 5:
				row.notes.entries.clear();
				break;
			}
		};
		CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
		LONGS_EQUAL(field == 5 ? 0 : 2, row.notes.entries.size());
		clip.on_expect_event = {};
	}
}
TEST(NoteSnapshotRecording, corresponding_notes_reject_registered_clip_deleted_by_event) {
	auto* target = new InstrumentClip;
	target->row = &row;
	target->loopLength = 32;
	song.registered.push_back(target);
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, target, &row};
	target->on_expect_event = [&] {
		song.registered.clear();
		delete target;
	};
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
	LONGS_EQUAL(2, row.notes.entries.size());
}
TEST(NoteSnapshotRecording, corresponding_notes_reject_row_deleted_by_event) {
	auto* target = new NoteRow;
	clip.row = target;
	clip.loopLength = 32;
	ModelStackWithNoteRow stack{&song, &clip, target};
	clip.on_expect_event = [&] {
		clip.row = &row;
		delete target;
	};
	CHECK_TRUE(target->addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
	clip.on_expect_event = {};
}

TEST(NoteSnapshotRecording, row_source_count_mismatch_stays_invalid_after_restoration) {
	for (bool initially_empty : {false, true}) {
		row.notes.entries.clear();
		row.notes.entries.reserve(4);
		if (!initially_empty)
			row.notes.entries.push_back(note_for_test(0, 4));
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		row.notes.entries.push_back(note_for_test(8, 4));
		CHECK_FALSE(context.valid());
		row.notes.entries.pop_back();
		CHECK_FALSE(context.valid());
		CHECK_TRUE(context.target_valid());
		deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
		CHECK_TRUE(fresh_context.valid());
	}
}
TEST(NoteSnapshotRecording, row_source_buffer_mismatch_stays_invalid_after_restoration) {
	row.notes.entries = {note_for_test(0, 4)};
	std::vector<Note> replacement{note_for_test(8, 4)};
	deluge::model::NoteRowEditContext context(&clip, 7, &row);
	row.notes.entries.swap(replacement);
	CHECK_FALSE(context.valid());
	row.notes.entries.swap(replacement);
	CHECK_FALSE(context.valid());
	CHECK_TRUE(context.target_valid());
	deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
	CHECK_TRUE(fresh_context.valid());
}
TEST(NoteSnapshotRecording, invalid_row_source_does_not_read_later_freed_unpublished_target) {
	auto* target = new InstrumentClip;
	target->row = &row;
	row.notes.entries.clear();
	deluge::model::NoteRowEditContext context(target, 7, &row);
	row.notes.entries.push_back(note_for_test(0, 4));
	CHECK_FALSE(context.valid());
	delete target;
	CHECK_FALSE(context.valid());
}

TEST(NoteSnapshotRecording, corresponding_notes_reject_malformed_source_before_allocation) {
	clip.loopLength = 32;
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	for (int invalid = 0; invalid < 6; ++invalid) {
		row.notes.entries = {note_for_test(0, 2), note_for_test(16, 2)};
		switch (invalid) {
		case 0:
			row.notes.entries[0].pos = -1;
			break;
		case 1:
			row.notes.entries[1].pos = 32;
			break;
		case 2:
			row.notes.entries[1].pos = 0;
			break;
		case 3:
			row.notes.entries[0].pos = 24;
			break;
		case 4:
			row.notes.entries[1].length = 0;
			break;
		case 5:
			row.notes.entries[1].length = -1;
			break;
		}
		CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
		LONGS_EQUAL(2, row.notes.entries.size());
	}
	LONGS_EQUAL(0, allocations);
	LONGS_EQUAL(0, clip.expected_events);
}
TEST(NoteSnapshotRecording, corresponding_notes_recheck_source_contents_after_allocations) {
	clip.loopLength = 32;
	ModelStackWithNoteRow stack{&song, &clip, &row};
	for (bool vector_allocation : {false, true}) {
		row.notes.entries = {note_for_test(0, 2), note_for_test(16, 2)};
		auto invalidate = [&] { row.notes.entries[1].pos = -1; };
		if (vector_allocation)
			NoteVector::on_insert = invalidate;
		else
			recording::on_working_allocate = invalidate;
		CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
		LONGS_EQUAL(2, row.notes.entries.size());
		LONGS_EQUAL(-1, row.notes.entries[1].pos);
		LONGS_EQUAL(0, clip.expected_events);
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
	}
}
TEST(NoteSnapshotRecording, corresponding_notes_accept_existing_wraparound_tail) {
	clip.loopLength = 32;
	row.notes.entries = {note_for_test(24, 16)};
	ModelStackWithNoteRow stack{&song, &clip, &row};
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::NONE);
	LONGS_EQUAL(2, row.notes.entries.size());
	LONGS_EQUAL(8, row.notes.entries[0].pos);
	LONGS_EQUAL(24, row.notes.entries[1].pos);
	LONGS_EQUAL(16, row.notes.entries[1].length);
}

TEST(NoteSnapshotRecording, corresponding_notes_reject_in_place_corruption_after_publication) {
	clip.loopLength = 32;
	ModelStackWithNoteRow stack{&song, &clip, &row};
	for (int field = 0; field < 5; ++field) {
		row.notes.entries.clear();
		clip.expected_events = 0;
		clip.on_expect_event = [&] {
			switch (field) {
			case 0:
				row.notes.entries[0].pos = -1;
				break;
			case 1:
				row.notes.entries[1].pos = 32;
				break;
			case 2:
				row.notes.entries[1].pos = row.notes.entries[0].pos;
				break;
			case 3:
				row.notes.entries[1].length = 0;
				break;
			case 4:
				row.notes.entries[1].length = -1;
				break;
			}
		};
		CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
		LONGS_EQUAL(2, row.notes.entries.size());
		LONGS_EQUAL(1, clip.expected_events);
		if (field == 0)
			LONGS_EQUAL(-1, row.notes.entries[0].pos);
		if (field == 1)
			LONGS_EQUAL(32, row.notes.entries[1].pos);
		if (field == 2)
			LONGS_EQUAL(8, row.notes.entries[1].pos);
		if (field >= 3)
			LONGS_EQUAL(field == 3 ? 0 : -1, row.notes.entries[1].length);
		clip.on_expect_event = {};
	}
}
TEST(NoteSnapshotRecording, corresponding_notes_validate_new_notes_beyond_original_count) {
	clip.loopLength = 48;
	row.notes.entries = {note_for_test(0, 2)};
	ModelStackWithNoteRow stack{&song, &clip, &row};
	clip.on_expect_event = [&] { row.notes.entries.back().length = 0; };
	CHECK_TRUE(row.addCorrespondingNotes(8, 2, 90, &stack, true, nullptr) == Error::BUG);
	LONGS_EQUAL(4, row.notes.entries.size());
	LONGS_EQUAL(0, row.notes.entries.back().length);
	clip.on_expect_event = {};
}

TEST(NoteSnapshotRecording, row_context_direction_change_stays_invalid_after_restoration) {
	for (bool change_parent : {false, true}) {
		clip.sequenceDirectionMode = SequenceDirection::FORWARD;
		row.sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
		deluge::model::NoteRowEditContext context(&clip, 7, &row);
		if (change_parent)
			clip.sequenceDirectionMode = SequenceDirection::REVERSE;
		else
			row.sequenceDirectionMode = SequenceDirection::PINGPONG;
		CHECK_FALSE(context.target_valid());
		clip.sequenceDirectionMode = SequenceDirection::FORWARD;
		row.sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
		CHECK_FALSE(context.valid());
		deluge::model::NoteRowEditContext fresh_context(&clip, 7, &row);
		CHECK_TRUE(fresh_context.valid());
	}
}
TEST(NoteSnapshotRecording, repeat_generation_rejects_direction_changes_during_parameter_callback) {
	for (bool change_parent : {false, true}) {
		clip.loopLength = 16;
		clip.sequenceDirectionMode = SequenceDirection::FORWARD;
		row.sequenceDirectionMode = SequenceDirection::PINGPONG;
		row.notes.entries = {note_for_test(2, 2)};
		ModelStackWithNoteRow stack{&song, &clip, &row};
		row.paramManager.on_repeat = [&] {
			if (change_parent)
				clip.sequenceDirectionMode = SequenceDirection::REVERSE;
			else
				row.sequenceDirectionMode = SequenceDirection::REVERSE;
		};
		CHECK_FALSE(row.generateRepeats(&stack, 16, 32, 2, nullptr));
		LONGS_EQUAL(1, row.notes.entries.size());
		LONGS_EQUAL(2, row.notes.entries[0].pos);
		CHECK_TRUE((change_parent ? clip.sequenceDirectionMode : row.sequenceDirectionMode)
		           == SequenceDirection::REVERSE);
		row.paramManager.on_repeat = {};
	}
}

TEST(NoteSnapshotRecording, corresponding_notes_reject_capacity_beyond_signed_array_bytes) {
	clip.wrapEditLevel = 1;
	clip.loopLength = INT32_MAX / sizeof(Note) + 1;
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	CHECK_TRUE(row.addCorrespondingNotes(0, 1, 90, &stack, true, nullptr) == Error::BUG);
	LONGS_EQUAL(0, allocations);
	LONGS_EQUAL(0, clip.expected_events);
	CHECK_TRUE(row.notes.entries.empty());
}
TEST(NoteSnapshotRecording, corresponding_notes_capacity_includes_existing_notes) {
	clip.wrapEditLevel = 1;
	clip.loopLength = INT32_MAX / sizeof(Note);
	row.notes.entries = {note_for_test(0, 1)};
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	CHECK_TRUE(row.addCorrespondingNotes(0, 1, 90, &stack, true, nullptr) == Error::BUG);
	LONGS_EQUAL(0, allocations);
	LONGS_EQUAL(1, row.notes.entries.size());
}
TEST(NoteSnapshotRecording, corresponding_notes_capacity_boundary_reaches_allocator_safely) {
	clip.wrapEditLevel = 1;
	clip.loopLength = INT32_MAX / sizeof(Note);
	row.notes.entries.clear();
	ModelStackWithNoteRow stack{&song, &clip, &row};
	int allocations = 0;
	recording::on_working_allocate = [&] { ++allocations; };
	recording::fail_working_allocation = true;
	CHECK_TRUE(row.addCorrespondingNotes(0, 1, 90, &stack, true, nullptr) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, allocations);
	CHECK_TRUE(row.notes.entries.empty());
	LONGS_EQUAL(0, clip.expected_events);
}
