/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <climits>
#include <cstdint>
#include <span>

namespace deluge::gui {
inline int32_t resizeNoteLength(int32_t length, int32_t steps, int32_t zoom, int32_t distanceToNextNote) {
	return std::clamp<int64_t>(static_cast<int64_t>(length) + static_cast<int64_t>(steps) * zoom, 1,
	                           distanceToNextNote);
}

struct NoteMoveRegion {
	int32_t row;
	int32_t position;
	int32_t length;
	int32_t loopLength;
};

// Find one common grid-aligned displacement before changing any notes. The caller
// enumerates only unselected notes; selected source positions are free to reuse.
// A blocked candidate jumps past the entire overlapping interval, not one tick at
// a time. Stop at the first row boundary rather than wrapping and splitting a shape.
template <typename ForEachObstacle>
int32_t findNoteGridMove(std::span<const NoteMoveRegion> selected, int32_t steps, int32_t zoom,
                         ForEachObstacle forEachObstacle) {
	if (selected.empty() || !steps || zoom <= 0) {
		return 0;
	}
	const int32_t direction = steps < 0 ? -1 : 1;
	int64_t distance = (steps < 0 ? -static_cast<int64_t>(steps) : steps) * zoom;
	int64_t limit = INT32_MAX;
	int64_t minimum = 0;
	for (const auto& note : selected) {
		if (direction < 0) {
			minimum = std::max(minimum, static_cast<int64_t>(note.position) + note.length - note.loopLength);
		}
		limit = std::min(limit, direction > 0 ? static_cast<int64_t>(note.loopLength) - note.position - note.length
		                                      : static_cast<int64_t>(note.position));
	}
	distance = std::max(distance, ((minimum + zoom - 1) / zoom) * zoom);
	while (distance <= limit) {
		int64_t advance = 0;
		forEachObstacle([&](const NoteMoveRegion& obstacle) {
			for (const auto& note : selected) {
				if (note.row != obstacle.row) {
					continue;
				}
				const int64_t start = note.position + direction * distance;
				const int64_t end = start + note.length;
				auto checkInterval = [&](int64_t obstacleStart, int64_t obstacleEnd) {
					if (start < obstacleEnd && end > obstacleStart) {
						advance = std::max(advance, direction > 0 ? obstacleEnd - start : end - obstacleStart);
					}
				};
				int64_t obstacleEnd = static_cast<int64_t>(obstacle.position) + obstacle.length;
				checkInterval(obstacle.position, std::min<int64_t>(obstacleEnd, obstacle.loopLength));
				if (obstacleEnd > obstacle.loopLength) {
					checkInterval(0, obstacleEnd - obstacle.loopLength);
				}
			}
		});
		if (!advance) {
			return direction * distance;
		}
		distance += ((advance + zoom - 1) / zoom) * zoom;
	}
	return 0;
}

// Paste never truncates or wraps a copied note. An existing wrapped tail also
// occupies the start of its row, even though its note-on lies near the end.
inline bool notePasteFits(int32_t position, int32_t length, int32_t loopLength) {
	return position >= 0 && length > 0 && static_cast<int64_t>(position) + length <= loopLength;
}

inline bool notePasteOverlaps(int32_t position, int32_t length, int32_t otherPosition, int32_t otherLength,
                              int32_t loopLength) {
	int64_t end = static_cast<int64_t>(position) + length;
	int64_t otherEnd = static_cast<int64_t>(otherPosition) + otherLength;
	return (position < otherEnd && end > otherPosition) || (otherEnd > loopLength && position < otherEnd - loopLength);
}

// Match NoteRow::attemptNoteAdd: occupied heads are skipped; a new tail stops
// at the next existing note. Existing notes themselves never change in gentle mode.
inline int32_t gentleNotePasteLength(int32_t position, int32_t length, int32_t otherPosition, int32_t otherLength,
                                     int32_t loopLength) {
	if (notePasteOverlaps(position, 1, otherPosition, otherLength, loopLength)) {
		return 0;
	}
	return otherPosition > position ? std::min(length, otherPosition - position) : length;
}

inline int32_t noteLengthAfterPasteClear(int32_t position, int32_t length, int32_t loopLength, int32_t clearStart,
                                         int64_t clearEnd) {
	if (clearStart >= loopLength || clearEnd <= clearStart) {
		return length;
	}
	if (position >= clearStart && position < clearEnd) {
		return 0;
	}
	if (position < clearStart) {
		return std::min(length, clearStart - position);
	}
	return std::min<int64_t>(length, static_cast<int64_t>(loopLength) + clearStart - position);
}

// Shared by the shortcut popup and the menu. Pass a copy of offsetFromNextSquare
// when reading, so rendering does not change the ongoing nudge gesture.
inline int32_t noteNudgeDisplayOffset(int32_t offset, int32_t loopLength, int32_t squareWidth, bool alreadyNudged,
                                      bool& offsetFromNextSquare) {
	if (offset >= (loopLength >> 1)) {
		offset -= loopLength;
	}
	else if (offset <= -(loopLength >> 1)) {
		offset += loopLength;
	}
	if (offset > 0) {
		if (offset > (squareWidth >> 1)) {
			if (!alreadyNudged) {
				offsetFromNextSquare = true;
			}
		}
		else {
			offsetFromNextSquare = false;
		}
		if (offsetFromNextSquare) {
			offset -= squareWidth;
		}
	}
	return offset;
}
} // namespace deluge::gui
