/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "hid/led/pad_leds.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_render_data.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/mirror.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

#include <cstring>
#include <limits>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;

namespace PadLEDs {
namespace {
PLACE_SDRAM_BSS deluge::gui::ui_session::State<PadState> states;
bool local_output() {
	return deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local;
}
} // namespace
PadState& state() {
	return states.active();
}

constexpr uint32_t kClipExpandCollapseRefreshMs = 25;

void init() {
	memset(slow_flash_squares_for_session(), 255, sizeof(slow_flash_squares_for_session()));
}

bool shouldNotRenderDuringTimerRoutine() {
	return (rendering_lock_for_session() || currentUIMode == UI_MODE_EXPLODE_ANIMATION
	        || currentUIMode == UI_MODE_IMPLODE_ANIMATION || currentUIMode == UI_MODE_ANIMATION_FADE
	        || currentUIMode == UI_MODE_HORIZONTAL_ZOOM || currentUIMode == UI_MODE_HORIZONTAL_SCROLL
	        || currentUIMode == UI_MODE_INSTRUMENT_CLIP_EXPANDING || currentUIMode == UI_MODE_INSTRUMENT_CLIP_COLLAPSING
	        || currentUIMode == UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING);
}

void clearTickSquares(bool shouldSend) {

	uint32_t colsToSend = 0;

	if (flash_cursor_for_session() == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {

			if (slow_flash_squares_for_session()[y] != 255) {
				colsToSend |= (1 << (slow_flash_squares_for_session()[y] >> 1));
			}
		}
	}

	memset(slow_flash_squares_for_session(), 255, sizeof(slow_flash_squares_for_session()));

	if (shouldSend && flash_cursor_for_session() == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		if (colsToSend) {
			for (int32_t x = 0; x < 8; x++) {
				if (colsToSend & (1 << x)) {
					if (local_output() && uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInColUpdateMessage) {
						break;
					}

					sortLedsForCol(x << 1);
				}
			}
			if (local_output())
				PIC::flush();
		}
	}
}

void setTickSquares(const uint8_t* squares, const uint8_t* colours) {

	uint32_t colsToSend = 0;

	if (flash_cursor_for_session() == FLASH_CURSOR_SLOW) {
		if (!shouldNotRenderDuringTimerRoutine()) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (squares[y] != slow_flash_squares_for_session()[y]
				    || colours[y] != slow_flash_colours_for_session()[y]) {

					// Remember to update the new column
					if (squares[y] != 255) {
						colsToSend |= (1 << (squares[y] >> 1));
					}

					// And the old column
					if (slow_flash_squares_for_session()[y] != 255) {
						colsToSend |= (1 << (slow_flash_squares_for_session()[y] >> 1));
					}
				}
			}
		}
	}
	else if (flash_cursor_for_session() == FLASH_CURSOR_FAST) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (squares[y] != slow_flash_squares_for_session()[y] && squares[y] != 255) {

				int32_t colour = 0;
				if (colours[y] == 1) { // "Muted" colour
					RGB mutedColour = gui::menu_item::mutedColourMenu.getRGB();
					auto transform = [](uint8_t& channel, size_t idx) {
						if (channel >= 64) {
							channel += (1 << idx);
						}
					};

					transform(mutedColour.r, 0);
					transform(mutedColour.g, 1);
					transform(mutedColour.b, 2);
				}
				else if (colours[y] == 2) { // Red
					colour = 0b00000001;
				}

				PadLEDs::flashMainPad(squares[y], y, colour);
			}
		}
	}

	memcpy(slow_flash_squares_for_session(), squares, kDisplayHeight);
	memcpy(slow_flash_colours_for_session(), colours, kDisplayHeight);

	if (flash_cursor_for_session() == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		// Actually send everything, if there was a change
		if (colsToSend) {
			for (int32_t x = 0; x < 8; x++) {
				if (colsToSend & (1 << x)) {
					if (local_output() && uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInColUpdateMessage) {
						break;
					}
					sortLedsForCol(x << 1);
				}
			}
			if (local_output())
				PIC::flush();
		}
	}
}

void clearAllPadsWithoutSending() {
	memset(image_for_session(), 0, sizeof(image_for_session()));
}

void clearMainPadsWithoutSending() {
	for (auto& y : image_for_session()) {
		std::fill(&y[0], &y[kDisplayWidth], gui::colours::black);
	}
}

void clearSideBar() {
	for (auto& y : image_for_session()) {
		y[kDisplayWidth] = gui::colours::black;
		y[kDisplayWidth + 1] = gui::colours::black;
	}

	sendOutSidebarColours();
}

void clearColumnWithoutSending(int32_t x) {
	for (auto& y : image_for_session()) {
		y[x] = gui::colours::black;
	}
}

RGB prepareColour(int32_t x, int32_t y, RGB colourSource);

// You'll want to call uartFlushToPICIfNotSending() after this
void sortLedsForCol(int32_t x) {
	AudioEngine::logAction("MatrixDriver::sortLedsForCol");

	x &= 0b11111110;

	std::array<RGB, kDisplayHeight * 2> doubleColumn{};
	size_t total = 0;
	for (size_t y = 0; y < kDisplayHeight; y++) {
		doubleColumn[total++] = prepareColour(x, y, image_for_session()[y][x]);
	}
	for (size_t y = 0; y < kDisplayHeight; y++) {
		doubleColumn[total++] = prepareColour(x + 1, y, image_for_session()[y][x + 1]);
	}
	state().frame.set_columns(x, doubleColumn);
	if (local_output())
		PIC::setColourForTwoColumns((x >> 1), doubleColumn);
	else {
		deluge::hid::mirror::panel_byte(1 + (x >> 1)); // SET_COLOUR_FOR_TWO_COLUMNS
		for (const RGB& colour : doubleColumn) {
			deluge::hid::mirror::panel_byte(colour.r);
			deluge::hid::mirror::panel_byte(colour.g);
			deluge::hid::mirror::panel_byte(colour.b);
		}
	}
}

const RGB flashColours[3] = {
    {130, 120, 130},
    gui::colours::muted, // Not used anymore
    gui::colours::red,
};

