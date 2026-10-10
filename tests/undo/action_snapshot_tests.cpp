#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/action/action_identity.h"
#include "model/consequence/consequence_clip_instance_change.h"
#include "model/consequence/consequence_clip_instance_existence.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_existence.h"
#include "model/note/note_row_edit_context.h"
#include "undo_model.h"
#include "util/misc.h"

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
inline std::function<void()> on_param_snapshot;
inline bool param_snapshot_valid = true;
inline int param_snapshots = 0, param_snapshots_destroyed = 0;
inline Error result = Error::NONE;
inline bool fail_allocation = false;
inline bool fail_working_allocation = false;
inline std::function<void()> on_working_allocate;
inline std::function<void()> on_allocate;
inline int allocated = 0, freed = 0, destroyed = 0, reverted = 0;
} // namespace recording
struct test_auto_param {
	struct {
		int emptied = 0;
		void empty() { ++emptied; }
	} nodes;
};
struct ModelStackWithAutoParam {
	ParamCollection* paramCollection = nullptr;
	int32_t paramId = 0;
	test_auto_param* autoParam = nullptr;
};
// Action recording is production code; parameter cloning is an injected collaborator.
class ConsequenceParamChange : public Consequence {
public:
	ConsequenceParamChange(ModelStackWithAutoParam const* stack, bool) : modelStack(*stack) {
		type = Consequence::PARAM_CHANGE;
		++recording::param_snapshots;
		if (recording::on_param_snapshot)
			recording::on_param_snapshot();
	}
	ModelStackWithAutoParam modelStack;
	~ConsequenceParamChange() override { ++recording::param_snapshots_destroyed; }
	bool snapshot_valid() const { return recording::param_snapshot_valid; }
	Error revert(TimeType, ModelStack*) override { return Error::NONE; }
};
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
// Automation values are injected; native parameter tests exercise the real insertion inverse.
class ConsequenceArrangerParamsTimeInserted : public Consequence {
public:
	int32_t pos, length;
	ConsequenceArrangerParamsTimeInserted(int32_t target_pos, int32_t target_length)
	    : pos(target_pos), length(target_length) {
		type = Consequence::ARRANGER_TIME_INSERTED;
	}
	Error revert(TimeType, ModelStack*) override {
		++reverts;
		if (revert_result != Error::NONE)
			return revert_result;
		automation_pos -= length;
		return Error::NONE;
	}
	inline static int32_t automation_pos = 0;
	inline static int reverts = 0;
	inline static Error revert_result = Error::NONE;
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
	bool recordParamChangeDefinitely(ModelStackWithAutoParam const*, bool);
	bool record_param_change(ModelStackWithAutoParam const*, bool, bool);
	bool recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam const*, bool);
	bool containsConsequenceParamChange(ParamCollection*, int32_t);
	bool rollback_empty_instance_deletions(Song*, Consequence*, int32_t);
	bool rollback_arranger_time_inserted(Song*, Consequence*);
	bool record_arranger_time_inserted(int32_t, int32_t);
	bool rollback_instance_batch(Song*, Consequence*, int32_t, int32_t, bool = false);
	Error snapshot_error = Error::NONE;
	bool require_complete_snapshots = false;
	int snapshot_failures = 0;
	bool snapshot_failed() {
		++snapshot_failures;
		if (require_complete_snapshots)
			snapshot_error = Error::INSUFFICIENT_RAM;
		return false;
	}
	Error recordNoteArrayChangeIfNotAlreadySnapshotted(InstrumentClip*, int32_t, NoteVector*, bool, bool = false);
	Error recordNoteArrayChangeDefinitely(InstrumentClip*, int32_t, NoteVector*, bool);
	Error record_note_array_change(InstrumentClip*, int32_t, NoteVector*, bool, bool, bool);
	Error recordNoteExistenceChange(InstrumentClip*, int32_t, Note*, ExistenceChangeType, Note** = nullptr,
	                                void** = nullptr);
	static void* allocate_note_existence_memory();
	int32_t xScrollClip[2] = {5, 9};
	void addConsequence(Consequence* c) {
		c->next = firstConsequence;
		firstConsequence = c;
	}
	bool recordClipExistenceChange(Song*, ClipArray*, Clip*, ExistenceChangeType);
	bool recordClipLengthChange(Clip*, int32_t);
	bool recordClipInstanceExistenceChange(Output*, ClipInstance*, ExistenceChangeType);
	bool containsConsequenceNoteArrayChange(InstrumentClip*, int32_t, bool = false);
};

