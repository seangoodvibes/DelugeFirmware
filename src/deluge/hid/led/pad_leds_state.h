#pragma once
#include "definitions_cxx.hpp"
#include "gui/colour/rgb.h"
#include "gui/ui/ui_session.h"
#include "gui/waveform/waveform_render_data.h"
#include <algorithm>
#include <array>
class UI;
namespace PadLEDs {
struct PadFrame {
	RGB colours[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	struct Flash {
		uint8_t colour = 0;
		uint32_t revision = 0;
	};
	Flash flashes[kDisplayHeight][kDisplayWidth]{};
	uint32_t revision = 0;
	void set_columns(int x, const std::array<RGB, kDisplayHeight * 2>& columns) {
		if (x < 0 || x + 1 >= kDisplayWidth + kSideBarWidth)
			return;
		bool changed = false;
		for (int col = 0; col < 2; ++col)
			for (int y = 0; y < kDisplayHeight; ++y) {
				auto value = columns[col * kDisplayHeight + y];
				if (colours[y][x + col] != value) {
					colours[y][x + col] = value;
					changed = true;
				}
			}
		if (changed)
			++revision;
	}
	void flash(int x, int y, uint8_t colour) {
		if (x < 0 || x >= kDisplayWidth || y < 0 || y >= kDisplayHeight)
			return;
		flashes[y][x].colour = colour;
		flashes[y][x].revision = ++revision;
	}
};
struct PadState {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};                      // 255 = full brightness
	uint8_t occupancyMask[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};          // 64 = full occupancy
	RGB imageStore[kDisplayHeight * 2][kDisplayWidth + kSideBarWidth]{};             // 255 = full brightness
	uint8_t occupancyMaskStore[kDisplayHeight * 2][kDisplayWidth + kSideBarWidth]{}; // 64 = full occupancy

	bool zoomingIn{};
	int8_t zoomMagnitude{};
	int32_t zoomPinSquare[kDisplayHeight]{};
	bool transitionTakingPlaceOnRow[kDisplayHeight]{};
	int8_t explodeAnimationDirection{};
	UI* explodeAnimationTargetUI = nullptr;

	int16_t animatedRowGoingTo[kMaxNumAnimatedRows]{};
	int16_t animatedRowGoingFrom[kMaxNumAnimatedRows]{};
	uint8_t numAnimatedRows{};

	int32_t greyProportion{};
	int8_t greyoutChangeDirection{};
	unsigned long greyoutChangeStartTime{};

	bool needToSendOutMainPadColours{};
	bool needToSendOutSidebarColours{};

	uint8_t flashCursor{};

	uint8_t slowFlashSquares[kDisplayHeight]{};
	uint8_t slowFlashColours[kDisplayHeight]{};

	int32_t explodeAnimationYOriginBig{};
	int32_t explodeAnimationXStartBig{};
	int32_t explodeAnimationXWidthBig{};

	// We stash these here for during UI-transition animation, because if that's happening as part of an undo, the
	// Sample might not be there anymore
	int32_t sampleValueCentrePoint{};
	int32_t sampleValueSpan{};
	int32_t sampleMaxPeakFromZero{};
	WaveformRenderData waveformRenderData{};
	RGB audioClipColour{};
	bool sampleReversed{};

	// Same for InstrumentClips
	int32_t clipLength{};
	RGB clipMuteSquareColour{};
	// Keyboard view has no mute / section columns of its own, so its two sidebar columns morph between the keyboard
	// colours and the Session colours of the row they collapse into (or expand out of). Both destination colours are
	// needed; the mute one is clipMuteSquareColour above.
	RGB clipSectionSquareColour{};
	bool morphKeyboardSidebar{};

	bool renderingLock{};

	uint32_t transitionLength{};
	uint32_t transitionStartTime{};

	uint32_t greyoutCols{};
	uint32_t greyoutRows{};

	struct Horizontal {

		uint8_t areaToScroll{};
		uint8_t squaresScrolled{};
		int8_t scrollDirection{};
		bool scrollingIntoNothing{}; // Means we're scrolling into a black screen

	} horizontal;
	struct Vertical {

