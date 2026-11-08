#include "CppUTest/TestHarness.h"
#include "gui/views/automation/editor_layout/note/expression_values.h"

using namespace note_expression;

TEST_GROUP(NoteExpression){};

TEST(NoteExpression, BipolarDimensionsHaveNeutralAndFullRange) {
	CHECK_EQUAL(INT32_MIN, valueFromRow(0, true));
	CHECK_EQUAL(0, valueFromRow(4, true));
	CHECK_EQUAL(INT32_MAX, valueFromRow(7, true));
	for (int row = 1; row < 8; ++row) {
		CHECK(valueFromRow(row, true) > valueFromRow(row - 1, true));
	}
}

TEST(NoteExpression, PressureIsUnipolar) {
	CHECK_EQUAL(0, valueFromRow(0, false));
	CHECK_EQUAL(INT32_MAX, valueFromRow(7, false));
	for (int row = 1; row < 8; ++row) {
		CHECK(valueFromRow(row, false) > valueFromRow(row - 1, false));
	}
}

TEST(NoteExpression, LongNoteCanBeEditedAtHeadAndTail) {
	auto head = editableRegion(12, 24, 96, 12, 24);
	auto tail = editableRegion(12, 24, 96, 24, 36);
	CHECK_EQUAL(12, head.start);
	CHECK_EQUAL(24, head.end);
	CHECK_EQUAL(24, tail.start);
	CHECK_EQUAL(36, tail.end);
}

TEST(NoteExpression, OffGridNoteDoesNotWriteOutsideItsBoundaries) {
	auto region = editableRegion(5, 4, 96, 0, 12);
	CHECK_EQUAL(5, region.start);
	CHECK_EQUAL(9, region.end);
}

TEST(NoteExpression, WrappedTailUsesRowLoopLength) {
	auto head = editableRegion(42, 12, 48, 36, 48);
	auto tail = editableRegion(42, 12, 48, 0, 12);
	CHECK_EQUAL(42, head.start);
	CHECK_EQUAL(48, head.end);
	CHECK_EQUAL(0, tail.start);
	CHECK_EQUAL(6, tail.end);
}

TEST(NoteExpression, EmptyRegionDoesNotEditFollowingGap) {
	auto region = editableRegion(0, 6, 96, 12, 24);
	CHECK(region.end <= region.start);
}