static struct {
	Action* firstAction[2]{};
} actionLogger;
#include "action_snapshot_method.inc"
#include "instance_change_consequence.inc"
#include "instance_change_method.inc"
#include "instance_existence_consequence.inc"
class ArrangerView {
public:
	bool delete_clip_instance(Output*, int32_t, ClipInstance*, Action*, bool = false);
	bool shift_clips_horizontally(int32_t, int32_t, Action*, bool* = nullptr);
	bool shift_arrangement_time(int32_t, int32_t, Action*);
	ModelStackWithAutoParam preparation_stack{};
	int automation_edits = 0;
	bool prepare_automation_contraction(Action* action) {
		return action && action->recordParamChangeDefinitely(&preparation_stack, false);
	}
	bool shift_automation_horizontally(int32_t offset, int32_t amount, Action* action) {
		if (!action || (offset >= 0 && !action->record_arranger_time_inserted(0, amount)))
			return false;
		++automation_edits;
		ConsequenceArrangerParamsTimeInserted::automation_pos += amount;
		return true;
	}
};
#include "arranger_delete_method.inc"
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
		currentSong = &song;
		actionLogger.firstAction[BEFORE] = &action;
		recording::on_allocate = {};
		recording::result = Error::NONE;
		recording::fail_allocation = false;
		recording::allocated = recording::freed = recording::destroyed = recording::reverted = 0;
	}
	void teardown() override {
		recording::on_allocate = {};
		actionLogger.firstAction[BEFORE] = nullptr;
		currentSong = nullptr;
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
	song.sessionClips.values = {&clip};
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
	song.sessionClips.values = {&clip};
	recording::fail_allocation = true;
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(ClipExistenceRecording, successful_deletion_is_recorded_once) {
	song.sessionClips.values = {&clip};
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
	song.sessionClips.values = {&clip};
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
	Output output;
	ReattachmentClip clip, other;
	ConsequenceClipExistence consequence{&clip, &song.sessionClips, ExistenceChangeType::DELETE};
	void setup() override {
		currentSong = &song;
		clip.output = &output;
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
		CHECK_FALSE(clip.retiring);
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
		recording::allocated = recording::freed = 0;
		currentSong = &song;
		actionLogger.firstAction[BEFORE] = nullptr;
		clip.row = &row;
		row.sequenced = true;
		recording::fail_allocation = false;
		recording::on_allocate = {};
		recording::on_param_snapshot = {};
		recording::param_snapshot_valid = true;
		recording::param_snapshots = recording::param_snapshots_destroyed = 0;
		NoteVector::on_clone = {};
		NoteVector::on_clone_attempt = {};
		NoteVector::fail_clone = false;
		NoteVector::fail_any_insert = false;
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
		recording::fail_working_allocation = false;
	}
	void teardown() override {
		recording::on_allocate = {};
		recording::on_param_snapshot = {};
		recording::param_snapshot_valid = true;
		recording::param_snapshots = recording::param_snapshots_destroyed = 0;
		NoteVector::on_clone = {};
		NoteVector::on_clone_attempt = {};
		NoteVector::fail_clone = false;
		NoteVector::fail_any_insert = false;
		NoteVector::on_insert = {};
		recording::on_working_allocate = {};
		recording::fail_working_allocation = false;
		currentSong = nullptr;
		actionLogger.firstAction[BEFORE] = nullptr;
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

TEST(NoteSnapshotRecording, prepared_deletion_records_undo_without_allocating_during_removal) {
	row.notes.entries = {note_for_test(4)};
	void* memory = Action::allocate_note_existence_memory();
	CHECK(memory);
	int allocations = recording::allocated;
	recording::fail_allocation = true;
	CHECK(row.deleteNoteByIndex(0, &action, 7, &clip, &memory) == Error::NONE);
	POINTERS_EQUAL(nullptr, memory);
	LONGS_EQUAL(allocations, recording::allocated);
	LONGS_EQUAL(0, row.notes.getNumElements());
	CHECK(action.firstConsequence);
	auto* saved = static_cast<ConsequenceNoteExistence*>(action.firstConsequence);
	LONGS_EQUAL(4, saved->pos);
}
TEST(NoteSnapshotRecording, missing_prepared_deletion_memory_does_not_fall_back_to_allocation) {
	row.notes.entries = {note_for_test(4)};
	void* memory = nullptr;
	int allocations = recording::allocated;
	CHECK(row.deleteNoteByIndex(0, &action, 7, &clip, &memory) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(allocations, recording::allocated);
	LONGS_EQUAL(1, row.notes.getNumElements());
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(NoteSnapshotRecording, existing_array_snapshot_leaves_unused_prepared_storage_with_caller) {
	row.notes.entries = {note_for_test(4)};
	CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	void* memory = Action::allocate_note_existence_memory();
	void* original_memory = memory;
	CHECK(memory);
	recording::fail_allocation = true;
	CHECK(row.deleteNoteByIndex(0, &action, 7, &clip, &memory) == Error::NONE);
	POINTERS_EQUAL(original_memory, memory);
	LONGS_EQUAL(0, row.notes.getNumElements());
	delugeDealloc(memory);
}
TEST(NoteSnapshotRecording, invalid_prepared_deletion_context_retains_caller_storage) {
	Note note = note_for_test(4);
	void* memory = Action::allocate_note_existence_memory();
	void* original_memory = memory;
	CHECK(action.recordNoteExistenceChange(nullptr, 7, &note, ExistenceChangeType::DELETE, nullptr, &memory)
	      == Error::BUG);
	POINTERS_EQUAL(original_memory, memory);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	delugeDealloc(memory);
}

TEST(NoteSnapshotRecording, parameter_recording_rejects_invalidated_action_at_both_boundaries) {
	ModelStackWithAutoParam stack;
	for (bool during_snapshot : {false, true}) {
		for (bool reuse_address : {false, true}) {
			Action replacement;
			actionLogger.firstAction[BEFORE] = &action;
			recording::on_allocate = recording::on_param_snapshot = {};
			auto invalidate = [&] {
				if (reuse_address)
					action.action_identity = deluge::model::next_action_identity();
				else
					actionLogger.firstAction[BEFORE] = &replacement;
			};
			if (during_snapshot)
				recording::on_param_snapshot = invalidate;
			else
				recording::on_allocate = invalidate;
			int allocated_before = recording::allocated, freed_before = recording::freed;
			CHECK_FALSE(action.recordParamChangeDefinitely(&stack, false));
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			POINTERS_EQUAL(nullptr, replacement.firstConsequence);
			LONGS_EQUAL(0, action.snapshot_failures);
			LONGS_EQUAL(recording::allocated - allocated_before, recording::freed - freed_before);
		}
	}
}
TEST(NoteSnapshotRecording, parameter_recording_distinguishes_allocation_failure_from_invalidation) {
	ModelStackWithAutoParam stack;
	recording::fail_allocation = true;
	CHECK_FALSE(action.recordParamChangeDefinitely(&stack, false));
	LONGS_EQUAL(1, action.snapshot_failures);
	LONGS_EQUAL(0, recording::param_snapshots);
	Action replacement;
	actionLogger.firstAction[BEFORE] = &action;
	recording::on_allocate = [&] { actionLogger.firstAction[BEFORE] = &replacement; };
	CHECK_FALSE(action.recordParamChangeDefinitely(&stack, false));
	LONGS_EQUAL(1, action.snapshot_failures);
	LONGS_EQUAL(0, replacement.snapshot_failures);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}
TEST(NoteSnapshotRecording, parameter_recording_rejects_owner_change_during_snapshot) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> callback_scope;
	ModelStackWithAutoParam stack;
	recording::on_param_snapshot = [&] { callback_scope.emplace(Id::Remote); };
	CHECK_FALSE(action.recordParamChangeDefinitely(&stack, false));
	LONGS_EQUAL(1, recording::param_snapshots_destroyed);
	LONGS_EQUAL(0, action.snapshot_failures);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	callback_scope.reset();
}

TEST(NoteSnapshotRecording, parameter_recording_handles_ordinary_snapshot_failure_and_retry) {
	ModelStackWithAutoParam stack;
	recording::param_snapshot_valid = false;
	CHECK_FALSE(action.recordParamChangeDefinitely(&stack, false));
	LONGS_EQUAL(1, action.snapshot_failures);
	LONGS_EQUAL(1, recording::param_snapshots_destroyed);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	recording::param_snapshot_valid = true;
	CHECK(action.recordParamChangeDefinitely(&stack, false));
	CHECK(action.firstConsequence);
}
TEST(NoteSnapshotRecording, parameter_recording_does_not_access_destroyed_action) {
	ModelStackWithAutoParam stack;
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_param_snapshot = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	CHECK_FALSE(target_action->recordParamChangeDefinitely(&stack, false));
	LONGS_EQUAL(1, recording::param_snapshots_destroyed);
}
TEST(NoteSnapshotRecording, parameter_recording_rejects_structural_change_before_using_model_stack) {
	ModelStackWithAutoParam stack;
	recording::on_allocate = [] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
	};
	CHECK_FALSE(action.recordParamChangeDefinitely(&stack, false));
	LONGS_EQUAL(0, recording::param_snapshots);
	LONGS_EQUAL(0, action.snapshot_failures);
}

TEST(NoteSnapshotRecording, array_recording_rejects_action_replacement_at_each_allocation_boundary) {
	for (bool during_clone : {false, true}) {
		for (bool fail : {false, true}) {
			row.notes.entries = {note_for_test(4)};
			Action replacement;
			actionLogger.firstAction[BEFORE] = &action;
			recording::on_allocate = {};
			NoteVector::on_clone = {};
			NoteVector::on_clone_attempt = {};
			recording::fail_allocation = !during_clone && fail;
			NoteVector::fail_clone = during_clone && fail;
			auto replace = [&] { actionLogger.firstAction[BEFORE] = &replacement; };
			if (during_clone)
				NoteVector::on_clone_attempt = replace;
			else
				recording::on_allocate = replace;
			int allocated_before = recording::allocated;
			int freed_before = recording::freed;
			CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			POINTERS_EQUAL(nullptr, replacement.firstConsequence);
			LONGS_EQUAL(1, row.notes.getNumElements());
			LONGS_EQUAL(recording::allocated - allocated_before, recording::freed - freed_before);
		}
	}
}
TEST(NoteSnapshotRecording, array_recording_rejects_identity_reuse_at_each_boundary) {
	for (bool during_clone : {false, true}) {
		row.notes.entries = {note_for_test(4)};
		actionLogger.firstAction[BEFORE] = &action;
		recording::on_allocate = {};
		NoteVector::on_clone = {};
		auto replace = [&] { action.action_identity = deluge::model::next_action_identity(); };
		if (during_clone)
			NoteVector::on_clone = replace;
		else
			recording::on_allocate = replace;
		CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(1, row.notes.getNumElements());
	}
}
TEST(NoteSnapshotRecording, array_recording_does_not_access_action_destroyed_during_clone) {
	row.notes.entries = {note_for_test(4)};
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	NoteVector::on_clone = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	int allocated_before = recording::allocated;
	int freed_before = recording::freed;
	CHECK(target_action->recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
	LONGS_EQUAL(recording::allocated - allocated_before, recording::freed - freed_before);
	LONGS_EQUAL(1, row.notes.getNumElements());
}
TEST(NoteSnapshotRecording, array_recording_rejects_owner_change_during_clone) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> callback_scope;
	row.notes.entries = {note_for_test(4)};
	NoteVector::on_clone = [&] { callback_scope.emplace(Id::Remote); };
	CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(1, row.notes.getNumElements());
	callback_scope.reset();
}

TEST(NoteSnapshotRecording, note_recording_rejects_action_replacement_even_when_allocation_fails) {
	for (bool fail : {false, true}) {
		row.notes.entries = {note_for_test(4)};
		Action replacement;
		actionLogger.firstAction[BEFORE] = &action;
		recording::fail_allocation = fail;
		recording::on_allocate = [&] { actionLogger.firstAction[BEFORE] = &replacement; };
		int freed_before = recording::freed;
		Note* recorded = row.notes.getElement(0);
		CHECK(action.recordNoteExistenceChange(&clip, 7, recorded, ExistenceChangeType::CREATE, &recorded)
		      == Error::BUG);
		POINTERS_EQUAL(nullptr, recorded);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		POINTERS_EQUAL(nullptr, replacement.firstConsequence);
		LONGS_EQUAL(1, row.notes.getNumElements());
		LONGS_EQUAL(freed_before + (fail ? 0 : 1), recording::freed);
	}
}
TEST(NoteSnapshotRecording, note_recording_rejects_action_identity_reuse) {
	row.notes.entries = {note_for_test(4)};
	actionLogger.firstAction[BEFORE] = &action;
	recording::on_allocate = [&] { action.action_identity = deluge::model::next_action_identity(); };
	CHECK(row.deleteNoteByIndex(0, &action, 7, &clip) == Error::BUG);
	LONGS_EQUAL(1, row.notes.getNumElements());
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}
TEST(NoteSnapshotRecording, note_recording_does_not_access_action_destroyed_by_allocation) {
	Note note = note_for_test(4);
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	int freed_before = recording::freed;
	CHECK(target_action->recordNoteExistenceChange(&clip, 7, &note, ExistenceChangeType::DELETE) == Error::BUG);
	LONGS_EQUAL(freed_before + 1, recording::freed);
}
TEST(NoteSnapshotRecording, note_recording_rejects_ui_owner_change) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> callback_scope;
	Note note = note_for_test(4);
	recording::on_allocate = [&] { callback_scope.emplace(Id::Remote); };
	CHECK(action.recordNoteExistenceChange(&clip, 7, &note, ExistenceChangeType::DELETE) == Error::BUG);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	callback_scope.reset();
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
			// Upstream #4943 retains only the matching loop for FIRST/LAST.
			// Flattening (repeats == 0) treats the original loop as the sole full loop.
			LONGS_EQUAL(2, row.notes.entries.size());
			LONGS_EQUAL(2, row.notes.entries[0].pos);
			LONGS_EQUAL(repeats == 0 ? 8 : (pingpong ? 23 : 24), row.notes.entries[1].pos);
			CHECK_TRUE(row.notes.entries[0].iterance == (Iterance{0, 1}));
			CHECK_TRUE(row.notes.entries[1].iterance == (Iterance{0, 2}));
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
			LONGS_EQUAL(1, row.notes.entries.size());
			LONGS_EQUAL(condition == 1 ? 0 : 16, row.notes.entries[0].pos);
			LONGS_EQUAL(16, row.notes.entries[0].length);
			CHECK_TRUE(row.notes.entries[0].iterance == (Iterance{0, condition}));
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

TEST(NoteSnapshotRecording, parameter_snapshot_once_keeps_nested_snapshot_at_both_boundaries) {
	ModelStackWithAutoParam stack;
	for (bool during_clone : {false, true}) {
		for (bool fail_outer : {false, true}) {
			stack.paramId++;
			const int outstanding_before = recording::allocated - recording::freed;
			bool entered = false;
			Consequence* nested_snapshot = nullptr;
			auto nested_edit = [&] {
				if (entered)
					return;
				entered = true;
				CHECK(action.recordParamChangeDefinitely(&stack, false));
				nested_snapshot = action.firstConsequence;
				if (during_clone)
					recording::param_snapshot_valid = !fail_outer;
				else
					recording::fail_allocation = fail_outer;
			};
			if (during_clone)
				recording::on_param_snapshot = nested_edit;
			else
				recording::on_allocate = nested_edit;
			CHECK(action.recordParamChangeIfNotAlreadySnapshotted(&stack, false));
			POINTERS_EQUAL(nested_snapshot, action.firstConsequence);
			LONGS_EQUAL(outstanding_before + 1, recording::allocated - recording::freed);
			LONGS_EQUAL(0, action.snapshot_failures);
			recording::on_allocate = recording::on_param_snapshot = {};
			recording::fail_allocation = false;
			recording::param_snapshot_valid = true;
		}
	}
}
TEST(NoteSnapshotRecording, parameter_definite_snapshot_preserves_explicit_duplicates) {
	ModelStackWithAutoParam stack;
	CHECK(action.recordParamChangeDefinitely(&stack, false));
	auto* first = action.firstConsequence;
	CHECK(action.recordParamChangeDefinitely(&stack, false));
	POINTERS_EQUAL(first, action.firstConsequence->next);
}
TEST(NoteSnapshotRecording, parameter_nested_snapshot_still_clears_requested_source) {
	test_auto_param parameter;
	ModelStackWithAutoParam stack{nullptr, 0, &parameter};
	bool entered = false;
	recording::on_allocate = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordParamChangeDefinitely(&stack, false));
	};
	CHECK(action.recordParamChangeIfNotAlreadySnapshotted(&stack, true));
	LONGS_EQUAL(1, parameter.nodes.emptied);
	LONGS_EQUAL(1, recording::param_snapshots);
}
TEST(NoteSnapshotRecording, parameter_existing_scalar_snapshot_accepts_steal_without_automation) {
	ModelStackWithAutoParam stack;
	CHECK(action.recordParamChangeDefinitely(&stack, false));
	CHECK(action.recordParamChangeIfNotAlreadySnapshotted(&stack, true));
	LONGS_EQUAL(1, recording::param_snapshots);
}

TEST(NoteSnapshotRecording, parameter_snapshot_once_does_not_suppress_a_different_parameter) {
	ModelStackWithAutoParam stack;
	ModelStackWithAutoParam other_stack;
	other_stack.paramId = 1;
	bool entered = false;
	recording::on_param_snapshot = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordParamChangeDefinitely(&other_stack, false));
	};
	CHECK(action.recordParamChangeIfNotAlreadySnapshotted(&stack, false));
	CHECK(action.containsConsequenceParamChange(nullptr, 0));
	CHECK(action.containsConsequenceParamChange(nullptr, 1));
	LONGS_EQUAL(2, recording::allocated - recording::freed);
}

TEST(NoteSnapshotRecording, array_snapshot_once_preserves_nested_snapshot_at_both_boundaries) {
	row.notes.entries = {note_for_test(4)};
	for (bool during_clone : {false, true}) {
		for (bool fail_outer : {false, true}) {
			// Each iteration uses a fresh row identity so earlier snapshots cannot satisfy it.
			row.undo_identity = deluge::model::next_note_row_identity();
			const int outstanding_before = recording::allocated - recording::freed;
			bool entered = false;
			Consequence* nested_snapshot = nullptr;
			auto nested_edit = [&] {
				if (entered)
					return;
				entered = true;
				CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
				nested_snapshot = action.firstConsequence;
				if (during_clone)
					NoteVector::fail_clone = fail_outer;
				else
					recording::fail_allocation = fail_outer;
			};
			if (during_clone)
				NoteVector::on_clone_attempt = nested_edit;
			else
				recording::on_allocate = nested_edit;
			CHECK(action.recordNoteArrayChangeIfNotAlreadySnapshotted(&clip, 7, &row.notes, false) == Error::NONE);
			POINTERS_EQUAL(nested_snapshot, action.firstConsequence);
			LONGS_EQUAL(outstanding_before + 1, recording::allocated - recording::freed);
			LONGS_EQUAL(1, row.notes.getNumElements());
			recording::on_allocate = {};
			NoteVector::on_clone_attempt = {};
			recording::fail_allocation = NoteVector::fail_clone = false;
		}
	}
}
TEST(NoteSnapshotRecording, array_definite_snapshot_preserves_explicit_duplicates) {
	row.notes.entries = {note_for_test(4)};
	CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	auto* first = action.firstConsequence;
	CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	POINTERS_EQUAL(first, action.firstConsequence->next);
}

TEST(NoteSnapshotRecording, array_nested_snapshot_does_not_steal_source_twice) {
	row.notes.entries = {note_for_test(4)};
	bool entered = false;
	recording::on_allocate = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
	};
	CHECK(action.recordNoteArrayChangeIfNotAlreadySnapshotted(&clip, 7, &row.notes, true) == Error::NONE);
	LONGS_EQUAL(1, row.notes.getNumElements());
	LONGS_EQUAL(1, recording::allocated - recording::freed);
}

TEST(NoteSnapshotRecording, array_nested_snapshot_does_not_override_context_invalidation) {
	row.notes.entries = {note_for_test(4)};
	bool entered = false;
	NoteVector::on_clone_attempt = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordNoteArrayChangeDefinitely(&clip, 7, &row.notes, false) == Error::NONE);
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
	};
	CHECK(action.recordNoteArrayChangeIfNotAlreadySnapshotted(&clip, 7, &row.notes, false) == Error::BUG);
	LONGS_EQUAL(1, recording::allocated - recording::freed);
	LONGS_EQUAL(1, row.notes.getNumElements());
}

TEST(LengthHistoryRecording, nested_snapshot_satisfies_failed_outer_allocation) {
	bool entered = false;
	recording::on_allocate = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordClipLengthChange(&clip, 24));
		recording::fail_allocation = true;
	};
	CHECK(action.recordClipLengthChange(&clip, 48));
	LONGS_EQUAL(1, recording::allocated);
	LONGS_EQUAL(0, recording::freed);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next);
	LONGS_EQUAL(24, static_cast<ConsequenceClipLength*>(action.firstConsequence)->lengthToRevertTo);
}

TEST(LengthHistoryRecording, nested_snapshot_cannot_mask_invalidated_target_on_failed_allocation) {
	bool entered = false;
	recording::on_allocate = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordClipLengthChange(&clip, 24));
		++clip.loopLength;
		recording::fail_allocation = true;
	};
	CHECK_FALSE(action.recordClipLengthChange(&clip, 48));
	LONGS_EQUAL(1, recording::allocated);
	LONGS_EQUAL(0, recording::freed);
	LONGS_EQUAL(24, static_cast<ConsequenceClipLength*>(action.firstConsequence)->lengthToRevertTo);
}