RGB prepareColour(int32_t x, int32_t y, RGB colourSource) {
	if (flash_cursor_for_session() == FLASH_CURSOR_SLOW && slow_flash_squares_for_session()[y] == x
	    && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {
		if (slow_flash_colours_for_session()[y] == 1) { // If it's to be the "muted" colour, get that
			colourSource = gui::menu_item::mutedColourMenu.getRGB();
		}
		else { // Otherwise, pull from a referenced table line
			colourSource = flashColours[slow_flash_colours_for_session()[y]];
		}
	}

	if ((greyout_rows_for_session() || greyout_cols_for_session())
	    && ((greyout_rows_for_session() & (1 << y))
	        || (greyout_cols_for_session() & (1 << (kDisplayWidth + kSideBarWidth - 1 - x))))) {
		return colourSource.greyOut(grey_proportion_for_session());
	}
	return colourSource;
}

void set(Cartesian pad, RGB colour) {
	image_for_session()[pad.y][pad.x] = colour;
}

void writeToSideBar(uint8_t sideBarX, uint8_t yDisplay, uint8_t red, uint8_t green, uint8_t blue) {
	image_for_session()[yDisplay][sideBarX + kDisplayWidth] = RGB(red, green, blue);
}

void refreshSidebarOccupancy(RGB rowImage[], uint8_t rowOccupancyMask[]) {
	for (int32_t x = kDisplayWidth; x < kDisplayWidth + kSideBarWidth; x++) {
		rowOccupancyMask[x] = (rowImage[x] == gui::colours::black) ? 0 : 64;
	}
}

void clearTransitionStoreOffScreenRows() {
	for (int32_t storeRow : {int32_t{0}, int32_t{kDisplayHeight + 1}}) {
		std::fill_n(image_store_for_session()[storeRow], kDisplayWidth + kSideBarWidth, gui::colours::black);
		memset(occupancy_mask_store_for_session()[storeRow], 0, kDisplayWidth + kSideBarWidth);
	}
}

void setupInstrumentClipCollapseAnimation(bool collapsingOutOfClipMinder) {
	clip_length_for_session() = getCurrentClip()->loopLength;
	morph_keyboard_sidebar_for_session() = false;

	if (collapsingOutOfClipMinder) {
		// This shouldn't have to be done every time
		clip_mute_square_colour_for_session() =
		    view_for_session().getClipMuteSquareColour(getCurrentClip(), clip_mute_square_colour_for_session());
	}
}

void enableKeyboardSidebarMorph(RGB sessionSectionColour) {
	morph_keyboard_sidebar_for_session() = true;
	clip_section_square_colour_for_session() = sessionSectionColour;
}

// Smoothstep, in 16.16. Avoids harsh colour changes on the sidebar while rows are moving quickly.
uint16_t smoothProgress(uint16_t progress) {
	uint32_t p = progress;
	uint32_t pSquared = (p * p) >> 16;
	uint32_t pCubed = (pSquared * p) >> 16;
	return static_cast<uint16_t>(std::min<uint32_t>((3 * pSquared) - (2 * pCubed), 65535));
}

void renderInstrumentClipCollapseAnimation(int32_t xStart, int32_t xEndOverall, int32_t progress) {
	AudioEngine::logAction("MatrixDriver::renderCollapseAnimation");

	memset(image_for_session(), 0, sizeof(image_for_session()));
	memset(occupancy_mask_for_session(), 0, sizeof(occupancy_mask_for_session()));

	if (!(isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING))) {
		for (int32_t row = 0; row < kDisplayHeight; row++) {
			image_for_session()[row][kDisplayWidth] = gui::colours::enabled;
			occupancy_mask_for_session()[row][kDisplayWidth] = 64;
		}
	}

	// Do some pre figuring out, which applies to all columns
	uint16_t* intensity1Array = (uint16_t*)miscStringBuffer;
	uint16_t* intensity2Array = (uint16_t*)&miscStringBuffer[kMaxNumAnimatedRows * sizeof(uint16_t)];
	// Full precision row positions are needed for keyboard sidebar colour blends, which depend on proximity to
	// the destination session row.
	int32_t newRowPositionArray[kMaxNumAnimatedRows];
	int8_t newRowPosition1Array[kMaxNumAnimatedRows];

	for (int32_t i = 0; i < num_animated_rows_for_session(); i++) {
		int32_t newRowPosition =
		    (int32_t)animated_row_going_from_for_session()[i] * 65536
		    + ((int32_t)animated_row_going_to_for_session()[i] - animated_row_going_from_for_session()[i])
		          * (65536 - progress);
		newRowPositionArray[i] = newRowPosition;
		newRowPosition1Array[i] = newRowPosition >> 16;
		intensity2Array[i] = newRowPosition; // & 65535;
		intensity1Array[i] = 65535 - intensity2Array[i];
	}

	// Blend amounts for the two sidebar columns. They don't vary per column, so work them out once.
	bool expanding = isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING);
	bool transitioning = expanding || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING);
	bool keyboardSidebarMorphing = morph_keyboard_sidebar_for_session() && transitioning;
	uint16_t clippedProgress = std::min<int32_t>(progress, 65535);
	uint16_t muteBlendProgress = expanding ? smoothProgress(clippedProgress) : clippedProgress;
	// How much of the Session sidebar colour to show. `progress` already runs backwards for a collapse, so this
	// single expression covers both directions: it starts at the Session colours and ends at the keyboard ones when
	// expanding, and does the reverse when collapsing.
	uint16_t keyboardSessionBlend = keyboardSidebarMorphing ? smoothProgress(65535 - clippedProgress) : 0;

	int32_t greyStart = instrument_clip_view_for_session().getSquareFromPos(
	                        clip_length_for_session() - 1, NULL, currentSong->x_scroll_for_session()[NAVIGATION_CLIP])
	                    + 1;
	int32_t xEnd = std::min(kDisplayWidth, greyStart);

	int32_t greyTop, greyBottom;

	if (currentUIMode == UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING) {
		greyTop = kDisplayHeight;
		greyBottom = 0;
	}

	else {
		greyTop = animated_row_going_to_for_session()[0] + 1
		          + (((kDisplayHeight - animated_row_going_to_for_session()[0]) * progress + 32768) >> 16);
		greyBottom = animated_row_going_to_for_session()[0]
		             - (((animated_row_going_to_for_session()[0]) * progress + 32768) >> 16);
		if (greyTop > kDisplayHeight) {
			greyTop = kDisplayHeight;
		}
		if (greyBottom < 0) {
			greyBottom = 0;
		}
	}

	if (xEnd < kDisplayWidth) {

		if (xEnd < 0) {
			xEnd = 0;
		}

		for (int32_t yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
			auto* begin = &image_for_session()[yDisplay][xEnd];
			std::fill(begin, begin + (kDisplayWidth - xEnd), deluge::gui::colours::grey);
		}
	}

	for (int32_t col = xStart; col < xEndOverall; col++) {

		if (col < kDisplayWidth) {
			if (col >= xEnd) {
				continue; // It's beyond the end of the Clip, and it's already been filled in grey
			}

			// Or if it's greyed out cos of triplets...
			if (!instrument_clip_view_for_session().isSquareDefined(
			        col, currentSong->x_scroll_for_session()[NAVIGATION_CLIP])) {
				for (int32_t yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
					PadLEDs::image_for_session()[yDisplay][col] = gui::colours::grey;
				}
				continue;
			}
		}

		bool expandingMuteColumn = expanding && col == kDisplayWidth;
		bool keyboardSidebarColumn =
		    keyboardSidebarMorphing && col >= kDisplayWidth && col < kDisplayWidth + kSideBarWidth;

		if (expandingMuteColumn && animated_row_going_to_for_session()[0] >= 0
		    && animated_row_going_to_for_session()[0] < kDisplayHeight) {
			int32_t sessionMuteIntensity = 65535 - muteBlendProgress;
			PadLEDs::image_for_session()[animated_row_going_to_for_session()[0]][col] =
			    drawSquare(clip_mute_square_colour_for_session(), sessionMuteIntensity,
			               PadLEDs::image_for_session()[animated_row_going_to_for_session()[0]][col],
			               &occupancy_mask_for_session()[animated_row_going_to_for_session()[0]][col], 64);
		}

		for (int32_t i = 0; i < num_animated_rows_for_session(); i++) {
			if (!occupancy_mask_store_for_session()[i][col]) {
				continue; // Nothing to do if there was nothing in this square
			}

			// Work on a local copy so per-row colour morphs do not alter the stored source frame.
			RGB squareColours = image_store_for_session()[i][col];

			int32_t intensity1 = intensity1Array[i];
			int32_t intensity2 = intensity2Array[i];

			if (transitioning) {

				// Keyboard's sidebar columns morph between their keyboard colours and the Session colours of the row
				// they collapse into / expand out of, in whichever direction we're going.
				if (keyboardSidebarColumn) {
					RGB sessionColour = (col == kDisplayWidth) ? clip_mute_square_colour_for_session()
					                                           : clip_section_square_colour_for_session();
					uint16_t sessionBlend = keyboardSessionBlend;
					if (animated_row_going_to_for_session()[i] >= 0
					    && animated_row_going_to_for_session()[i] < kDisplayHeight) {
						constexpr int32_t kKeyboardColourBlendDistance = 3 * 65536;
						// Cap the morph by how far this row still is from the Session row, so pads hold their keyboard
						// colours until they're visually close to it rather than changing colour across the display.
						int32_t distanceFromDestination =
						    newRowPositionArray[i] - ((int32_t)animated_row_going_to_for_session()[i] * 65536);
						if (distanceFromDestination < 0) {
							distanceFromDestination = -distanceFromDestination;
						}
						uint16_t proximityBlend = 0;
						if (distanceFromDestination < kKeyboardColourBlendDistance) {
							// 64-bit intermediate: the numerator reaches 3 * 65536 * 65535, well past INT32_MAX.
							uint32_t proximityProgress =
							    ((uint64_t)(kKeyboardColourBlendDistance - distanceFromDestination) * 65535)
							    / kKeyboardColourBlendDistance;
							proximityBlend = smoothProgress(std::min<uint32_t>(proximityProgress, 65535));
						}
						sessionBlend = std::min(sessionBlend, proximityBlend);
					}
					squareColours = RGB::blend(sessionColour, squareColours, sessionBlend);
				}

				// The clip's audition column has nowhere to go in Session view, so fade it as we go: `progress` runs
				// forwards when expanding and backwards when collapsing, so this fades it in and out respectively.
				else if (col == kDisplayWidth + kSideBarWidth - 1) {
					intensity1 = ((uint32_t)intensity1 * progress) >> 16;
					intensity2 = ((uint32_t)intensity2 * progress) >> 16;
				}

				// If the mute-col, we want to alter the colour
				else if (col == kDisplayWidth) {
					if (expandingMuteColumn) {
						// Fade in the session mute pad underneath as the clip mute column expands out of it.
						intensity1 = ((uint32_t)intensity1 * muteBlendProgress) >> 16;
						intensity2 = ((uint32_t)intensity2 * muteBlendProgress) >> 16;
					}
					squareColours = RGB::blend(squareColours, clip_mute_square_colour_for_session(), muteBlendProgress);
				}
			}

			if (newRowPosition1Array[i] >= 0 && newRowPosition1Array[i] < kDisplayHeight) {
				PadLEDs::image_for_session()[newRowPosition1Array[i]][col] =
				    drawSquare(squareColours, intensity1, PadLEDs::image_for_session()[newRowPosition1Array[i]][col],
				               &occupancy_mask_for_session()[newRowPosition1Array[i]][col],
				               occupancy_mask_store_for_session()[i][col]);
			}

			if (newRowPosition1Array[i] >= -1 && newRowPosition1Array[i] < kDisplayHeight - 1) {
				PadLEDs::image_for_session()[newRowPosition1Array[i] + 1][col] = drawSquare(
				    squareColours, intensity2, PadLEDs::image_for_session()[newRowPosition1Array[i] + 1][col],
				    &occupancy_mask_for_session()[newRowPosition1Array[i] + 1][col],
				    occupancy_mask_store_for_session()[i][col]);
			}
		}
	}

	sendOutMainPadColours();
	sendOutSidebarColours();
}

