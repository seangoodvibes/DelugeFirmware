#include "CppUTest/TestHarness.h"
#include "model/favourite/favourites_session_state.h"

using namespace deluge::gui::ui_session;

TEST_GROUP(FavouritesSessions){};

TEST(FavouritesSessions, different_categories_and_banks_have_independent_navigation) {
	FavouritesSessionState state;
	CHECK(state.select("SONG", 3));
	state.selection().favourite = 4;
	state.bank().favourites[4].filename = "SONGS/one.XML";
	{
		Scope remote(Id::Remote);
		CHECK(state.select("SAMPLES", 3));
		CHECK_FALSE(state.selection().favourite.has_value());
		CHECK(state.bank().favourites[4].filename.empty());
		state.selection().favourite = 7;
		CHECK(state.select("SONG", 2));
		CHECK_EQUAL(2, state.bank().number);
	}
	CHECK_EQUAL(3, state.bank().number);
	CHECK_EQUAL(4, state.selection().favourite.value());
	STRCMP_EQUAL("SONGS/one.XML", state.bank().favourites[4].filename.c_str());
}

TEST(FavouritesSessions, same_bank_shares_unsaved_edits_without_sharing_selection) {
	FavouritesSessionState state;
	state.select("SYNTHS", 0);
	auto* local_bank = &state.bank();
	state.selection().favourite = 1;
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(state.select("SYNTHS", 0));
		POINTERS_EQUAL(local_bank, &state.bank());
		state.selection().favourite = 5;
		state.bank().favourites[5].colour = 12;
		state.bank().unsavedChanges = true;
	}
	CHECK_EQUAL(1, state.selection().favourite.value());
	CHECK_EQUAL(12, state.bank().favourites[5].colour.value());
	CHECK(state.bank().unsavedChanges);
}

TEST(FavouritesSessions, closing_and_reusing_one_panel_does_not_erase_peer_bank) {
	FavouritesSessionState state;
	state.select("SONG", 0);
	state.bank().favourites[0].filename = "keep";
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(state.select("SONG", 0));
	}
	state.release();
	CHECK(state.select("SAMPLES", 1));
	{
		Scope remote(Id::Remote);
		STRCMP_EQUAL("keep", state.bank().favourites[0].filename.c_str());
		state.release();
		CHECK(state.select("SYNTHS", 2));
		CHECK(state.bank().favourites[0].filename.empty());
		for (size_t i = 0; i < kNumFavourites; ++i) {
			CHECK_EQUAL(i, state.bank().favourites[i].position);
		}
	}
	STRCMP_EQUAL("SAMPLES", state.bank().category.c_str());
	CHECK_EQUAL(1, state.bank().number);
}

TEST(FavouritesSessions, reselecting_same_bank_keeps_edits_and_resets_selection) {
	FavouritesSessionState state;
	state.select("SONG", 0);
	state.selection().favourite = 2;
	state.bank().favourites[2].filename = "keep";
	CHECK_FALSE(state.select(state.bank().category, 0));
	CHECK_FALSE(state.selection().favourite.has_value());
	STRCMP_EQUAL("keep", state.bank().favourites[2].filename.c_str());
	CHECK(state.select(state.bank().category, 2));
	STRCMP_EQUAL("SONG", state.bank().category.c_str());
	CHECK_EQUAL(2, state.bank().number);
}
