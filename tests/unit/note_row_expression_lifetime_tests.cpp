#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace note_row_expression_lifetime_test {
constexpr int kNumExpressionDimensions = 3;
enum class ClipType { INSTRUMENT, AUDIO };
struct Song {};
Song song;
Song* currentSong = &song;
std::function<void()> on_expression, on_param, on_report, on_write;
int writes = 0, reports = 0;
struct ModelStackWithAutoParam;
struct AutoParam {
	int getDistanceToNextNode(ModelStackWithAutoParam*, uint32_t, bool) { return 20; }
	void setValueForRegion(int, int, int32_t, ModelStackWithAutoParam*) {
		++writes;
		if (on_write)
			on_write();
	}
	void setCurrentValueInResponseToUserInput(int32_t, ModelStackWithAutoParam*, bool, uint32_t, bool, bool) {
		++writes;
		if (on_write)
			on_write();
	}
};
struct ExpressionParamSet {
	AutoParam value;
	AutoParam* slot = &value;
	AutoParam* getParam(int, bool create = true, const deluge::lifetime::callback_validation* validation = nullptr) {
		if (validation && !validation->valid())
			return nullptr;
		if (create && on_param)
			on_param();
		if (validation && !validation->valid())
			return nullptr;
		return slot;
	}
};
struct ParamCollectionSummary {
	ExpressionParamSet* paramCollection;
};
struct ParamManager {
	ExpressionParamSet expression;
	ParamCollectionSummary summary{&expression};
	bool fail_creation = false;
	bool ensureExpressionParamSetExists(bool, const deluge::lifetime::callback_validation* validation) {
		if (!validation->valid())
			return false;
		if (on_expression)
			on_expression();
		return validation->valid() && !fail_creation;
	}
	ParamCollectionSummary* getExpressionParamSetSummary() { return &summary; }
	ExpressionParamSet* getExpressionParamSet() { return summary.paramCollection; }
};
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
};
struct NoteRow;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	ClipType type = ClipType::INSTRUMENT;
	Output* output = nullptr;
	NoteRow* row = nullptr;
	NoteRow* find_note_row_from_id(int id) { return id == 7 ? row : nullptr; }
};
struct ModelStackWithNoteRow {
	Song* song = &note_row_expression_lifetime_test::song;
	InstrumentClip* clip = nullptr;
	NoteRow* row = nullptr;
	int noteRowId = 7;
	ModelStackWithAutoParam* auto_stack = nullptr;
	NoteRow* getNoteRowAllowNull() { return row; }
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
	InstrumentClip* getTimelineCounter() { return clip; }
	uint32_t getLivePos() { return 10; }
	bool isCurrentlyPlayingReversed() { return false; }
	ModelStackWithNoteRow* addOtherTwoThingsAutomaticallyGivenNoteRow() { return this; }
	ModelStackWithAutoParam* addParam(ExpressionParamSet*, ParamCollectionSummary*, int, AutoParam*);
};
struct ModelStackWithAutoParam : ModelStackWithNoteRow {};
ModelStackWithAutoParam* ModelStackWithNoteRow::addParam(ExpressionParamSet*, ParamCollectionSummary*, int,
                                                         AutoParam*) {
	static_cast<ModelStackWithNoteRow&>(*auto_stack) = *this;
	return auto_stack;
}
struct NoteRow {
	uint64_t undo_identity = 1;
	uint32_t ignoreNoteOnsBefore_ = 0;
	ParamManager paramManager;
	int getDistanceToNextNote(uint32_t, ModelStackWithAutoParam*, bool) { return 10; }
	bool recordPolyphonicExpressionEvent(ModelStackWithNoteRow*, int32_t, int32_t, bool);
};
struct View {
	int modLength = 0, modPos = 0, modNoteRowId = 7;
	ModelStackWithNoteRow activeModControllableModelStack;
} view;
View& view_for_session() {
	return view;
}
struct InstrumentClipView {
	void reportMPEValueForNoteEditing(int, int32_t) {
		++reports;
		if (on_report)
			on_report();
	}
} clip_view;
InstrumentClipView& instrument_clip_view_for_session() {
	return clip_view;
}
#include "note_row_expression_lifetime.inc"
} // namespace note_row_expression_lifetime_test
using namespace note_row_expression_lifetime_test;
TEST_GROUP(note_row_expression_lifetime) {
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<Output> output;
	std::unique_ptr<NoteRow> row;
	ModelStackWithNoteRow stack;
	ModelStackWithAutoParam auto_stack;
	void setup() override {
		clip = std::make_unique<InstrumentClip>();
		output = std::make_unique<Output>();
		row = std::make_unique<NoteRow>();
		clip->output = output.get();
		clip->row = row.get();
		stack.clip = clip.get();
		stack.row = row.get();
		stack.auto_stack = &auto_stack;
		view = {};
		view.activeModControllableModelStack.clip = clip.get();
		writes = reports = 0;
		currentSong = &song;
	}
	void teardown() override {
		on_expression = {};
		on_param = {};
		on_report = {};
		on_write = {};
		currentSong = &song;
	}
	bool record(int dimension = 0) {
		return row->recordPolyphonicExpressionEvent(&stack, 123, dimension, true);
	}
	void destroy_owners() {
		row.reset();
		clip.reset();
		output.reset();
	}
};
TEST(note_row_expression_lifetime, live_recording_and_step_edit_write) {
	CHECK(record());
	LONGS_EQUAL(1, writes);
	LONGS_EQUAL(0, reports);
	view.modLength = 4;
	CHECK(record());
	LONGS_EQUAL(2, writes);
	LONGS_EQUAL(1, reports);
}
TEST(note_row_expression_lifetime, expression_creation_can_destroy_owners) {
	on_expression = [&] { destroy_owners(); };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, parameter_creation_can_destroy_owners) {
	on_param = [&] { destroy_owners(); };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, expression_creation_can_remove_only_row) {
	on_expression = [&] {
		clip->row = nullptr;
		row.reset();
	};
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, parameter_creation_rejects_replaced_collection) {
	ExpressionParamSet replacement;
	on_param = [&] { row->paramManager.summary.paramCollection = &replacement; };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, report_can_destroy_owners) {
	view.modLength = 4;
	on_report = [&] { destroy_owners(); };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
	LONGS_EQUAL(1, reports);
}
TEST(note_row_expression_lifetime, report_rejects_removed_parameter_slot) {
	view.modLength = 4;
	on_report = [&] { row->paramManager.expression.slot = nullptr; };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, write_can_destroy_owners_without_post_write_access) {
	on_write = [&] { destroy_owners(); };
	CHECK_FALSE(record());
	LONGS_EQUAL(1, writes);
}
TEST(note_row_expression_lifetime, creation_rejects_reused_row_identity) {
	on_expression = [&] { ++row->undo_identity; };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, creation_preserves_stack_retarget) {
	InstrumentClip replacement;
	on_expression = [&] { stack.clip = &replacement; };
	CHECK_FALSE(record());
	POINTERS_EQUAL(&replacement, stack.clip);
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, unsupported_dimension_and_retired_entry_do_not_write) {
	CHECK_FALSE(record(-1));
	CHECK_FALSE(record(3));
	output->lifetime.retire();
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, allocation_failure_and_early_position_do_not_write) {
	row->paramManager.fail_creation = true;
	CHECK_FALSE(record());
	row->paramManager.fail_creation = false;
	row->ignoreNoteOnsBefore_ = 11;
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}

TEST(note_row_expression_lifetime, successful_scalar_write_can_release_parameter_slot) {
	on_write = [&] { row->paramManager.expression.slot = nullptr; };
	CHECK(record());
	LONGS_EQUAL(1, writes);
}
TEST(note_row_expression_lifetime, report_can_destroy_output_without_writing) {
	view.modLength = 4;
	on_report = [&] { output.reset(); };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, missing_row_or_parameter_rejects_recording) {
	clip->row = nullptr;
	CHECK_FALSE(record());
	clip->row = row.get();
	row->paramManager.expression.slot = nullptr;
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
TEST(note_row_expression_lifetime, creation_rejects_changed_clip_type) {
	on_expression = [&] { clip->type = ClipType::AUDIO; };
	CHECK_FALSE(record());
	LONGS_EQUAL(0, writes);
}
