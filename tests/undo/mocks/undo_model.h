#pragma once
// Minimal model collaborators. The consequence classes/implementations are real.
#include "definitions_cxx.hpp"
#include "model/note/note.h"
#include "model/note/note_row_identity.h"
#include "util/container/array/resizeable_array.h"
#include "util/container/retained_list.h"
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

class Action;
class InstrumentClip;
class ModelStackWithTimelineCounter;
class ModelStackWithNoteRow;
class NoteVector {
public:
	std::vector<int> values;
	std::vector<Note> entries;
	bool fail_insert = false, fail_commit = false;
	std::function<void()> on_reserve;
	bool ensureEnoughSpaceAllocated(int32_t) {
		if (on_reserve)
			on_reserve();
		return !fail_insert;
	}
	Error insert_at_index_without_allocation(int32_t i) {
		++insert_calls;
		if (fail_insert || fail_commit)
			return Error::INSUFFICIENT_RAM;
		entries.insert(entries.begin() + i, Note{});
		return Error::NONE;
	}
	int insert_calls = 0, delete_calls = 0;
	int32_t search(int32_t pos, int32_t) {
		return std::lower_bound(entries.begin(), entries.end(), pos,
		                        [](const Note& n, int32_t key) { return n.pos < key; })
		       - entries.begin();
	}
	int32_t getNumElements() { return entries.size(); }
	Note* getElement(int32_t i) { return i < 0 || static_cast<size_t>(i) >= entries.size() ? nullptr : &entries[i]; }
	int32_t insertAtKey(int32_t pos) {
		++insert_calls;
		if (fail_insert)
			return -1;
		auto i = search(pos, 0);
		entries.insert(entries.begin() + i, Note{});
		entries[i].pos = pos;
		return i;
	}
	void deleteAtIndex(int32_t i, int32_t count = 1, bool = true) {
		++delete_calls;
		entries.erase(entries.begin() + i, entries.begin() + i + count);
	}
	inline static std::function<void()> on_insert;
	inline static bool fail_any_insert = false;
	Error insertAtIndex(int32_t i, int32_t count = 1) {
		if (on_insert)
			on_insert();
		if (fail_any_insert || fail_insert)
			return Error::INSUFFICIENT_RAM;
		entries.insert(entries.begin() + i, count, Note{});
		return Error::NONE;
	}
	std::function<void()> on_repeats;
	bool fail_repeats = false;
	bool generateRepeats(int32_t length, int32_t end) {
		if (fail_repeats)
			return false;
		auto source = entries;
		source.erase(
		    std::remove_if(source.begin(), source.end(), [length](auto const& note) { return note.pos >= length; }),
		    source.end());
		entries = source;
		for (int32_t offset = length; offset < end; offset += length) {
			for (auto note : source) {
				note.pos += offset;
				if (note.pos < end)
					entries.push_back(note);
			}
		}
		if (on_repeats)
			on_repeats();
		return true;
	}
	Note* getElementAddress(int32_t i) { return getElement(i); }
	void testSequentiality(const char*) {}
	Note* getLast() { return getElement(getNumElements() - 1); }
	void empty() {
		entries.clear();
		values.clear();
	}
	void searchMultiple(int32_t* terms, int32_t count, int32_t = -1) {
		for (int i = 0; i < count; ++i)
			terms[i] = search(terms[i], 0);
	}
	inline static bool fail_clone = false;
	inline static std::function<void()> on_clone;
	bool cloneFrom(NoteVector* other) {
		if (fail_clone)
			return false;
		values = other->values;
		entries = other->entries;
		if (on_clone)
			on_clone();
		return true;
	}
	void swapStateWith(NoteVector* other) {
		values.swap(other->values);
		entries.swap(other->entries);
	}
};
struct ModControllableAudio {
	int required_param_manager_type() const { return 0; }
};
using ModControllable = ModControllableAudio;
class ModelStackWithParamCollection {};
struct ParamCollection {
	int clears = 0;
	std::function<void()> on_clear;
	void deleteAllAutomation(Action*, ModelStackWithParamCollection*) {
		++clears;
		auto callback = on_clear;
		if (callback)
			callback();
	}
};
struct ExpressionParamSet : ParamCollection {};
struct ParamCollectionSummary {
	ParamCollection* paramCollection = nullptr;
};
class ModelStackWithThreeMainThings {
public:
	ModelStackWithParamCollection collection;
	ModelStackWithParamCollection* addParamCollection(ParamCollection*, ParamCollectionSummary*) { return &collection; }
};
namespace ParamManagerType {
constexpr int ANY = 0;
}