TEST(LengthHistoryRecording, unrelated_nested_snapshot_does_not_satisfy_failed_allocation) {
	Clip other_clip;
	bool entered = false;
	recording::on_allocate = [&] {
		if (entered)
			return;
		entered = true;
		CHECK(action.recordClipLengthChange(&other_clip, 24));
		recording::fail_allocation = true;
	};
	CHECK_FALSE(action.recordClipLengthChange(&clip, 48));
	LONGS_EQUAL(1, recording::allocated);
	POINTERS_EQUAL(&other_clip, static_cast<ConsequenceClipLength*>(action.firstConsequence)->clip);
}

TEST(ClipExistenceRecording, allocation_invalidation_never_begins_deletion) {
	using namespace deluge::gui::ui_session;
	for (bool fail_allocation : {false, true}) {
		for (int change = 0; change < 7; ++change) {
			currentSong = &song;
			song.registered = {&clip};
			clip.type = ClipType::INSTRUMENT;
			actionLogger.firstAction[BEFORE] = &action;
			recording::fail_allocation = fail_allocation;
			recording::on_allocate = [&] {
				switch (change) {
				case 0:
					currentSong = nullptr;
					break;
				case 1:
					song.registered.clear();
					break;
				case 2:
					actionLogger.firstAction[BEFORE] = nullptr;
					break;
				case 3:
					action.action_identity = deluge::model::next_action_identity();
					break;
				case 4:
					navigation.for_owner(Id::Local).structural_refresh.request();
					break;
				case 5:
					navigation.for_owner(Id::Remote).structural_refresh.request();
					break;
				case 6:
					clip.type = ClipType::AUDIO;
					break;
				}
			};
			CHECK_FALSE(
			    action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
			LONGS_EQUAL(0, recording::reverted);
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			LONGS_EQUAL(recording::allocated, recording::freed);
			LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
			LONGS_EQUAL(9, action.xScrollClip[AFTER]);
		}
	}
}
TEST(ClipExistenceRecording, allocation_can_destroy_registered_target_without_access_afterward) {
	auto* target_clip = new Clip;
	song.registered = {target_clip};
	recording::on_allocate = [&] {
		song.registered.clear();
		delete target_clip;
	};
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, target_clip, ExistenceChangeType::DELETE));
	LONGS_EQUAL(0, recording::reverted);
}
TEST(ClipExistenceRecording, allocation_can_destroy_registered_action_without_access_afterward) {
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	CHECK_FALSE(
	    target_action->recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	LONGS_EQUAL(0, recording::reverted);
}

TEST(ClipExistenceRecording, owner_switch_during_allocation_rejects_recording) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	recording::on_allocate = [&] { remote.emplace(Id::Remote); };
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	remote.reset();
}
TEST(ClipExistenceRecording, changed_output_during_allocation_rejects_deletion) {
	song.sessionClips.values = {&clip};
	Output replacement;
	recording::on_allocate = [&] { clip.output = &replacement; };
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	LONGS_EQUAL(0, recording::reverted);
}
TEST(ClipExistenceRecording, missing_target_is_rejected_before_allocation) {
	CHECK_FALSE(action.recordClipExistenceChange(nullptr, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	CHECK_FALSE(action.recordClipExistenceChange(&song, nullptr, &clip, ExistenceChangeType::CREATE));
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, nullptr, ExistenceChangeType::CREATE));
	LONGS_EQUAL(0, recording::allocated);
}

TEST_GROUP(InstanceHistoryRecording) {
	Song song;
	Output output;
	Action action;
	void setup() override {
		currentSong = &song;
		song.firstOutput = &output;
		output.clipInstances.entries = {{4, 16, nullptr}};
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
			consequence->~Consequence();
			delugeDealloc(consequence);
		}
		LONGS_EQUAL(recording::allocated, recording::freed);
		currentSong = nullptr;
		actionLogger.firstAction[BEFORE] = nullptr;
	}
};
TEST(InstanceHistoryRecording, unchanged_empty_instance_records_and_allocation_failure_retries) {
	auto* instance = output.clipInstances.getElement(0);
	recording::fail_allocation = true;
	CHECK_FALSE(action.recordClipInstanceExistenceChange(&output, instance, ExistenceChangeType::CREATE));
	recording::fail_allocation = false;
	CHECK(action.recordClipInstanceExistenceChange(&output, instance, ExistenceChangeType::CREATE));
	auto& snapshot = *static_cast<ConsequenceClipInstanceExistence*>(action.firstConsequence);
	LONGS_EQUAL(4, snapshot.pos);
	LONGS_EQUAL(16, snapshot.length);
	POINTERS_EQUAL(nullptr, snapshot.clip);
}
TEST(InstanceHistoryRecording, callbacks_cannot_publish_changed_instance_or_context) {
	using namespace deluge::gui::ui_session;
	Clip replacement;
	for (bool failed_allocation : {false, true}) {
		for (int change = 0; change < 9; ++change) {
			currentSong = &song;
			song.firstOutput = &output;
			actionLogger.firstAction[BEFORE] = &action;
			output.clipInstances.entries = {{4, 16, nullptr}};
			auto* instance = output.clipInstances.getElement(0);
			recording::fail_allocation = failed_allocation;
			recording::on_allocate = [&] {
				switch (change) {
				case 0:
					currentSong = nullptr;
					break;
				case 1:
					song.firstOutput = nullptr;
					break;
				case 2:
					actionLogger.firstAction[BEFORE] = nullptr;
					break;
				case 3:
					action.action_identity = deluge::model::next_action_identity();
					break;
				case 4:
					navigation.for_owner(Id::Remote).structural_refresh.request();
					break;
				case 5:
					output.clipInstances.entries.clear();
					break;
				case 6:
					output.clipInstances.entries[0].pos++;
					break;
				case 7:
					output.clipInstances.entries[0].length++;
					break;
				case 8:
					output.clipInstances.entries[0].clip = &replacement;
					break;
				}
			};
			CHECK_FALSE(action.recordClipInstanceExistenceChange(&output, instance, ExistenceChangeType::DELETE));
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			LONGS_EQUAL(recording::allocated, recording::freed);
		}
	}
}
TEST(InstanceHistoryRecording, allocation_relocates_instance_storage_without_reading_old_pointer) {
	auto* instance = output.clipInstances.getElement(0);
	recording::on_allocate = [&] { output.clipInstances.entries.reserve(output.clipInstances.entries.capacity() + 8); };
	CHECK_FALSE(action.recordClipInstanceExistenceChange(&output, instance, ExistenceChangeType::DELETE));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(InstanceHistoryRecording, destroyed_registered_action_is_not_accessed_after_allocation) {
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	CHECK_FALSE(target_action->recordClipInstanceExistenceChange(&output, output.clipInstances.getElement(0),
	                                                             ExistenceChangeType::DELETE));
}
TEST(InstanceHistoryRecording, destroyed_output_is_not_accessed_after_allocation) {
	auto* target_output = new Output;
	target_output->clipInstances.entries = {{4, 16, nullptr}};
	song.firstOutput = target_output;
	auto* instance = target_output->clipInstances.getElement(0);
	recording::on_allocate = [&] {
		song.firstOutput = nullptr;
		delete target_output;
	};
	CHECK_FALSE(action.recordClipInstanceExistenceChange(target_output, instance, ExistenceChangeType::DELETE));
}
TEST(InstanceHistoryRecording, owner_switch_rejects_recording) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	recording::on_allocate = [&] { remote.emplace(Id::Remote); };
	CHECK_FALSE(action.recordClipInstanceExistenceChange(&output, output.clipInstances.getElement(0),
	                                                     ExistenceChangeType::CREATE));
	remote.reset();
}

TEST(InstanceHistoryRecording, registered_clip_removed_during_allocation_rejects_recording) {
	auto* clip = new Clip;
	song.registered = {clip};
	output.clipInstances.entries[0].clip = clip;
	recording::on_allocate = [&] {
		song.registered.clear();
		delete clip;
	};
	CHECK_FALSE(action.recordClipInstanceExistenceChange(&output, output.clipInstances.getElement(0),
	                                                     ExistenceChangeType::DELETE));
}
TEST(InstanceHistoryRecording, cross_output_move_can_record_before_clip_output_is_updated) {
	Clip clip;
	Output previous_output;
	clip.output = &previous_output;
	song.registered = {&clip};
	output.clipInstances.entries[0].clip = &clip;
	CHECK(action.recordClipInstanceExistenceChange(&output, output.clipInstances.getElement(0),
	                                               ExistenceChangeType::CREATE));
	POINTERS_EQUAL(&clip, static_cast<ConsequenceClipInstanceExistence*>(action.firstConsequence)->clip);
}

TEST(InstanceHistoryRecording, deletion_stops_when_recording_cannot_allocate) {
	ArrangerView view;
	recording::fail_allocation = true;
	CHECK_FALSE(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(0, song.deleted_instance_notifications);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}
TEST(InstanceHistoryRecording, deletion_stops_after_recording_relocates_instance) {
	ArrangerView view;
	auto* instance = output.clipInstances.getElement(0);
	recording::on_allocate = [&] { output.clipInstances.entries.reserve(output.clipInstances.entries.capacity() + 8); };
	CHECK_FALSE(view.delete_clip_instance(&output, 0, instance, &action));
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(0, song.deleted_instance_notifications);
}
TEST(InstanceHistoryRecording, deletion_does_not_access_output_destroyed_during_recording) {
	ArrangerView view;
	auto* target_output = new Output;
	target_output->clipInstances.entries = {{4, 16, nullptr}};
	song.firstOutput = target_output;
	auto* instance = target_output->clipInstances.getElement(0);
	recording::on_allocate = [&] {
		song.firstOutput = nullptr;
		delete target_output;
	};
	CHECK_FALSE(view.delete_clip_instance(target_output, 0, instance, &action));
	LONGS_EQUAL(0, song.deleted_instance_notifications);
}
TEST(InstanceHistoryRecording, successful_deletion_records_history_and_notifies_song) {
	ArrangerView view;
	CHECK(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	CHECK(output.clipInstances.entries.empty());
	LONGS_EQUAL(1, song.deleted_instance_notifications);
	CHECK(action.firstConsequence);
}
TEST(InstanceHistoryRecording, deletion_without_action_and_cleanup_failure_are_reported) {
	ArrangerView view;
	song.deletion_result = Error::BUG;
	CHECK_FALSE(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), nullptr));
	CHECK(output.clipInstances.entries.empty());
	LONGS_EQUAL(1, song.deleted_instance_notifications);
	LONGS_EQUAL(0, recording::allocated);
}
TEST(InstanceHistoryRecording, stale_index_is_rejected_before_recording) {
	ArrangerView view;
	CHECK_FALSE(view.delete_clip_instance(&output, 1, output.clipInstances.getElement(0), &action));
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(0, song.deleted_instance_notifications);
	LONGS_EQUAL(0, recording::allocated);
}

TEST(InstanceHistoryRecording, instance_change_rejects_relocation_without_mutation) {
	auto* instance = output.clipInstances.getElement(0);
	recording::on_allocate = [&] { output.clipInstances.entries.reserve(output.clipInstances.entries.capacity() + 8); };
	CHECK_FALSE(instance->change(&action, &output, 8, 32, nullptr));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(16, output.clipInstances.entries[0].length);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}
TEST(InstanceHistoryRecording, instance_change_valid_snapshot_records_before_and_after) {
	auto* instance = output.clipInstances.getElement(0);
	CHECK(instance->change(&action, &output, 8, 32, nullptr));
	auto* consequence = static_cast<ConsequenceClipInstanceChange*>(action.firstConsequence);
	LONGS_EQUAL(4, consequence->pos[BEFORE]);
	LONGS_EQUAL(16, consequence->length[BEFORE]);
	LONGS_EQUAL(8, consequence->pos[AFTER]);
	LONGS_EQUAL(32, instance->length);
}
TEST(InstanceHistoryRecording, instance_change_preserves_strict_and_optional_snapshot_failure_behavior) {
	auto* instance = output.clipInstances.getElement(0);
	recording::fail_allocation = true;
	action.require_complete_snapshots = true;
	CHECK_FALSE(instance->change(&action, &output, 8, 32, nullptr));
	LONGS_EQUAL(4, instance->pos);
	LONGS_EQUAL(1, action.snapshot_failures);
	action.require_complete_snapshots = false;
	CHECK(instance->change(&action, &output, 8, 32, nullptr));
	LONGS_EQUAL(8, instance->pos);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}
