#include "quad_menu.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "util/container/static_vector.hpp"
#include "hid/buttons.h"

#include "io/debug/log.h" // TODO: remove

namespace deluge::gui::menu_item {

void QuadMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	if (navigatedBackwardFrom) {
		// Focus on the item we came back from
		for (uint8_t i = 0; i < kNumItems; i++) {
			if (items[i] == navigatedBackwardFrom) {
				currentPos = i;
				break;
			}
		}
	}
	updateDisplay();
}

void QuadMenu::updateDisplay() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		items[currentPos]->readValueAgain();
	}
	else {
		items[currentPos]->drawName();
	}
}

void QuadMenu::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	int32_t boxHeight = OLED_MAIN_VISIBLE_HEIGHT - baseY;
	int32_t boxWidth = OLED_MAIN_WIDTH_PIXELS / kNumItems;

	for (uint8_t i = 0; i < kNumItems; i++) {
		int32_t startX = boxWidth * i;
		items[i]->renderInBox(startX+1, boxWidth, baseY, boxHeight);
	}

	image.invertArea(boxWidth * currentPos, boxWidth, baseY, baseY + boxHeight);
}

void QuadMenu::selectEncoderAction(int32_t offset) {
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		return items[currentPos]->selectEncoderAction(offset);
	} else {
		currentPos = mod(currentPos + offset, kNumItems);
		updateDisplay();
	}
}

MenuItem* QuadMenu::selectButtonPress() {
	return items[currentPos];
}

} // namespace deluge::gui::menu_item
