#pragma once

#include "gui/ui/ui_navigation_state.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include <array>
#include <type_traits>

namespace deluge::model {

// Cooperative allocation-boundary check, not an object pin or interrupt lock.
// A changed source must not be used to finish a precomputed bulk edit.
class NoteRowEditContext {
public:
	NoteRowEditContext(InstrumentClip* clip, int32_t row_id, NoteRow* row)
	    : song_(currentSong), clip_(clip), row_(row), row_id_(row_id),
	      local_revision_(revision(gui::ui_session::Id::Local)),
	      remote_revision_(revision(gui::ui_session::Id::Remote)) {
		if (!song_ || !clip_ || !row_ || clip_->type != ClipType::INSTRUMENT
		    || clip_->getNoteRowFromId(row_id_) != row_)
			return;
		clip_registered_ = song_->contains_clip_for_undo(clip_);
		identity_ = row_->undo_identity;
		count_ = row_->notes.getNumElements();
		first_ = count_ ? row_->notes.getElement(0) : nullptr;
		clip_length_ = clip_->loopLength;
		row_length_ = row_->loopLengthIfIndependent;
		output_ = clip_->output;
		clip_direction_ = clip_->sequenceDirectionMode;
		row_direction_ = row_->sequenceDirectionMode;
		expression_offset_ = row_->paramManager.getExpressionParamSetOffset();
		for (size_t i = 0; i < collections_.size(); ++i)
			collections_[i] = row_->paramManager.summaries[i].paramCollection;
		snapshot_valid_ = true;
	}

	bool valid() const {
		if (!source_valid_ || !target_valid())
			return false;
		if (row_->notes.getNumElements() != count_ || (count_ && row_->notes.getElement(0) != first_)) {
			source_valid_ = false;
			return false;
		}
		return true;
	}

	// For operations that deliberately resize the note vector, retain ownership
	// checks without requiring its old count/address.
	bool target_valid() const {
		if (!snapshot_valid_ || !song_ || currentSong != song_ || gui::ui_session::current() != initiating_owner_
		    || revision(gui::ui_session::Id::Local) != local_revision_
		    || revision(gui::ui_session::Id::Remote) != remote_revision_)
			return invalidate();
		// Registered clips must remain owned before any clip or row storage is read.
		// Unpublished clones still rely on structural invalidation for lifetime changes.
		const bool currently_registered = song_->contains_clip_for_undo(clip_);
		if (clip_registered_ && !currently_registered) {
			return invalidate();
		}
		// Publication observed at any validation makes ownership mandatory thereafter.
		clip_registered_ = clip_registered_ || currently_registered;
		// Compare the looked-up address before touching potentially released row storage.
		if (clip_->type != ClipType::INSTRUMENT || clip_->getNoteRowFromId(row_id_) != row_)
			return invalidate();
		if (!identity_ || row_->undo_identity != identity_ || clip_->loopLength != clip_length_
		    || row_->loopLengthIfIndependent != row_length_ || clip_->output != output_
		    || clip_->sequenceDirectionMode != clip_direction_ || row_->sequenceDirectionMode != row_direction_
		    || row_->paramManager.getExpressionParamSetOffset() != expression_offset_)
			return invalidate();
		for (size_t i = 0; i < collections_.size(); ++i) {
			if (row_->paramManager.summaries[i].paramCollection != collections_[i])
				return invalidate();
		}
		return true;
	}

private:
	bool invalidate() const {
		snapshot_valid_ = false;
		return false;
	}
	static uint64_t revision(gui::ui_session::Id owner) {
		return gui::ui_session::navigation.for_owner(owner).structural_refresh.revision();
	}
	const gui::ui_session::Id initiating_owner_ = gui::ui_session::current();
	Song* song_;
	InstrumentClip* clip_;
	mutable bool clip_registered_ = false;
	mutable bool snapshot_valid_ = false;
	mutable bool source_valid_ = true;
	NoteRow* row_;
	int32_t row_id_;
	uint64_t identity_ = 0;
	int32_t count_ = 0;
	Note* first_ = nullptr;
	int32_t clip_length_ = 0;
	int32_t row_length_ = 0;
	Output* output_ = nullptr;
	SequenceDirection clip_direction_ = SequenceDirection::FORWARD;
	SequenceDirection row_direction_ = SequenceDirection::OBEY_PARENT;
	int32_t expression_offset_ = 0;
	std::array<ParamCollection*, std::extent_v<decltype(ParamManager::summaries)>> collections_{};
	uint64_t local_revision_;
	uint64_t remote_revision_;
};

} // namespace deluge::model