TEST(InstanceHistoryRecording, instance_change_does_not_access_destroyed_action) {
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	CHECK_FALSE(output.clipInstances.getElement(0)->change(target_action, &output, 8, 32, nullptr));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
}
TEST(InstanceHistoryRecording, instance_change_does_not_access_destroyed_output) {
	auto* target_output = new Output;
	target_output->clipInstances.entries = {{4, 16, nullptr}};
	song.firstOutput = target_output;
	auto* instance = target_output->clipInstances.getElement(0);
	recording::on_allocate = [&] {
		song.firstOutput = nullptr;
		delete target_output;
	};
	CHECK_FALSE(instance->change(&action, target_output, 8, 32, nullptr));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(InstanceHistoryRecording, instance_change_rejects_context_and_payload_changes_even_on_allocation_failure) {
	using namespace deluge::gui::ui_session;
	for (bool failed_allocation : {false, true}) {
		for (int change = 0; change < 6; ++change) {
			currentSong = &song;
			actionLogger.firstAction[BEFORE] = &action;
			output.clipInstances.entries = {{4, 16, nullptr}};
			recording::fail_allocation = failed_allocation;
			recording::on_allocate = [&] {
				switch (change) {
				case 0:
					currentSong = nullptr;
					break;
				case 1:
					action.action_identity = deluge::model::next_action_identity();
					break;
				case 2:
					navigation.for_owner(Id::Local).structural_refresh.request();
					break;
				case 3:
					navigation.for_owner(Id::Remote).structural_refresh.request();
					break;
				case 4:
					output.clipInstances.entries[0].length = 19;
					break;
				case 5:
					output.clipInstances.entries.clear();
					break;
				}
			};
			CHECK_FALSE(output.clipInstances.getElement(0)->change(&action, &output, 8, 32, nullptr));
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			LONGS_EQUAL(recording::allocated, recording::freed);
			LONGS_EQUAL(0, action.snapshot_failures);
		}
	}
}
TEST(InstanceHistoryRecording, instance_change_rejects_deleted_replacement_clip) {
	auto* replacement = new Clip;
	song.registered = {replacement};
	recording::on_allocate = [&] {
		song.registered.clear();
		delete replacement;
	};
	CHECK_FALSE(output.clipInstances.getElement(0)->change(&action, &output, 8, 32, replacement));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	POINTERS_EQUAL(nullptr, output.clipInstances.entries[0].clip);
}
TEST(InstanceHistoryRecording, instance_change_without_action_does_not_allocate) {
	CHECK(output.clipInstances.getElement(0)->change(nullptr, &output, 8, 32, nullptr));
	LONGS_EQUAL(0, recording::allocated);
	LONGS_EQUAL(8, output.clipInstances.entries[0].pos);
}
TEST(InstanceHistoryRecording, instance_change_rejects_owner_switch) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	recording::on_allocate = [&] { remote.emplace(Id::Remote); };
	CHECK_FALSE(output.clipInstances.getElement(0)->change(&action, &output, 8, 32, nullptr));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	remote.reset();
}

TEST(InstanceHistoryRecording, forward_batch_shift_preserves_final_positions) {
	ArrangerView view;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	CHECK(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(20, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(24, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(28, output.clipInstances.entries[2].pos);
	LONGS_EQUAL(3, recording::allocated);
}
TEST(InstanceHistoryRecording, batch_shift_reports_first_and_later_recording_failures) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	recording::fail_allocation = true;
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	recording::fail_allocation = false;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 2; };
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[2].pos);
	LONGS_EQUAL(2, attempts);
}
TEST(InstanceHistoryRecording, batch_contraction_stops_on_deletion_snapshot_failure) {
	ArrangerView view;
	output.clipInstances.entries = {{4, 2, nullptr}, {12, 2, nullptr}};
	recording::fail_allocation = true;
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	LONGS_EQUAL(0, song.deleted_instance_notifications);
	LONGS_EQUAL(12, output.clipInstances.entries[1].pos);
}
TEST(InstanceHistoryRecording, batch_shift_rejects_relocation_without_accessing_retained_instance) {
	ArrangerView view;
	output.clipInstances.entries = {{4, 2, nullptr}, {12, 2, nullptr}};
	recording::on_allocate = [&] { output.clipInstances.entries.reserve(output.clipInstances.entries.capacity() + 8); };
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[1].pos);
}

TEST(InstanceHistoryRecording, expansion_preserves_order_at_every_allocation_and_failure_boundary) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	for (int failed_attempt = 1; failed_attempt <= 4; ++failed_attempt) {
		output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}, {16, 2, nullptr}};
		int attempts = 0;
		recording::on_allocate = [&] {
			for (size_t index = 1; index < output.clipInstances.entries.size(); ++index) {
				auto& previous = output.clipInstances.entries[index - 1];
				CHECK(previous.pos + previous.length <= output.clipInstances.entries[index].pos);
			}
			recording::fail_allocation = ++attempts == failed_attempt;
		};
		CHECK_FALSE(view.shift_clips_horizontally(1, 24, &action));
		LONGS_EQUAL(failed_attempt, attempts);
		for (int index = 0; index < 4; ++index) {
			const int expected_pos = (index + 1) * 4;
			LONGS_EQUAL(expected_pos, output.clipInstances.entries[index].pos);
		}
	}
}
TEST(InstanceHistoryRecording, expansion_only_moves_instances_at_or_after_cursor) {
	ArrangerView view;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	CHECK(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(24, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(28, output.clipInstances.entries[2].pos);
	LONGS_EQUAL(2, recording::allocated);
}
TEST(InstanceHistoryRecording, expansion_stops_before_processing_later_outputs_on_failure) {
	ArrangerView view;
	Output later_output;
	output.next = &later_output;
	later_output.clipInstances.entries = {{4, 2, nullptr}};
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 2; };
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(4, later_output.clipInstances.entries[0].pos);
	output.next = nullptr;
}
TEST(InstanceHistoryRecording, instance_change_still_accepts_retained_storage_in_unsorted_array) {
	output.clipInstances.entries = {{20, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	CHECK(output.clipInstances.getElement(1)->change(&action, &output, 24, 2, nullptr));
	LONGS_EQUAL(24, output.clipInstances.entries[1].pos);
}

TEST(InstanceHistoryRecording, instance_undo_and_redo_reject_later_length_edits) {
	auto* instance = output.clipInstances.getElement(0);
	CHECK(instance->change(&action, &output, 8, 32, nullptr));
	ModelStack stack;
	stack.song = &song;
	instance->length = 40;
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(8, instance->pos);
	LONGS_EQUAL(40, instance->length);
	instance->length = 32;
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(4, instance->pos);
	LONGS_EQUAL(16, instance->length);
	instance->length = 19;
	CHECK(action.firstConsequence->revert(AFTER, &stack) == Error::BUG);
	LONGS_EQUAL(4, instance->pos);
	LONGS_EQUAL(19, instance->length);
	instance->length = 16;
	CHECK(action.firstConsequence->revert(AFTER, &stack) == Error::NONE);
	LONGS_EQUAL(8, instance->pos);
	LONGS_EQUAL(32, instance->length);
}
TEST(InstanceHistoryRecording, partial_expansion_history_can_undo_and_redo_completed_suffix) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 3; };
	CHECK(output.clipInstances.getElement(2)->change(&action, &output, 28, 2, nullptr));
	CHECK(output.clipInstances.getElement(1)->change(&action, &output, 24, 2, nullptr));
	CHECK_FALSE(output.clipInstances.getElement(0)->change(&action, &output, 20, 2, nullptr));
	recording::on_allocate = {};
	ModelStack stack;
	stack.song = &song;
	// Newest consequence first for undo; reverse that order for redo.
	auto* newest = action.firstConsequence;
	auto* oldest = newest->next;
	CHECK(newest->revert(BEFORE, &stack) == Error::NONE);
	CHECK(oldest->revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[2].pos);
	CHECK(oldest->revert(AFTER, &stack) == Error::NONE);
	CHECK(newest->revert(AFTER, &stack) == Error::NONE);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(24, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(28, output.clipInstances.entries[2].pos);
}
TEST(InstanceHistoryRecording, instance_undo_rejects_replaced_or_removed_target) {
	auto* instance = output.clipInstances.getElement(0);
	CHECK(instance->change(&action, &output, 8, 32, nullptr));
	ModelStack stack;
	stack.song = &song;
	Clip replacement;
	instance->clip = &replacement;
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(8, instance->pos);
	POINTERS_EQUAL(&replacement, instance->clip);
	output.clipInstances.entries.clear();
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::BUG);
}
TEST(InstanceHistoryRecording, instance_undo_rejects_unowned_output) {
	CHECK(output.clipInstances.getElement(0)->change(&action, &output, 8, 32, nullptr));
	ModelStack stack;
	stack.song = &song;
	song.firstOutput = nullptr;
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(8, output.clipInstances.entries[0].pos);
}

TEST(InstanceHistoryRecording, failed_expansion_restores_instances_and_preserves_older_history) {
	ArrangerView view;
	ModelStackWithAutoParam stack;
	CHECK(action.recordParamChangeDefinitely(&stack, false));
	auto* older_history = action.firstConsequence;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 3; };
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[2].pos);
	POINTERS_EQUAL(older_history, action.firstConsequence);
	CHECK(action.snapshot_error == Error::NONE);
	LONGS_EQUAL(1, recording::allocated - recording::freed);
}
TEST(InstanceHistoryRecording, failed_expansion_refuses_to_overwrite_changed_completed_instance) {
	ArrangerView view;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 3) {
			output.clipInstances.entries[2].length = 3;
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(24, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(28, output.clipInstances.entries[2].pos);
	LONGS_EQUAL(3, output.clipInstances.entries[2].length);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, recording::allocated - recording::freed);
}
TEST(InstanceHistoryRecording, failed_expansion_refuses_rollback_into_newly_occupied_space) {
	ArrangerView view;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}, {16, 2, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 3) {
			output.clipInstances.entries[0].length = 12;
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(28, output.clipInstances.entries[2].pos);
	LONGS_EQUAL(32, output.clipInstances.entries[3].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[0].length);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, recording::allocated - recording::freed);
}
TEST(InstanceHistoryRecording, expansion_rolls_back_completed_outputs_when_later_output_fails) {
	ArrangerView view;
	Output later_output;
	output.next = &later_output;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}};
	later_output.clipInstances.entries = {{4, 2, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 3; };
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(4, later_output.clipInstances.entries[0].pos);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	output.next = nullptr;
}

TEST(InstanceHistoryRecording, failed_expansion_preserves_nested_history_instead_of_rolling_it_back) {
	ArrangerView view;
	output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	ModelStackWithAutoParam stack;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			CHECK(action.recordParamChangeDefinitely(&stack, false));
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_clips_horizontally(1, 16, &action));
	LONGS_EQUAL(24, output.clipInstances.entries[1].pos);
	CHECK(action.containsConsequenceParamChange(nullptr, 0));
	LONGS_EQUAL(2, recording::allocated - recording::freed);
}

TEST(InstanceHistoryRecording, contraction_rolls_back_every_failed_shift_without_deletions) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	for (int failed_attempt = 1; failed_attempt <= 3; ++failed_attempt) {
		output.clipInstances.entries = {{4, 2, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
		int attempts = 0;
		recording::on_allocate = [&] { recording::fail_allocation = ++attempts == failed_attempt; };
		CHECK_FALSE(view.shift_clips_horizontally(-1, -2, &action));
		for (int index = 0; index < 3; ++index)
			LONGS_EQUAL((index + 1) * 4, output.clipInstances.entries[index].pos);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		CHECK(action.snapshot_error == Error::NONE);
		LONGS_EQUAL(recording::allocated, recording::freed);
	}
}
TEST(InstanceHistoryRecording, contraction_restores_shortened_predecessor_and_completed_moves) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	for (int failed_attempt = 1; failed_attempt <= 3; ++failed_attempt) {
		output.clipInstances.entries = {{4, 6, nullptr}, {11, 2, nullptr}, {16, 2, nullptr}};
		int attempts = 0;
		recording::on_allocate = [&] { recording::fail_allocation = ++attempts == failed_attempt; };
		CHECK_FALSE(view.shift_clips_horizontally(-1, -3, &action));
		LONGS_EQUAL(6, output.clipInstances.entries[0].length);
		LONGS_EQUAL(11, output.clipInstances.entries[1].pos);
		LONGS_EQUAL(16, output.clipInstances.entries[2].pos);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		CHECK(action.snapshot_error == Error::NONE);
	}
}
TEST(InstanceHistoryRecording, contraction_recovers_mixed_empty_deletion_and_move_failure) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{1, 1, nullptr}, {8, 2, nullptr}, {12, 2, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 3; };
	CHECK_FALSE(view.shift_clips_horizontally(-1, -2, &action));
	LONGS_EQUAL(3, output.clipInstances.entries.size());
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[2].pos);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	CHECK(action.snapshot_error == Error::NONE);
}

TEST(InstanceHistoryRecording, contraction_revalidates_pending_instance_after_predecessor_snapshot) {
	ArrangerView view;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	Clip replacement;
	for (int change = 0; change < 4; ++change) {
		output.clipInstances.entries = {{4, 6, nullptr}, {11, 2, nullptr}, {16, 2, nullptr}};
		int allocations = 0;
		recording::on_allocate = [&] {
			++allocations;
			switch (change) {
			case 0:
				output.clipInstances.entries.erase(output.clipInstances.entries.begin() + 1);
				break;
			case 1:
				output.clipInstances.entries[1].pos = 12;
				break;
			case 2:
				output.clipInstances.entries[1].length = 3;
				break;
			case 3:
				output.clipInstances.entries[1].clip = &replacement;
				break;
			}
		};
		CHECK_FALSE(view.shift_clips_horizontally(-1, -3, &action));
		LONGS_EQUAL(1, allocations);
		LONGS_EQUAL(4, output.clipInstances.entries[0].length);
		LONGS_EQUAL(change == 0 ? 16 : (change == 1 ? 12 : 11), output.clipInstances.entries[1].pos);
		if (change == 2)
			LONGS_EQUAL(3, output.clipInstances.entries[1].length);
		if (change == 3)
			POINTERS_EQUAL(&replacement, output.clipInstances.entries[1].clip);
	}
}
TEST(InstanceHistoryRecording, contraction_rejects_pending_instance_deleted_at_end_of_array) {
	ArrangerView view;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	output.clipInstances.entries = {{4, 6, nullptr}, {11, 2, nullptr}};
	recording::on_allocate = [&] { output.clipInstances.entries.pop_back(); };
	CHECK_FALSE(view.shift_clips_horizontally(-1, -3, &action));
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(4, output.clipInstances.entries[0].length);
}
TEST(InstanceHistoryRecording, contraction_rejects_pending_clip_destroyed_during_predecessor_snapshot) {
	ArrangerView view;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	auto* clip = new Clip;
	clip->output = &output;
	song.registered = {clip};
	output.clipInstances.entries = {{4, 6, nullptr}, {11, 2, clip}};
	recording::on_allocate = [&] {
		song.registered.clear();
		delete clip;
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -3, &action));
	LONGS_EQUAL(11, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(1, recording::allocated);
}
TEST(InstanceHistoryRecording, contraction_still_shortens_and_moves_unchanged_instances) {
	ArrangerView view;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	output.clipInstances.entries = {{4, 6, nullptr}, {11, 2, nullptr}, {16, 2, nullptr}};
	CHECK(view.shift_clips_horizontally(-1, -3, &action));
	LONGS_EQUAL(4, output.clipInstances.entries[0].length);
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(13, output.clipInstances.entries[2].pos);
	LONGS_EQUAL(3, recording::allocated);
}