void setupAudioClipCollapseOrExplodeAnimation(AudioClip* clip) {
	clip_length_for_session() = clip->loopLength;
	audio_clip_colour_for_session() = clip->getColour();

	sample_reversed_for_session() = clip->sampleControls.isCurrentlyReversed();

	Sample* sample = (Sample*)clip->sampleHolder.audioFile;

	if (ALPHA_OR_BETA_VERSION && !sample) {
		FREEZE_WITH_ERROR("E311");
	}

	sample_max_peak_from_zero_for_session() = sample->getMaxPeakFromZero();
	sample_value_centre_point_for_session() = sample->getFoundValueCentrePoint();
	sample_value_span_for_session() = sample->getValueSpan();

	waveform_render_data_for_session() = clip->renderData;
}

void renderAudioClipCollapseAnimation(int32_t progress) {
	memset(image_for_session(), 0, sizeof(image_for_session()));

	int32_t endSquareDisplay = divide_round_negative(
	    clip_length_for_session() - currentSong->x_scroll_for_session()[NAVIGATION_CLIP] - 1,
	    currentSong
	        ->x_zoom_for_session()[NAVIGATION_CLIP]); // Rounds it well down, so we get the "final square" kinda...
	int32_t greyStart = endSquareDisplay + 1;
	int32_t xEnd = std::min(kDisplayWidth, greyStart);

	for (int32_t col = 0; col < xEnd; col++) {
		waveform_renderer_for_session().renderOneColForCollapseAnimation(
		    col, col, sample_max_peak_from_zero_for_session(), progress, PadLEDs::image_for_session(),
		    &waveform_render_data_for_session(), audio_clip_colour_for_session(), sample_reversed_for_session(),
		    sample_value_centre_point_for_session(), sample_value_span_for_session());
	}

	if (xEnd < kDisplayWidth) {

		if (xEnd < 0) {
			xEnd = 0;
		}

		int32_t greyTop =
		    waveform_renderer_for_session().collapseAnimationToWhichRow + 1
		    + (((kDisplayHeight - waveform_renderer_for_session().collapseAnimationToWhichRow) * progress + 32768)
		       >> 16);
		int32_t greyBottom =
		    waveform_renderer_for_session().collapseAnimationToWhichRow
		    - (((waveform_renderer_for_session().collapseAnimationToWhichRow) * progress + 32768) >> 16);

		if (greyTop > kDisplayHeight) {
			greyTop = kDisplayHeight;
		}
		if (greyBottom < 0) {
			greyBottom = 0;
		}

		for (int32_t yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
			auto* begin = &PadLEDs::image_for_session()[yDisplay][xEnd];
			std::fill(begin, begin + (kDisplayWidth - xEnd), gui::colours::grey);
		}
	}

	// What about the sidebar, did I just not animate that?

	sendOutMainPadColours();
}

