/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/views/clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/action/reversible_shift.h"
#include "model/clip/clip.h"
#include "model/clip/clip_length_resync.h"
#include "model/consequence/consequence_clip_horizontal_shift.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include <memory>

uint32_t ClipView::getMaxZoom() {
	return getCurrentClip()->getMaxZoom();
}

uint32_t ClipView::getMaxLength() {
	return getCurrentClip()->getMaxLength();
}

void ClipView::focusRegained() {
	ClipNavigationTimelineView::focusRegained();
}

ActionResult ClipView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Horizontal encoder button press-down - don't let it do its zoom level thing if zooming etc not currently
	// accessible
	if (b == X_ENC && on && !getCurrentClip()->currentlyScrollableAndZoomable()) {}

#ifdef BUTTON_SEQUENCE_DIRECTION_X
	else if (x == BUTTON_SEQUENCE_DIRECTION_X && y == BUTTON_SEQUENCE_DIRECTION_Y) {
		if (on && isNoUIModeActive()) {
			getCurrentClip()->sequenceDirection++;
			if (getCurrentClip()->sequenceDirection == NUM_SEQUENCE_DIRECTION_OPTIONS) {
				getCurrentClip()->sequenceDirection = 0;
			}
			view_for_session().setModLedStates();
		}
	}
#endif
	else {
		return ClipNavigationTimelineView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

bool ClipView::lengthenClip(int32_t newLength, Action*& action) {

	action = nullptr;

	Song* const target_song = currentSong;
	if (!target_song || newLength <= 0 || newLength > kMaxSequenceLength)
		return false;
	Clip* const target_clip = getCurrentClip();
	if (!target_clip || !target_song->contains_clip_for_undo(target_clip) || !target_clip->output
	    || target_clip->loopLength <= 0)
		return false;
	auto* const target_output = target_clip->output;
	const auto target_type = target_clip->type;
	const auto owner = deluge::gui::ui_session::current();
	auto revision = [](deluge::gui::ui_session::Id id) {
		return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
	};
	const auto local_revision = revision(deluge::gui::ui_session::Id::Local);
	const auto remote_revision = revision(deluge::gui::ui_session::Id::Remote);
	auto context_valid = [&] {
		return currentSong == target_song && deluge::gui::ui_session::current() == owner
		       && revision(deluge::gui::ui_session::Id::Local) == local_revision
		       && revision(deluge::gui::ui_session::Id::Remote) == remote_revision && getCurrentClip() == target_clip
		       && target_song->contains_clip_for_undo(target_clip) && target_clip->output == target_output
		       && target_clip->type == target_type;
	};

	// Check membership before dereferencing history retained across callbacks.
	auto history_valid = [&] {
		return !action
		       || (actionLogger.firstAction[BEFORE] == action && action->navigation_owner == owner
		           && action->captured_song == target_song && action->captured_output == target_output
		           && action->currentClip == target_clip);
	};

	// If the last action was a shorten, undo it
	bool undoing = (actionLogger.firstAction[BEFORE]
	                && actionLogger.firstAction[BEFORE]->navigation_owner == deluge::gui::ui_session::current()
	                && actionLogger.firstAction[BEFORE]->captured_song == currentSong
	                && actionLogger.firstAction[BEFORE]->captured_output == getCurrentClip()->output
	                && actionLogger.firstAction[BEFORE]->openForAdditions
	                && actionLogger.firstAction[BEFORE]->type == ActionType::CLIP_LENGTH_DECREASE
	                && actionLogger.firstAction[BEFORE]->currentClip == getCurrentClip());

	if (undoing) {
		bool& resync_allowed = deluge::model::clip_length_resync_allowed();
		const bool previous_resync = resync_allowed;
		resync_allowed = false; // Little bit of a hack. We don't want any resyncing to happen to this Clip
		undoing = actionLogger.revert(BEFORE, false, false);
		resync_allowed = previous_resync;
		if (!undoing || !context_valid() || target_clip->loopLength <= 0
		    || target_clip->loopLength > kMaxSequenceLength) {
			return false;
		}
	}

	// Only if that didn't get us directly to the correct length, manually set length. This will do a resync if playback
	// active
	if (getCurrentClip()->loopLength != newLength) {
		const int32_t allocation_length = target_clip->loopLength;
		ActionType actionType = (newLength < getCurrentClip()->loopLength) ? ActionType::CLIP_LENGTH_DECREASE
		                                                                   : ActionType::CLIP_LENGTH_INCREASE;

		// If we are in middle of PATTERN_PASTE Action -> Resize need to be part of preview Pattern
		if (actionLogger.firstAction[BEFORE]
		    && actionLogger.firstAction[BEFORE]->navigation_owner == deluge::gui::ui_session::current()
		    && actionLogger.firstAction[BEFORE]->captured_song == currentSong
		    && actionLogger.firstAction[BEFORE]->captured_output == getCurrentClip()->output
		    && actionLogger.firstAction[BEFORE]->currentClip == getCurrentClip()
		    && actionLogger.firstAction[BEFORE]->openForAdditions
		    && actionLogger.firstAction[BEFORE]->type == ActionType::PATTERN_PASTE) {
			actionType = ActionType::PATTERN_PASTE;
		}

		action = actionLogger.getNewAction(actionType, ActionAddition::ALLOWED);
		if (!context_valid() || target_clip->loopLength != allocation_length
		    || (action && actionLogger.firstAction[BEFORE] != action)) {
			action = nullptr;
			return false;
		}
		if (action && action->currentClip != getCurrentClip()) {
			action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
			if (!context_valid() || target_clip->loopLength != allocation_length
			    || (action && actionLogger.firstAction[BEFORE] != action)) {
				action = nullptr;
				return false;
			}
		}

		if (!history_valid() || !currentSong->setClipLength(getCurrentClip(), newLength, action)) {
			action = nullptr;
			return false;
		}
	}

	// Otherwise, do the resync that we missed out on doing
	else {
		if (undoing && deluge::model::clip_length_resync_allowed() && playbackHandler.isEitherClockActive()) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			currentPlaybackMode->reSyncClip(modelStack);
		}
	}

	if (!context_valid() || !history_valid() || target_clip->loopLength != newLength) {
		action = nullptr;
		return false;
	}
	return true;
}