TEST(InstanceHistoryRecording, existence_undo_rejects_resized_instance_and_supports_retry) {
	auto* instance = output.clipInstances.getElement(0);
	CHECK(action.recordClipInstanceExistenceChange(&output, instance, ExistenceChangeType::CREATE));
	ModelStack stack;
	stack.song = &song;
	instance->length = 20;
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::BUG);
	LONGS_EQUAL(20, instance->length);
	instance->length = 16;
	CHECK(action.firstConsequence->revert(BEFORE, &stack) == Error::NONE);
	CHECK(output.clipInstances.entries.empty());
	CHECK(action.firstConsequence->revert(AFTER, &stack) == Error::NONE);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(16, output.clipInstances.entries[0].length);
}
TEST(InstanceHistoryRecording, instance_recreation_rejects_neighbor_overlap_before_and_after_reservation) {
	ClipInstance original{8, 4, nullptr};
	ConsequenceClipInstanceExistence consequence(&output, &original, ExistenceChangeType::DELETE);
	ModelStack stack;
	stack.song = &song;
	for (bool during_reservation : {false, true}) {
		for (bool overlap_previous : {false, true}) {
			output.clipInstances.entries = {{4, 4, nullptr}, {12, 4, nullptr}};
			auto overlap = [&] {
				if (overlap_previous)
					output.clipInstances.entries[0].length = 5;
				else
					output.clipInstances.entries[1].pos = 11;
			};
			output.clipInstances.on_reserve = {};
			if (during_reservation)
				output.clipInstances.on_reserve = overlap;
			else
				overlap();
			CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
			LONGS_EQUAL(2, output.clipInstances.entries.size());
		}
	}
	output.clipInstances.on_reserve = {};
}
TEST(InstanceHistoryRecording, instance_recreation_allows_adjacent_neighbors_and_retries_allocation_failure) {
	ClipInstance original{8, 4, nullptr};
	ConsequenceClipInstanceExistence consequence(&output, &original, ExistenceChangeType::DELETE);
	ModelStack stack;
	stack.song = &song;
	output.clipInstances.entries = {{4, 4, nullptr}, {12, 4, nullptr}};
	output.clipInstances.fail_reservation = true;
	CHECK(consequence.revert(BEFORE, &stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	output.clipInstances.fail_reservation = false;
	CHECK(consequence.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(3, output.clipInstances.entries.size());
	LONGS_EQUAL(8, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(4, output.clipInstances.entries[1].length);
}
TEST(InstanceHistoryRecording, instance_recreation_rejects_owner_switch_during_reservation) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	ClipInstance original{8, 4, nullptr};
	ConsequenceClipInstanceExistence consequence(&output, &original, ExistenceChangeType::DELETE);
	ModelStack stack;
	stack.song = &song;
	output.clipInstances.entries.clear();
	output.clipInstances.on_reserve = [&] { remote.emplace(Id::Remote); };
	CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
	CHECK(output.clipInstances.entries.empty());
	remote.reset();
	output.clipInstances.on_reserve = {};
}

TEST(InstanceHistoryRecording, contraction_restores_empty_deletions_on_each_snapshot_failure) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	for (int failed_attempt = 1; failed_attempt <= 4; ++failed_attempt) {
		output.clipInstances.entries = {{1, 1, nullptr}, {3, 1, nullptr}, {5, 1, nullptr}, {12, 2, nullptr}};
		int attempts = 0;
		recording::on_allocate = [&] { recording::fail_allocation = ++attempts == failed_attempt; };
		CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
		LONGS_EQUAL(4, output.clipInstances.entries.size());
		LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
		LONGS_EQUAL(3, output.clipInstances.entries[1].pos);
		LONGS_EQUAL(5, output.clipInstances.entries[2].pos);
		LONGS_EQUAL(12, output.clipInstances.entries[3].pos);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		CHECK(action.snapshot_error == Error::NONE);
		LONGS_EQUAL(recording::allocated, recording::freed);
	}
}
TEST(InstanceHistoryRecording, empty_deletion_recovery_rejects_occupied_original_space) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	output.clipInstances.entries = {{4, 2, nullptr}, {9, 1, nullptr}, {11, 1, nullptr}, {20, 2, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 3) {
			output.clipInstances.entries[0].length = 6;
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	LONGS_EQUAL(6, output.clipInstances.entries[0].length);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
	CHECK(action.firstConsequence);
}
TEST(InstanceHistoryRecording, empty_deletion_recovery_requires_capacity_before_mutation) {
	output.clipInstances.entries = {{1, 1, nullptr}, {3, 1, nullptr}};
	ArrangerView view;
	CHECK(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	CHECK(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	std::vector<ClipInstance>().swap(output.clipInstances.entries);
	CHECK_FALSE(action.rollback_empty_instance_deletions(&song, nullptr, 2));
	CHECK(output.clipInstances.entries.empty());
	CHECK(action.firstConsequence);
}

TEST(InstanceHistoryRecording, empty_deletion_recovery_restores_prior_outputs_and_keeps_older_history) {
	ArrangerView view;
	ModelStackWithAutoParam stack;
	CHECK(action.recordParamChangeDefinitely(&stack, false));
	auto* older_history = action.firstConsequence;
	Output later_output;
	output.next = &later_output;
	output.clipInstances.entries = {{1, 1, nullptr}, {3, 1, nullptr}};
	later_output.clipInstances.entries = {{1, 1, nullptr}};
	action.require_complete_snapshots = true;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 3; };
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(3, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(1, later_output.clipInstances.entries.size());
	POINTERS_EQUAL(older_history, action.firstConsequence);
	CHECK(action.snapshot_error == Error::NONE);
	output.next = nullptr;
}
TEST(InstanceHistoryRecording, empty_deletion_recovery_refuses_clip_bearing_history) {
	Clip clip;
	output.clipInstances.entries = {{1, 1, &clip}};
	CHECK(action.recordClipInstanceExistenceChange(&output, output.clipInstances.getElement(0),
	                                               ExistenceChangeType::DELETE));
	output.clipInstances.delete_at_index_preserving_capacity(0);
	CHECK_FALSE(action.rollback_empty_instance_deletions(&song, nullptr, 1));
	CHECK(output.clipInstances.entries.empty());
	CHECK(action.firstConsequence);
}

TEST(InstanceHistoryRecording, clip_deletion_cleanup_cannot_leave_batch_using_destroyed_action) {
	ArrangerView view;
	Clip clip;
	output.clipInstances.entries = {{1, 1, &clip}};
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	song.on_instance_cleanup = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		while (target_action->firstConsequence) {
			auto* consequence = target_action->firstConsequence;
			target_action->firstConsequence = consequence->next;
			consequence->~Consequence();
			delugeDealloc(consequence);
		}
		delete target_action;
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, target_action));
	CHECK(output.clipInstances.entries.empty());
	LONGS_EQUAL(1, song.deleted_instance_notifications);
}
TEST(InstanceHistoryRecording, clip_deletion_cleanup_cannot_leave_batch_using_destroyed_output) {
	ArrangerView view;
	Clip clip;
	auto* target_output = new Output;
	target_output->clipInstances.entries = {{1, 1, &clip}};
	song.firstOutput = target_output;
	song.on_instance_cleanup = [&] {
		song.firstOutput = nullptr;
		delete target_output;
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(1, song.deleted_instance_notifications);
}

TEST(InstanceHistoryRecording, mixed_contraction_restores_deletion_shortening_and_moves_at_every_failure) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	for (int failed_attempt = 1; failed_attempt <= 4; ++failed_attempt) {
		output.clipInstances.entries = {{4, 5, nullptr}, {9, 1, nullptr}, {12, 2, nullptr}, {16, 2, nullptr}};
		int attempts = 0;
		recording::on_allocate = [&] { recording::fail_allocation = ++attempts == failed_attempt; };
		CHECK_FALSE(view.shift_clips_horizontally(-1, -4, &action));
		LONGS_EQUAL(4, output.clipInstances.entries.size());
		LONGS_EQUAL(5, output.clipInstances.entries[0].length);
		LONGS_EQUAL(9, output.clipInstances.entries[1].pos);
		LONGS_EQUAL(12, output.clipInstances.entries[2].pos);
		LONGS_EQUAL(16, output.clipInstances.entries[3].pos);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		CHECK(action.snapshot_error == Error::NONE);
	}
}
TEST(InstanceHistoryRecording, mixed_recovery_moves_survivor_out_of_deleted_instances_original_position) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{1, 1, nullptr}, {9, 1, nullptr}, {20, 1, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 3; };
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(3, output.clipInstances.entries.size());
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(9, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(20, output.clipInstances.entries[2].pos);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}

TEST(InstanceHistoryRecording, mixed_recovery_validates_capacity_before_reverting_any_moves) {
	ArrangerView view;
	output.clipInstances.entries = {{1, 1, nullptr}, {9, 1, nullptr}, {20, 1, nullptr}};
	CHECK(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	CHECK(output.clipInstances.getElement(0)->change(&action, &output, 1, 1, nullptr));
	std::vector<ClipInstance>(output.clipInstances.entries).swap(output.clipInstances.entries);
	CHECK_FALSE(action.rollback_instance_batch(&song, nullptr, 2, -8));
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	CHECK(action.firstConsequence->next);
}
TEST(InstanceHistoryRecording, mixed_recovery_refuses_changed_moved_instance_without_restoring_deletions) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{1, 1, nullptr}, {9, 1, nullptr}, {20, 1, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 3) {
			output.clipInstances.entries[0].length = 3;
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(3, output.clipInstances.entries[0].length);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, recording::allocated - recording::freed);
}
TEST(InstanceHistoryRecording, mixed_recovery_restores_multiple_outputs) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	Output later_output;
	output.next = &later_output;
	output.clipInstances.entries = {{1, 1, nullptr}, {9, 1, nullptr}};
	later_output.clipInstances.entries = {{2, 1, nullptr}, {20, 1, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 4; };
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(2, output.clipInstances.entries.size());
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(9, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(2, later_output.clipInstances.entries.size());
	LONGS_EQUAL(2, later_output.clipInstances.entries[0].pos);
	LONGS_EQUAL(20, later_output.clipInstances.entries[1].pos);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	output.next = nullptr;
}

TEST(InstanceHistoryRecording, time_insertion_records_requested_range) {
	song.test_scroll[NAVIGATION_ARRANGEMENT] = 12;
	CHECK(action.record_arranger_time_inserted(12, 8));
	auto* consequence = static_cast<ConsequenceArrangerParamsTimeInserted*>(action.firstConsequence);
	LONGS_EQUAL(12, consequence->pos);
	LONGS_EQUAL(8, consequence->length);
}
TEST(InstanceHistoryRecording, time_insertion_allocation_failure_stops_and_marks_strict_action) {
	action.require_complete_snapshots = true;
	recording::fail_allocation = true;
	CHECK_FALSE(action.record_arranger_time_inserted(0, 8));
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
}
TEST(InstanceHistoryRecording, time_insertion_rejects_scroll_change_during_allocation) {
	recording::on_allocate = [&] { song.test_scroll[NAVIGATION_ARRANGEMENT] = 1; };
	CHECK_FALSE(action.record_arranger_time_inserted(0, 8));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(recording::allocated, recording::freed);
}
TEST(InstanceHistoryRecording, time_insertion_rejects_song_change_during_allocation) {
	recording::on_allocate = [&] { currentSong = nullptr; };
	CHECK_FALSE(action.record_arranger_time_inserted(0, 8));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(recording::allocated, recording::freed);
}
TEST(InstanceHistoryRecording, time_insertion_rejects_destroyed_registered_action) {
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	recording::on_allocate = [&] {
		actionLogger.firstAction[BEFORE] = nullptr;
		delete target_action;
	};
	CHECK_FALSE(target_action->record_arranger_time_inserted(0, 8));
	LONGS_EQUAL(recording::allocated, recording::freed);
}
TEST(InstanceHistoryRecording, time_insertion_preserves_nested_history_and_rejects_outer_edit) {
	bool nested = false;
	recording::on_allocate = [&] {
		if (nested)
			return;
		nested = true;
		CHECK(action.record_arranger_time_inserted(0, 4));
	};
	CHECK_FALSE(action.record_arranger_time_inserted(0, 8));
	auto* consequence = static_cast<ConsequenceArrangerParamsTimeInserted*>(action.firstConsequence);
	LONGS_EQUAL(4, consequence->length);
	POINTERS_EQUAL(nullptr, consequence->next);
}

TEST(InstanceHistoryRecording, time_insertion_rejects_peer_structural_changes) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		recording::on_allocate = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_FALSE(action.record_arranger_time_inserted(0, 8));
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		LONGS_EQUAL(recording::allocated, recording::freed);
	}
}
TEST(InstanceHistoryRecording, time_insertion_rejects_reused_action_identity) {
	recording::on_allocate = [&] { action.action_identity = deluge::model::next_action_identity(); };
	CHECK_FALSE(action.record_arranger_time_inserted(0, 8));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(recording::allocated, recording::freed);
}

TEST(InstanceHistoryRecording, expansion_failure_recovers_automation_and_every_instance_prefix) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	for (int failed_attempt = 1; failed_attempt <= 4; ++failed_attempt) {
		output.clipInstances.entries = {{4, 2, nullptr}, {12, 2, nullptr}, {20, 2, nullptr}};
		ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
		ConsequenceArrangerParamsTimeInserted::reverts = 0;
		int attempts = 0;
		recording::on_allocate = [&] { recording::fail_allocation = ++attempts == failed_attempt; };
		CHECK_FALSE(view.shift_arrangement_time(1, 8, &action));
		LONGS_EQUAL(12, ConsequenceArrangerParamsTimeInserted::automation_pos);
		LONGS_EQUAL(failed_attempt == 1 ? 0 : 1, ConsequenceArrangerParamsTimeInserted::reverts);
		for (int index = 0; index < 3; ++index)
			LONGS_EQUAL(4 + index * 8, output.clipInstances.entries[index].pos);
		POINTERS_EQUAL(nullptr, action.firstConsequence);
		action.snapshot_error = Error::NONE;
	}
}
TEST(InstanceHistoryRecording, expansion_success_retains_history_and_shifted_automation) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	CHECK(view.shift_arrangement_time(1, 8, &action));
	LONGS_EQUAL(20, ConsequenceArrangerParamsTimeInserted::automation_pos);
	LONGS_EQUAL(12, output.clipInstances.entries[0].pos);
	CHECK(action.firstConsequence->type == Consequence::CLIP_INSTANCE_CHANGE);
	CHECK(action.firstConsequence->next->type == Consequence::ARRANGER_TIME_INSERTED);
}
TEST(InstanceHistoryRecording, expansion_context_change_does_not_attempt_automation_inverse) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	ConsequenceArrangerParamsTimeInserted::reverts = 0;
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			currentSong = nullptr;
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_arrangement_time(1, 8, &action));
	LONGS_EQUAL(20, ConsequenceArrangerParamsTimeInserted::automation_pos);
	LONGS_EQUAL(0, ConsequenceArrangerParamsTimeInserted::reverts);
	CHECK(action.firstConsequence);
}
TEST(InstanceHistoryRecording, expansion_inverse_failure_retains_automation_history) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	ConsequenceArrangerParamsTimeInserted::revert_result = Error::BUG;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 2; };
	CHECK_FALSE(view.shift_arrangement_time(1, 8, &action));
	ConsequenceArrangerParamsTimeInserted::revert_result = Error::NONE;
	LONGS_EQUAL(20, ConsequenceArrangerParamsTimeInserted::automation_pos);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	CHECK(action.firstConsequence->type == Consequence::ARRANGER_TIME_INSERTED);
	CHECK(action.snapshot_error == Error::BUG);
}

