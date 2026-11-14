#include "CppUTest/TestHarness.h"
#include "gui/views/note_edit_math.h"
#include <climits>

using namespace deluge::gui;
TEST_GROUP(NoteEditMath){};

static int32_t findMove(std::initializer_list<NoteMoveRegion> selected, std::initializer_list<NoteMoveRegion> obstacles,
                        int32_t steps, int32_t zoom = 4) {
	return findNoteGridMove({selected.begin(), selected.size()}, steps, zoom, [&](auto visit) {
		for (const auto& obstacle : obstacles) {
			visit(obstacle);
		}
	});
}

TEST(NoteEditMath, ClearMoveKeepsFineTiming) {
	CHECK_EQUAL(4, findMove({{0, 3, 2, 32}}, {}, 1));
	CHECK_EQUAL(-4, findMove({{0, 7, 2, 32}}, {}, -1));
}

TEST(NoteEditMath, SkipsOccupiedNotesInEitherDirection) {
	CHECK_EQUAL(12, findMove({{0, 0, 4, 32}}, {{0, 4, 8, 32}}, 1));
	CHECK_EQUAL(-12, findMove({{0, 16, 4, 32}}, {{0, 8, 8, 32}}, -1));
}

TEST(NoteEditMath, EntireSelectionMovesBySameDistanceAcrossRows) {
	CHECK_EQUAL(12, findMove({{0, 0, 4, 32}, {1, 8, 4, 32}}, {{1, 12, 8, 32}}, 1));
	// A note on another row is not an obstacle.
	CHECK_EQUAL(4, findMove({{0, 0, 4, 32}}, {{1, 4, 24, 32}}, 1));
}

TEST(NoteEditMath, ProtectsBothSelectedAndUnselectedNoteTails) {
	CHECK_EQUAL(12, findMove({{0, 0, 6, 32}}, {{0, 8, 4, 32}}, 1));
	CHECK_EQUAL(-20, findMove({{0, 24, 4, 32}}, {{0, 8, 14, 32}}, -1));
}

TEST(NoteEditMath, StopsWholeShapeAtBoundaryOrWhenNoGapFits) {
	CHECK_EQUAL(0, findMove({{0, 0, 4, 16}}, {{0, 4, 12, 16}}, 1));
	CHECK_EQUAL(0, findMove({{0, 0, 4, 32}, {1, 28, 4, 32}}, {}, 1));
	CHECK_EQUAL(0, findMove({{0, 0, 4, 32}, {1, 8, 4, 32}}, {}, -1));
	CHECK_EQUAL(0, findMove({{0, 0, 4, 32}, {1, 8, 4, 16}}, {{0, 4, 8, 32}}, 1));
}

TEST(NoteEditMath, MovingSourcesAreAvailableToOtherSelectedNotes) {
	CHECK_EQUAL(4, findMove({{0, 0, 4, 32}, {0, 4, 4, 32}}, {}, 1));
}

TEST(NoteEditMath, ExistingWrappedTailBlocksStartOfLoop) {
	CHECK_EQUAL(-4, findMove({{0, 8, 2, 16}}, {{0, 14, 6, 16}}, -1));
	CHECK_EQUAL(0, findMove({{0, 4, 2, 16}}, {{0, 14, 6, 16}}, -1));
}

TEST(NoteEditMath, SelectedWrappedTailMustFitCompletelyBeforePlacement) {
	CHECK_EQUAL(-2, findMove({{0, 14, 4, 16}}, {}, -1, 1));
	CHECK_EQUAL(-6, findMove({{0, 14, 4, 16}}, {{0, 12, 2, 16}}, -1, 1));
}

TEST(NoteEditMath, AcceleratedMoveCannotOverflowOrWrap) {
	CHECK_EQUAL(0, findMove({{0, 5, 4, 32}}, {}, INT32_MAX, INT32_MAX));
	CHECK_EQUAL(0, findMove({{0, 5, 4, 32}}, {}, INT32_MIN, INT32_MAX));
}

TEST(NoteEditMath, SearchMatchesExhaustiveGridPlacement) {
	// Exhaust all obstacle patterns on a small grid, testing two selected notes
	// as a rigid shape in both directions. Selected sources are excluded.
	constexpr NoteMoveRegion selected[] = {{0, 4, 2, 16}, {0, 8, 2, 16}};
	for (int mask = 0; mask < 256; ++mask) {
		for (int direction : {-1, 1}) {
			auto obstacles = [&](auto visit) {
				for (int cell = 0; cell < 8; ++cell) {
					if (cell != 2 && cell != 4 && (mask & (1 << cell))) {
						visit(NoteMoveRegion{0, cell * 2, 2, 16});
					}
				}
			};
			int expected = 0;
			for (int distance = 2; distance <= 16; distance += 2) {
				bool fits = true;
				for (const auto& note : selected) {
					int start = note.position + direction * distance;
					fits &= start >= 0 && start + note.length <= 16;
					obstacles([&](const auto& obstacle) {
						fits &=
						    !(start < obstacle.position + obstacle.length && start + note.length > obstacle.position);
					});
				}
				if (fits) {
					expected = direction * distance;
					break;
				}
			}
			CHECK_EQUAL(expected, findNoteGridMove(selected, direction, 2, obstacles));
		}
	}
}

TEST(NoteEditMath, NudgeWrapDisplaysSignedDistanceFromOriginalSquare) {
	bool nextSquare = false;
	CHECK_EQUAL(-1, noteNudgeDisplayOffset(95, 96, 24, true, nextSquare));
	CHECK_EQUAL(1, noteNudgeDisplayOffset(-95, 96, 24, true, nextSquare));
}

TEST(NoteEditMath, InitialLateNoteDisplaysDistanceFromNextSquare) {
	bool nextSquare = false;
	CHECK_EQUAL(-1, noteNudgeDisplayOffset(23, 96, 24, false, nextSquare));
	CHECK_TRUE(nextSquare);
	CHECK_EQUAL(-2, noteNudgeDisplayOffset(22, 96, 24, true, nextSquare));
	CHECK_EQUAL(12, noteNudgeDisplayOffset(12, 96, 24, true, nextSquare));
	CHECK_FALSE(nextSquare);
}

TEST(NoteEditMath, OngoingNudgeDoesNotJumpToNextSquareReference) {
	bool nextSquare = false;
	CHECK_EQUAL(13, noteNudgeDisplayOffset(13, 96, 24, true, nextSquare));
	CHECK_FALSE(nextSquare);
}
