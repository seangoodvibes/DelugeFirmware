/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <climits>
#include <cstdint>
#include <span>

namespace deluge::gui {
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