TEST(InstanceHistoryRecording, expansion_recovery_preserves_older_history) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	CHECK(action.record_arranger_time_inserted(0, 4));
	auto* older_history = action.firstConsequence;
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	int attempts = 0;
	recording::on_allocate = [&] { recording::fail_allocation = ++attempts == 2; };
	CHECK_FALSE(view.shift_arrangement_time(1, 8, &action));
	LONGS_EQUAL(12, ConsequenceArrangerParamsTimeInserted::automation_pos);
	POINTERS_EQUAL(older_history, action.firstConsequence);
	CHECK(action.snapshot_error == Error::NONE);
}
TEST(InstanceHistoryRecording, expansion_nested_history_prevents_automation_recovery) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	ConsequenceArrangerParamsTimeInserted::reverts = 0;
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			CHECK(action.record_arranger_time_inserted(0, 4));
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_arrangement_time(1, 8, &action));
	LONGS_EQUAL(20, ConsequenceArrangerParamsTimeInserted::automation_pos);
	LONGS_EQUAL(0, ConsequenceArrangerParamsTimeInserted::reverts);
	CHECK(action.firstConsequence->next);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
}

TEST(InstanceHistoryRecording, contraction_failure_never_edits_automation) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	for (int failed_attempt = 1; failed_attempt <= 4; ++failed_attempt) {
		output.clipInstances.entries = {{1, 1, nullptr}, {9, 1, nullptr}, {20, 1, nullptr}};
		ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
		auto* older_history = action.firstConsequence;
		int attempts = 0;
		recording::on_allocate = [&] { recording::fail_allocation = ++attempts == failed_attempt; };
		CHECK_FALSE(view.shift_arrangement_time(-1, -8, &action));
		LONGS_EQUAL(0, view.automation_edits);
		LONGS_EQUAL(12, ConsequenceArrangerParamsTimeInserted::automation_pos);
		LONGS_EQUAL(3, output.clipInstances.entries.size());
		LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
		LONGS_EQUAL(9, output.clipInstances.entries[1].pos);
		LONGS_EQUAL(20, output.clipInstances.entries[2].pos);
		if (failed_attempt == 1)
			POINTERS_EQUAL(older_history, action.firstConsequence);
		else {
			CHECK(action.firstConsequence->type == Consequence::PARAM_CHANGE);
			POINTERS_EQUAL(older_history, action.firstConsequence->next);
		}
		action.snapshot_error = Error::NONE;
	}
}
TEST(InstanceHistoryRecording, contraction_success_commits_automation_once_after_instances) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{9, 1, nullptr}, {20, 1, nullptr}};
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	recording::on_allocate = [&] {
		LONGS_EQUAL(0, view.automation_edits);
		LONGS_EQUAL(12, ConsequenceArrangerParamsTimeInserted::automation_pos);
	};
	CHECK(view.shift_arrangement_time(-1, -8, &action));
	LONGS_EQUAL(1, view.automation_edits);
	LONGS_EQUAL(4, ConsequenceArrangerParamsTimeInserted::automation_pos);
	LONGS_EQUAL(1, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(12, output.clipInstances.entries[1].pos);
}
TEST(InstanceHistoryRecording, contraction_rejects_cursor_change_after_snapshot_before_instances) {
	ArrangerView view;
	recording::on_allocate = [&] { song.test_scroll[NAVIGATION_ARRANGEMENT] = 8; };
	CHECK_FALSE(view.shift_arrangement_time(-1, -8, &action));
	LONGS_EQUAL(0, view.automation_edits);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
}
TEST(InstanceHistoryRecording, contraction_destroyed_action_does_not_commit_automation) {
	ArrangerView view;
	auto* target_action = new Action;
	target_action->require_complete_snapshots = true;
	actionLogger.firstAction[BEFORE] = target_action;
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			action.firstConsequence = target_action->firstConsequence;
			actionLogger.firstAction[BEFORE] = &action;
			delete target_action;
		}
	};
	CHECK_FALSE(view.shift_arrangement_time(-1, -8, target_action));
	LONGS_EQUAL(0, view.automation_edits);
}

TEST(InstanceHistoryRecording, failed_clip_cleanup_restores_attached_instance_and_preserves_older_history) {
	ArrangerView view;
	Clip clip;
	clip.output = &output;
	song.sessionClips.values = {&clip};
	output.clipInstances.entries = {{4, 16, &clip}};
	CHECK(action.record_arranger_time_inserted(0, 4));
	auto* older_history = action.firstConsequence;
	song.deletion_result = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	CHECK(song.preserved_cleanup_history);
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(16, output.clipInstances.entries[0].length);
	POINTERS_EQUAL(&clip, output.clipInstances.entries[0].clip);
	POINTERS_EQUAL(older_history, action.firstConsequence);
}
TEST(InstanceHistoryRecording, failed_clip_cleanup_does_not_restore_detached_clip) {
	ArrangerView view;
	Clip clip;
	clip.output = &output;
	song.sessionClips.values = {&clip};
	output.clipInstances.entries = {{4, 16, &clip}};
	song.deletion_result = Error::INSUFFICIENT_RAM;
	song.on_instance_cleanup = [&] { song.sessionClips.values.clear(); };
	CHECK_FALSE(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	LONGS_EQUAL(0, output.clipInstances.entries.size());
	CHECK(action.firstConsequence);
}
TEST(InstanceHistoryRecording, failed_clip_cleanup_does_not_restore_into_changed_song) {
	ArrangerView view;
	song.deletion_result = Error::INSUFFICIENT_RAM;
	song.on_instance_cleanup = [&] { currentSong = nullptr; };
	CHECK_FALSE(view.delete_clip_instance(&output, 0, output.clipInstances.getElement(0), &action));
	LONGS_EQUAL(0, output.clipInstances.entries.size());
	CHECK(action.firstConsequence);
}

TEST(ClipRestorationReservation, reservation_rejects_owner_switch_on_success_or_failure) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	for (bool reserved : {false, true}) {
		song.sessionClips.reserve_succeeds = reserved;
		song.sessionClips.on_reserve = [&] { remote.emplace(Id::Remote); };
		CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.commit_calls);
		remote.reset();
	}
	song.sessionClips.on_reserve = {};
	song.sessionClips.reserve_succeeds = true;
	CHECK(consequence.reserve_for_recreation(&song) == Error::NONE);
}
TEST(ClipRestorationReservation, reattachment_rejects_owner_switch_on_success_or_failure) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	for (auto result : {Error::NONE, Error::INSUFFICIENT_RAM}) {
		clip.on_reattach = [&] {
			remote.emplace(Id::Remote);
			return result;
		};
		CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.commit_calls);
		remote.reset();
	}
	clip.on_reattach = {};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::NONE);
}
TEST(ClipRestorationReservation, owner_switch_still_reconciles_clip_returned_to_song) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	song.sessionClips.on_reserve = [&] {
		remote.emplace(Id::Remote);
		song.sessionClips.values = {&clip};
	};
	CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	remote.reset();
	song.sessionClips.on_reserve = {};
	song.sessionClips.values.clear();
	consequence.owns_detached_clip = true;
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [&] {
		remote.emplace(Id::Remote);
		song.sessionClips.values = {&clip};
		return Error::NONE;
	};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	remote.reset();
	clip.on_reattach = {};
}
TEST(ClipRestorationReservation, nested_owner_scope_returning_to_caller_allows_recreation) {
	using namespace deluge::gui::ui_session;
	Scope remote(Id::Remote);
	song.sessionClips.on_reserve = [] { Scope local(Id::Local); };
	CHECK(consequence.reserve_for_recreation(&song) == Error::NONE);
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [] {
		Scope local(Id::Local);
		return Error::NONE;
	};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::NONE);
	CHECK(consequence.commit_recreation(&song) == Error::NONE);
	CHECK_FALSE(consequence.owns_detached_clip);
	POINTERS_EQUAL(&clip, song.sessionClips.getClipAtIndex(0));
}

TEST(InstanceHistoryRecording, expansion_cursor_change_stops_before_second_instance) {
	ArrangerView view;
	output.clipInstances.entries = {{4, 2, nullptr}, {12, 2, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] {
		++attempts;
		song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	};
	CHECK_FALSE(view.shift_clips_horizontally(1, 8, &action));
	LONGS_EQUAL(1, attempts);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(20, output.clipInstances.entries[1].pos);
	CHECK(action.firstConsequence);
	POINTERS_EQUAL(nullptr, action.firstConsequence->next);
}
TEST(InstanceHistoryRecording, contraction_cursor_change_stops_after_first_move) {
	ArrangerView view;
	output.clipInstances.entries = {{12, 2, nullptr}, {20, 2, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] {
		++attempts;
		song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(1, attempts);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(20, output.clipInstances.entries[1].pos);
	CHECK(action.firstConsequence);
}
TEST(InstanceHistoryRecording, empty_deletion_cursor_change_stops_before_next_deletion) {
	ArrangerView view;
	output.clipInstances.entries = {{1, 1, nullptr}, {3, 1, nullptr}};
	song.on_instance_cleanup = [&] { song.test_scroll[NAVIGATION_ARRANGEMENT] = 8; };
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action));
	LONGS_EQUAL(1, song.deleted_instance_notifications);
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(3, output.clipInstances.entries[0].pos);
	CHECK(action.firstConsequence);
}
TEST(InstanceHistoryRecording, empty_deletion_cleanup_cannot_continue_with_destroyed_action) {
	ArrangerView view;
	output.clipInstances.entries = {{1, 1, nullptr}, {3, 1, nullptr}};
	auto* target_action = new Action;
	actionLogger.firstAction[BEFORE] = target_action;
	song.on_instance_cleanup = [&] {
		action.firstConsequence = target_action->firstConsequence;
		actionLogger.firstAction[BEFORE] = &action;
		delete target_action;
	};
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, target_action));
	LONGS_EQUAL(1, song.deleted_instance_notifications);
	LONGS_EQUAL(1, output.clipInstances.entries.size());
}

