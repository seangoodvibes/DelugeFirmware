#pragma once

#include "gui/ui/ui_session.h"
#include <array>
#include <span>

namespace deluge::gui::menu_item {

// Each cursor refers into its own panel's list. Lists start from the same
// declaration, never from another panel's potentially reordered entries.
template <typename Container>
class SessionMenuEntries {
public:
	using Iterator = typename Container::iterator;
	struct Bank {
		explicit Bank(std::span<const typename Container::value_type> entries)
		    : items(entries.begin(), entries.end()), current(items.end()) {}
		Container items;
		Iterator current;
		bool initial_selection_pending = true;
	};
	explicit SessionMenuEntries(std::span<const typename Container::value_type> entries)
	    : banks_{Bank(entries), Bank(entries)} {}
	SessionMenuEntries(const SessionMenuEntries&) = delete;
	SessionMenuEntries& operator=(const SessionMenuEntries&) = delete;
	Bank& active() { return banks_[static_cast<size_t>(ui_session::current())]; }
	const Bank& active() const { return banks_[static_cast<size_t>(ui_session::current())]; }

private:
	std::array<Bank, 2> banks_;
};

} // namespace deluge::gui::menu_item