bool ClipView::shortenClip(int32_t newLength, Action*& action) {

	action = nullptr;

	Song* const target_song = currentSong;
	if (!target_song || newLength <= 0 || newLength > kMaxSequenceLength)
		return false;
	Clip* const target_clip = getCurrentClip();
	if (!target_clip || !target_song->contains_clip_for_undo(target_clip) || !target_clip->output
	    || target_clip->loopLength <= 0)
		return false;
	auto* const target_output = target_clip->output;
	const auto target_type = target_clip->type;
	const auto owner = deluge::gui::ui_session::current();
	auto revision = [](deluge::gui::ui_session::Id id) {
		return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
	};
	const auto local_revision = revision(deluge::gui::ui_session::Id::Local);
	const auto remote_revision = revision(deluge::gui::ui_session::Id::Remote);
	auto context_valid = [&] {
		return currentSong == target_song && deluge::gui::ui_session::current() == owner
		       && revision(deluge::gui::ui_session::Id::Local) == local_revision
		       && revision(deluge::gui::ui_session::Id::Remote) == remote_revision && getCurrentClip() == target_clip
		       && target_song->contains_clip_for_undo(target_clip) && target_clip->output == target_output
		       && target_clip->type == target_type;
	};

	// Check membership before dereferencing history retained across callbacks.
	auto history_valid = [&] {
		return !action
		       || (actionLogger.firstAction[BEFORE] == action && action->navigation_owner == owner
		           && action->captured_song == target_song && action->captured_output == target_output
		           && action->currentClip == target_clip);
	};

	const int32_t allocation_length = target_clip->loopLength;
	// If we are in middle of pasting Pattern ACtion -> Resize is part of preview Pattern
	if (actionLogger.firstAction[BEFORE]
	    && actionLogger.firstAction[BEFORE]->navigation_owner == deluge::gui::ui_session::current()
	    && actionLogger.firstAction[BEFORE]->captured_song == currentSong
	    && actionLogger.firstAction[BEFORE]->captured_output == getCurrentClip()->output
	    && actionLogger.firstAction[BEFORE]->currentClip == getCurrentClip()
	    && actionLogger.firstAction[BEFORE]->openForAdditions
	    && actionLogger.firstAction[BEFORE]->type == ActionType::PATTERN_PASTE) {
		action = actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
		if (!context_valid() || target_clip->loopLength != allocation_length
		    || (action && actionLogger.firstAction[BEFORE] != action)) {
			action = nullptr;
			return false;
		}
	}
	else {
		action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::ALLOWED);
		if (!context_valid() || target_clip->loopLength != allocation_length
		    || (action && actionLogger.firstAction[BEFORE] != action)) {
			action = nullptr;
			return false;
		}
		if (action && action->currentClip != getCurrentClip()) {
			action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::NOT_ALLOWED);
			if (!context_valid() || target_clip->loopLength != allocation_length
			    || (action && actionLogger.firstAction[BEFORE] != action)) {
				action = nullptr;
				return false;
			}
		}
	}

	if (!history_valid() || !currentSong->setClipLength(getCurrentClip(), newLength, action)) {
		action = nullptr;
		return false;
	}
	if (!context_valid() || !history_valid() || target_clip->loopLength != newLength) {
		action = nullptr;
		return false;
	}
	return true;
}

