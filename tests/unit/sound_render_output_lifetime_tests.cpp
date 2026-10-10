#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
namespace sound_render_output_lifetime_test {
constexpr int PARAM_COLLECTIONS_STORAGE_NUM = 5, kMaxNumPatchCables = 64, kNumExpressionDimensions = 3;
constexpr int kMaxSampleValue = INT32_MAX;
namespace params {
constexpr int kNumParams = 96, UNPATCHED_SOUND_MAX_NUM = 64;
}
struct StereoSample {};
struct ParamCollection {};
struct ParamCollectionSummary {
	ParamCollection* paramCollection = nullptr;
	int whichParamsAreInterpolating[3]{};
};
struct ModelStack;
using ModelStackWithThreeMainThings = ModelStack;
std::function<void()> on_render, on_tick, on_note;
int note_starts = 0, note_stops = 0;
bool note_context_valid = true;
int renders = 0, ticks = 0, tick_completions = 0;
struct ParamManager {
	ParamCollectionSummary summaries[5];
	ParamManager* toForTimeline() { return this; }
	void tickSamples(int, ModelStack*, const deluge::lifetime::callback_validation* validation) {
		CHECK(validation);
		CHECK(validation->valid());
		++ticks;
		if (on_tick)
			on_tick();
		if (validation->valid())
			++tick_completions;
	}
};
struct NoteRow {
	uint64_t undo_identity = 1;
	int y = 60;
	ParamManager paramManager;
};
struct SoundInstrument;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	SoundInstrument* output = nullptr;
	ParamManager paramManager;
	struct {
		std::vector<NoteRow*> entries;
		int getNumElements() const { return entries.size(); }
		NoteRow* getElement(int index) { return entries.at(index); }
	} noteRows;
	NoteRow* find_note_row_from_id(int id) {
		for (auto* row : noteRows.entries)
			if (row->y == id)
				return row;
		return nullptr;
	}
};
int song;
int* currentSong = &song;
struct ModelStack {
	int* song = currentSong;
	InstrumentClip* clip = nullptr;
	ParamManager* paramManager = nullptr;
	ModelStack* addTimelineCounter(InstrumentClip* value) {
		clip = value;
		return this;
	}
	ModelStack* addOtherTwoThingsButNoNoteRow(SoundInstrument*, ParamManager* value) {
		paramManager = value;
		return this;
	}
	InstrumentClip* getTimelineCounter() { return clip; }
	NoteRow* row = nullptr;
	int noteRowId = 0;
	NoteRow* getNoteRowAllowNull() { return row; }
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
	void setNoteRow(NoteRow* value, int id) {
		row = value;
		noteRowId = id;
	}
};
struct {
	bool clock = true;
	int ticksLeftInCountIn = 0;
	bool isEitherClockActive() { return clock; }
} playbackHandler;
struct Sound {
	void noteOn(ModelStack*, void*, int, const int16_t*, uint32_t, int32_t, uint32_t, int, int,
	            const deluge::lifetime::callback_validation* validation) {
		CHECK(validation);
		CHECK(validation->valid());
		++note_starts;
		if (on_note)
			on_note();
		note_context_valid = validation->valid();
	}
	void noteOff(ModelStack*, void*, int, const deluge::lifetime::callback_validation* validation) {
		CHECK(validation);
		CHECK(validation->valid());
		++note_stops;
		if (on_note)
			on_note();
		note_context_valid = validation->valid();
	}
	void render(ModelStack*, std::span<StereoSample>, int32_t*, int32_t, int32_t, bool, int32_t, void*,
	            const deluge::lifetime::callback_validation* validation) {
		CHECK(validation);
		CHECK(validation->valid());
		++renders;
		if (on_render)
			on_render();
	}
};
struct SoundInstrument : Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	InstrumentClip* activeClip = nullptr;
	void* recorder = nullptr;
	bool skippingRendering = false, inValidState = true;
	int arpeggiator = 0;
	void sendNote(ModelStack*, bool, int32_t, const int16_t*, int32_t, uint8_t, uint32_t, int32_t, uint32_t);
	struct {
		int gainReduction = 1;
		void reset() {}
	} compressor;
	void renderOutput(ModelStack*, std::span<StereoSample>, int32_t*, int32_t, int32_t, bool, bool);
};
#include "sound_render_output_lifetime.inc"
} // namespace sound_render_output_lifetime_test
using namespace sound_render_output_lifetime_test;
TEST_GROUP(sound_render_output_lifetime) {
	std::unique_ptr<SoundInstrument> instrument;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<NoteRow> first, second;
	ModelStack stack;
	void reset() {
		instrument = std::make_unique<SoundInstrument>();
		clip = std::make_unique<InstrumentClip>();
		first = std::make_unique<NoteRow>();
		second = std::make_unique<NoteRow>();
		instrument->activeClip = clip.get();
		clip->output = instrument.get();
		clip->noteRows.entries = {first.get(), second.get()};
		clip->paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
		first->paramManager.summaries[0].whichParamsAreInterpolating[0] = 1;
		second->paramManager.summaries[0].whichParamsAreInterpolating[0] = 1;
		on_render = on_tick = on_note = {};
		renders = ticks = tick_completions = note_starts = note_stops = 0;
		note_context_valid = true;
		currentSong = &song;
		stack = {};
		stack.clip = clip.get();
		stack.paramManager = &clip->paramManager;
		playbackHandler.clock = true;
		playbackHandler.ticksLeftInCountIn = 0;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_render = on_tick = on_note = {};
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void render(bool active = true) {
		instrument->renderOutput(&stack, {}, nullptr, 0, 0, false, active);
	}
};
TEST(sound_render_output_lifetime, live_render_ticks_clip_and_both_rows) {
	render();
	LONGS_EQUAL(1, renders);
	LONGS_EQUAL(3, ticks);
	LONGS_EQUAL(3, tick_completions);
}
TEST(sound_render_output_lifetime, skipped_render_still_ticks_active_parameters) {
	instrument->skippingRendering = true;
	render();
	LONGS_EQUAL(0, renders);
	LONGS_EQUAL(0, instrument->compressor.gainReduction);
	LONGS_EQUAL(3, ticks);
}
TEST(sound_render_output_lifetime, render_deletion_stops_parameter_access) {
	on_render = [&] {
		instrument.reset();
		clip.reset();
		first.reset();
		second.reset();
	};
	render();
	LONGS_EQUAL(0, ticks);
}
TEST(sound_render_output_lifetime, every_tick_deletion_cancels_remaining_traversal) {
	for (int boundary = 1; boundary <= 3; ++boundary) {
		reset();
		on_tick = [&] {
			if (ticks == boundary) {
				instrument.reset();
				clip.reset();
				first.reset();
				second.reset();
			}
		};
		render();
		LONGS_EQUAL(boundary, ticks);
		LONGS_EQUAL(boundary - 1, tick_completions);
	}
}
TEST(sound_render_output_lifetime, render_retargeting_stops_ticks) {
	ParamCollection replacement;
	for (int mutation = 0; mutation < 8; ++mutation) {
		reset();
		on_render = [&] {
			switch (mutation) {
			case 0:
				instrument->activeClip = nullptr;
				break;
			case 1:
				clip->output = nullptr;
				break;
			case 2:
				currentSong = nullptr;
				break;
			case 3:
				stack.clip = nullptr;
				break;
			case 4:
				stack.paramManager = nullptr;
				break;
			case 5:
				instrument->recorder = &replacement;
				break;
			case 6:
				clip->paramManager.summaries[1].paramCollection = &replacement;
				break;
			case 7:
				deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote;
				break;
			}
		};
		render();
		LONGS_EQUAL(0, ticks);
	}
}
TEST(sound_render_output_lifetime, row_removal_or_replacement_cancels_before_advancing) {
	for (int mutation = 0; mutation < 3; ++mutation) {
		reset();
		on_tick = [&] {
			if (ticks != 2)
				return;
			if (mutation == 0) {
				clip->noteRows.entries.clear();
				first.reset();
			}
			if (mutation == 1) {
				clip->noteRows.entries[0] = second.get();
				first.reset();
			}
			if (mutation == 2)
				++first->undo_identity;
		};
		render();
		LONGS_EQUAL(2, ticks);
		LONGS_EQUAL(1, tick_completions);
	}
}
TEST(sound_render_output_lifetime, inactive_clock_countin_or_clip_suppresses_ticks) {
	for (int mode = 0; mode < 3; ++mode) {
		reset();
		if (mode == 0)
			playbackHandler.clock = false;
		if (mode == 1)
			playbackHandler.ticksLeftInCountIn = 1;
		render(mode != 2);
		LONGS_EQUAL(1, renders);
		LONGS_EQUAL(0, ticks);
	}
}
TEST(sound_render_output_lifetime, every_collection_interpolation_flag_can_start_clip_tick) {
	for (int collection = 0; collection < 4; ++collection) {
		reset();
		clip->paramManager.summaries[1].whichParamsAreInterpolating[0] = 0;
		clip->paramManager.summaries[collection].whichParamsAreInterpolating[0] = 1;
		render();
		LONGS_EQUAL(3, ticks);
	}
}
TEST(sound_render_output_lifetime, invalid_entry_does_not_render) {
	stack.song = nullptr;
	render();
	stack.song = currentSong;
	clip->output = nullptr;
	render();
	clip->output = instrument.get();
	clip->lifetime.retire();
	render();
	instrument->activeClip = nullptr;
	render();
	instrument->lifetime.retire();
	render();
	LONGS_EQUAL(0, renders);
}

TEST(sound_render_output_lifetime, direct_note_sender_dispatches_on_and_off) {
	instrument->sendNote(&stack, true, 60, nullptr, 2, 99, 0, 0, 0);
	instrument->sendNote(&stack, false, 60, nullptr, 2, 99, 0, 0, 0);
	LONGS_EQUAL(1, note_starts);
	LONGS_EQUAL(1, note_stops);
	CHECK(note_context_valid);
}
TEST(sound_render_output_lifetime, direct_note_validator_detects_owner_deletion) {
	on_note = [&] {
		instrument.reset();
		clip.reset();
	};
	instrument->sendNote(&stack, true, 60, nullptr, 2, 99, 0, 0, 0);
	LONGS_EQUAL(1, note_starts);
	CHECK_FALSE(note_context_valid);
}
TEST(sound_render_output_lifetime, direct_note_validator_detects_removed_row_and_stack_retargeting) {
	for (int mutation = 0; mutation < 3; ++mutation) {
		reset();
		stack.row = first.get();
		stack.noteRowId = first->y;
		on_note = [&] {
			if (mutation == 0) {
				clip->noteRows.entries.clear();
				first.reset();
			}
			if (mutation == 1)
				++first->undo_identity;
			if (mutation == 2)
				stack.paramManager = nullptr;
		};
		instrument->sendNote(&stack, true, 60, nullptr, 2, 99, 0, 0, 0);
		CHECK_FALSE(note_context_valid);
	}
}
TEST(sound_render_output_lifetime, direct_note_sender_rejects_invalid_state_and_retired_owners) {
	instrument->inValidState = false;
	instrument->sendNote(&stack, true, 60, nullptr, 2, 99, 0, 0, 0);
	instrument->inValidState = true;
	clip->lifetime.retire();
	instrument->sendNote(&stack, true, 60, nullptr, 2, 99, 0, 0, 0);
	LONGS_EQUAL(0, note_starts);
}
TEST(sound_render_output_lifetime, direct_note_sender_preserves_clipless_path) {
	instrument->activeClip = nullptr;
	stack.clip = nullptr;
	instrument->sendNote(&stack, true, 60, nullptr, 2, 99, 0, 0, 0);
	LONGS_EQUAL(1, note_starts);
	CHECK(note_context_valid);
}

TEST(sound_render_output_lifetime, direct_note_off_sender_forwards_lifetime_validation) {
	on_note = [&] {
		instrument.reset();
		clip.reset();
	};
	instrument->sendNote(&stack, false, 60, nullptr, 2, 99, 0, 0, 0);
	LONGS_EQUAL(1, note_stops);
	CHECK_FALSE(note_context_valid);
}
