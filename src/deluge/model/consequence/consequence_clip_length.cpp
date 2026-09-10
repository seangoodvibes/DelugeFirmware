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

#include "model/consequence/consequence_clip_length.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"

ConsequenceClipLength::ConsequenceClipLength(Clip* newClip, int32_t oldLength) {
	type = Consequence::CLIP_LENGTH;

	clip = newClip;
	lengthToRevertTo = oldLength;
}

Error ConsequenceClipLength::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip))
		return Error::BUG;
	if (!clip->output || lengthToRevertTo <= 0 || clip->loopLength <= 0)
		return Error::BUG;

	Song* const owner = modelStack->song;
	Song* const active_song = currentSong;
	Clip* const target_clip = clip;
	const auto target_type = clip->type;
	auto* const target_output = clip->output;
	const int32_t requested_length = lengthToRevertTo;
	auto revision = [](deluge::gui::ui_session::Id id) {
		return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
	};
	const auto local_revision = revision(deluge::gui::ui_session::Id::Local);
	const auto remote_revision = revision(deluge::gui::ui_session::Id::Remote);
	uint64_t marker_before = 0;
	uint64_t requested_marker = 0;
	uint64_t expected_start = 0, expected_end = 0;
	uint64_t* target_marker = nullptr;
	auto* const target_sample =
	    target_type == ClipType::AUDIO ? static_cast<AudioClip*>(clip)->sampleHolder.audioFile : nullptr;
	const bool target_reversed =
	    target_type == ClipType::AUDIO && static_cast<AudioClip*>(clip)->sampleControls.isCurrentlyReversed();
	if (sample_marker != SampleMarker::NONE) {
		if (clip->type != ClipType::AUDIO)
			return Error::BUG;
		auto& holder = static_cast<AudioClip*>(clip)->sampleHolder;
		uint64_t* marker;
		switch (sample_marker) {
		case SampleMarker::START:
			marker = &holder.startPos;
			break;
		case SampleMarker::END:
			marker = &holder.endPos;
			break;
		default:
			return Error::BUG;
		}
		marker_before = *marker;
		requested_marker = markerValueToRevertTo;
		target_marker = marker;
		*marker = requested_marker;
		expected_start = holder.startPos;
		expected_end = holder.endPos;
	}

	int32_t lengthBeforeRevert = clip->loopLength;

	if (!modelStack->song->setClipLength(clip, lengthToRevertTo, nullptr))
		return Error::BUG;

	if (currentSong != active_song || modelStack->song != owner
	    || revision(deluge::gui::ui_session::Id::Local) != local_revision
	    || revision(deluge::gui::ui_session::Id::Remote) != remote_revision)
		return Error::BUG;
	if (!owner->contains_clip_for_undo(target_clip) || target_clip->type != target_type
	    || target_clip->output != target_output || target_clip->loopLength != requested_length)
		return Error::BUG;
	if (target_marker) {
		auto* audio_clip = static_cast<AudioClip*>(target_clip);
		if (audio_clip->sampleHolder.audioFile != target_sample
		    || audio_clip->sampleControls.isCurrentlyReversed() != target_reversed
		    || audio_clip->sampleHolder.startPos != expected_start || audio_clip->sampleHolder.endPos != expected_end)
			return Error::BUG;
	}

	if (target_marker)
		markerValueToRevertTo = marker_before;
	lengthToRevertTo = lengthBeforeRevert;

	return Error::NONE;
}