// 2^16 used in place of "1" in "big" arithmetic below
void renderAudioClipExplodeAnimation(int32_t explodedness, bool shouldSendOut) {

	memset(PadLEDs::image_for_session(), 0, sizeof(PadLEDs::image_for_session()));
	memset(occupancy_mask_for_session(), 0, sizeof(occupancy_mask_for_session()));

	int32_t startBigNow = (((int64_t)explode_animation_x_start_big_for_session() * (65536 - explodedness)) >> 16);
	int32_t widthBigWhenExploded = (kDisplayWidth << 16);
	int32_t widthBigWhenNotExploded = explode_animation_x_width_big_for_session();
	int32_t difference = widthBigWhenExploded - widthBigWhenNotExploded;
	int32_t widthBigNow = widthBigWhenNotExploded + (((int64_t)difference * explodedness) >> 16);

	int32_t inverseScale = ((uint64_t)widthBigWhenExploded << 16) / widthBigNow;

	int32_t xSourceRightEdge = 0;

	for (int32_t xDestSquareRightEdge = 0; xDestSquareRightEdge <= kDisplayWidth; xDestSquareRightEdge++) {

		int32_t xSourceLeftEdge =
		    xSourceRightEdge; // What was the last square's right edge is now the current square's left edge

		// From here on, we talk about the right edge of the destination square
		int32_t xDestBig = xDestSquareRightEdge << 16;
		int32_t xDestBigRelativeToStart = xDestBig - startBigNow;

		int32_t xSourceBigRelativeToStartNow = ((int64_t)xDestBigRelativeToStart * inverseScale) >> 16;
		int32_t xSourceBig = xSourceBigRelativeToStartNow;
		xSourceRightEdge = xSourceBig >> 16;

		// For first iteration, we just wanted that value, to use next time - and we should get out now
		if (!xDestSquareRightEdge) {
			continue;
		}

		if (xSourceRightEdge <= 0) {
			continue; // <=0 probably looks a little bit better than <0
		}

		// Ok, we need the max values between xSourceLeftEdge and xSourceRightEdge
		int32_t xSourceLeftEdgeLimited = std::max(xSourceLeftEdge, 0_i32);
		int32_t xSourceRightEdgeLimited = std::min(xSourceRightEdge, kDisplayWidth);

		int32_t xDest = xDestSquareRightEdge - 1;
		waveform_renderer_for_session().renderOneColForCollapseAnimationZoomedOut(
		    xSourceLeftEdgeLimited, xSourceRightEdgeLimited, xDest, sample_max_peak_from_zero_for_session(),
		    explodedness, PadLEDs::image_for_session(), &waveform_render_data_for_session(),
		    audio_clip_colour_for_session(), sample_reversed_for_session(), sample_value_centre_point_for_session(),
		    sample_value_span_for_session());

		if (xSourceRightEdge >= kDisplayWidth) {
			break; // If we got to the right edge of everything we want to draw onscreen
		}
	}

	if (shouldSendOut) {
		sendOutMainPadColours();
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 35);
	}
}

// 2^16 used in place of "1" in "big" arithmetic below
void renderExplodeAnimation(int32_t explodedness, bool shouldSendOut) {
	memset(image_for_session(), 0, sizeof(image_for_session()));
	memset(occupancy_mask_for_session(), 0, sizeof(occupancy_mask_for_session()));

	// Set up some stuff for each x-pos that we don't want to be constantly re-calculating
	int32_t xDestArray[kDisplayWidth];
	uint16_t xIntensityArray[kDisplayWidth][2];

	int32_t xStart = 0;
	int32_t xEnd = kDisplayWidth;

	for (int32_t xSource = 0; xSource < kDisplayWidth; xSource++) {
		int32_t xSourceBig = xSource << 16;
		int32_t xOriginBig =
		    explode_animation_x_start_big_for_session()
		    + (((int64_t)explode_animation_x_width_big_for_session() * xSourceBig) >> (kDisplayWidthMagnitude + 16));
		// xOriginBig = std::min(xOriginBig, explodeAnimationXStartBig + explodeAnimationXWidthBig - 65536);

		xOriginBig &= ~(uint32_t)65535; // Make sure each pixel's "origin-point" is right on an exact square - rounded
		                                // to the left. That'll match what we'll see in the arranger

		int32_t xSourceBigRelativeToOrigin = xSourceBig - xOriginBig;
		int32_t xDestBig = xOriginBig + (((int64_t)xSourceBigRelativeToOrigin * explodedness) >> 16);

		// Ok, so we're gonna squish this source square amongst 4 destination squares
		xDestArray[xSource] = xDestBig >> 16;

		// May as well narrow things down if we know now that some xSources won't end up onscreen
		if (xDestArray[xSource] < -1) {
			xStart = xSource + 1;
			continue;
		}
		else if (xDestArray[xSource] >= kDisplayWidth) {
			xEnd = xSource;
			break;
		}
		xIntensityArray[xSource][1] = xDestBig; // & 65535;
		xIntensityArray[xSource][0] = 65535 - xIntensityArray[xSource][1];
	}

	for (int32_t ySource = -1; ySource < kDisplayHeight + 1; ySource++) {

		int32_t ySourceBig = ySource << 16;
		int32_t ySourceBigRelativeToOrigin = ySourceBig - explode_animation_y_origin_big_for_session();
		int32_t yDestBig =
		    explode_animation_y_origin_big_for_session() + (((int64_t)ySourceBigRelativeToOrigin * explodedness) >> 16);
		int32_t yDest = yDestBig >> 16;

		uint32_t yIntensity[2];
		yIntensity[1] = yDestBig & 65535;
		yIntensity[0] = 65535 - yIntensity[1];

		for (int32_t xSource = xStart; xSource < xEnd; xSource++) {

			if (occupancy_mask_store_for_session()[ySource + 1]
			                                      [xSource]) { // If there's actually anything in this source square...

				for (int32_t xOffset = 0; xOffset < 2; xOffset++) {
					int32_t xNow = xDestArray[xSource] + xOffset;
					if (xNow < 0) {
						continue;
					}
					if (xNow >= kDisplayWidth) {
						break;
					}

					for (int32_t yOffset = 0; yOffset < 2; yOffset++) {
						int32_t yNow = yDest + yOffset;
						if (yNow < 0) {
							continue;
						}
						if (yNow >= kDisplayHeight) {
							break;
						}

						uint32_t intensityNow = (yIntensity[yOffset] * xIntensityArray[xSource][xOffset]) >> 16;
						PadLEDs::image_for_session()[yNow][xNow] = drawSquare(
						    image_store_for_session()[ySource + 1][xSource], intensityNow,
						    PadLEDs::image_for_session()[yNow][xNow], &occupancy_mask_for_session()[yNow][xNow],
						    occupancy_mask_store_for_session()[ySource + 1][xSource]);
					}
				}
			}
		}
	}

	if (shouldSendOut) {
		sendOutMainPadColours();
		// Nice small number of milliseconds here. This animation is prone to looking jerky
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 35);
	}
}

void reassessGreyout(bool doInstantly) {
	auto [newCols, newRows] = getUIGreyoutColsAndRows();

	// If same as before, get out
	if (newCols == greyout_cols_for_session() && newRows == greyout_rows_for_session()) {
		return;
	}

	bool anythingBefore = (greyout_cols_for_session() || greyout_rows_for_session());
	bool anythingNow = (newCols || newRows);

	bool anythingBoth = (anythingBefore && anythingNow);

	if (anythingNow) {
		greyout_cols_for_session() = newCols;
		greyout_rows_for_session() = newRows;
	}

	if (doInstantly || anythingBoth) {
		setGreyoutAmount(1);
		sendOutMainPadColoursSoon();
		sendOutSidebarColoursSoon();
	}
	else {
		greyout_change_start_time_for_session() = AudioEngine::audioSampleTimer;
		greyout_change_direction_for_session() = anythingNow ? 1 : -1;
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
	}
}

void skipGreyoutFade() {

	if (greyout_change_direction_for_session() > 0) {
		setGreyoutAmount(1);
	}
	else if (greyout_change_direction_for_session() < 0) {
		setGreyoutAmount(0);
		greyout_cols_for_session() = 0;
		greyout_rows_for_session() = 0;
	}

	greyout_change_direction_for_session() = 0;
}

void doGreyoutInstantly() {
	greyout_change_direction_for_session() = 0;
	greyout_cols_for_session() = 0xFFFFFFFF;
	greyout_rows_for_session() = 0xFFFFFFFF;

	setGreyoutAmount(1);
}

void setGreyoutAmount(float newAmount) {
	grey_proportion_for_session() = newAmount * 6500000;
}

int32_t refreshTime = 23;
int32_t dimmerInterval = 0;