TEST(InstanceHistoryRecording, expansion_failure_after_cursor_change_retains_completed_prefix) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{4, 2, nullptr}, {12, 2, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
			recording::fail_allocation = true;
		}
	};
	bool recovered = true;
	CHECK_FALSE(view.shift_clips_horizontally(1, 8, &action, &recovered));
	CHECK_FALSE(recovered);
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(20, output.clipInstances.entries[1].pos);
	CHECK(action.firstConsequence);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
}
TEST(InstanceHistoryRecording, empty_deletion_failure_after_cursor_change_retains_history) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	output.clipInstances.entries = {{1, 1, nullptr}, {3, 1, nullptr}};
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
			recording::fail_allocation = true;
		}
	};
	bool recovered = true;
	CHECK_FALSE(view.shift_clips_horizontally(-1, -8, &action, &recovered));
	CHECK_FALSE(recovered);
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(3, output.clipInstances.entries[0].pos);
	CHECK(action.firstConsequence);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
}
TEST(InstanceHistoryRecording, changed_cursor_prevents_followup_automation_inverse) {
	ArrangerView view;
	action.require_complete_snapshots = true;
	ConsequenceArrangerParamsTimeInserted::automation_pos = 12;
	ConsequenceArrangerParamsTimeInserted::reverts = 0;
	int attempts = 0;
	recording::on_allocate = [&] {
		if (++attempts == 2) {
			song.test_scroll[NAVIGATION_ARRANGEMENT] = 8;
			recording::fail_allocation = true;
		}
	};
	CHECK_FALSE(view.shift_arrangement_time(1, 8, &action));
	LONGS_EQUAL(20, ConsequenceArrangerParamsTimeInserted::automation_pos);
	LONGS_EQUAL(0, ConsequenceArrangerParamsTimeInserted::reverts);
	CHECK(action.firstConsequence->type == Consequence::ARRANGER_TIME_INSERTED);
	CHECK(action.snapshot_error == Error::INSUFFICIENT_RAM);
}

TEST(ClipRestorationReservation, reservation_rejects_output_change_on_success_or_failure) {
	Output original_output, replacement_output;
	for (bool reserved : {false, true}) {
		clip.output = &original_output;
		song.sessionClips.reserve_succeeds = reserved;
		song.sessionClips.on_reserve = [&] { clip.output = &replacement_output; };
		CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.commit_calls);
	}
	clip.output = &original_output;
	song.sessionClips.on_reserve = {};
	song.sessionClips.reserve_succeeds = true;
	CHECK(consequence.reserve_for_recreation(&song) == Error::NONE);
}

TEST(ClipRestorationReservation, output_change_does_not_prevent_returned_clip_ownership_reconciliation) {
	Output original_output, replacement_output;
	clip.output = &original_output;
	song.sessionClips.on_reserve = [&] {
		clip.output = &replacement_output;
		song.sessionClips.values = {&clip};
	};
	CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);
}

TEST(ClipRestorationReservation, reattachment_rejects_output_change_regardless_of_callback_result) {
	Output original_output, replacement_output;
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	for (auto result : {Error::NONE, Error::INSUFFICIENT_RAM}) {
		clip.output = &original_output;
		clip.on_reattach = [&] {
			clip.output = &replacement_output;
			return result;
		};
		CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.commit_calls);
	}
	clip.output = &original_output;
	clip.on_reattach = {};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::NONE);
}

TEST(ClipRestorationReservation, reattachment_output_change_reconciles_returned_ownership_on_error) {
	Output original_output, replacement_output;
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.output = &original_output;
	clip.on_reattach = [&] {
		clip.output = &replacement_output;
		song.arrangementOnlyClips.values = {&clip};
		return Error::INSUFFICIENT_RAM;
	};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);
}

TEST(ClipExistenceRecording, foreign_array_is_rejected_before_allocation_or_deletion) {
	Song other_song;
	bool allocated = false;
	recording::on_allocate = [&] { allocated = true; };
	action.xScrollClip[BEFORE] = 17;
	action.xScrollClip[AFTER] = 23;
	for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
		CHECK_FALSE(action.recordClipExistenceChange(&song, &other_song.sessionClips, &clip, type));
		CHECK_FALSE(action.recordClipExistenceChange(&song, &other_song.arrangementOnlyClips, &clip, type));
	}
	CHECK_FALSE(allocated);
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(17, action.xScrollClip[BEFORE]);
	LONGS_EQUAL(23, action.xScrollClip[AFTER]);
}

TEST(ClipExistenceRecording, both_song_arrays_accept_creation_history) {
	CHECK(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	auto* first = action.firstConsequence;
	CHECK(action.recordClipExistenceChange(&song, &song.arrangementOnlyClips, &clip, ExistenceChangeType::CREATE));
	POINTERS_EQUAL(first, action.firstConsequence->next);
	LONGS_EQUAL(0, recording::reverted);
}

TEST(ClipExistenceRecording, allocation_move_to_other_song_array_rejects_history) {
	for (bool from_session : {false, true}) {
		for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
			for (bool fail_allocation : {false, true}) {
				auto* source = from_session ? &song.sessionClips : &song.arrangementOnlyClips;
				auto* destination = from_session ? &song.arrangementOnlyClips : &song.sessionClips;
				source->values = {&clip};
				destination->values.clear();
				recording::fail_allocation = fail_allocation;
				recording::on_allocate = [&] {
					source->values.clear();
					destination->values = {&clip};
				};
				CHECK_FALSE(action.recordClipExistenceChange(&song, source, &clip, type));
				LONGS_EQUAL(0, recording::reverted);
				POINTERS_EQUAL(nullptr, action.firstConsequence);
				LONGS_EQUAL(recording::allocated, recording::freed);
				LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
				LONGS_EQUAL(9, action.xScrollClip[AFTER]);
			}
		}
	}
}

TEST(ClipExistenceRecording, inactive_song_is_rejected_before_allocation_or_deletion) {
	Song other_song;
	bool allocated = false;
	recording::on_allocate = [&] { allocated = true; };
	for (Song* active_song : {&other_song, static_cast<Song*>(nullptr)}) {
		currentSong = active_song;
		for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
			CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, type));
			CHECK_FALSE(action.recordClipExistenceChange(&song, &song.arrangementOnlyClips, &clip, type));
		}
	}
	CHECK_FALSE(allocated);
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
	LONGS_EQUAL(9, action.xScrollClip[AFTER]);
	currentSong = &song;
	recording::on_allocate = {};
	CHECK(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
}

TEST(ClipExistenceRecording, nested_history_during_allocation_stops_outer_recording) {
	song.sessionClips.values = {&clip};
	for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
		for (bool fail_outer_allocation : {false, true}) {
			auto* older = action.firstConsequence;
			Consequence* nested = nullptr;
			bool nested_allocation = false;
			recording::fail_allocation = false;
			recording::on_allocate = [&] {
				if (nested_allocation)
					return;
				nested_allocation = true;
				CHECK(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
				nested = action.firstConsequence;
				action.xScrollClip[BEFORE] = 17;
				action.xScrollClip[AFTER] = 23;
				recording::fail_allocation = fail_outer_allocation;
			};
			CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, type));
			POINTERS_EQUAL(nested, action.firstConsequence);
			POINTERS_EQUAL(older, nested->next);
			LONGS_EQUAL(0, recording::reverted);
			LONGS_EQUAL(17, action.xScrollClip[BEFORE]);
			LONGS_EQUAL(23, action.xScrollClip[AFTER]);
		}
	}
}

TEST(ClipExistenceRecording, output_removed_during_allocation_rejects_recording) {
	song.sessionClips.values = {&clip};
	Output output;
	clip.output = &output;
	for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
		for (bool fail_allocation : {false, true}) {
			song.firstOutput = &output;
			recording::fail_allocation = fail_allocation;
			recording::on_allocate = [&] { song.firstOutput = nullptr; };
			CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, type));
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			LONGS_EQUAL(0, recording::reverted);
			LONGS_EQUAL(recording::allocated, recording::freed);
			LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
			LONGS_EQUAL(9, action.xScrollClip[AFTER]);
		}
	}
}

TEST(ClipExistenceRecording, destroyed_output_is_not_used_after_allocation) {
	song.sessionClips.values = {&clip};
	auto* output = new Output;
	clip.output = output;
	song.firstOutput = output;
	recording::on_allocate = [&] {
		song.firstOutput = nullptr;
		delete output;
	};
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(0, recording::reverted);
}

TEST(ClipExistenceRecording, output_retained_for_undo_remains_valid_during_allocation) {
	Output output;
	clip.output = &output;
	song.firstOutput = &output;
	recording::on_allocate = [&] {
		song.firstOutput = nullptr;
		song.undo_detached_outputs.retain(&output);
	};
	CHECK(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
	CHECK(action.firstConsequence != nullptr);
	LONGS_EQUAL(0, recording::reverted);
	song.undo_detached_outputs.release(&output);
}

TEST(ClipRestorationReservation, reservation_rejects_removed_output_even_on_allocation_failure) {
	Output output;
	clip.output = &output;
	for (bool reserved : {false, true}) {
		song.firstOutput = &output;
		song.sessionClips.reserve_succeeds = reserved;
		song.sessionClips.on_reserve = [&] { song.firstOutput = nullptr; };
		CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.commit_calls);
	}
}

TEST(ClipRestorationReservation, reattachment_rejects_removed_output_even_on_callback_failure) {
	Output output;
	clip.output = &output;
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	for (auto result : {Error::NONE, Error::INSUFFICIENT_RAM}) {
		song.firstOutput = &output;
		clip.on_reattach = [&] {
			song.firstOutput = nullptr;
			return result;
		};
		CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.commit_calls);
	}
}

TEST(ClipRestorationReservation, output_moved_into_undo_retention_remains_valid) {
	Output output;
	clip.output = &output;
	song.firstOutput = &output;
	song.sessionClips.on_reserve = [&] {
		song.firstOutput = nullptr;
		song.undo_detached_outputs.retain(&output);
	};
	CHECK(consequence.reserve_for_recreation(&song) == Error::NONE);
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	CHECK(consequence.reattach_for_recreation(&stack) == Error::NONE);
	song.undo_detached_outputs.release(&output);
	song.firstOutput = &output;
	clip.on_reattach = [&] {
		song.firstOutput = nullptr;
		song.undo_detached_outputs.retain(&output);
		return Error::NONE;
	};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::NONE);
	song.undo_detached_outputs.release(&output);
}

TEST(ClipRestorationReservation, returned_clip_reconciles_ownership_despite_invalid_array) {
	Song other_song;
	for (bool in_session : {false, true}) {
		song.sessionClips.values.clear();
		song.arrangementOnlyClips.values.clear();
		auto* returned_array = in_session ? &song.sessionClips : &song.arrangementOnlyClips;
		returned_array->values = {&clip};
		for (ClipArray* invalid_array : {static_cast<ClipArray*>(nullptr), &other_song.sessionClips}) {
			consequence.clipArray = invalid_array;
			consequence.owns_detached_clip = true;
			CHECK_FALSE(consequence.can_recreate(&song));
			CHECK_FALSE(consequence.owns_detached_clip);
		}
	}
}

TEST(ClipRestorationReservation, callback_returning_clip_and_changing_array_reconciles_ownership) {
	Song other_song;
	song.sessionClips.on_reserve = [&] {
		song.arrangementOnlyClips.values = {&clip};
		consequence.clipArray = &other_song.sessionClips;
	};
	CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);

	consequence.clipArray = &song.sessionClips;
	consequence.owns_detached_clip = true;
	song.arrangementOnlyClips.values.clear();
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	clip.on_reattach = [&] {
		song.arrangementOnlyClips.values = {&clip};
		consequence.clipArray = nullptr;
		return Error::INSUFFICIENT_RAM;
	};
	CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
}

TEST(ClipRestorationReservation, missing_output_rejects_reattachment_and_commit_without_mutation) {
	clip.output = nullptr;
	bool reattached = false;
	clip.on_reattach = [&] {
		reattached = true;
		return Error::NONE;
	};
	ModelStackWithTimelineCounter stack;
	stack.song = &song;
	stack.clip = &clip;
	CHECK(consequence.reattach_for_recreation(&stack) == Error::BUG);
	CHECK_FALSE(reattached);
	CHECK(consequence.commit_recreation(&song) == Error::BUG);
	CHECK(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);
	LONGS_EQUAL(0, song.sessionClips.pointer_writes);
	LONGS_EQUAL(0, song.sessionClips.getNumElements());
	clip.output = &output;
	CHECK(consequence.reattach_for_recreation(&stack) == Error::NONE);
	CHECK(consequence.commit_recreation(&song) == Error::NONE);
	CHECK_FALSE(consequence.owns_detached_clip);
}

TEST(ClipRestorationReservation, missing_output_still_reconciles_returned_clip_ownership) {
	clip.output = nullptr;
	song.arrangementOnlyClips.values = {&clip};
	CHECK(consequence.commit_recreation(&song) == Error::BUG);
	CHECK_FALSE(consequence.owns_detached_clip);
	LONGS_EQUAL(0, song.sessionClips.commit_calls);
}

