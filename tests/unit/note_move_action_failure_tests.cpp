#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/views/edit_pad_snapshot.h"
#include <cstdint>
#include <functional>
namespace note_move_action_failure_test {
constexpr int kEditPadPressBufferSize = 2, kNumExpressionDimensions = 3, BEFORE = 0;
enum class Error { NONE, INSUFFICIENT_RAM, BUG };
struct ModelStackWithAutoParam;
int copied_records = 0, freed_records = 0;
int deletion_storage[2];
int deletion_attempts = 0, deletion_fail_at = -1, deletion_freed = 0, deletion_consumed = 0;
bool invalidate_deletion_reservation = false;
int* currentSong = nullptr;
std::function<void()> on_action_creation, on_snapshot, on_capture, on_deletion_reservation;
std::function<void()> on_row_creation, on_expression_creation, on_parameter_creation, on_node_reservation,
    on_note_insertion, on_node_transfer;
void delugeDealloc(void* memory) {
	if (memory == &deletion_storage[0] || memory == &deletion_storage[1]) {
		++deletion_freed;
		return;
	}
	++freed_records;
}
enum class ActionType { NOTE_EDIT };
enum class ActionAddition { ALLOWED };
struct InstrumentClip;
struct Action {
	uint64_t action_identity = 1;
	static void* allocate_note_existence_memory() {
		int request = deletion_attempts++;
		if (on_deletion_reservation)
			on_deletion_reservation();
		if (invalidate_deletion_reservation)
			deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
			    .structural_refresh.request();
		return request == deletion_fail_at ? nullptr : &deletion_storage[request % 2];
	}
	int updates = 0;
	int snapshot_requests = 0, fail_snapshot_at = -1;
	bool recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam*) {
		if (on_snapshot)
			on_snapshot();
		return snapshot_requests++ != fail_snapshot_at;
	}
	void updateYScrollClipViewAfter(InstrumentClip*) { ++updates; }
};
struct logger {
	Action* firstAction[2]{};
	Action* result = nullptr;
	Action* getNewAction(ActionType, ActionAddition) {
		if (on_action_creation)
			on_action_creation();
		firstAction[BEFORE] = result;
		return result;
	}
} actionLogger;
struct display_type {
	int errors = 0;
	void displayError(Error) { ++errors; }
} display_instance;
auto* display = &display_instance;
struct ModelStackWithTimelineCounter {};
struct ModelStackWithAutoParam {};
struct stolen_nodes {
	int num = 0;
	void* nodes = nullptr;
};
using StolenParamNodes = stolen_nodes;
struct AutoParam {
	int inserted_nodes = 0;
	int source_nodes = 2, captures = 0, removals = 0;
	bool capture_succeeds = true;
	Error copy_nodes_for_move(int, int, int, stolen_nodes* record) {
		++captures;
		if (on_capture)
			on_capture();
		if (!capture_succeeds)
			return Error::INSUFFICIENT_RAM;
		record->num = source_nodes;
		if (source_nodes) {
			record->nodes = this;
			++copied_records;
		}
		return Error::NONE;
	}
	Error stealNodes(ModelStackWithAutoParam*, int, int, int, Action*, stolen_nodes* record) {
		CHECK(record == nullptr);
		++removals;
		source_nodes = 0;
		return Error::NONE;
	}
	bool reservation_succeeds = true;
	int reservation_requests = 0;
	bool reserve_stolen_nodes(int pos, int region_length, int loop_length, const stolen_nodes*) {
		++reservation_requests;
		if (on_node_reservation)
			on_node_reservation();
		LONGS_EQUAL(4, pos);
		LONGS_EQUAL(16, region_length);
		LONGS_EQUAL(16, loop_length);
		return reservation_succeeds;
	}
	Error insertStolenNodes(ModelStackWithAutoParam*, int, int, int, Action*, stolen_nodes* nodes) {
		inserted_nodes += nodes->num;
		if (on_node_transfer)
			on_node_transfer();
		return Error::NONE;
	}
};
struct ExpressionParamSet {
	AutoParam parameters[3];
	bool allocated[3]{};
	int failing_dimension = -1;
	int creation_requests[3]{};
	AutoParam* getParam(int dimension, bool create) {
		if (create) {
			++creation_requests[dimension];
			if (on_parameter_creation)
				on_parameter_creation();
			if (dimension == failing_dimension)
				return nullptr;
			allocated[dimension] = true;
		}
		return allocated[dimension] ? &parameters[dimension] : nullptr;
	}
};
struct ParamCollectionSummary {
	ExpressionParamSet* paramCollection = nullptr;
};
struct ModelStackWithParamCollection {
	ModelStackWithAutoParam parameter;
	ModelStackWithAutoParam* addAutoParam(int, AutoParam*) { return &parameter; }
};
struct model_context {
	ModelStackWithParamCollection collection;
	ModelStackWithParamCollection* addParamCollection(ExpressionParamSet*, ParamCollectionSummary*) {
		return &collection;
	}
};
struct ModelStackWithNoteRow;
struct NoteRow {
	struct manager {
		ParamCollectionSummary summary;
		ExpressionParamSet expressions;
		bool allocation_succeeds = true;
		int allocations = 0;
		bool ensureExpressionParamSetExists(bool) {
			++allocations;
			if (on_expression_creation)
				on_expression_creation();
			if (allocation_succeeds)
				summary.paramCollection = &expressions;
			return allocation_succeeds;
		}
		ParamCollectionSummary* getExpressionParamSetSummary() { return &summary; }
	} paramManager;
	int notes = 0, last_position = -1;
	bool insertion_succeeds = true;
	int deletions = 0;
	Error deleteNoteByPos(ModelStackWithNoteRow*, int, Action* action, void** prepared_memory = nullptr) {
		if (action) {
			CHECK(prepared_memory && *prepared_memory);
			*prepared_memory = nullptr;
			++deletion_consumed;
		}
		++deletions;
		return Error::NONE;
	}
	Action* received_action = nullptr;
	bool attemptNoteAdd(int pos, int, int, int, int, int, ModelStackWithNoteRow*, Action* action) {
		if (on_note_insertion)
			on_note_insertion();
		if (!insertion_succeeds)
			return false;
		++notes;
		last_position = pos;
		received_action = action;
		return true;
	}
	int getDistanceToNextNote(int, ModelStackWithNoteRow*) { return 16; }
};
struct ModelStackWithNoteRow {
	NoteRow* row = nullptr;
	model_context context;
	NoteRow* getNoteRowAllowNull() { return row; }
	NoteRow* getNoteRow() { return row; }
	int getLoopLength() { return 16; }
	model_context* addOtherTwoThingsAutomaticallyGivenNoteRow() { return &context; }
};
struct InstrumentClip {
	ModelStackWithNoteRow stack, second_stack;
	ModelStackWithNoteRow* getNoteRowOnScreen(int row, ModelStackWithTimelineCounter*) {
		return row == 1 ? &second_stack : &stack;
	}
};
struct press {
	uint64_t gesture_revision = 0;
	bool isBlurredSquare = false;
	bool isActive = false, deleteOnDepress = true, deleteOnScroll = false, mpeCachedYet = false;
	int xDisplay = 0, yDisplay = 0, intendedPos = 4, intendedLength = 2, intendedVelocity = 100;
	int intendedProbability = 20, intendedIterance = 0, intendedFill = 0;
	stolen_nodes stolenMPE[kNumExpressionDimensions];
};
using EditPadPress = press;
struct InstrumentClipView {
	press editPadPresses[kEditPadPressBufferSize];
	ModelStackWithNoteRow failed_creation;
	int finalized = 0, ended = 0, creations = 0;
	void scrollVertical_placeNotesPressed(ModelStackWithTimelineCounter*, InstrumentClip*, bool);
	bool scrollVertical_grabNotesPressed(ModelStackWithTimelineCounter*, InstrumentClip*);
	void reassessAuditionStatus(int) {}
	ModelStackWithNoteRow* createNoteRowForYDisplay(ModelStackWithTimelineCounter*, int) {
		deluge::gui::ui_session::PeerStructuralChange structural_change;
		++creations;
		if (on_row_creation)
			on_row_creation();
		return &failed_creation;
	}
	void endEditPadPress(int i) {
		++ended;
		editPadPresses[i].isActive = false;
	}
	void checkIfAllEditPadPressesEnded(bool) { ++finalized; }
};
#include "note_move_action_failure.inc"
TEST_GROUP(NoteMoveActionFailure){void setup() override{actionLogger.result = nullptr;
display_instance.errors = 0;
copied_records = freed_records = 0;
deletion_attempts = deletion_freed = deletion_consumed = 0;
deletion_fail_at = -1;
invalidate_deletion_reservation = false;
on_action_creation = on_snapshot = on_capture = on_deletion_reservation = {};
currentSong = nullptr;
on_row_creation = on_expression_creation = on_parameter_creation = on_node_reservation = on_note_insertion =
    on_node_transfer = {};
} // namespace note_move_action_failure_test
}
;
TEST(NoteMoveActionFailure, placement_without_undo_action_still_restores_note) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	view.editPadPresses[0].isActive = true;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, row.notes);
	LONGS_EQUAL(4, row.last_position);
	CHECK(row.received_action == nullptr);
	CHECK(view.editPadPresses[0].isActive);
	CHECK(view.editPadPresses[0].deleteOnScroll);
	CHECK(!view.editPadPresses[0].deleteOnDepress);
	LONGS_EQUAL(1, view.finalized);
	LONGS_EQUAL(0, display_instance.errors);
}
TEST(NoteMoveActionFailure, successful_action_retains_scroll_bookkeeping_and_note_placement) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	Action action;
	actionLogger.result = &action;
	clip.stack.row = &row;
	view.editPadPresses[0].isActive = true;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, action.updates);
	LONGS_EQUAL(1, row.notes);
	CHECK(row.received_action == &action);
}
TEST(NoteMoveActionFailure, failed_row_creation_keeps_pending_note_and_cache_for_retry) {
	InstrumentClipView view;
	InstrumentClip clip;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.mpeCachedYet = pending.deleteOnScroll = true;
	pending.stolenMPE[0].num = 3;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	CHECK(pending.isActive);
	CHECK(pending.mpeCachedYet);
	CHECK(!pending.deleteOnDepress);
	CHECK(!pending.deleteOnScroll);
	LONGS_EQUAL(3, pending.stolenMPE[0].num);
	LONGS_EQUAL(4, pending.intendedPos);
	LONGS_EQUAL(0, view.ended);
	LONGS_EQUAL(1, display_instance.errors);
	NoteRow destination;
	clip.stack.row = &destination;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, destination.notes);
	LONGS_EQUAL(4, destination.last_position);
	CHECK(pending.deleteOnScroll);
	CHECK(pending.isActive);
	LONGS_EQUAL(0, view.ended);
}
TEST(NoteMoveActionFailure, missing_kit_row_keeps_existing_cancel_behavior_without_allocating) {
	InstrumentClipView view;
	InstrumentClip clip;
	view.editPadPresses[0].isActive = true;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, true);
	CHECK(!view.editPadPresses[0].isActive);
	LONGS_EQUAL(1, view.ended);
	LONGS_EQUAL(0, view.creations);
	LONGS_EQUAL(0, display_instance.errors);
}
TEST(NoteMoveActionFailure, expression_allocation_failure_precedes_note_insertion_and_allows_retry) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow destination;
	clip.stack.row = &destination;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.mpeCachedYet = pending.deleteOnScroll = true;
	pending.stolenMPE[1].num = 2;
	destination.paramManager.allocation_succeeds = false;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(0, destination.notes);
	LONGS_EQUAL(1, destination.paramManager.allocations);
	LONGS_EQUAL(1, display_instance.errors);
	CHECK(pending.isActive);
	CHECK(pending.mpeCachedYet);
	CHECK(!pending.deleteOnDepress);
	CHECK(!pending.deleteOnScroll);
	LONGS_EQUAL(2, pending.stolenMPE[1].num);
	LONGS_EQUAL(0, view.ended);
	destination.paramManager.allocation_succeeds = true;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, destination.notes);
	LONGS_EQUAL(2, destination.paramManager.allocations);
	LONGS_EQUAL(2, destination.paramManager.expressions.parameters[1].inserted_nodes);
	CHECK(pending.deleteOnScroll);
}
TEST(NoteMoveActionFailure, empty_expression_cache_does_not_allocate_before_placement) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow destination;
	clip.stack.row = &destination;
	view.editPadPresses[0].isActive = view.editPadPresses[0].mpeCachedYet = true;
	destination.paramManager.allocation_succeeds = false;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, destination.notes);
	LONGS_EQUAL(0, destination.paramManager.allocations);
	LONGS_EQUAL(0, display_instance.errors);
}
TEST(NoteMoveActionFailure, parameter_allocation_failure_preserves_entire_cache_before_note_insertion) {
	for (int failure = 0; failure < kNumExpressionDimensions; ++failure) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow destination;
		clip.stack.row = &destination;
		auto& pending = view.editPadPresses[0];
		pending.isActive = pending.mpeCachedYet = pending.deleteOnScroll = true;
		for (int dimension = 0; dimension < kNumExpressionDimensions; ++dimension)
			pending.stolenMPE[dimension].num = dimension + 1;
		auto& expressions = destination.paramManager.expressions;
		expressions.failing_dimension = failure;
		display_instance.errors = 0;
		view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
		LONGS_EQUAL(0, destination.notes);
		LONGS_EQUAL(1, display_instance.errors);
		LONGS_EQUAL(0, view.ended);
		CHECK(pending.isActive);
		CHECK(!pending.deleteOnDepress);
		CHECK(!pending.deleteOnScroll);
		for (int dimension = 0; dimension < kNumExpressionDimensions; ++dimension) {
			LONGS_EQUAL(dimension + 1, pending.stolenMPE[dimension].num);
			LONGS_EQUAL(0, expressions.parameters[dimension].inserted_nodes);
		}
		expressions.failing_dimension = -1;
		view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
		LONGS_EQUAL(1, destination.notes);
		CHECK(pending.deleteOnScroll);
		for (int dimension = 0; dimension < kNumExpressionDimensions; ++dimension) {
			LONGS_EQUAL(dimension + 1, expressions.parameters[dimension].inserted_nodes);
			LONGS_EQUAL(dimension <= failure ? 2 : 1, expressions.creation_requests[dimension]);
		}
	}
}
TEST(NoteMoveActionFailure, preallocation_skips_dimensions_without_cached_nodes) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow destination;
	clip.stack.row = &destination;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.mpeCachedYet = true;
	pending.stolenMPE[2].num = 4;
	destination.paramManager.expressions.failing_dimension = 0;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, destination.notes);
	LONGS_EQUAL(0, destination.paramManager.expressions.creation_requests[0]);
	LONGS_EQUAL(0, destination.paramManager.expressions.creation_requests[1]);
	LONGS_EQUAL(1, destination.paramManager.expressions.creation_requests[2]);
	LONGS_EQUAL(4, destination.paramManager.expressions.parameters[2].inserted_nodes);
	LONGS_EQUAL(0, display_instance.errors);
}
TEST(NoteMoveActionFailure, node_reservation_failure_preserves_all_dimensions_before_note_insertion) {
	for (int failing_dimension = 0; failing_dimension < kNumExpressionDimensions; ++failing_dimension) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow destination;
		clip.stack.row = &destination;
		auto& pending = view.editPadPresses[0];
		pending.isActive = pending.mpeCachedYet = pending.deleteOnScroll = true;
		auto& expressions = destination.paramManager.expressions;
		for (int dimension = 0; dimension < kNumExpressionDimensions; ++dimension) {
			pending.stolenMPE[dimension].num = dimension + 1;
			expressions.parameters[dimension].inserted_nodes = 10 + dimension;
		}
		expressions.parameters[failing_dimension].reservation_succeeds = false;
		display_instance.errors = 0;
		view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
		LONGS_EQUAL(0, destination.notes);
		LONGS_EQUAL(1, display_instance.errors);
		CHECK(pending.isActive);
		CHECK(pending.mpeCachedYet);
		CHECK(!pending.deleteOnDepress);
		CHECK(!pending.deleteOnScroll);
		for (int dimension = 0; dimension < kNumExpressionDimensions; ++dimension) {
			LONGS_EQUAL(dimension + 1, pending.stolenMPE[dimension].num);
			LONGS_EQUAL(10 + dimension, expressions.parameters[dimension].inserted_nodes);
			LONGS_EQUAL(dimension <= failing_dimension ? 1 : 0, expressions.parameters[dimension].reservation_requests);
		}
		expressions.parameters[failing_dimension].reservation_succeeds = true;
		view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
		LONGS_EQUAL(1, destination.notes);
		CHECK(pending.deleteOnScroll);
		for (int dimension = 0; dimension < kNumExpressionDimensions; ++dimension) {
			LONGS_EQUAL(11 + 2 * dimension, expressions.parameters[dimension].inserted_nodes);
		}
	}
}
TEST(NoteMoveActionFailure, failed_note_insertion_after_reservation_does_not_transfer_cached_nodes) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow destination;
	clip.stack.row = &destination;
	destination.insertion_succeeds = false;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.mpeCachedYet = true;
	pending.stolenMPE[0].num = 3;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(0, destination.notes);
	LONGS_EQUAL(1, destination.paramManager.expressions.parameters[0].reservation_requests);
	LONGS_EQUAL(0, destination.paramManager.expressions.parameters[0].inserted_nodes);
	LONGS_EQUAL(3, pending.stolenMPE[0].num);
	CHECK(pending.isActive);
	CHECK(!pending.deleteOnScroll);
	CHECK(!pending.deleteOnDepress);
	destination.insertion_succeeds = true;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, destination.notes);
	LONGS_EQUAL(3, destination.paramManager.expressions.parameters[0].inserted_nodes);
	CHECK(pending.deleteOnScroll);
}
TEST(NoteMoveActionFailure, failed_source_capture_preserves_all_held_notes_and_cleans_staged_records) {
	for (int failure = 0; failure < 6; ++failure) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow rows[2];
		clip.stack.row = &rows[0];
		clip.second_stack.row = &rows[1];
		copied_records = freed_records = 0;
		for (int index = 0; index < 2; ++index) {
			auto& pending = view.editPadPresses[index];
			pending.isActive = pending.deleteOnScroll = true;
			pending.yDisplay = index;
			auto& expressions = rows[index].paramManager.expressions;
			rows[index].paramManager.summary.paramCollection = &expressions;
			for (int dimension = 0; dimension < 3; ++dimension)
				expressions.allocated[dimension] = true;
		}
		rows[failure / 3].paramManager.expressions.parameters[failure % 3].capture_succeeds = false;
		CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
		LONGS_EQUAL(failure, copied_records);
		LONGS_EQUAL(copied_records, freed_records);
		for (int index = 0; index < 2; ++index) {
			LONGS_EQUAL(0, rows[index].deletions);
			CHECK_FALSE(view.editPadPresses[index].mpeCachedYet);
			for (int dimension = 0; dimension < 3; ++dimension) {
				LONGS_EQUAL(2, rows[index].paramManager.expressions.parameters[dimension].source_nodes);
				LONGS_EQUAL(0, view.editPadPresses[index].stolenMPE[dimension].num);
			}
		}
		rows[failure / 3].paramManager.expressions.parameters[failure % 3].capture_succeeds = true;
		CHECK_TRUE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
		for (int index = 0; index < 2; ++index) {
			LONGS_EQUAL(1, rows[index].deletions);
			CHECK(view.editPadPresses[index].mpeCachedYet);
			for (int dimension = 0; dimension < 3; ++dimension) {
				LONGS_EQUAL(0, rows[index].paramManager.expressions.parameters[dimension].source_nodes);
				LONGS_EQUAL(2, view.editPadPresses[index].stolenMPE[dimension].num);
			}
		}
		LONGS_EQUAL(6, copied_records - freed_records);
	}
}
TEST(NoteMoveActionFailure, source_snapshot_failure_stops_before_note_or_expression_deletion) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	row.paramManager.summary.paramCollection = &row.paramManager.expressions;
	row.paramManager.expressions.allocated[0] = row.paramManager.expressions.allocated[1] = true;
	view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
	Action action;
	action.fail_snapshot_at = 1;
	actionLogger.result = &action;
	CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
	LONGS_EQUAL(0, row.deletions);
	LONGS_EQUAL(1, copied_records);
	LONGS_EQUAL(1, freed_records);
	CHECK_FALSE(view.editPadPresses[0].mpeCachedYet);
	LONGS_EQUAL(2, row.paramManager.expressions.parameters[0].source_nodes);
}
TEST(NoteMoveActionFailure, previously_cached_press_does_not_recapture_source) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	row.paramManager.summary.paramCollection = &row.paramManager.expressions;
	row.paramManager.expressions.allocated[0] = true;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.deleteOnScroll = pending.mpeCachedYet = true;
	pending.stolenMPE[0].num = 7;
	CHECK_TRUE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
	LONGS_EQUAL(0, copied_records);
	LONGS_EQUAL(1, row.deletions);
	LONGS_EQUAL(7, pending.stolenMPE[0].num);
	LONGS_EQUAL(1, row.paramManager.expressions.parameters[0].removals);
}
TEST(NoteMoveActionFailure, deletion_reservation_failure_preserves_every_source_and_allows_retry) {
	for (int failure = 0; failure < 2; ++failure) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow rows[2];
		clip.stack.row = &rows[0];
		clip.second_stack.row = &rows[1];
		Action action;
		actionLogger.result = &action;
		deletion_attempts = deletion_freed = deletion_consumed = 0;
		deletion_fail_at = failure;
		for (int index = 0; index < 2; ++index) {
			view.editPadPresses[index].isActive = view.editPadPresses[index].deleteOnScroll = true;
			view.editPadPresses[index].yDisplay = index;
		}
		CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
		LONGS_EQUAL(failure, deletion_freed);
		LONGS_EQUAL(0, deletion_consumed);
		for (int index = 0; index < 2; ++index) {
			LONGS_EQUAL(0, rows[index].deletions);
			CHECK_FALSE(view.editPadPresses[index].mpeCachedYet);
		}
		deletion_fail_at = -1;
		CHECK(view.scrollVertical_grabNotesPressed(nullptr, &clip));
		LONGS_EQUAL(2, deletion_consumed);
		LONGS_EQUAL(failure, deletion_freed);
		LONGS_EQUAL(1, rows[0].deletions);
		LONGS_EQUAL(1, rows[1].deletions);
	}
}
TEST(NoteMoveActionFailure, deletion_reservation_structural_change_cancels_before_source_removal) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	Action action;
	actionLogger.result = &action;
	view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
	invalidate_deletion_reservation = true;
	CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
	LONGS_EQUAL(0, row.deletions);
	LONGS_EQUAL(1, deletion_freed);
	CHECK_FALSE(view.editPadPresses[0].mpeCachedYet);
}
TEST(NoteMoveActionFailure, action_creation_invalidation_stops_before_accessing_source_rows) {
	for (bool with_action : {false, true}) {
		InstrumentClipView view;
		Action action;
		actionLogger.result = with_action ? &action : nullptr;
		view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
		on_action_creation = [] {
			deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local)
			    .structural_refresh.request();
		};
		// No clip can be touched after invalidation.
		CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, nullptr));
		LONGS_EQUAL(0, copied_records);
		LONGS_EQUAL(0, deletion_attempts);
	}
}
TEST(NoteMoveActionFailure, snapshot_invalidation_stops_before_capture_on_success_or_failure) {
	for (bool snapshot_succeeds : {false, true}) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow row;
		clip.stack.row = &row;
		row.paramManager.summary.paramCollection = &row.paramManager.expressions;
		row.paramManager.expressions.allocated[0] = true;
		view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
		Action action;
		action.fail_snapshot_at = snapshot_succeeds ? -1 : 0;
		actionLogger.result = &action;
		on_snapshot = [] {
			deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
			    .structural_refresh.request();
		};
		CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
		LONGS_EQUAL(0, row.paramManager.expressions.parameters[0].captures);
		LONGS_EQUAL(0, row.deletions);
		LONGS_EQUAL(0, deletion_attempts);
		CHECK_FALSE(view.editPadPresses[0].mpeCachedYet);
	}
}
TEST(NoteMoveActionFailure, capture_invalidation_without_action_frees_staging_before_cache_commit) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	row.paramManager.summary.paramCollection = &row.paramManager.expressions;
	row.paramManager.expressions.allocated[0] = true;
	view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
	on_capture = [] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
	};
	CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
	LONGS_EQUAL(1, copied_records);
	LONGS_EQUAL(1, freed_records);
	LONGS_EQUAL(0, row.deletions);
	CHECK_FALSE(view.editPadPresses[0].mpeCachedYet);
	LONGS_EQUAL(0, view.editPadPresses[0].stolenMPE[0].num);
}
TEST(NoteMoveActionFailure, song_replacement_during_action_creation_stops_before_source_access) {
	InstrumentClipView view;
	view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
	int replacement_song = 0;
	on_action_creation = [&] { currentSong = &replacement_song; };
	CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, nullptr));
	LONGS_EQUAL(0, deletion_attempts);
	currentSong = nullptr;
}
TEST(NoteMoveActionFailure, temporary_peer_scope_during_action_creation_allows_normal_grab) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
	on_action_creation = [] { deluge::gui::ui_session::Scope peer(deluge::gui::ui_session::Id::Remote); };
	CHECK(view.scrollVertical_grabNotesPressed(nullptr, &clip));
	LONGS_EQUAL(1, row.deletions);
	CHECK(view.editPadPresses[0].mpeCachedYet);
}
TEST(NoteMoveActionFailure, gesture_changes_during_preparation_cancel_without_overwriting_new_state) {
	for (int boundary = 0; boundary < 4; ++boundary) {
		for (int change = 0; change < 15; ++change) {
			InstrumentClipView view;
			InstrumentClip clip;
			NoteRow row;
			clip.stack.row = &row;
			row.paramManager.summary.paramCollection = &row.paramManager.expressions;
			row.paramManager.expressions.allocated[0] = true;
			auto& pending = view.editPadPresses[0];
			pending.isActive = pending.deleteOnScroll = true;
			Action action;
			actionLogger.result = &action;
			copied_records = freed_records = deletion_freed = deletion_attempts = deletion_consumed = 0;
			on_action_creation = on_snapshot = on_capture = on_deletion_reservation = {};
			auto mutate = [&] {
				switch (change) {
				case 0:
					pending.isActive = false;
					break;
				case 1:
					pending.intendedPos = 9;
					break;
				case 2:
					pending.intendedVelocity = 72;
					break;
				case 3:
					pending.mpeCachedYet = true;
					pending.stolenMPE[0].num = 7;
					pending.stolenMPE[0].nodes = &row;
					break;
				case 4:
					view.editPadPresses[1].isActive = true;
					break;
				case 5:
					pending.xDisplay = 1;
					break;
				case 6:
					pending.yDisplay = 1;
					break;
				case 7:
					pending.deleteOnDepress = !pending.deleteOnDepress;
					break;
				case 8:
					pending.deleteOnScroll = false;
					break;
				case 9:
					pending.isBlurredSquare = true;
					break;
				case 10:
					pending.intendedLength = 9;
					break;
				case 11:
					pending.intendedProbability = 10;
					break;
				case 12:
					pending.intendedIterance = 1;
					break;
				case 13:
					pending.intendedFill = 1;
					break;
				case 14:
					pending.stolenMPE[1].num = 2;
					pending.stolenMPE[1].nodes = &clip;
					break;
				}
			};
			if (boundary == 0)
				on_action_creation = mutate;
			if (boundary == 1)
				on_snapshot = mutate;
			if (boundary == 2)
				on_capture = mutate;
			if (boundary == 3)
				on_deletion_reservation = mutate;
			CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
			LONGS_EQUAL(0, row.deletions);
			LONGS_EQUAL(copied_records, freed_records);
			LONGS_EQUAL(deletion_attempts, deletion_freed);
			LONGS_EQUAL(0, deletion_consumed);
			LONGS_EQUAL(change == 1 ? 9 : 4, pending.intendedPos);
			LONGS_EQUAL(change == 2 ? 72 : 100, pending.intendedVelocity);
			CHECK_EQUAL(change != 0, pending.isActive);
			CHECK_EQUAL(change == 3, pending.mpeCachedYet);
			LONGS_EQUAL(change == 3 ? 7 : 0, pending.stolenMPE[0].num);
			if (change == 3)
				POINTERS_EQUAL(&row, pending.stolenMPE[0].nodes);
		}
	}
}
TEST(NoteMoveActionFailure, cached_record_replacement_with_same_count_cancels_deletion) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	Action action;
	actionLogger.result = &action;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.deleteOnScroll = pending.mpeCachedYet = true;
	pending.stolenMPE[0].num = 2;
	pending.stolenMPE[0].nodes = &row;
	on_deletion_reservation = [&] { pending.stolenMPE[0].nodes = &clip; };
	CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
	LONGS_EQUAL(0, row.deletions);
	LONGS_EQUAL(1, deletion_freed);
	LONGS_EQUAL(2, pending.stolenMPE[0].num);
	POINTERS_EQUAL(&clip, pending.stolenMPE[0].nodes);
}
TEST(NoteMoveActionFailure, identical_repress_during_preparation_cancels_the_older_grab) {
	for (int boundary = 0; boundary < 4; ++boundary) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow row;
		clip.stack.row = &row;
		row.paramManager.summary.paramCollection = &row.paramManager.expressions;
		row.paramManager.expressions.allocated[0] = true;
		auto& pending = view.editPadPresses[0];
		pending.isActive = pending.deleteOnScroll = true;
		Action action;
		actionLogger.result = &action;
		copied_records = freed_records = deletion_freed = deletion_attempts = deletion_consumed = 0;
		on_action_creation = on_snapshot = on_capture = on_deletion_reservation = {};
		// Same final pad/note/cache metadata, but a completed release changed its identity.
		auto repress = [&] { ++pending.gesture_revision; };
		if (boundary == 0)
			on_action_creation = repress;
		if (boundary == 1)
			on_snapshot = repress;
		if (boundary == 2)
			on_capture = repress;
		if (boundary == 3)
			on_deletion_reservation = repress;
		CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
		LONGS_EQUAL(0, row.deletions);
		LONGS_EQUAL(copied_records, freed_records);
		LONGS_EQUAL(deletion_attempts, deletion_freed);
		CHECK(pending.isActive);
		LONGS_EQUAL(1, pending.gesture_revision);
		LONGS_EQUAL(4, pending.intendedPos);
		CHECK_FALSE(pending.mpeCachedYet);
	}
}
TEST(NoteMoveActionFailure, placement_preparation_invalidation_stops_before_note_insertion) {
	for (int boundary = 0; boundary < 4; ++boundary) {
		for (bool change_gesture : {false, true}) {
			InstrumentClipView view;
			InstrumentClip clip;
			NoteRow row;
			clip.stack.row = boundary == 0 ? nullptr : &row;
			view.failed_creation.row = &row;
			auto& pending = view.editPadPresses[0];
			pending.isActive = pending.mpeCachedYet = pending.deleteOnScroll = true;
			pending.stolenMPE[0].num = 2;
			on_row_creation = on_expression_creation = on_parameter_creation = on_node_reservation = {};
			auto invalidate = [&] {
				if (change_gesture)
					++pending.gesture_revision;
				else
					deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
					    .structural_refresh.request();
			};
			if (boundary == 0)
				on_row_creation = invalidate;
			if (boundary == 1)
				on_expression_creation = invalidate;
			if (boundary == 2)
				on_parameter_creation = invalidate;
			if (boundary == 3)
				on_node_reservation = invalidate;
			view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
			LONGS_EQUAL(0, row.notes);
			LONGS_EQUAL(0, row.paramManager.expressions.parameters[0].inserted_nodes);
			CHECK(pending.deleteOnScroll);
			CHECK(pending.deleteOnDepress);
			LONGS_EQUAL(2, pending.stolenMPE[0].num);
			LONGS_EQUAL(0, view.finalized);
		}
	}
}
TEST(NoteMoveActionFailure, placement_action_invalidation_does_not_access_clip) {
	InstrumentClipView view;
	Action action;
	actionLogger.result = &action;
	on_action_creation = [] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local).structural_refresh.request();
	};
	view.scrollVertical_placeNotesPressed(nullptr, nullptr, false);
	LONGS_EQUAL(0, action.updates);
}
TEST(NoteMoveActionFailure, insertion_invalidation_stops_cache_transfer_and_preserves_gesture_flags) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.mpeCachedYet = true;
	pending.stolenMPE[0].num = 2;
	on_note_insertion = [&] { ++pending.gesture_revision; };
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, row.notes);
	LONGS_EQUAL(0, row.paramManager.expressions.parameters[0].inserted_nodes);
	CHECK(pending.deleteOnDepress);
	CHECK_FALSE(pending.deleteOnScroll);
	LONGS_EQUAL(0, view.finalized);
}
TEST(NoteMoveActionFailure, placement_accepts_its_own_row_creation_in_either_session) {
	for (auto owner : {deluge::gui::ui_session::Id::Local, deluge::gui::ui_session::Id::Remote}) {
		deluge::gui::ui_session::Scope scope(owner);
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow row;
		view.failed_creation.row = &row;
		view.editPadPresses[0].isActive = true;
		view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
		LONGS_EQUAL(1, row.notes);
		CHECK(view.editPadPresses[0].deleteOnScroll);
	}
}
TEST(NoteMoveActionFailure, transfer_invalidation_stops_before_later_expression_dimensions) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow row;
	clip.stack.row = &row;
	auto& pending = view.editPadPresses[0];
	pending.isActive = pending.mpeCachedYet = true;
	pending.stolenMPE[0].num = pending.stolenMPE[1].num = 2;
	on_node_transfer = [&] { ++pending.gesture_revision; };
	view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
	LONGS_EQUAL(1, row.notes);
	LONGS_EQUAL(2, row.paramManager.expressions.parameters[0].inserted_nodes);
	LONGS_EQUAL(0, row.paramManager.expressions.parameters[1].inserted_nodes);
	LONGS_EQUAL(0, view.finalized);
}
TEST(NoteMoveActionFailure, placement_action_creation_rejects_replaced_or_new_gestures) {
	for (int change = 0; change < 3; ++change) {
		InstrumentClipView view;
		Action action;
		actionLogger.result = &action;
		auto& pending = view.editPadPresses[0];
		pending.isActive = true;
		on_action_creation = [&] {
			if (change == 0)
				++pending.gesture_revision;
			if (change == 1)
				pending.isActive = false;
			if (change == 2)
				view.editPadPresses[1].isActive = true;
		};
		view.scrollVertical_placeNotesPressed(nullptr, nullptr, false);
		LONGS_EQUAL(0, action.updates);
		LONGS_EQUAL(0, view.finalized);
	}
}
TEST(NoteMoveActionFailure, placement_does_not_adopt_a_later_slot_replaced_during_an_earlier_note) {
	for (bool initially_active : {false, true}) {
		InstrumentClipView view;
		InstrumentClip clip;
		NoteRow first_row, second_row;
		clip.stack.row = &first_row;
		clip.second_stack.row = &second_row;
		view.editPadPresses[0].isActive = true;
		auto& later = view.editPadPresses[1];
		later.isActive = initially_active;
		later.yDisplay = 1;
		on_note_insertion = [&] {
			later.isActive = true;
			++later.gesture_revision;
		};
		view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
		LONGS_EQUAL(1, first_row.notes);
		LONGS_EQUAL(0, second_row.notes);
		LONGS_EQUAL(1, later.gesture_revision);
		CHECK(later.isActive);
		CHECK(later.deleteOnDepress);
		CHECK_FALSE(later.deleteOnScroll);
		LONGS_EQUAL(0, view.finalized);
	}
}
TEST(NoteMoveActionFailure, placement_continues_after_its_own_missing_kit_row_cancellation) {
	InstrumentClipView view;
	InstrumentClip clip;
	NoteRow second_row;
	clip.second_stack.row = &second_row;
	view.editPadPresses[0].isActive = true;
	view.editPadPresses[1].isActive = true;
	view.editPadPresses[1].yDisplay = 1;
	view.scrollVertical_placeNotesPressed(nullptr, &clip, true);
	CHECK_FALSE(view.editPadPresses[0].isActive);
	LONGS_EQUAL(1, second_row.notes);
	CHECK(view.editPadPresses[1].deleteOnScroll);
	LONGS_EQUAL(1, view.finalized);
}
TEST(NoteMoveActionFailure, placement_rejects_note_and_cache_changes_without_release) {
	for (int boundary = 0; boundary < 6; ++boundary) {
		for (int change = 0; change < 4; ++change) {
			InstrumentClipView view;
			InstrumentClip clip;
			NoteRow row;
			clip.stack.row = &row;
			auto& pending = view.editPadPresses[0];
			pending.isActive = pending.mpeCachedYet = true;
			pending.stolenMPE[0] = {2, &row};
			on_action_creation = on_expression_creation = on_parameter_creation = on_node_reservation =
			    on_note_insertion = on_node_transfer = {};
			auto mutate = [&] {
				if (change == 0)
					pending.intendedPos = 9;
				if (change == 1)
					pending.intendedVelocity = 72;
				if (change == 2)
					pending.stolenMPE[0].nodes = &clip;
				if (change == 3)
					pending.stolenMPE[0].num = 3;
			};
			if (boundary == 0)
				on_action_creation = mutate;
			if (boundary == 1)
				on_expression_creation = mutate;
			if (boundary == 2)
				on_parameter_creation = mutate;
			if (boundary == 3)
				on_node_reservation = mutate;
			if (boundary == 4)
				on_note_insertion = mutate;
			if (boundary == 5)
				on_node_transfer = mutate;
			view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
			LONGS_EQUAL(boundary >= 4 ? 1 : 0, row.notes);
			LONGS_EQUAL(boundary == 5 ? 2 : 0, row.paramManager.expressions.parameters[0].inserted_nodes);
			LONGS_EQUAL(0, view.finalized);
			LONGS_EQUAL(0, pending.gesture_revision);
			LONGS_EQUAL(change == 0 ? 9 : 4, pending.intendedPos);
			LONGS_EQUAL(change == 1 ? 72 : 100, pending.intendedVelocity);
			LONGS_EQUAL(change == 3 ? 3 : 2, pending.stolenMPE[0].num);
			POINTERS_EQUAL(change == 2 ? static_cast<void*>(&clip) : static_cast<void*>(&row),
			               pending.stolenMPE[0].nodes);
		}
	}
}
TEST(NoteMoveActionFailure, later_gesture_edits_during_preparation_stop_before_current_note_insertion) {
	for (int boundary = 0; boundary < 4; ++boundary) {
		for (int change = 0; change < 4; ++change) {
			InstrumentClipView view;
			InstrumentClip clip;
			NoteRow first_row, second_row;
			clip.stack.row = boundary == 0 ? nullptr : &first_row;
			view.failed_creation.row = &first_row;
			clip.second_stack.row = &second_row;
			auto& current_press = view.editPadPresses[0];
			current_press.isActive = current_press.mpeCachedYet = true;
			current_press.stolenMPE[0] = {2, &first_row};
			auto& later = view.editPadPresses[1];
			later.isActive = change != 0;
			later.yDisplay = 1;
			later.stolenMPE[0] = {2, &second_row};
			on_row_creation = on_expression_creation = on_parameter_creation = on_node_reservation = {};
			auto mutate = [&] {
				if (change == 0)
					later.isActive = true;
				if (change == 1)
					++later.gesture_revision;
				if (change == 2)
					later.intendedVelocity = 72;
				if (change == 3)
					later.stolenMPE[0].nodes = &clip;
			};
			if (boundary == 0)
				on_row_creation = mutate;
			if (boundary == 1)
				on_expression_creation = mutate;
			if (boundary == 2)
				on_parameter_creation = mutate;
			if (boundary == 3)
				on_node_reservation = mutate;
			view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
			LONGS_EQUAL(0, first_row.notes);
			LONGS_EQUAL(0, second_row.notes);
			LONGS_EQUAL(0, first_row.paramManager.expressions.parameters[0].inserted_nodes);
			CHECK(current_press.deleteOnDepress);
			CHECK_FALSE(current_press.deleteOnScroll);
			LONGS_EQUAL(2, current_press.stolenMPE[0].num);
			CHECK(later.isActive);
			LONGS_EQUAL(change == 1 ? 1 : 0, later.gesture_revision);
			LONGS_EQUAL(change == 2 ? 72 : 100, later.intendedVelocity);
			if (change == 3)
				POINTERS_EQUAL(&clip, later.stolenMPE[0].nodes);
			LONGS_EQUAL(0, view.finalized);
		}
	}
}
TEST(NoteMoveActionFailure, grab_stops_if_callbacks_replace_the_undo_action) {
	for (int boundary = 0; boundary < 3; ++boundary) {
		for (bool reuse_address : {false, true}) {
			InstrumentClipView view;
			InstrumentClip clip;
			NoteRow row;
			clip.stack.row = &row;
			row.paramManager.summary.paramCollection = &row.paramManager.expressions;
			row.paramManager.expressions.allocated[0] = true;
			view.editPadPresses[0].isActive = view.editPadPresses[0].deleteOnScroll = true;
			Action original, replacement;
			actionLogger.result = &original;
			copied_records = freed_records = deletion_attempts = deletion_freed = 0;
			on_snapshot = on_capture = on_deletion_reservation = {};
			auto replace = [&] {
				if (reuse_address)
					++original.action_identity;
				else
					actionLogger.firstAction[BEFORE] = &replacement;
			};
			if (boundary == 0)
				on_snapshot = replace;
			if (boundary == 1)
				on_capture = replace;
			if (boundary == 2)
				on_deletion_reservation = replace;
			CHECK_FALSE(view.scrollVertical_grabNotesPressed(nullptr, &clip));
			LONGS_EQUAL(0, row.deletions);
			LONGS_EQUAL(copied_records, freed_records);
			LONGS_EQUAL(deletion_attempts, deletion_freed);
			CHECK_FALSE(view.editPadPresses[0].mpeCachedYet);
		}
	}
}
TEST(NoteMoveActionFailure, placement_stops_if_callbacks_replace_the_undo_action) {
	for (int boundary = 0; boundary < 5; ++boundary) {
		for (bool reuse_address : {false, true}) {
			InstrumentClipView view;
			InstrumentClip clip;
			NoteRow row;
			clip.stack.row = &row;
			auto& pending = view.editPadPresses[0];
			pending.isActive = pending.mpeCachedYet = true;
			pending.stolenMPE[0] = {2, &row};
			Action original, replacement;
			actionLogger.result = &original;
			on_expression_creation = on_parameter_creation = on_node_reservation = on_note_insertion =
			    on_node_transfer = {};
			auto replace = [&] {
				if (reuse_address)
					++original.action_identity;
				else
					actionLogger.firstAction[BEFORE] = &replacement;
			};
			if (boundary == 0)
				on_expression_creation = replace;
			if (boundary == 1)
				on_parameter_creation = replace;
			if (boundary == 2)
				on_node_reservation = replace;
			if (boundary == 3)
				on_note_insertion = replace;
			if (boundary == 4)
				on_node_transfer = replace;
			view.scrollVertical_placeNotesPressed(nullptr, &clip, false);
			LONGS_EQUAL(boundary >= 3 ? 1 : 0, row.notes);
			LONGS_EQUAL(boundary == 4 ? 2 : 0, row.paramManager.expressions.parameters[0].inserted_nodes);
			LONGS_EQUAL(0, view.finalized);
			LONGS_EQUAL(2, pending.stolenMPE[0].num);
		}
	}
}
} // namespace note_move_action_failure_test
