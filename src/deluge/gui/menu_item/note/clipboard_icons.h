/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>

namespace deluge::gui::menu_item::note {
enum class ClipboardOperation { CUT, COPY, PASTE_FIRMLY, PASTE_GENTLY, DELETE };

// All icons fit an 18 x 15 pixel horizontal-menu content area.
template <typename Canvas>
void drawClipboardIcon(Canvas& canvas, int32_t x, int32_t y, ClipboardOperation operation) {
	switch (operation) {
	case ClipboardOperation::COPY:
		canvas.drawLine(x + 1, y + 3, x + 1, y + 14);
		canvas.drawLine(x + 1, y + 14, x + 12, y + 14);
		canvas.drawLine(x + 12, y + 12, x + 12, y + 14);
		canvas.drawLine(x + 1, y + 3, x + 3, y + 3);
		canvas.drawRectangle(x + 4, y, x + 16, y + 11);
		for (int32_t line = 3; line <= 9; line += 3) {
			canvas.drawLine(x + 6, y + line, x + 14, y + line);
		}
		break;
	case ClipboardOperation::CUT:
		canvas.drawCircle(x + 4, y + 11, 3);
		canvas.drawCircle(x + 13, y + 11, 3);
		canvas.drawLine(x + 5, y + 8, x + 14, y);
		canvas.drawLine(x + 12, y + 8, x + 3, y);
		canvas.drawPixel(x + 8, y + 5);
		break;
	case ClipboardOperation::PASTE_FIRMLY:
	case ClipboardOperation::PASTE_GENTLY:
		canvas.drawRectangle(x + 2, y + 2, x + 15, y + 14);
		canvas.drawRectangle(x + 6, y, x + 11, y + 3);
		// Five-by-seven F/G glyphs on the clipboard, readable without the label.
		if (operation == ClipboardOperation::PASTE_FIRMLY) {
			canvas.drawLine(x + 7, y + 5, x + 7, y + 11);
			canvas.drawLine(x + 7, y + 5, x + 11, y + 5);
			canvas.drawLine(x + 7, y + 8, x + 10, y + 8);
		}
		else {
			canvas.drawLine(x + 7, y + 5, x + 11, y + 5);
			canvas.drawLine(x + 6, y + 6, x + 6, y + 10);
			canvas.drawLine(x + 7, y + 11, x + 11, y + 11);
			canvas.drawLine(x + 11, y + 8, x + 11, y + 10);
			canvas.drawLine(x + 9, y + 8, x + 11, y + 8);
		}
		break;
	case ClipboardOperation::DELETE:
		canvas.drawLine(x + 2, y + 2, x + 16, y + 2);
		canvas.drawRectangle(x + 6, y, x + 12, y + 1);
		canvas.drawRectangle(x + 4, y + 4, x + 14, y + 14);
		canvas.drawLine(x + 7, y + 6, x + 7, y + 12);
		canvas.drawLine(x + 11, y + 6, x + 11, y + 12);
		break;
	}
}
} // namespace deluge::gui::menu_item::note
