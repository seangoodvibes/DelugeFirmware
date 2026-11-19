#include "CppUTest/TestHarness.h"
#include "gui/ui/browser/preset_navigation.h"
#include <array>
#include <cstddef>

using deluge::gui::performPresetLoad;
using deluge::gui::presetInstrumentAvailable;
using deluge::gui::PresetNavigationContext;
using deluge::gui::PresetNavigationDestination;

TEST_GROUP(PresetLoadTests) {
	bool pending = true;
	uint32_t mode = UI_MODE_NONE;
	Error result = Error::NONE;
};

TEST(PresetLoadTests, ConsumesPreviewAndSetsBusyBeforeTimerCancellation) {
	bool cancelled = false;
	performPresetLoad(
	    pending, mode, result,
	    [&] {
		    CHECK_FALSE(pending);
		    CHECK_EQUAL(UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED, mode);
		    cancelled = true;
	    },
	    [&] {
		    CHECK_TRUE(cancelled);
		    CHECK_FALSE(pending);
		    CHECK_EQUAL(UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED, mode);
		    return Error::NONE;
	    });
	CHECK_FALSE(pending);
	CHECK_EQUAL(UI_MODE_NONE, mode);
}

TEST(PresetLoadTests, FailedDirectLoadRecordsErrorAndRestoresPreviousMode) {
	mode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
	performPresetLoad(pending, mode, result, [] {}, [] { return Error::FILE_CORRUPTED; });
	CHECK(result == Error::FILE_CORRUPTED);
	CHECK_EQUAL(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, mode);
	CHECK_FALSE(pending);
}

TEST(PresetLoadTests, SuccessfulRetryClearsPreviousError) {
	result = Error::FILE_CORRUPTED;
	pending = false; // Direct retry also works without a scheduled preview.
	performPresetLoad(pending, mode, result, [] {}, [] { return Error::NONE; });
	CHECK(result == Error::NONE);
	CHECK_EQUAL(UI_MODE_NONE, mode);
}

TEST_GROUP(PresetNavigationContextTests) {
	PresetNavigationContext context;
	// The context compares identities without dereferencing model/UI objects.
	// Distinct, aligned addresses supply those identities without fake class definitions.
	std::array<std::max_align_t, 5> identities{};
	Song* song;
	UI* ui;
	Clip* clip;
	Clip* otherClip;

	void setup() override {
		song = reinterpret_cast<Song*>(&identities[0]);
		ui = reinterpret_cast<UI*>(&identities[1]);
		clip = reinterpret_cast<Clip*>(&identities[2]);
		otherClip = reinterpret_cast<Clip*>(&identities[3]);
		context.song = song;
		context.ui = ui;
		context.currentClip = clip;
		context.selectedClip = clip;
		context.displayClip = clip;
		context.uiMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
	}
};

TEST(PresetNavigationContextTests, SameHeldTargetMatches) {
	CHECK(context.matchesUI(song, ui, context.uiMode, clip));
	CHECK(context.matchesSelection(clip, false));
}

TEST(PresetNavigationContextTests, ChangedSongUIOrCurrentClipCancels) {
	CHECK_FALSE(context.matchesUI(nullptr, ui, context.uiMode, clip));
	CHECK_FALSE(context.matchesUI(song, nullptr, context.uiMode, clip));
	CHECK_FALSE(context.matchesUI(song, ui, context.uiMode, otherClip));
}

TEST(PresetNavigationContextTests, UnrecordedModeChangeCancels) {
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_NONE, clip));
}

TEST(PresetNavigationContextTests, ChangedSelectionCancelsWhilePadHeld) {
	CHECK_FALSE(context.matchesSelection(otherClip, true));
	CHECK_FALSE(context.matchesSelection(nullptr, true));
}

TEST(PresetNavigationContextTests, SessionPadReleasePreservesPendingTargetAfterSelectionClears) {
	context.markPadReleased(true);
	CHECK_TRUE(context.padReleased);
	CHECK(context.matchesUI(song, ui, UI_MODE_NONE, clip));
	CHECK(context.matchesSelection(nullptr, false));
	POINTERS_EQUAL(clip, context.displayClip);
}

TEST(PresetNavigationContextTests, ReleaseCannotAuthorizeChangedTarget) {
	context.markPadReleased(false);
	CHECK_FALSE(context.padReleased);
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_NONE, clip));
}