ActionResult ClipView::horizontalEncoderAction(int32_t offset) {

	// Shift button pressed - edit length
	if (isNoUIModeActive() && !Buttons::isButtonPressed(deluge::hid::button::Y_ENC)
	    && Buttons::isShiftButtonPressed()) {

		// If tempoless recording, don't allow
		if (!getCurrentClip()->currentlyScrollableAndZoomable()) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH));
			return ActionResult::DEALT_WITH;
		}

		uint32_t oldLength = getCurrentClip()->loopLength;

		// If we're not scrolled all the way to the right, go there now
		if (scrollRightToEndOfLengthIfNecessary(oldLength)) {
			return ActionResult::DEALT_WITH;
		}

		// Or if still here, we've already scrolled far-right

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		Action* action = nullptr;

		uint32_t newLength = changeClipLength(offset, oldLength, action);
		if (!newLength)
			return ActionResult::DEALT_WITH;

		displayNumberOfBarsAndBeats(newLength, currentSong->x_zoom_for_session()[NAVIGATION_CLIP], false, "LONG");

		if (action) {
			action->xScrollClip[AFTER] = currentSong->x_scroll_for_session()[NAVIGATION_CLIP];
		}
		return ActionResult::DEALT_WITH;
	}

	// Or, maybe shift everything horizontally
	else if ((isNoUIModeActive() && Buttons::isButtonPressed(deluge::hid::button::Y_ENC))
	         || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	             && Buttons::isButtonPressed(deluge::hid::button::CLIP_VIEW))) {
		// Just be safe - maybe not necessary
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		auto movement = deluge::model::horizontal_shift_amount(offset, getPosFromSquare(0), getPosFromSquare(1));
		if (!movement || *movement == 0)
			return ActionResult::DEALT_WITH;
		int32_t shiftAmount = *movement;
		Clip* clip = getCurrentClip();

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		UI* currentUI = getCurrentUI();

		// Always shift automation when in Automation View
		// or also shift automation when default setting to only shift automation in Automation View is false
		bool shiftAutomation = (currentUI == &automation_view_for_session() || !FlashStorage::automationShift);

		// Always shift Notes and MPE when you're not in Automation View
		bool shiftSequenceAndMPE = (currentUI != &automation_view_for_session());
		// An already-impossible shift must not create an action and clear redo.
		if (!clip->can_shift_horizontally(shiftAmount, shiftSequenceAndMPE))
			return ActionResult::DEALT_WITH;

		Song* const song = currentSong;
		Output* const output = clip->output;
		const auto owner = deluge::gui::ui_session::current();
		const auto revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
		auto context_unchanged = [&]() {
			return currentSong == song && deluge::gui::ui_session::current() == owner && getCurrentUI() == currentUI
			       && deluge::gui::ui_session::navigation.active().structural_refresh.revision() == revision
			       && song->getCurrentClip() == clip && song->contains_clip_for_undo(clip) && clip->output == output;
		};

		Action* action = actionLogger.firstAction[BEFORE];
		bool reuse = action && action->navigation_owner == owner && action->captured_song == song
		             && action->captured_output == output && action->type == ActionType::CLIP_HORIZONTAL_SHIFT
		             && action->openForAdditions && action->currentClip == clip && action->view == currentUI
		             && (!action->firstConsequence
		                 || static_cast<ConsequenceClipHorizontalShift*>(action->firstConsequence)
		                        ->can_accumulate(clip, shiftAmount, shiftAutomation, shiftSequenceAndMPE));
		if (!reuse) {
			action = actionLogger.getNewAction(ActionType::CLIP_HORIZONTAL_SHIFT, ActionAddition::NOT_ALLOWED);
		}
		if (!action || !context_unchanged() || actionLogger.firstAction[BEFORE] != action)
			return ActionResult::DEALT_WITH;

		// Reserve the complete history entry before editing. Keep a new consequence
		// private until the shift succeeds, so a rejected shift records no inverse.
		auto destroy_consequence = [](ConsequenceClipHorizontalShift* consequence) {
			consequence->~ConsequenceClipHorizontalShift();
			delugeDealloc(consequence);
		};
		std::unique_ptr<ConsequenceClipHorizontalShift, decltype(destroy_consequence)> pending(nullptr,
		                                                                                       destroy_consequence);
		Consequence* const previous = action->firstConsequence;
		const int32_t previous_amount = previous ? static_cast<ConsequenceClipHorizontalShift*>(previous)->amount : 0;
		auto history_unchanged = [&]() {
			// Identity checks must precede every dereference of retained history.
			if (!context_unchanged() || actionLogger.firstAction[BEFORE] != action)
				return false;
			if (action->navigation_owner != owner || action->captured_song != song || action->captured_output != output
			    || action->type != ActionType::CLIP_HORIZONTAL_SHIFT || !action->openForAdditions
			    || action->currentClip != clip || action->view != currentUI || action->firstConsequence != previous)
				return false;
			if (!previous)
				return true;
			auto* shift = static_cast<ConsequenceClipHorizontalShift*>(previous);
			return shift->amount == previous_amount
			       && shift->can_accumulate(clip, shiftAmount, shiftAutomation, shiftSequenceAndMPE);
		};
		if (!previous) {
			void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipHorizontalShift));
			if (!memory) {
				display->displayError(Error::INSUFFICIENT_RAM);
				return ActionResult::DEALT_WITH;
			}
			pending.reset(new (memory)
			                  ConsequenceClipHorizontalShift(clip, shiftAmount, shiftAutomation, shiftSequenceAndMPE));
		}
		if (!history_unchanged())
			return ActionResult::DEALT_WITH;

		bool wasShifted = clip->shiftHorizontally(modelStack, shiftAmount, shiftAutomation, shiftSequenceAndMPE);
		if (!wasShifted)
			return ActionResult::DEALT_WITH;
		// Do not dereference retained history after a yielding operation changed it.
		if (!history_unchanged())
			return ActionResult::DEALT_WITH;
		if (pending)
			action->addConsequence(pending.release());
		else
			static_cast<ConsequenceClipHorizontalShift*>(previous)->amount += shiftAmount;
		uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
		return ActionResult::DEALT_WITH;
	}

	// Or, if shift button not pressed...
	else {

		// If tempoless recording, don't allow
		if (!getCurrentClip()->currentlyScrollableAndZoomable()) {
			return ActionResult::DEALT_WITH;
		}

		// Otherwise, let parent do scrolling and zooming
		return ClipNavigationTimelineView::horizontalEncoderAction(offset);
	}
}
uint32_t ClipView::changeClipLength(int32_t offset, uint32_t oldLength, Action*& action) {
	action = nullptr;
	bool rightOnSquare;
	int64_t newLength;
	int32_t endSquare = getSquareFromPos(oldLength, &rightOnSquare);

	// Lengthening
	if (offset > 0) {

		newLength = static_cast<int64_t>(getPosFromSquare(endSquare)) + getLengthExtendAmount(endSquare);
		if (newLength <= 0 || newLength > kMaxSequenceLength)
			return 0;

		// If we're still within limits
		if (newLength <= (uint32_t)kMaxSequenceLength) {

			if (!lengthenClip(newLength, action))
				return 0;

			if (!scrollRightToEndOfLengthIfNecessary(newLength)) {
doReRender:
				uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
			}
		}
	}

	// Shortening
	else {

		if (!rightOnSquare) {
			newLength = getPosFromSquare(endSquare);
		}
		else {
			newLength = static_cast<int64_t>(oldLength) - getLengthChopAmount(endSquare);
		}

		if (newLength <= 0 || newLength > kMaxSequenceLength)
			return 0;

		if (newLength > 0) {

			if (!shortenClip(newLength, action))
				return 0;

			// Scroll / zoom as needed
			if (!scrollLeftIfTooFarRight(newLength)) {
				// If this zoom level no longer valid...
				if (zoomToMax(true)) {
					// editor.displayZoomLevel(true);
				}
				else {
					goto doReRender;
				}
			}
		}
	}
	return newLength;
}