		uint8_t squaresScrolled{};
		int8_t scrollDirection{};
		bool scrollingToNothing{};

	} vertical;
	PadFrame frame;
	PadState() { std::fill_n(slowFlashSquares, kDisplayHeight, 255); }
};
PadState& state();
inline auto& image_for_session() {
	return state().image;
}
inline auto& occupancy_mask_for_session() {
	return state().occupancyMask;
}
inline auto& image_store_for_session() {
	return state().imageStore;
}
inline auto& occupancy_mask_store_for_session() {
	return state().occupancyMaskStore;
}
inline auto& zooming_in_for_session() {
	return state().zoomingIn;
}
inline auto& zoom_magnitude_for_session() {
	return state().zoomMagnitude;
}
inline auto& zoom_pin_square_for_session() {
	return state().zoomPinSquare;
}
inline auto& transition_taking_place_on_row_for_session() {
	return state().transitionTakingPlaceOnRow;
}
inline auto& explode_animation_direction_for_session() {
	return state().explodeAnimationDirection;
}
inline auto& explode_animation_target_ui_for_session() {
	return state().explodeAnimationTargetUI;
}
inline auto& animated_row_going_to_for_session() {
	return state().animatedRowGoingTo;
}
inline auto& animated_row_going_from_for_session() {
	return state().animatedRowGoingFrom;
}
inline auto& num_animated_rows_for_session() {
	return state().numAnimatedRows;
}
inline auto& grey_proportion_for_session() {
	return state().greyProportion;
}
inline auto& greyout_change_direction_for_session() {
	return state().greyoutChangeDirection;
}
inline auto& greyout_change_start_time_for_session() {
	return state().greyoutChangeStartTime;
}
inline auto& need_to_send_out_main_pad_colours_for_session() {
	return state().needToSendOutMainPadColours;
}
inline auto& need_to_send_out_sidebar_colours_for_session() {
	return state().needToSendOutSidebarColours;
}
inline auto& flash_cursor_for_session() {
	return state().flashCursor;
}
inline auto& slow_flash_squares_for_session() {
	return state().slowFlashSquares;
}
inline auto& slow_flash_colours_for_session() {
	return state().slowFlashColours;
}
inline auto& explode_animation_y_origin_big_for_session() {
	return state().explodeAnimationYOriginBig;
}
inline auto& explode_animation_x_start_big_for_session() {
	return state().explodeAnimationXStartBig;
}
inline auto& explode_animation_x_width_big_for_session() {
	return state().explodeAnimationXWidthBig;
}
inline auto& sample_value_centre_point_for_session() {
	return state().sampleValueCentrePoint;
}
inline auto& sample_value_span_for_session() {
	return state().sampleValueSpan;
}
inline auto& sample_max_peak_from_zero_for_session() {
	return state().sampleMaxPeakFromZero;
}
inline auto& waveform_render_data_for_session() {
	return state().waveformRenderData;
}
inline auto& audio_clip_colour_for_session() {
	return state().audioClipColour;
}
inline auto& sample_reversed_for_session() {
	return state().sampleReversed;
}
inline auto& clip_length_for_session() {
	return state().clipLength;
}
inline auto& clip_mute_square_colour_for_session() {
	return state().clipMuteSquareColour;
}
inline auto& clip_section_square_colour_for_session() {
	return state().clipSectionSquareColour;
}
inline auto& morph_keyboard_sidebar_for_session() {
	return state().morphKeyboardSidebar;
}
inline auto& rendering_lock_for_session() {
	return state().renderingLock;
}
inline auto& transition_length_for_session() {
	return state().transitionLength;
}
inline auto& transition_start_time_for_session() {
	return state().transitionStartTime;
}
inline auto& greyout_cols_for_session() {
	return state().greyoutCols;
}
inline auto& greyout_rows_for_session() {
	return state().greyoutRows;
}
namespace horizontal {
inline auto& area_to_scroll_for_session() {
	return state().horizontal.areaToScroll;
}
inline auto& squares_scrolled_for_session() {
	return state().horizontal.squaresScrolled;
}
inline auto& scroll_direction_for_session() {
	return state().horizontal.scrollDirection;
}
inline auto& scrolling_into_nothing_for_session() {
	return state().horizontal.scrollingIntoNothing;
}
} // namespace horizontal

namespace vertical {
inline auto& squares_scrolled_for_session() {
	return state().vertical.squaresScrolled;
}
inline auto& scroll_direction_for_session() {
	return state().vertical.scrollDirection;
}
inline auto& scrolling_to_nothing_for_session() {
	return state().vertical.scrollingToNothing;
}
} // namespace vertical
inline const PadFrame& frame_for_session() {
	return state().frame;
}
} // namespace PadLEDs