TEST(PresetNavigationContextTests, NewGestureAfterReleaseCancels) {
	context.markPadReleased(true);
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, clip));
}

TEST(PresetNavigationContextTests, ChangingViewOrTargetAfterReleaseStillCancels) {
	context.markPadReleased(true);
	CHECK_FALSE(context.matchesUI(song, nullptr, UI_MODE_NONE, clip));
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_NONE, otherClip));
	CHECK_FALSE(context.matchesUI(nullptr, ui, UI_MODE_NONE, clip));
}

TEST(PresetNavigationContextTests, UnrelatedUIModeDoesNotBecomeAPadRelease) {
	context.uiMode = UI_MODE_NONE;
	context.markPadReleased(true);
	CHECK_FALSE(context.padReleased);
}

TEST(PresetNavigationContextTests, ArrangerNavigationUsesOriginatingCallerRegardlessOfVisibleUI) {
	context.fromArranger = true;
	context.replacingWholeOutput = true;
	context.displayClip = nullptr;
	CHECK(context.destination() == PresetNavigationDestination::ARRANGER);
	context.ui = nullptr; // Dispatch does not infer the caller from this UI pointer.
	CHECK(context.destination() == PresetNavigationDestination::ARRANGER);
}

TEST(PresetNavigationContextTests, ArrangerTargetChangeCancelsEvenAfterPadRelease) {
	context.uiMode = UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;
	context.fromArranger = true;
	context.selectedClip = nullptr;
	CHECK_FALSE(context.matchesSelection(nullptr, false));
	context.markPadReleased(true);
	CHECK(context.matchesUI(song, ui, UI_MODE_NONE, clip));
	CHECK(context.matchesSelection(nullptr, true));
	CHECK_FALSE(context.matchesSelection(nullptr, false));
}

TEST(PresetNavigationContextTests, RowNavigationResumesWithSavedClip) {
	CHECK(context.destination() == PresetNavigationDestination::CLIP);
	context.markPadReleased(true);
	CHECK(context.destination() == PresetNavigationDestination::CLIP);
	POINTERS_EQUAL(clip, context.displayClip);
}

TEST(PresetNavigationContextTests, GridLabelClipDoesNotTurnWholeOutputReplacementIntoClipReplacement) {
	context.replacingWholeOutput = true;
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
	context.markPadReleased(true);
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
}

TEST(PresetNavigationContextTests, GridReplacementScopeSurvivesDelegationToAnotherUI) {
	context.replacingWholeOutput = true;
	context.ui = reinterpret_cast<UI*>(&identities[4]);
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
	CHECK_FALSE(context.matchesSelection(otherClip, true));
}

TEST(PresetNavigationContextTests, NavigationWithoutADisplayClipReplacesOutput) {
	context.displayClip = nullptr;
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
}

TEST_GROUP(PresetAvailabilityTests) {
	std::array<std::max_align_t, 2> identities{};
	Instrument* origin;
	Instrument* other;
	void setup() override {
		origin = reinterpret_cast<Instrument*>(&identities[0]);
		other = reinterpret_cast<Instrument*>(&identities[1]);
	}
};

TEST(PresetAvailabilityTests, OriginWithActiveSessionClipRemainsReachableFromCandidate) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, true, origin, origin));
}

TEST(PresetAvailabilityTests, WholeOutputNavigationCanReturnToOccupiedOrigin) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, true, origin, origin));
}

TEST(PresetAvailabilityTests, ExemptionDoesNotMakeAnotherActivePresetAvailable) {
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, true, other, origin));
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, false, other, origin));
}

TEST(PresetAvailabilityTests, OrdinaryNavigationStillFiltersOccupiedOrigin) {
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, true, origin));
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, false, origin));
}

TEST(PresetAvailabilityTests, InactiveSessionInstrumentIsAvailableOnlyForClipNavigation) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, false, other));
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, false, other));
}

TEST(PresetAvailabilityTests, DiskAndHibernatingPresetsRemainAvailable) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, false, false, nullptr));
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, false, false, other));
}

TEST(PresetAvailabilityTests, UnrestrictedBrowserKeepsOccupiedPresets) {
	CHECK(presetInstrumentAvailable(Availability::ANY, true, true, other));
}