void setBrightnessLevel(uint8_t offset) {
	return setDimmerInterval(kMaxLedBrightness - offset);
}

void setRefreshTime(int32_t newTime) {
	gui::ui_session::Scope hardware(gui::ui_session::Id::Local);
	PIC::setRefreshTime(newTime);
	refreshTime = newTime;
}

void changeRefreshTime(int32_t offset) {
	int32_t newTime = refreshTime + offset;
	if (newTime > 255 || newTime < 1) {
		return;
	}
	setRefreshTime(newTime);
	char buffer[12];
	intToString(refreshTime, buffer);
	display->displayPopup(buffer);
}

void changeDimmerInterval(int32_t offset) {
	int32_t newInterval = dimmerInterval - offset;
	if (newInterval > 25 || newInterval < 0) {}
	else {
		setDimmerInterval(newInterval);
	}

	if (display->haveOLED()) {
		char text[20];
		strcpy(text, "Brightness: ");
		char* pos = strchr(text, 0);
		intToString((25 - dimmerInterval) << 2, pos);
		pos = strchr(text, 0);
		*(pos++) = '%';
		*pos = 0;
		display->popupTextTemporary(text);
	}
}

void setDimmerInterval(int32_t newInterval) {
	gui::ui_session::Scope hardware(gui::ui_session::Id::Local);
	// Uart::print("dimmerInterval: ");
	// Uart::println(newInterval);
	dimmerInterval = newInterval;

	int32_t newRefreshTime = 23 - newInterval;
	while (newRefreshTime < 8) {
		newRefreshTime++;
		newInterval *= 1.2; // (From Roha, Nov 2023) Hmm, not sure why this was necessary...
	}

	// Uart::print("newInterval: ");
	// Uart::println(newInterval);

	setRefreshTime(newRefreshTime);
	PIC::setDimmerInterval(newInterval);
}

void timerRoutine() {
	// If output buffer is too full, come back in a little while instead
	if (local_output()
	    && uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) {
		setTimerForSoon();
		return;
	}

	int32_t progress;

	if (isUIModeActive(UI_MODE_HORIZONTAL_ZOOM)) {
		renderZoom();
	}

	else if (isUIModeActive(UI_MODE_HORIZONTAL_SCROLL)) {
		horizontal::renderScroll();
	}

	else if (isUIModeActive(UI_MODE_AUDIO_CLIP_EXPANDING) || isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)) {
		renderAudioClipExpandOrCollapse();
	}

	else if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {
		renderClipExpandOrCollapse();
	}

	else if (isUIModeActive(UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING)) {
		renderNoteRowExpandOrCollapse();
	}

	else if (isUIModeActive(UI_MODE_EXPLODE_ANIMATION) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		Clip* clip = getCurrentClip();

		progress = getTransitionProgress();
		if (progress >= 65536) { // If finished transitioning...

			// If going to keyboard screen, no sidebar or anything to fade in
			if (explode_animation_direction_for_session() == 1 && clip->type == ClipType::INSTRUMENT
			    && ((InstrumentClip*)clip)->on_keyboard_screen_for_session()) {
				currentUIMode = UI_MODE_NONE;
				changeRootUI(&keyboard_screen_for_session());
			}

			// Otherwise, there's stuff we want to fade in / to
			else {
				int32_t explodedness = (explode_animation_direction_for_session() == 1) ? 65536 : 0;
				if ((clip->type == ClipType::INSTRUMENT) || (clip->on_automation_clip_view_for_session())) {
					renderExplodeAnimation(explodedness, false);
				}
				else {
					renderAudioClipExplodeAnimation(explodedness, false);
				}
				memcpy(PadLEDs::image_store_for_session(), PadLEDs::image_for_session(),
				       (kDisplayWidth + kSideBarWidth) * kDisplayHeight * sizeof(RGB));

				bool anyZoomingDone = false;
				currentUIMode = UI_MODE_ANIMATION_FADE;
				if (explode_animation_direction_for_session() == 1) {
					if (clip->on_automation_clip_view_for_session()) {
						changeRootUI(&automation_view_for_session()); // We want to fade the sidebar in
						anyZoomingDone = instrument_clip_view_for_session().zoomToMax(true);
						if (anyZoomingDone) {
							uiNeedsRendering(&automation_view_for_session(), 0, 0xFFFFFFFF);
						}
					}
					else if (clip->type == ClipType::INSTRUMENT) {
						changeRootUI(&instrument_clip_view_for_session()); // We want to fade the sidebar in
						anyZoomingDone = instrument_clip_view_for_session().zoomToMax(true);
						if (anyZoomingDone) {
							uiNeedsRendering(&instrument_clip_view_for_session(), 0, 0xFFFFFFFF);
						}
					}
					else {
						changeRootUI(&audio_clip_view_for_session());
						goto stopFade; // No need for fade since no sidebar, and also if we tried it'd get glitchy cos
						               // we're not set up for it
					}
				}
				else {
					UI* nextUI = &arranger_view_for_session();
					if (explode_animation_target_ui_for_session() != nullptr) {
						nextUI = explode_animation_target_ui_for_session();
						explode_animation_target_ui_for_session() = nullptr;
					}

					changeRootUI(nextUI);

					if (nextUI == &arranger_view_for_session() && arranger_view_for_session().doingAutoScrollNow) {
						goto stopFade; // If we suddenly just started doing an auto-scroll, there's no time to fade
					}
					else if (nextUI == &session_view_for_session()) {
						session_view_for_session().finishedTransitioningHere();
					}
				}

				// if you zoomed in and re-rendered the sidebar, pause the animation
				// we'll continue the transition and render the fade after the next refresh
				// this ensures that the sidebar doesn't get rendered empty
				if (anyZoomingDone) {
					uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
				// continue transition and render the fade
				else {
					recordTransitionBegin(130);
					renderFade(0);
				}
			}
		}
		else {
			int32_t explodedness = (explode_animation_direction_for_session() == 1) ? 0 : 65536;
			explodedness += progress * explode_animation_direction_for_session();

			if ((clip->type == ClipType::INSTRUMENT) || (clip->on_automation_clip_view_for_session())) {
				renderExplodeAnimation(explodedness);
			}
			else {
				renderAudioClipExplodeAnimation(explodedness);
			}
		}
	}

	else if (isUIModeActive(UI_MODE_ANIMATION_FADE)) {
		progress = getTransitionProgress();
		if (progress >= 65536) {
stopFade:
			currentUIMode = UI_MODE_NONE;
			renderingNeededRegardlessOfUI(); // Just in case some waveforms couldn't be rendered when the store was
			                                 // written to, we want to re-render everything now
		}
		else {
			renderFade(progress);
		}
	}

	else {
		// Progress greyout
		if (greyout_change_direction_for_session() != 0) {
			float amountDone =
			    (float)(AudioEngine::audioSampleTimer - greyout_change_start_time_for_session()) / kGreyoutSpeed;
			if (greyout_change_direction_for_session() > 0) {
				if (amountDone > 1) {
					greyout_change_direction_for_session() = 0;
					setGreyoutAmount(1);
				}
				else {
					setGreyoutAmount(amountDone);
					uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
			}
			else {
				// If we've finished exiting greyout mode
				if (amountDone > 1) {
					greyout_change_direction_for_session() = 0;
					greyout_cols_for_session() = 0;
					greyout_rows_for_session() = 0;
				}
				else {
					setGreyoutAmount(1 - amountDone);
					uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
			}
			need_to_send_out_main_pad_colours_for_session() = need_to_send_out_sidebar_colours_for_session() = true;
		}
	}

	if (need_to_send_out_main_pad_colours_for_session()) {
		sendOutMainPadColours();
	}
	if (need_to_send_out_sidebar_colours_for_session()) {
		sendOutSidebarColours();
	}
}

void sendOutMainPadColours() {
	AudioEngine::logAction("sendOutMainPadColours 1");
	if (local_output() && uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInMainPadRedraw) {
		sendOutMainPadColoursSoon();
		return;
	}

	for (int32_t col = 0; col < kDisplayWidth; col++) {
		if (col & 1) {
			sortLedsForCol(col - 1);
		}
	}

	if (local_output())
		PIC::flush();

	need_to_send_out_main_pad_colours_for_session() = false;

	AudioEngine::logAction("sendOutMainPadColours 2");
}

void sendOutMainPadColoursSoon() {
	need_to_send_out_main_pad_colours_for_session() = true;
	setTimerForSoon();
}

void sendOutSidebarColours() {

	if (local_output() && uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInSidebarRedraw) {
		sendOutSidebarColoursSoon();
		return;
	}

	sortLedsForCol(kDisplayWidth);

	if (local_output())
		PIC::flush();

	need_to_send_out_sidebar_colours_for_session() = false;
}

void sendOutSidebarColoursSoon() {
	need_to_send_out_sidebar_colours_for_session() = true;
	setTimerForSoon();
}

void setTimerForSoon() {
	if (!uiTimerManager.isTimerSet(TimerName::MATRIX_DRIVER)) {
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 20);
	}
}

void renderAudioClipExpandOrCollapse() {

	int32_t progress = getTransitionProgress();
	if (isUIModeActive(UI_MODE_AUDIO_CLIP_EXPANDING)) {
		if (progress >= 65536) {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audio_clip_view_for_session());
			return;
		}
	}

	else {
		// If collapse finished, switch to session view and do fade-in
		if (progress >= 65536) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

			memset(image_store_for_session(), 0, sizeof(image_store_for_session()));
			session_view_for_session().renderRow(
			    modelStack, waveform_renderer_for_session().collapseAnimationToWhichRow,
			    image_store_for_session()[waveform_renderer_for_session().collapseAnimationToWhichRow],
			    occupancy_mask_store_for_session()[waveform_renderer_for_session().collapseAnimationToWhichRow], true);
			session_view_for_session().finishedTransitioningHere();
			return;
		}
		progress = 65536 - progress;
	}

	renderAudioClipCollapseAnimation(progress);

	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderClipExpandOrCollapse() {
	int32_t progress = getTransitionProgress();
	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {
		if (progress >= 65536) {
			currentUIMode = UI_MODE_NONE;

			Clip* clip = getCurrentClip();

			bool onKeyboardScreen =
			    ((clip->type == ClipType::INSTRUMENT) && ((InstrumentClip*)clip)->on_keyboard_screen_for_session());

			// when transitioning back to clip, if keyboard view is enabled, it takes precedent
			// over automation and instrument clip views.
			if (clip->on_automation_clip_view_for_session() && !onKeyboardScreen) {
				changeRootUI(&automation_view_for_session());
				// If we need to zoom in horizontally because the Clip's too short...
				bool anyZoomingDone = instrument_clip_view_for_session().zoomToMax(true);
				if (anyZoomingDone) {
					uiNeedsRendering(&automation_view_for_session(), 0, 0xFFFFFFFF);
				}
			}
			else {
				if (onKeyboardScreen) {
					changeRootUI(&keyboard_screen_for_session());
				}
				else {
					changeRootUI(&instrument_clip_view_for_session());
					// If we need to zoom in horizontally because the Clip's too short...
					bool anyZoomingDone = instrument_clip_view_for_session().zoomToMax(true);
					if (anyZoomingDone) {
						uiNeedsRendering(&instrument_clip_view_for_session(), 0, 0xFFFFFFFF);
					}
				}
			}
			return;
		}
	}

	else {
		// If collapse finished, switch to session view and do fade-in
		if (progress >= 65536) {
			renderInstrumentClipCollapseAnimation(0, kDisplayWidth + kSideBarWidth, 0);
			memcpy(image_store_for_session(), PadLEDs::image_for_session(), sizeof(PadLEDs::image_for_session()));
			session_view_for_session().finishedTransitioningHere();
			return;
		}
		progress = 65536 - progress;
	}

	renderInstrumentClipCollapseAnimation(0, kDisplayWidth + kSideBarWidth, progress);

	// The sidebar rows move a long way over this short transition, so keep the samples close to one row apart.
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, kClipExpandCollapseRefreshMs);
}