int32_t ClipView::getLengthChopAmount(int32_t square) {

	square--; // We want the width of the square before
	while (!isSquareDefined(square)) {
		square--;
	}

	uint32_t xZoom = currentSong->x_zoom_for_session()[getNavSysId()];

	if (inTripletsView()) {
		if (xZoom < currentSong->triplets_level_for_session()) {
			return xZoom * 4 / 3;
		}
		else if (xZoom < currentSong->triplets_level_for_session() * 2) {
			return xZoom * 2 / 3 * (((square + 1) % 2) + 1);
		}
	}
	return xZoom;
}

int32_t ClipView::getLengthExtendAmount(int32_t square) {

	while (!isSquareDefined(square)) {
		square++;
	}

	uint32_t xZoom = currentSong->x_zoom_for_session()[getNavSysId()];

	if (inTripletsView()) {
		if (xZoom < currentSong->triplets_level_for_session()) {
			return xZoom * 4 / 3;
		}
		else if (xZoom < currentSong->triplets_level_for_session() * 2) {
			return xZoom * 2 / 3 * (((square + 1) % 2) + 1);
		}
	}
	return xZoom;
}

int32_t ClipView::getTickSquare() {

	int32_t newTickSquare = getSquareFromPos(getCurrentClip()->getLivePos());

	// See if we maybe want to do an auto-scroll
	if (getCurrentClip()->getCurrentlyRecordingLinearly()) {

		if (newTickSquare == kDisplayWidth && (!currentUIMode || currentUIMode == UI_MODE_AUDITIONING)
		    && getCurrentUI() == this && // currentPlaybackMode == &session &&
		    (getCurrentClip()->armState == ArmState::OFF || xScrollBeforeFollowingAutoExtendingLinearRecording != -1)) {

			if (xScrollBeforeFollowingAutoExtendingLinearRecording == -1) {
				xScrollBeforeFollowingAutoExtendingLinearRecording =
				    currentSong->x_scroll_for_session()[NAVIGATION_CLIP];
			}

			int32_t newXScroll = currentSong->x_scroll_for_session()[NAVIGATION_CLIP]
			                     + currentSong->x_zoom_for_session()[NAVIGATION_CLIP] * kDisplayWidth;

			horizontalScrollForLinearRecording(newXScroll);
		}
	}

	// Or if not, cancel following scrolling along, and go back to where we started
	else {
		if (xScrollBeforeFollowingAutoExtendingLinearRecording != -1) {
			int32_t newXScroll = xScrollBeforeFollowingAutoExtendingLinearRecording;
			xScrollBeforeFollowingAutoExtendingLinearRecording = -1;

			if (newXScroll != currentSong->x_zoom_for_session()[NAVIGATION_CLIP]) {
				horizontalScrollForLinearRecording(newXScroll);
			}
		}
	}

	return newTickSquare;
}