TEST(ClipRestorationReservation, missing_output_rejects_reservation_before_allocator_callbacks) {
	clip.output = nullptr;
	bool callback_ran = false;
	song.sessionClips.on_reserve = [&] { callback_ran = true; };
	for (bool reserved : {false, true}) {
		song.sessionClips.reserve_succeeds = reserved;
		CHECK(consequence.reserve_for_recreation(&song) == Error::BUG);
		CHECK_FALSE(callback_ran);
		CHECK(consequence.owns_detached_clip);
		LONGS_EQUAL(0, song.sessionClips.reserve_calls);
	}
	clip.output = &output;
	CHECK(consequence.reserve_for_recreation(&song) == Error::NONE);
	CHECK(callback_ran);
	LONGS_EQUAL(1, song.sessionClips.reserve_calls);
}

TEST(ClipExistenceRecording, registered_clip_in_other_array_is_rejected_before_allocation) {
	bool allocated = false;
	recording::on_allocate = [&] { allocated = true; };
	for (bool in_session : {false, true}) {
		song.sessionClips.values.clear();
		song.arrangementOnlyClips.values.clear();
		auto* actual_array = in_session ? &song.sessionClips : &song.arrangementOnlyClips;
		auto* wrong_array = in_session ? &song.arrangementOnlyClips : &song.sessionClips;
		actual_array->values = {&clip};
		for (auto type : {ExistenceChangeType::CREATE, ExistenceChangeType::DELETE}) {
			CHECK_FALSE(action.recordClipExistenceChange(&song, wrong_array, &clip, type));
		}
		LONGS_EQUAL(1, actual_array->getNumElements());
		POINTERS_EQUAL(&clip, actual_array->getClipAtIndex(0));
	}
	CHECK_FALSE(allocated);
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
	LONGS_EQUAL(9, action.xScrollClip[AFTER]);
}

TEST(ClipExistenceRecording, registered_clip_accepts_its_own_array_for_creation_history) {
	song.arrangementOnlyClips.values = {&clip};
	CHECK(action.recordClipExistenceChange(&song, &song.arrangementOnlyClips, &clip, ExistenceChangeType::CREATE));
	CHECK(action.firstConsequence != nullptr);
	LONGS_EQUAL(0, recording::reverted);
}

TEST(ClipExistenceRecording, allocation_registering_clip_in_wrong_array_rejects_history) {
	for (bool target_session : {false, true}) {
		for (bool fail_allocation : {false, true}) {
			song.sessionClips.values.clear();
			song.arrangementOnlyClips.values.clear();
			auto* target_array = target_session ? &song.sessionClips : &song.arrangementOnlyClips;
			auto* other_array = target_session ? &song.arrangementOnlyClips : &song.sessionClips;
			recording::fail_allocation = fail_allocation;
			recording::on_allocate = [&] { other_array->values = {&clip}; };
			CHECK_FALSE(action.recordClipExistenceChange(&song, target_array, &clip, ExistenceChangeType::CREATE));
			POINTERS_EQUAL(nullptr, action.firstConsequence);
			LONGS_EQUAL(recording::allocated, recording::freed);
			LONGS_EQUAL(0, recording::reverted);
			LONGS_EQUAL(1, other_array->getNumElements());
			LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
			LONGS_EQUAL(9, action.xScrollClip[AFTER]);
		}
	}
}

TEST(ClipExistenceRecording, allocation_registering_clip_in_target_array_accepts_history) {
	recording::on_allocate = [&] { song.arrangementOnlyClips.values = {&clip}; };
	CHECK(action.recordClipExistenceChange(&song, &song.arrangementOnlyClips, &clip, ExistenceChangeType::CREATE));
	CHECK(action.firstConsequence != nullptr);
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(&clip, song.arrangementOnlyClips.getClipAtIndex(0));
}

TEST(ClipExistenceRecording, missing_deletion_target_rejects_before_allocation) {
	bool allocated = false;
	recording::on_allocate = [&] { allocated = true; };
	CHECK_FALSE(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::DELETE));
	CHECK_FALSE(
	    action.recordClipExistenceChange(&song, &song.arrangementOnlyClips, &clip, ExistenceChangeType::DELETE));
	CHECK_FALSE(allocated);
	LONGS_EQUAL(0, recording::reverted);
	POINTERS_EQUAL(nullptr, action.firstConsequence);
	LONGS_EQUAL(5, action.xScrollClip[BEFORE]);
	LONGS_EQUAL(9, action.xScrollClip[AFTER]);
	CHECK(action.recordClipExistenceChange(&song, &song.sessionClips, &clip, ExistenceChangeType::CREATE));
}

TEST(InstanceHistoryRecording, instance_change_rejects_inactive_song_without_mutation_and_can_retry) {
	Song replacement_song;
	ClipInstance original{4, 16, nullptr};
	ConsequenceClipInstanceChange consequence(&output, &original, 24, 8, nullptr);
	ModelStack stack;
	stack.song = &song;
	for (auto time : {BEFORE, AFTER}) {
		for (auto* active_song : {&replacement_song, static_cast<Song*>(nullptr)}) {
			output.clipInstances.entries = {time == BEFORE ? ClipInstance{24, 8, nullptr} : original};
			currentSong = active_song;
			CHECK(consequence.revert(time, &stack) == Error::BUG);
			LONGS_EQUAL(time == BEFORE ? 24 : 4, output.clipInstances.entries[0].pos);
			LONGS_EQUAL(time == BEFORE ? 8 : 16, output.clipInstances.entries[0].length);
			currentSong = &song;
			CHECK(consequence.revert(time, &stack) == Error::NONE);
			LONGS_EQUAL(time == BEFORE ? 4 : 24, output.clipInstances.entries[0].pos);
			LONGS_EQUAL(time == BEFORE ? 16 : 8, output.clipInstances.entries[0].length);
		}
	}
}

TEST(InstanceHistoryRecording, instance_existence_rejects_inactive_song_without_mutation_and_can_retry) {
	Song replacement_song;
	ClipInstance original{4, 16, nullptr};
	ConsequenceClipInstanceExistence consequence(&output, &original, ExistenceChangeType::CREATE);
	ModelStack stack;
	stack.song = &song;
	for (auto time : {BEFORE, AFTER}) {
		for (auto* active_song : {&replacement_song, static_cast<Song*>(nullptr)}) {
			output.clipInstances.entries.clear();
			if (time == BEFORE)
				output.clipInstances.entries.push_back(original);
			currentSong = active_song;
			int reservations = 0;
			output.clipInstances.on_reserve = [&] { ++reservations; };
			CHECK(consequence.revert(time, &stack) == Error::BUG);
			LONGS_EQUAL(time == BEFORE ? 1 : 0, output.clipInstances.entries.size());
			LONGS_EQUAL(0, reservations);
			currentSong = &song;
			CHECK(consequence.revert(time, &stack) == Error::NONE);
			LONGS_EQUAL(time == BEFORE ? 0 : 1, output.clipInstances.entries.size());
			output.clipInstances.on_reserve = {};
		}
	}
}

TEST(InstanceHistoryRecording, instance_recreation_rejects_stack_song_change_during_reservation_and_can_retry) {
	Song replacement_song;
	ClipInstance original{4, 16, nullptr};
	ConsequenceClipInstanceExistence consequence(&output, &original, ExistenceChangeType::DELETE);
	ModelStack stack;
	stack.song = &song;
	output.clipInstances.entries.clear();
	output.clipInstances.on_reserve = [&] { stack.song = &replacement_song; };
	CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
	CHECK(output.clipInstances.entries.empty());
	stack.song = &song;
	output.clipInstances.on_reserve = {};
	CHECK(consequence.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(1, output.clipInstances.entries.size());
	LONGS_EQUAL(4, output.clipInstances.entries[0].pos);
	LONGS_EQUAL(16, output.clipInstances.entries[0].length);
}

TEST(InstanceHistoryRecording, instance_change_rejects_occupied_or_crossed_destination_without_mutation) {
	ClipInstance original{4, 4, nullptr};
	ConsequenceClipInstanceChange consequence(&output, &original, 20, 4, nullptr);
	ModelStack stack;
	stack.song = &song;
	for (auto time : {BEFORE, AFTER}) {
		const int32_t source_pos = time == BEFORE ? 20 : 4;
		const int32_t target_pos = time == BEFORE ? 4 : 20;
		for (int conflict = 0; conflict < 3; ++conflict) {
			ClipInstance neighbor{conflict == 0   ? target_pos - 2
			                      : conflict == 1 ? target_pos + 3
			                                      : 12,
			                      conflict == 0 ? 3 : 2, nullptr};
			output.clipInstances.entries = {{source_pos, 4, nullptr}, neighbor};
			std::sort(output.clipInstances.entries.begin(), output.clipInstances.entries.end(),
			          [](const auto& left, const auto& right) { return left.pos < right.pos; });
			const auto before = output.clipInstances.entries;
			CHECK(consequence.revert(time, &stack) == Error::BUG);
			LONGS_EQUAL(2, output.clipInstances.entries.size());
			for (int index = 0; index < 2; ++index) {
				LONGS_EQUAL(before[index].pos, output.clipInstances.entries[index].pos);
				LONGS_EQUAL(before[index].length, output.clipInstances.entries[index].length);
			}
		}
	}
}

TEST(InstanceHistoryRecording, instance_change_allows_adjacent_destinations_and_retries_after_conflict_removed) {
	ClipInstance original{4, 4, nullptr};
	ConsequenceClipInstanceChange consequence(&output, &original, 20, 4, nullptr);
	ModelStack stack;
	stack.song = &song;
	output.clipInstances.entries = {{2, 3, nullptr}, {20, 4, nullptr}, {24, 4, nullptr}};
	CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
	output.clipInstances.entries[0].length = 2;
	CHECK(consequence.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(4, output.clipInstances.entries[1].pos);
	CHECK(consequence.revert(AFTER, &stack) == Error::NONE);
	LONGS_EQUAL(20, output.clipInstances.entries[1].pos);
	LONGS_EQUAL(24, output.clipInstances.entries[2].pos);
}

TEST(InstanceHistoryRecording, expansion_batch_can_undo_and_redo_without_destination_conflicts) {
	ArrangerView view;
	ModelStack stack;
	stack.song = &song;
	output.clipInstances.entries = {{4, 4, nullptr}, {8, 4, nullptr}, {12, 4, nullptr}};
	CHECK(view.shift_clips_horizontally(1, 16, &action));
	CHECK(action.revert(BEFORE, &stack) == Error::NONE);
	for (int index = 0; index < 3; ++index)
		LONGS_EQUAL(4 + 4 * index, output.clipInstances.entries[index].pos);
	CHECK(action.revert(AFTER, &stack) == Error::NONE);
	for (int index = 0; index < 3; ++index)
		LONGS_EQUAL(20 + 4 * index, output.clipInstances.entries[index].pos);
}

TEST(InstanceHistoryRecording, contraction_batch_can_undo_and_redo_without_destination_conflicts) {
	ArrangerView view;
	ModelStack stack;
	stack.song = &song;
	output.clipInstances.entries = {{20, 4, nullptr}, {24, 4, nullptr}, {28, 4, nullptr}};
	CHECK(view.shift_clips_horizontally(-1, -16, &action));
	CHECK(action.revert(BEFORE, &stack) == Error::NONE);
	for (int index = 0; index < 3; ++index)
		LONGS_EQUAL(20 + 4 * index, output.clipInstances.entries[index].pos);
	CHECK(action.revert(AFTER, &stack) == Error::NONE);
	for (int index = 0; index < 3; ++index)
		LONGS_EQUAL(4 + 4 * index, output.clipInstances.entries[index].pos);
}

TEST(InstanceHistoryRecording, instance_resize_rejects_neighbor_overlap_in_both_directions) {
	ModelStack stack;
	stack.song = &song;
	for (auto time : {BEFORE, AFTER}) {
		ClipInstance original{4, time == BEFORE ? 8 : 4, nullptr};
		ConsequenceClipInstanceChange consequence(&output, &original, 4, time == BEFORE ? 4 : 8, nullptr);
		output.clipInstances.entries = {{4, 4, nullptr}, {10, 4, nullptr}};
		CHECK(consequence.revert(time, &stack) == Error::BUG);
		LONGS_EQUAL(4, output.clipInstances.entries[0].length);
		LONGS_EQUAL(10, output.clipInstances.entries[1].pos);
		output.clipInstances.entries[1].pos = 12;
		CHECK(consequence.revert(time, &stack) == Error::NONE);
		LONGS_EQUAL(8, output.clipInstances.entries[0].length);
	}
}

TEST(ClipRestorationReservation, detached_cleanup_retires_before_backup_callback) {
	auto* detached = new Clip;
	consequence.clip = detached;
	bool cleanup_called = false;
	song.on_delete_clip_backups = [&](Clip* target) {
		cleanup_called = true;
		POINTERS_EQUAL(detached, target);
		CHECK_TRUE(target->retiring);
	};
	consequence.prepareForDestruction(BEFORE, &song);
	CHECK_TRUE(cleanup_called);
	CHECK_FALSE(consequence.owns_detached_clip);
}
