/*
 * Copyright (c) 2025 Sean Ditny
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

#include "gui/views/automation/context/clip.h"
#include "hid/led/pad_leds.h"
#include "model/song/song.h"

PLACE_SDRAM_BSS AutomationViewClip automationViewClip{};

AutomationViewClip::AutomationViewClip() {
}

void AutomationViewClip::openedInBackground() {
	Clip* clip = getCurrentClip();

	clip->onAutomationClipView = true;

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("AutomationViewClip::beginSession");

	if (renderingToStore) {
		AutomationView::renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
		                               &PadLEDs::occupancyMaskStore[kDisplayHeight], true);
		clip->renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight],
		                    &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(getRootUI());
	}

	AutomationView::openedInBackground();
}

// rendering
bool AutomationViewClip::possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind,
                                                             int32_t paramID) {
	bool doRefreshGrid =
	    (clip != nullptr) && (clip->lastSelectedParamID == paramID) && (clip->lastSelectedParamKind == paramKind);

	if (doRefreshGrid) {
		uiNeedsRendering(getRootUI());
	}

	return doRefreshGrid;
}

// defers to audio clip or instrument clip sidebar render functions depending on the active clip
bool AutomationViewClip::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	return getCurrentClip()->renderSidebar(whichRows, image, occupancyMask);
}

int32_t AutomationViewClip::getNavSysId() const {
	return NAVIGATION_CLIP;
}

uint32_t AutomationViewClip::getMaxLength() {
	return getCurrentClip()->getMaxLength();
}

uint32_t AutomationViewClip::getMaxZoom() {
	return getCurrentClip()->getMaxZoom();
}
