

#include "gui/context_menu/clip_settings/launch_style.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

constexpr size_t kNumValues = 3;

namespace {
LaunchStyleMenu local_launch_style{};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<LaunchStyleMenu> remote_launch_style;
} // namespace
LaunchStyleMenu& launch_style_for_session() {
	return remote_launch_style.get(local_launch_style);
}

char const* LaunchStyleMenu::getTitle() {
	static char const* title = "Clip Mode";
	return title;
}

std::span<char const*> LaunchStyleMenu::getOptions() {
	using enum l10n::String;
	static const char* optionsls[] = {
	    l10n::get(STRING_FOR_DEFAULT_LAUNCH),
	    l10n::get(STRING_FOR_FILL_LAUNCH),
	    l10n::get(STRING_FOR_ONCE_LAUNCH),
	};
	return {optionsls, kNumValues};
}

bool LaunchStyleMenu::has_current_clip() const {
	return currentSong && currentSong->contains_clip_for_undo(clip);
}

bool LaunchStyleMenu::setupAndCheckAvailability() {
	if (!has_current_clip())
		return false;
	currentUIMode = UI_MODE_NONE;
	this->currentOption = static_cast<int32_t>(clip->launchStyle);

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void LaunchStyleMenu::selectEncoderAction(int8_t offset) {
	if (!has_current_clip())
		return;
	// The other panel may have committed before its deferred refresh was serviced.
	refresh_shared_model();
	const auto previous_style = clip->launchStyle;
	ContextMenu::selectEncoderAction(offset);
	clip->launchStyle = static_cast<LaunchStyle>(currentOption);
	if (clip->launchStyle != previous_style) {
		const auto peer =
		    ui_session::current() == ui_session::Id::Local ? ui_session::Id::Remote : ui_session::Id::Local;
		ui_session::navigation.for_owner(peer).shared_model_refresh.request();
	}
}

void LaunchStyleMenu::refresh_shared_model() {
	if (!has_current_clip() || currentOption == static_cast<int32_t>(clip->launchStyle))
		return;
	currentOption = static_cast<int32_t>(clip->launchStyle);
	scrollPos = currentOption;
	if (display->haveOLED())
		renderUIsForOled();
	else
		drawCurrentOption();
}

} // namespace deluge::gui::context_menu::clip_settings