void renderNoteRowExpandOrCollapse() {
	int32_t progress = getTransitionProgress();
	if (progress >= 65536) {
		currentUIMode = UI_MODE_NONE;
		if (getCurrentClip()->on_automation_clip_view_for_session()) {
			uiNeedsRendering(&automation_view_for_session());
		}
		else {
			uiNeedsRendering(&instrument_clip_view_for_session());
		}
		return;
	}

	renderInstrumentClipCollapseAnimation(0, kDisplayWidth + 1, 65536 - progress);

	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderZoom() {
	AudioEngine::logAction("MatrixDriver::renderZoom");

	int32_t transitionProgress = getTransitionProgress();
	// If we've finished zooming...
	if (transitionProgress >= 65536) {
		exitUIMode(UI_MODE_HORIZONTAL_ZOOM);
		uiNeedsRendering(getCurrentUI(), 0xFFFFFFFF, 0);
		return;
	}

	if (!zooming_in_for_session()) {
		transitionProgress = 65536 - transitionProgress;
	}

	uint32_t sineValue = (getSine((transitionProgress + 98304) & 131071, 17) >> 16) + 32768;

	// The commented line is equivalent to the other lines just below
	// int32_t negativeFactorProgress = pow(zoomFactor, transitionProgress - 1) * 134217728; // Sorry, the purpose of
	// this variable has got a bit cryptic, it exists after much simplification
	int32_t powersOfTwo = ((int32_t)(transitionProgress >> 7) - 512) << zoom_magnitude_for_session();
	int32_t fine = powersOfTwo & 1023;
	int32_t coarse = powersOfTwo >> 10;

	// Numbers below here represent 1 as 65536

	// inImageWidthComparedToNormal and outImageWidthComparedToNormal show how much bigger than "normal" those two
	// images are to appear. E.g. when fully zoomed out, the out-image would be "1" (65536), and the in-image would be
	// "0.5" (32768). And so on.

	uint32_t inImageTimesBiggerThanNormal =
	    interpolateTable(fine, 10, expTableSmall); // This could be changed to run on a bigger number of bits in input
	inImageTimesBiggerThanNormal = increaseMagnitude(inImageTimesBiggerThanNormal, coarse - 14);

	renderZoomWithProgress(inImageTimesBiggerThanNormal, sineValue, &image_store_for_session()[0][0][0],
	                       &image_store_for_session()[kDisplayHeight][0][0], 0, 0, kDisplayWidth, kDisplayWidth,
	                       kDisplayWidth + kSideBarWidth, kDisplayWidth + kSideBarWidth);

	sendOutMainPadColours();
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

// inImageFadeAmount is how much of the in-image we'll see, out of 65536
void renderZoomWithProgress(int32_t inImageTimesBiggerThanNative, uint32_t inImageFadeAmount,
                            uint8_t* __restrict__ innerImage, uint8_t* __restrict__ outerImage,
                            int32_t innerImageLeftEdge, int32_t outerImageLeftEdge, int32_t innerImageRightEdge,
                            int32_t outerImageRightEdge, int32_t innerImageTotalWidth, int32_t outerImageTotalWidth) {

	uint32_t outImageTimesBiggerThanNative = inImageTimesBiggerThanNative << zoom_magnitude_for_session();

	uint32_t inImageTimesSmallerThanNative =
	    4294967295u / inImageTimesBiggerThanNative; // How many squares of the zoomed-in image fit into each square of
	                                                // our output image, at current zoom level
	uint32_t outImageTimesSmallerThanNative =
	    4294967295u / outImageTimesBiggerThanNative; // How many squares of the zoomed-out image fit into each square of
	                                                 // our output image, at current zoom level

	int32_t lastZoomPinSquareDone = 2147483647;

	// To save on stack usage, these arrays are stored in the string buffer
	int32_t* outputSquareStartOnInImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 0];
	int32_t* outputSquareEndOnInImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 1];
	int32_t* outputSquareStartOnOutImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 2];
	int32_t* outputSquareEndOnOutImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 3];
	uint16_t* inImageFadePerCol = (uint16_t*)shortStringBuffer; // 0 means show none. 65536 means show all, only
