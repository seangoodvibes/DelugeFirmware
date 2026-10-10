#include "patch_cables.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "patch_cable_strength/range.h"
#include "patch_cable_strength/regular.h"
#include "processing/sound/sound.h"
#include "source_selection/range.h"
#include "source_selection/regular.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void PatchCables::beginSession(MenuItem* navigatedBackwardFrom) {
	panel_state().currentValue = 0;

	if (navigatedBackwardFrom != nullptr) {
		panel_state().currentValue = panel_state().savedVal;
	}

	if (display->haveOLED()) {
		panel_state().scrollPos = std::max((int32_t)0, panel_state().currentValue - 1);
	}

	readValueAgain();
}

void PatchCables::readValueAgain() {
	PatchCableSet* set = sound_editor_for_session().currentParamManager->getPatchCableSet();
	if (panel_state().currentValue >= set->numPatchCables) {
		// The last patch cable was deleted and it was selected, need to adjust
		panel_state().currentValue = std::max(0, set->numPatchCables - 1);
		panel_state().scrollPos = std::max((int32_t)0, panel_state().currentValue - 1);
	}

	renderOptions();

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
	blinkShortcutsSoon();
}

void PatchCables::renderOptions() {
	panel_state().options.clear();
	PatchCableSet* set = sound_editor_for_session().currentParamManager->getPatchCableSet();

	for (int i = 0; i < set->numPatchCables; i++) {
		PatchCable* cable = set->patch_cables_[i];
		PatchSource src = cable->from;
		PatchSource src2 = PatchSource::NOT_AVAILABLE;
		ParamDescriptor desc = cable->destinationParamDescriptor;
		if (!desc.isJustAParam()) {
			src2 = desc.getTopLevelSource();
		}
		int dest = desc.getJustTheParam();

		const int item_max_len = 30;
		struct Labels {
			char text[kMaxNumPatchCables][item_max_len];
		};
		PLACE_SDRAM_BSS static ui_session::State<Labels> labels;
		char* buf = labels.active().text[i];

		const char* src_name = sourceToStringShort(src); // exactly 4 chars
		const char* dest_name = deluge::modulation::params::getPatchedParamShortName(
		    dest, sound_editor_for_session().currentModControllable);

		memcpy(buf, src_name, 4);
		buf[4] = ' ';
		int off = 0;
		if (!desc.isJustAParam()) {
			const char* src2_name = sourceToStringShort(src2);
			memcpy(buf + 5, src2_name, 4);
			buf[9] = ' ';
			off = 5;
		}

		int32_t param_value = cable->get_current_value();
		int32_t level = ((int64_t)param_value * kMaxMenuPatchCableValue + (1 << 29)) >> 30;

		float floatLevel = (float)level / 100;
		int floatoff = floatLevel < 0 ? 1 : 0;

		floatToString(floatLevel, buf + off + 5 - floatoff, 2, 2);
		// fmt::vformat_to_n(buf + off + 5, 5, "{:4}", fmt::make_format_args();

		buf[off + 9] = ' ';
		strncpy(buf + off + 10, dest_name, item_max_len - 10 - off);
		buf[item_max_len - 1] = 0;

		panel_state().options.push_back(buf);
	}
}

void PatchCables::drawPixelsForOled() {
	drawItemsForOled(panel_state().options, panel_state().currentValue - panel_state().scrollPos,
	                 panel_state().scrollPos);
}

void PatchCables::drawValue() {
	PatchCableSet* set = sound_editor_for_session().currentParamManager->getPatchCableSet();
	if (set->numPatchCables == 0) {
		display->setText("none", false, false);
		return;
	}

	display->setScrollingText(panel_state().options[panel_state().currentValue].begin());
}

void PatchCables::selectEncoderAction(int32_t offset) {
	PatchCableSet* set = sound_editor_for_session().currentParamManager->getPatchCableSet();

	int32_t newValue = std::clamp<int32_t>(panel_state().currentValue + offset, 0, set->numPatchCables - 1);

	// if no change, just exit
	if (newValue == panel_state().currentValue) {
		return;
	}

	panel_state().currentValue = newValue;

	if (display->haveOLED()) {
		int32_t max = std::max<int32_t>(0, set->numPatchCables - kOLEDMenuNumOptionsVisible);
		panel_state().scrollPos = std::clamp<int32_t>(newValue - 1, 0, max);
	}

	readValueAgain(); // redraw
}

void PatchCables::blinkShortcutsSoon() {
	// some throttling so menu scrolling doesn't become a lightning storm of flashes
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, display->haveOLED() ? 500 : 200);
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
}

ActionResult PatchCables::timerCallback() {
	blinkShortcuts();
	return ActionResult::DEALT_WITH;
}

void PatchCables::blinkShortcuts() {
	PatchCableSet* set = sound_editor_for_session().currentParamManager->getPatchCableSet();
	PatchCable* cable = set->patch_cables_[panel_state().currentValue];
	ParamDescriptor desc = cable->destinationParamDescriptor;
	int dest = desc.getJustTheParam();

	if (dest == deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND
	    || dest == deluge::modulation::params::LOCAL_VOLUME) {
		dest = deluge::modulation::params::GLOBAL_VOLUME_POST_FX;
	}

	int32_t x, y;
	bool isSecondLayerParam;
	if (sound_editor_for_session().findPatchedParam(dest, &x, &y, &isSecondLayerParam)) {
		sound_editor_for_session().setupShortcutBlink(x, y, 3, isSecondLayerParam ? 0b00000011 /*yellow*/ : 0L);
	}

	PatchSource src = cable->from;
	PatchSource src2 = PatchSource::NOT_AVAILABLE;
	if (!desc.isJustAParam()) {
		src2 = desc.getTopLevelSource();
	}
	panel_state().blinkSrc = src;
	panel_state().blinkSrc2 = src2;
	sound_editor_for_session().updateSourceBlinks(this);

	sound_editor_for_session().blinkShortcut();
}

uint8_t PatchCables::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {
	if (s == panel_state().blinkSrc) {
		*colour = 0b110;
		return 0;
	}
	else if (s == panel_state().blinkSrc2) {
		return 3; // something #patchingoverhaul2021
	}
	return 255;
}

MenuItem* PatchCables::selectButtonPress() {
	PatchCableSet* set = sound_editor_for_session().currentParamManager->getPatchCableSet();
	int val = panel_state().currentValue;

	if (val >= set->numPatchCables) {
		// There were no items. If the user wants to create some, they need
		// to select a source anyway, so take them back there.
		return MenuItem::selectButtonPress();
	}
	PatchCable* cable = set->patch_cables_[val];
	panel_state().savedVal = val;
	ParamDescriptor desc = cable->destinationParamDescriptor;
	int dest = desc.getJustTheParam();
	sound_editor_for_session().patchingParamSelected = dest;

	panel_state().options.clear();
	if (cable->destinationParamDescriptor.isJustAParam()) {
		source_selection::regularMenu.source_for_session() = cable->from;
		return &patch_cable_strength::regularMenu;
	}
	else {
		PatchSource src2 = desc.getTopLevelSource();
		source_selection::regularMenu.source_for_session() = src2;
		source_selection::rangeMenu.source_for_session() = cable->from;
		return &patch_cable_strength::rangeMenu;
	}
}

} // namespace deluge::gui::menu_item
