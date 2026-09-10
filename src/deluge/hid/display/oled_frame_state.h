#pragma once
#include "hid/display/oled_canvas/canvas.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace deluge::hid::display {

struct OLEDFrameState {
	oled_canvas::Canvas main;
	oled_canvas::Canvas popup;
	oled_canvas::Canvas console;
	oled_canvas::Canvas completed;
	uint8_t (*current_image)[OLED_MAIN_WIDTH_PIXELS] = nullptr;
	bool needsSending = false;
	uint32_t revision = 0;
	bool published = false;

	void invalidate_published() {
		published = false;
		current_image = nullptr;
		needsSending = false;
	}

	// Cooperative UI/transport code only: copying must not yield to rendering.
	// The destination belongs to transport, so later publications cannot tear
	// an image being packetized. A wrapped revision of zero is still valid.
	std::optional<uint32_t> copy_published(std::span<uint8_t> destination) {
		if (!published || destination.size() != sizeof(oled_canvas::Canvas::ImageStore))
			return std::nullopt;
		memcpy(destination.data(), completed.hackGetImageStore(), destination.size());
		return revision;
	}

	// Rendering may resume before transport reads the frame. Keep the published
	// bytes separate from all three working canvases.
	void publish() {
		if (!current_image)
			return;
		if (current_image != completed.hackGetImageStore()) {
			memcpy(completed.hackGetImageStore(), current_image, sizeof(oled_canvas::Canvas::ImageStore));
		}
		current_image = completed.hackGetImageStore();
		++revision;
		published = true;
	}
};

} // namespace deluge::hid::display