#define zoomPinSquareInner zoom_pin_square_for_session()
#define zoomPinSquareOuter zoom_pin_square_for_session()

	// Go through each row
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (transition_taking_place_on_row_for_session()[yDisplay]) {

			// If this row doesn't have the same pin-square as the last, we have to calculate some stuff. Otherwise,
			// this can be reused.
			if (zoom_pin_square_for_session()[yDisplay] != lastZoomPinSquareDone) {
				lastZoomPinSquareDone = zoom_pin_square_for_session()[yDisplay];

				// Work out what square the thinner image begins at (i.e. its left-most edge)
				int32_t inImagePos0Onscreen = zoom_pin_square_for_session()[yDisplay]
				                              - (zoomPinSquareInner[yDisplay] >> 8)
				                                    * (inImageTimesBiggerThanNative >> 8); // Beware rounding inaccuracy
				int32_t inImageLeftEdgeOnscreen =
				    inImagePos0Onscreen + inImageTimesBiggerThanNative * innerImageLeftEdge;
				int32_t inImageRightEdgeOnscreen =
				    inImagePos0Onscreen + inImageTimesBiggerThanNative * innerImageRightEdge;

				// Do some pre-figuring-out for each column of the final-rendered image - which we can hopefully refer
				// to for each row
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					int32_t outputSquareLeftEdge = xDisplay * 65536;
					int32_t outputSquareRightEdge = outputSquareLeftEdge + 65536;

					// Work out how much of this square will be covered by the "in" (thinner) image (often it'll be all
					// of it, or none)
					int32_t inImageOverlap = std::min(outputSquareRightEdge, inImageRightEdgeOnscreen)
					                         - std::max(outputSquareLeftEdge, inImageLeftEdgeOnscreen);
					if (inImageOverlap < 0) {
						inImageOverlap = 0;
					}

					// Convert that into knowing what proportion of colour from each image we want to grab
					inImageFadePerCol[xDisplay] = ((uint32_t)inImageOverlap * inImageFadeAmount) >> 16;

					int32_t outputSquareLeftEdgePositionRelativeToPinSquare =
					    zoom_pin_square_for_session()[yDisplay] - outputSquareLeftEdge;

					int32_t outputSquareLeftEdgePositionOnInImageRelativeToPinSquare =
					    ((int64_t)outputSquareLeftEdgePositionRelativeToPinSquare * inImageTimesSmallerThanNative)
					    >> 16;
					int32_t outputSquareLeftEdgePositionOnOutImageRelativeToPinSquare =
					    ((int64_t)outputSquareLeftEdgePositionRelativeToPinSquare * outImageTimesSmallerThanNative)
					    >> 16;

					// Work out, for this square/col/pixel, the corresponding local coordinate for both the in- and
					// out-images. Do that for both the leftmost and rightmost edge of this square
					outputSquareStartOnOutImage[xDisplay] =
					    zoomPinSquareOuter[yDisplay] - outputSquareLeftEdgePositionOnOutImageRelativeToPinSquare;
					outputSquareStartOnInImage[xDisplay] =
					    zoomPinSquareInner[yDisplay] - outputSquareLeftEdgePositionOnInImageRelativeToPinSquare;

					outputSquareEndOnInImage[xDisplay] =
					    outputSquareStartOnInImage[xDisplay] + inImageTimesSmallerThanNative;
					outputSquareEndOnOutImage[xDisplay] =
					    outputSquareStartOnOutImage[xDisplay] + outImageTimesSmallerThanNative;
				}
			}

			// Go through each column onscreen
			for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

				uint32_t outValue[3];
				memset(outValue, 0, sizeof(outValue));

				bool drawingAnything = false;

				if (inImageFadePerCol[xDisplay]) {

					renderZoomedSquare(outputSquareStartOnInImage[xDisplay], outputSquareEndOnInImage[xDisplay],
					                   inImageTimesBiggerThanNative, inImageFadePerCol[xDisplay], outValue, innerImage,
					                   innerImageRightEdge, &drawingAnything);
				}

				{
					renderZoomedSquare(outputSquareStartOnOutImage[xDisplay], outputSquareEndOnOutImage[xDisplay],
					                   outImageTimesBiggerThanNative, 65535 - inImageFadePerCol[xDisplay], outValue,
					                   outerImage, outerImageRightEdge, &drawingAnything);
				}

				if (drawingAnything) {
					for (int32_t colour = 0; colour < 3; colour++) {
						int32_t result = rshift_round(outValue[colour], 16);
						PadLEDs::image_for_session()[yDisplay][xDisplay][colour] =
						    std::min<int32_t>(std::numeric_limits<uint8_t>::max(), result);
					}
				}
				else {
					PadLEDs::image_for_session()[yDisplay][xDisplay] = gui::colours::black;
				}
			}
		}

		innerImage += innerImageTotalWidth * 3;
		outerImage += outerImageTotalWidth * 3;
	}
	{
		gui::ui_session::Scope hardware(gui::ui_session::Id::Local);
		AudioEngine::routineWithClusterLoading();
	}
}

void renderZoomedSquare(int32_t outputSquareStartOnSourceImage, int32_t outputSquareEndOnSourceImage,
                        uint32_t sourceImageTimesBiggerThanNormal, uint32_t sourceImageFade, uint32_t* output,
                        uint8_t* inputImageRow, int32_t inputImageWidth, bool* drawingAnything) {

	int32_t outImageStartSquareLeftEdge = (uint32_t)(outputSquareStartOnSourceImage) & ~(uint32_t)65535;
	for (int32_t sourceSquareLeftEdge = std::max((int32_t)0, outImageStartSquareLeftEdge); true;
	     sourceSquareLeftEdge += 65536) {
		if (sourceSquareLeftEdge >= outputSquareEndOnSourceImage) {
			break;
		}
		int32_t xSource = sourceSquareLeftEdge >> 16;
		if (xSource >= inputImageWidth) {
			break;
		}

		// If nothing (i.e. black) at this input pixel, continue
		if (!((*(uint32_t*)&inputImageRow[xSource * 3]) & (uint32_t)16777215)) {
			continue;
		}

		*drawingAnything = true;

		int32_t sourceSquareRightEdge = sourceSquareLeftEdge + 65536;
		uint32_t intensity =
		    std::min(sourceSquareRightEdge, outputSquareEndOnSourceImage)
		    - std::max(sourceSquareLeftEdge, outputSquareStartOnSourceImage); // Will end up at max 65536

		intensity = ((uint64_t)intensity * sourceImageFade * sourceImageTimesBiggerThanNormal) >> 32;

		for (int32_t colour = 0; colour < 3; colour++) {
			output[colour] += (int32_t)inputImageRow[xSource * 3 + colour] * intensity;
		}
	}
}

