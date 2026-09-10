#pragma once
#include "gui/l10n/l10n.h"
#include "gui/ui/ui_session.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

// Mixin for a formatted title
class FormattedTitle {
public:
	FormattedTitle(l10n::String format_str, std::optional<uint8_t> arg = std::nullopt) : format_str_(format_str) {
		states_.for_owner(ui_session::Id::Local).arg = arg;
		states_.for_owner(ui_session::Id::Remote).arg = arg;
	}

	void format(int32_t arg) const {
		states_.active().title = l10n::get(format_str_);
		asterixToInt(states_.active().title.data(), arg);
	}

	[[nodiscard]] std::string_view title() const {
		if (states_.active().arg.has_value()) {
			format(states_.active().arg.value());
			states_.active().arg = std::nullopt;
		}
		return states_.active().title;
	}

private:
	l10n::String format_str_;
	struct TitleState {
		std::string title;
		std::optional<uint8_t> arg;
	};
	mutable ui_session::State<TitleState> states_;
};
} // namespace deluge::gui::menu_item
