#include "gui/ui/browser/preset_navigation.h"

namespace deluge::gui {

bool PresetNavigationContext::matchesUI(Song* liveSong, UI* liveUI, uint32_t liveMode, Clip* liveCurrentClip) const {
	uint32_t expectedMode = padReleased ? UI_MODE_NONE : uiMode;
	return liveSong == song && liveUI == ui && liveMode == expectedMode && liveCurrentClip == currentClip;
}

bool PresetNavigationContext::matchesSelection(Clip* liveSelectedClip, bool arrangerTargetMatches) const {
	return (padReleased || !selectedClip || liveSelectedClip == selectedClip)
	       && (!fromArranger || arrangerTargetMatches);
}

void PresetNavigationContext::markPadReleased(bool targetMatches) {
	if (targetMatches
	    && (uiMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION || uiMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
		padReleased = true;
	}
}

PresetNavigationDestination PresetNavigationContext::destination() const {
	if (displayClip && !replacingWholeOutput) {
		return PresetNavigationDestination::CLIP;
	}
	return fromArranger ? PresetNavigationDestination::ARRANGER : PresetNavigationDestination::OUTPUT;
}

bool presetInstrumentAvailable(Availability requirement, bool alreadyInSong, bool activeInSession,
                               const Instrument* candidate, const Instrument* exemptInstrument) {
	if (!alreadyInSong || (candidate && candidate == exemptInstrument)) {
		return true;
	}
	if (requirement == Availability::INSTRUMENT_UNUSED) {
		return false;
	}
	return requirement != Availability::INSTRUMENT_AVAILABLE_IN_SESSION || !activeInSession;
}

PresetLoadGuard::PresetLoadGuard(bool& previewPending, uint32_t& uiMode) : uiMode_(uiMode), previousMode_(uiMode) {
	previewPending = false;
	uiMode_ = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
}

PresetLoadGuard::~PresetLoadGuard() {
	uiMode_ = previousMode_;
}

} // namespace deluge::gui