void horizontal::renderScroll() {

	squares_scrolled_for_session()++;
	int32_t copyCol = (scroll_direction_for_session() > 0)
	                      ? squares_scrolled_for_session() - 1
	                      : area_to_scroll_for_session() - squares_scrolled_for_session();
	int32_t startSquare = (scroll_direction_for_session() > 0) ? 0 : area_to_scroll_for_session() - 1;
	int32_t endSquare = (scroll_direction_for_session() > 0) ? area_to_scroll_for_session() - 1 : 0;
	for (int32_t row = 0; row < kDisplayHeight; row++) {
		if (transition_taking_place_on_row_for_session()[row]) {
			for (int32_t colour = 0; colour < 3; colour++) {
				for (int32_t x = startSquare; x != endSquare; x += scroll_direction_for_session()) {
					PadLEDs::image_for_session()[row][x][colour] =
					    PadLEDs::image_for_session()[row][x + scroll_direction_for_session()][colour];
				}
				// And, bring in a col from the temp image
				if (scrolling_into_nothing_for_session()) {
					PadLEDs::image_for_session()[row][endSquare][colour] = 0;
				}
				else {
					PadLEDs::image_for_session()[row][endSquare][colour] =
					    image_store_for_session()[row][copyCol][colour];
				}
			}

			if (local_output())
				PIC::sendScrollRow(row, prepareColour(endSquare, row, image_for_session()[row][endSquare]));
		}
	}

	if (local_output())
		PIC::doneSendingRows();
	else {
		sendOutMainPadColours();
		sendOutSidebarColours();
	}
	if (local_output())
		PIC::flush();

	if (squares_scrolled_for_session() >= area_to_scroll_for_session()) {
		getCurrentUI()->scrollFinished();
	}
	else {
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH_SCROLLING);
	}
}

void horizontal::setupScroll(int8_t thisScrollDirection, uint8_t thisAreaToScroll, bool scrollIntoNothing,
                             int32_t numSquaresToScroll) {
	scroll_direction_for_session() = thisScrollDirection;
	area_to_scroll_for_session() = thisAreaToScroll;
	squares_scrolled_for_session() = thisAreaToScroll - numSquaresToScroll;
	scrolling_into_nothing_for_session() = scrollIntoNothing;

	uint8_t flags = 0;
	if (thisScrollDirection >= 0) {
		flags |= 1;
	}
	if (thisAreaToScroll == kDisplayWidth + kSideBarWidth) {
		flags |= 2;
	}
	if (local_output())
		PIC::setupHorizontalScroll(flags);
	renderScroll();
}

void vertical::renderScroll() {
	squares_scrolled_for_session()++;
	int32_t copyRow = (scroll_direction_for_session() > 0) ? squares_scrolled_for_session() - 1
	                                                       : kDisplayHeight - squares_scrolled_for_session();
	int32_t startSquare = (scroll_direction_for_session() > 0) ? 0 : 1;
	int32_t endSquare = (scroll_direction_for_session() > 0) ? kDisplayHeight - 1 : 0;

	// matrixDriver.greyoutMinYDisplay = (scrollDirection > 0) ? kDisplayHeight - squaresScrolled : squaresScrolled;

	// Move the scrolling region
	memmove(image_for_session()[startSquare], image_for_session()[1 - startSquare],
	        (kDisplayWidth + kSideBarWidth) * (kDisplayHeight - 1) * sizeof(RGB));

	// And, bring in a row from the temp image (or from nowhere)
	if (scrolling_to_nothing_for_session()) {
		memset(image_for_session()[endSquare], 0, (kDisplayWidth + kSideBarWidth) * 3);
	}
	else {
		memcpy(image_for_session()[endSquare], image_store_for_session()[copyRow],
		       (kDisplayWidth + kSideBarWidth) * sizeof(RGB));
	}

	std::array<RGB, kDisplayWidth + kSideBarWidth> colours{};
	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
		colours[x] = prepareColour(x, endSquare, image_for_session()[endSquare][x]);
	}
	if (local_output())
		PIC::doVerticalScroll(scroll_direction_for_session() > 0, colours);
	else {
		sendOutMainPadColours();
		sendOutSidebarColours();
	}
	if (local_output())
		PIC::flush();
}

void vertical::setupScroll(int8_t thisScrollDirection, bool scrollIntoNothing) {
	scroll_direction_for_session() = thisScrollDirection;
	scrolling_to_nothing_for_session() = scrollIntoNothing;
	squares_scrolled_for_session() = 0;
}

void renderFade(int32_t progress) {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			PadLEDs::image_for_session()[y][x] =
			    RGB::transform2(image_store_for_session()[y][x], image_store_for_session()[y + kDisplayHeight][x],
			                    [progress](auto channelA, auto channelB) {
				                    int32_t difference = (int32_t)channelB - (int32_t)channelA;
				                    uint32_t progressedDifference = rshift_round(difference * progress, 16);
				                    return channelA + progressedDifference;
			                    });
		}
	}
	sendOutMainPadColours();
	sendOutSidebarColours();
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void recordTransitionBegin(uint32_t newTransitionLength) {
	clearPendingUIRendering();
	transition_length_for_session() = newTransitionLength * 44;
	transition_start_time_for_session() = AudioEngine::audioSampleTimer;
}

int32_t getTransitionProgress() {
	return ((uint64_t)(AudioEngine::audioSampleTimer - transition_start_time_for_session()) * 65536)
	       / transition_length_for_session();
}

void copyBetweenImageStores(RGB* __restrict__ dest, RGB* __restrict__ source, int32_t destWidth, int32_t sourceWidth,
                            int32_t copyWidth) {
	if (destWidth == sourceWidth && copyWidth >= sourceWidth - 2) {
		memcpy(dest, source, sourceWidth * kDisplayHeight * sizeof(RGB));
		return;
	}

	RGB* destEndOverall = dest + destWidth * kDisplayHeight;
	for (; dest < destEndOverall; dest += destWidth, source += sourceWidth) {
		memcpy(dest, source, copyWidth * sizeof(RGB));
	}
}

void moveBetweenImageStores(uint8_t* dest, uint8_t* source, int32_t destWidth, int32_t sourceWidth, int32_t copyWidth) {

	uint8_t* destEndOverall = dest + destWidth * kDisplayHeight * 3;
	do {
		memmove(dest, source, copyWidth * 3);
		dest += destWidth * 3;
		source += sourceWidth * 3;
	} while (dest < destEndOverall);
}

} // namespace PadLEDs

void PadLEDs::flashMainPad(int32_t x, int32_t y, int32_t colour) {
	if (x < 0 || x >= kDisplayWidth || y < 0 || y >= kDisplayHeight)
		return;
	state().frame.flash(x, y, colour);
	auto idx = y + (x * kDisplayHeight);
	if (!local_output()) {
		if (colour > 0)
			deluge::hid::mirror::panel_byte(10 + colour); // SET_FLASH_COLOR
		deluge::hid::mirror::panel_byte(24 + idx);        // SET_PAD_FLASHING
		return;
	}
	if (colour > 0)
		PIC::flashMainPadWithColourIdx(idx, colour);
	else
		PIC::flashMainPad(idx);
}