struct ParamManager {
	int getExpressionParamSetOffset() { return expressionParamSetOffset; }
	bool matches_type(int) const { return valid; }
	void destructMainParamCollections() { main = 0; }
	int main = 0, expression = 0;
	bool valid = true;
	ParamCollectionSummary summaries[3];
	int expressionParamSetOffset = 1;
	std::function<void()> on_repeat;
	void generateRepeats(ModelStackWithThreeMainThings*, uint32_t, uint32_t, bool) {
		auto callback = on_repeat;
		if (callback)
			callback();
	}
	std::function<void()> on_trim;
	void trimToLength(int32_t, ModelStackWithThreeMainThings*, void*, bool = false) {
		auto callback = on_trim;
		if (callback)
			callback();
	}
	void stealParamCollectionsFrom(ParamManager* source, bool include_expression) {
		main = std::exchange(source->main, 0);
		if (include_expression)
			expression = std::exchange(source->expression, 0);
	}
};
using ParamManagerForTimeline = ParamManager;
struct Sound {
	bool hasCutModeSamples(ParamManager*) { return false; }
	bool hasAnyTimeStretchSyncing(ParamManager*) { return false; }
};
struct SoundDrum : ModControllableAudio, Sound {
	DrumType type = DrumType::SOUND;
};
class NoteRow {
public:
	uint32_t ignoreNoteOnsBefore_ = 0;
	bool generateRepeats(ModelStackWithNoteRow*, uint32_t, uint32_t, int32_t, Action*);
	SequenceDirection sequenceDirectionMode = SequenceDirection::FORWARD;
	SequenceDirection direction = SequenceDirection::FORWARD;
	SequenceDirection getEffectiveSequenceDirectionMode(ModelStackWithNoteRow*) { return direction; }
	void recordNoteOff(uint32_t, ModelStackWithNoteRow*, Action*, int32_t);
	void clear(Action*, ModelStackWithNoteRow*, bool, bool);
	int note_stops = 0;
	std::function<void()> on_note_stop;
	bool sequenced = false;
	void stopCurrentlyPlayingNote(ModelStackWithNoteRow*, bool = true, Note* = nullptr);
	void playNote(bool, ModelStackWithNoteRow*, Note*) {
		++note_stops;
		auto callback = on_note_stop;
		if (callback)
			callback();
	}
	Error addCorrespondingNotes(int32_t, int32_t, uint8_t, ModelStackWithNoteRow*, bool, Action*);
	Error clearArea(int32_t, int32_t, ModelStackWithNoteRow*, Action*, uint32_t, bool = false);
	Error editNoteRepeatAcrossAllScreens(int32_t, int32_t, ModelStackWithNoteRow*, Action*, uint32_t, int32_t);
	Error nudgeNotesAcrossAllScreens(int32_t, ModelStackWithNoteRow*, Action*, uint32_t, int32_t);
	Error changeNotesAcrossAllScreens(int32_t, ModelStackWithNoteRow*, Action*, int32_t, int32_t);
	Error trimToLength(uint32_t, ModelStackWithNoteRow*, Action*);
	Error trimNoteDataToNewClipLength(uint32_t, InstrumentClip*, Action*, int32_t);
	Error complexSetNoteLength(Note*, uint32_t, ModelStackWithNoteRow*, Action*);
	int32_t getDefaultProbability() { return 20; }
	Iterance getDefaultIterance() { return Iterance{}; }
	int32_t getDefaultFill(ModelStackWithNoteRow*) { return 0; }
	Error deleteNoteByIndex(int32_t, Action*, int32_t, InstrumentClip*);
	SoundDrum* drum = nullptr;
	ParamManager paramManager;
	int trims = 0;
	std::function<void()> on_trim;
	void trimParamManager(ModelStackWithNoteRow*) {
		++trims;
		if (on_trim)
			on_trim();
	}
	uint64_t undo_identity = deluge::model::next_note_row_identity();
	int32_t loopLengthIfIndependent = 0;
	NoteVector notes;
	int length_calls = 0, mute_calls = 0;
	bool muted = false;
	bool hasIndependentPlayPos() { return loopLengthIfIndependent != 0; }
	Error length_error = Error::NONE;
	std::function<void()> on_length;
	Error setLength(ModelStackWithNoteRow*, int32_t length, Action*, int32_t, bool) {
		++length_calls;
		if (length_error != Error::NONE)
			return length_error;
		loopLengthIfIndependent = length;
		auto callback = on_length;
		if (callback)
			callback();
		return Error::NONE;
	}
	void toggleMute(ModelStackWithNoteRow*, bool) {
		++mute_calls;
		muted = !muted;
	}
};
struct Output {
	OutputType type = OutputType::SYNTH;
	ModControllableAudio mod;
	ModControllableAudio* toModControllable() { return &mod; }
	Output* next = nullptr;
};
struct SoundInstrument : Output, Sound {};
struct Sample {
	uint64_t lengthInSamples = 1000;
};

