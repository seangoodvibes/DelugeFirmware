#pragma once

#include "gui/ui/ui_session.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

inline constexpr size_t kNumFavourites = 16;

// Navigation belongs to a panel; edits belong to a bank. Two panels viewing the
// same bank must share its contents, including colour edits not yet saved to SD.
// Only two banks can be visible at once, so storage stays bounded.
class FavouritesSessionState {
public:
	struct Favourite {
		int position = 0;
		std::optional<uint8_t> colour;
		std::string filename;
	};
	struct Bank {
		std::string category;
		uint8_t number = 0;
		std::array<Favourite, kNumFavourites> favourites{};
		bool unsavedChanges = false;
		Bank(std::string category = {}, uint8_t number = 0) : category(std::move(category)), number(number) {
			for (size_t i = 0; i < favourites.size(); ++i) {
				favourites[i].position = i;
			}
		}
	};
	struct Selection {
		int bank_index = -1;
		std::optional<uint8_t> favourite;
	};

	Selection& selection() { return selections_.active(); }
	Bank& bank() {
		if (selection().bank_index < 0) {
			select({}, 0);
		}
		return banks_[selection().bank_index];
	}

	// The caller saves pending changes before switching. True means a new bank
	// needs loading from SD; false means its current in-memory contents are shared.
	bool select(std::string category, uint8_t number) {
		using namespace deluge::gui::ui_session;
		auto& own = selection();
		const auto& peer = selections_.for_owner(current() == Id::Local ? Id::Remote : Id::Local);
		auto matches = [&](int index) {
			return index >= 0 && banks_[index].category == category && banks_[index].number == number;
		};
		own.favourite.reset();
		if (matches(own.bank_index)) {
			return false;
		}
		if (matches(peer.bank_index)) {
			own.bank_index = peer.bank_index;
			return false;
		}
		const int available = peer.bank_index == 0 ? 1 : 0;
		banks_[available] = Bank(std::move(category), number);
		own.bank_index = available;
		return true;
	}

	// Do not clear a bank that the other panel is still viewing.
	void release() { selection() = {}; }

private:
	deluge::gui::ui_session::State<Selection> selections_;
	std::array<Bank, 2> banks_{};
};