class Clip {
public:
	int expected_events = 0;
	std::function<void()> on_expect_event;
	void expectEvent() {
		++expected_events;
		auto callback = on_expect_event;
		if (callback)
			callback();
	}
	virtual ~Clip() = default;
	std::function<Error()> on_reattach;
	virtual Error undoDetachmentFromOutput(ModelStackWithTimelineCounter*);
	ParamManager paramManager;
	virtual bool can_shift_horizontally(int32_t, bool) { return loopLength > 0; }
	Output* output = nullptr;
	ClipType type = ClipType::INSTRUMENT;
	int32_t loopLength = 96;
	bool shift_succeeds = true;
	int shift_calls = 0;
	int64_t total_shift = 0;
	bool last_automation = false, last_sequence = false;
	virtual bool shiftHorizontally(ModelStackWithTimelineCounter*, int32_t amount, bool automation, bool sequence) {
		++shift_calls;
		if (!shift_succeeds)
			return false;
		total_shift += amount;
		last_automation = automation;
		last_sequence = sequence;
		return true;
	}
};
class AudioClip : public Clip {
public:
	struct {
		bool reversed = false;
		bool isCurrentlyReversed() { return reversed; }
	} sampleControls;
	void* recorder = nullptr;
	int32_t originalLength = 96;
	bool linear = false;
	bool getCurrentlyRecordingLinearly() { return linear; }
	bool can_shift_horizontally(int32_t amount, bool sequence) override;
	AudioClip() { type = ClipType::AUDIO; }
	struct {
		uint64_t startPos = 10, endPos = 100;
		Sample* audioFile = nullptr;
	} sampleHolder;
};
class ModelStackWithModControllable {};
class InstrumentClip : public Clip {
public:
	struct {
		std::vector<NoteRow*> values;
		int32_t getNumElements() { return values.size(); }
		NoteRow* getElement(int32_t index) { return values.at(index); }
	} noteRows;
	Error undoUnassignmentOfAllNoteRowsFromDrums(ModelStackWithTimelineCounter*);
	Error undoDetachmentFromOutput(ModelStackWithTimelineCounter*);
	std::function<void()> on_midi_restore;
	void restoreBackedUpParamManagerMIDI(ModelStackWithModControllable*) {
		if (on_midi_restore)
			on_midi_restore();
	}
	SequenceDirection sequenceDirectionMode = SequenceDirection::FORWARD;
	bool allowNoteTails(ModelStackWithNoteRow*) { return true; }
	uint32_t wrapEditLevel = 16;
	bool wrapEditing = false;
	uint32_t getWrapEditLevel() { return wrapEditLevel; }
	bool wrap_editing_for_session() { return wrapEditing; }
	NoteRow* row = nullptr;
	int row_id = 7, row_lookups = 0, row_shift_calls = 0;
	int64_t row_shift = 0;
	NoteRow* getNoteRowFromId(int id) {
		++row_lookups;
		return id == row_id ? row : nullptr;
	}
	void shiftOnlyOneNoteRowHorizontally(ModelStackWithNoteRow*, int32_t amount, bool automation, bool sequence) {
		++row_shift_calls;
		row_shift += amount;
		last_automation = automation;
		last_sequence = sequence;
	}
};
struct BackedUpParamManager {
	ModControllableAudio* modControllable;
	Clip* clip;
	ParamManager paramManager;
};
class Song {
public:
	struct BackupArray {
		std::vector<BackedUpParamManager> values;
		int32_t getNumElements() const { return values.size(); }
		int32_t searchMultiWordExact(uint32_t* keys) {
			for (size_t i = 0; i < values.size(); ++i) {
				if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(values[i].modControllable)) == keys[0]
				    && static_cast<uint32_t>(reinterpret_cast<uintptr_t>(values[i].clip)) == keys[1])
					return i;
			}
			return -1;
		}
		void* getElementAddress(int32_t i) { return &values.at(i); }
		void deleteAtIndex(int32_t i) { values.erase(values.begin() + i); }
	} backedUpParamManagers;
	void deleteBackedUpParamManagersForClip(Clip* clip) {
		std::erase_if(backedUpParamManagers.values, [clip](const auto& backup) { return backup.clip == clip; });
	}
	ParamManager* getBackedUpParamManagerForExactClip(ModControllableAudio*, Clip*, ParamManager* = nullptr);
	struct ClipArray {
		Error commit_error = Error::NONE;
		int commit_calls = 0, pointer_writes = 0;
		Error insert_at_index_without_allocation(int32_t index) {
			++commit_calls;
			if (commit_error != Error::NONE)
				return commit_error;
			values.insert(values.begin() + index, nullptr);
			return Error::NONE;
		}
		void setPointerAtIndex(void* clip, int32_t index) {
			++pointer_writes;
			values.at(index) = static_cast<Clip*>(clip);
		}
		std::function<void()> on_reserve;
		bool reserve_succeeds = true;
		int reserve_calls = 0;
		bool ensureEnoughSpaceAllocated(int32_t) {
			++reserve_calls;
			if (on_reserve)
				on_reserve();
			return reserve_succeeds;
		}
		std::vector<Clip*> values;
		int32_t getNumElements() { return values.size(); }
		Clip* getClipAtIndex(int32_t index) { return values.at(index); }
	} sessionClips, arrangementOnlyClips;
	std::vector<Clip*>& registered = sessionClips.values;
	Output* firstOutput = nullptr;
	RetainedList<Output> undo_detached_outputs;
	Clip* selected = nullptr;
	std::function<Error(Action*)> on_clear_arrangement;
	Error clearArrangementBeyondPos(int32_t, Action* action) {
		return on_clear_arrangement ? on_clear_arrangement(action) : Error::NONE;
	}
	int length_calls = 0;
	std::function<void()> on_set_clip_length;
	uint64_t marker_observed_at_length_change = 0;
	bool contains_clip_for_undo(const Clip* clip);
	int32_t get_clip_index_for_undo(ClipArray* array, const Clip* clip);
	bool owns_output_for_undo(const Output* output, bool include_detached = true) const;
	bool can_reference_clip_from_output(const Clip* clip, const Output* output);
	Clip* getCurrentClip() { return selected; }
	bool isClipActive(Clip*) { return true; }
	bool length_change_succeeds = true;
	bool setClipLength(Clip* clip, int32_t length, Action*) {
		++length_calls;
		clip->loopLength = length;
		if (clip->type == ClipType::AUDIO)
			marker_observed_at_length_change = static_cast<AudioClip*>(clip)->sampleHolder.startPos;
		if (on_set_clip_length)
			on_set_clip_length();
		return length_change_succeeds;
	}
};
class ModelStackWithNoteRow {
public:
	Song* song = nullptr;
	Clip* clip = nullptr;
	NoteRow* row = nullptr;
	NoteRow* getNoteRow() { return row; }
	NoteRow* getNoteRowAllowNull() { return row; }
	Clip* getTimelineCounter() { return clip; }
	Clip* getTimelineCounterAllowNull() { return clip; }
	int32_t getLoopLength() { return row->loopLengthIfIndependent ? row->loopLengthIfIndependent : clip->loopLength; }
	ModelStackWithThreeMainThings main_stack;
	ModelStackWithThreeMainThings* addOtherTwoThingsAutomaticallyGivenNoteRow() { return &main_stack; }
	bool reversed = false;
	bool isCurrentlyPlayingReversed() { return reversed; }
	int32_t noteRowId = 7;
	int32_t getLastProcessedPos() { return 0; }
};
class ModelStackWithTimelineCounter {
public:
	Song* song = nullptr;
	Clip* clip = nullptr;
	ModelStackWithNoteRow row_stack;
	ModelStackWithThreeMainThings main_stack;
	ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(ModControllable*, ParamManager*) {
		return &main_stack;
	}
	ModelStackWithModControllable mod_stack;
	ModelStackWithModControllable* addModControllableButNoNoteRow(ModControllableAudio*) { return &mod_stack; }
	Clip* getTimelineCounter() { return clip; }
	Clip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithNoteRow* addNoteRow(int, NoteRow* row) {
		row_stack = {song, clip, row};
		return &row_stack;
	}
};
class ModelStack {
public:
	Song* song = nullptr;
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip* clip) {
		timeline.song = song;
		timeline.clip = clip;
		return &timeline;
	}
};
struct PlaybackHandler {
	bool isEitherClockActive() { return false; }
};
inline PlaybackHandler playbackHandler;

inline Song* currentSong = nullptr;
