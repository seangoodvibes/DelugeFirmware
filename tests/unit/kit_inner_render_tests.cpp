#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
namespace deluge::modulation::params {
constexpr int kNumParams = 90;
}
namespace kit_inner_render_test {
constexpr bool ALPHA_OR_BETA_VERSION = false;
constexpr int kMaxNumPatchCables = 32, kNumExpressionDimensions = 3;
namespace params {
constexpr int UNPATCHED_SOUND_MAX_NUM = 32;
}
struct StereoSample {};
enum class DrumType { SOUND };
struct Drum {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	DrumType type = DrumType::SOUND;
};
struct ModelStackWithThreeMainThings {};
struct ParamCollectionSummary {
	int whichParamsAreInterpolating[3] = {};
};
std::function<void()> on_render, on_kill, on_tick;
int tick_calls = 0, render_calls = 0, row_index = -1;
struct ParamManager {
	ParamCollectionSummary summaries[4];
	void tickSamples(size_t, ModelStackWithThreeMainThings*, const deluge::lifetime::callback_validation* = nullptr) {
		++tick_calls;
		if (on_tick)
			on_tick();
	}
} backup;
struct NoteRow {
	uint64_t undo_identity = 1;
	Drum* drum = nullptr;
	ParamManager paramManager;
};
struct NoteRowVector {
	std::vector<NoteRow*> rows;
	int getNumElements() { return rows.size(); }
	NoteRow* getElement(int index) { return rows.at(index); }
};
struct Kit;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* output = nullptr;
	NoteRowVector noteRows;
	NoteRow* find_note_row_from_id(int index) {
		return index >= 0 && index < noteRows.getNumElements() ? noteRows.getElement(index) : nullptr;
	}
	NoteRow* getNoteRowForDrum(Drum* drum, int* index) {
		for (int i = 0; i < noteRows.getNumElements(); ++i) {
			if (noteRows.getElement(i)->drum == drum) {
				*index = i;
				return noteRows.getElement(i);
			}
		}
		return nullptr;
	}
};
struct SoundDrum : Drum {
	bool skippingRendering = false;
	void killAllVoices() {
		if (on_kill)
			on_kill();
	}
	template <class... Args>
	void render(Args&&...) {
		++render_calls;
		if (on_render)
			on_render();
	}
};
struct Song {
	ParamManager* backup_manager = &backup;
	ParamManager* getBackedUpParamManagerPreferablyWithClip(SoundDrum*, void*) { return backup_manager; }
} song;
Song* currentSong = &song;
struct ModelStackWithTimelineCounter {
	Song* song = &kit_inner_render_test::song;
	InstrumentClip* clip = nullptr;
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithThreeMainThings main;
	ModelStackWithTimelineCounter* addNoteRow(int index, NoteRow*) {
		row_index = index;
		return this;
	}
	ModelStackWithThreeMainThings* addOtherTwoThings(SoundDrum*, ParamManager*) { return &main; }
};
struct {
	bool isEitherClockActive() { return true; }
	int ticksLeftInCountIn = 0;
} playbackHandler;
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	InstrumentClip* activeClip = nullptr;
	struct {
		std::vector<Drum*> drums;
		int getNumElements() { return drums.size(); }
		uintptr_t getKeyAtIndex(int index) { return reinterpret_cast<uintptr_t>(drums.at(index)); }
	} drumsWithRenderingActive;
	bool renderGlobalEffectableForClip(ModelStackWithTimelineCounter*, std::span<StereoSample>, int32_t*, int32_t*,
	                                   int32_t, int32_t, bool, bool, int32_t, int32_t, int32_t);
};
void FREEZE_WITH_ERROR(const char*) {
}
#include "kit_inner_render.inc"
} // namespace kit_inner_render_test
using namespace kit_inner_render_test;
TEST_GROUP(kit_inner_render){void setup() override{tick_calls = render_calls = 0;
row_index = -1;
on_render = {};
on_kill = {};
on_tick = {};
currentSong = &song;
song.backup_manager = &backup;
}
void teardown() override {
	on_render = {};
	on_kill = {};
	on_tick = {};
}
}
;
TEST(kit_inner_render, backup_render_has_defined_row_index_and_does_not_tick_missing_clip) {
	Kit kit;
	SoundDrum drum;
	kit.drumsWithRenderingActive.drums.push_back(&drum);
	ModelStackWithTimelineCounter stack;
	StereoSample samples[1];
	CHECK(kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0));
	LONGS_EQUAL(1, render_calls);
	LONGS_EQUAL(0, row_index);
	LONGS_EQUAL(0, tick_calls);
}
TEST(kit_inner_render, active_clip_still_ticks_interpolating_row) {
	Kit kit;
	SoundDrum drum;
	NoteRow row;
	row.drum = &drum;
	row.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	InstrumentClip clip;
	clip.noteRows.rows.push_back(&row);
	kit.activeClip = &clip;
	clip.output = &kit;
	kit.drumsWithRenderingActive.drums.push_back(&drum);
	ModelStackWithTimelineCounter stack;
	stack.clip = &clip;
	StereoSample samples[1];
	CHECK(kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0));
	LONGS_EQUAL(1, render_calls);
	LONGS_EQUAL(1, tick_calls);
}

TEST(kit_inner_render, owner_destruction_at_each_callback_stops_traversal) {
	for (int stage = 0; stage < 3; ++stage) {
		auto kit = std::make_unique<Kit>();
		auto clip = std::make_unique<InstrumentClip>();
		SoundDrum drum;
		NoteRow row;
		row.drum = &drum;
		row.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
		kit->activeClip = clip.get();
		clip->output = kit.get();
		if (stage != 1)
			clip->noteRows.rows.push_back(&row);
		if (stage != 2)
			kit->drumsWithRenderingActive.drums.push_back(&drum);
		ModelStackWithTimelineCounter stack;
		stack.clip = clip.get();
		auto destroy = [&] {
			clip.reset();
			kit.reset();
		};
		on_render = stage == 0 ? std::function<void()>(destroy) : std::function<void()>();
		on_kill = stage == 1 ? std::function<void()>(destroy) : std::function<void()>();
		on_tick = stage == 2 ? std::function<void()>(destroy) : std::function<void()>();
		StereoSample samples[1];
		kit->renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0);
		CHECK(!kit);
		CHECK(!clip);
		on_render = {};
		on_kill = {};
		on_tick = {};
	}
}
TEST(kit_inner_render, retarget_during_render_does_not_tick_replacement_clip) {
	Kit kit;
	InstrumentClip clip, replacement;
	SoundDrum drum;
	NoteRow row;
	row.drum = &drum;
	row.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	clip.output = replacement.output = &kit;
	clip.noteRows.rows.push_back(&row);
	replacement.noteRows.rows.push_back(&row);
	kit.activeClip = &clip;
	kit.drumsWithRenderingActive.drums.push_back(&drum);
	ModelStackWithTimelineCounter stack;
	stack.clip = &clip;
	on_render = [&] { kit.activeClip = &replacement; };
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0);
	LONGS_EQUAL(0, tick_calls);
}

TEST(kit_inner_render, finished_drum_can_remove_itself_and_rendering_continues) {
	Kit kit;
	SoundDrum first, last;
	kit.drumsWithRenderingActive.drums = {&first, &last};
	on_render = [&] { kit.drumsWithRenderingActive.drums.pop_back(); };
	ModelStackWithTimelineCounter stack;
	StereoSample samples[1];
	CHECK(kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, false, 0, 0, 0));
	LONGS_EQUAL(2, render_calls);
}
TEST(kit_inner_render, destruction_of_current_or_next_drum_cancels_rendering) {
	for (bool delete_current : {false, true}) {
		Kit kit;
		auto first = std::make_unique<SoundDrum>();
		auto last = std::make_unique<SoundDrum>();
		kit.drumsWithRenderingActive.drums = {first.get(), last.get()};
		render_calls = 0;
		on_render = [&] {
			if (delete_current) {
				kit.drumsWithRenderingActive.drums.pop_back();
				last.reset();
			}
			else {
				kit.drumsWithRenderingActive.drums.erase(kit.drumsWithRenderingActive.drums.begin());
				first.reset();
			}
		};
		ModelStackWithTimelineCounter stack;
		StereoSample samples[1];
		kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, false, 0, 0, 0);
		LONGS_EQUAL(1, render_calls);
		on_render = {};
	}
}
TEST(kit_inner_render, row_removal_during_parameter_tick_cancels_iteration) {
	Kit kit;
	InstrumentClip clip;
	SoundDrum drum;
	auto row = std::make_unique<NoteRow>();
	NoteRow next;
	row->drum = next.drum = &drum;
	row->paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	next.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	clip.noteRows.rows = {row.get(), &next};
	kit.activeClip = &clip;
	clip.output = &kit;
	on_tick = [&] {
		clip.noteRows.rows.erase(clip.noteRows.rows.begin());
		row.reset();
	};
	ModelStackWithTimelineCounter stack;
	stack.clip = &clip;
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0);
	LONGS_EQUAL(1, tick_calls);
}
TEST(kit_inner_render, replacing_next_render_entry_cancels_without_rendering_replacement) {
	Kit kit;
	SoundDrum first, last, replacement;
	kit.drumsWithRenderingActive.drums = {&first, &last};
	on_render = [&] { kit.drumsWithRenderingActive.drums[0] = &replacement; };
	ModelStackWithTimelineCounter stack;
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, false, 0, 0, 0);
	LONGS_EQUAL(1, render_calls);
}
TEST(kit_inner_render, same_address_row_identity_change_stops_parameter_iteration) {
	Kit kit;
	InstrumentClip clip;
	SoundDrum drum;
	NoteRow row, next;
	row.drum = next.drum = &drum;
	row.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	next.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	clip.noteRows.rows = {&row, &next};
	kit.activeClip = &clip;
	clip.output = &kit;
	on_tick = [&] { ++row.undo_identity; };
	ModelStackWithTimelineCounter stack;
	stack.clip = &clip;
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0);
	LONGS_EQUAL(1, tick_calls);
}

TEST(kit_inner_render, row_deletion_during_sound_render_stops_before_next_drum) {
	Kit kit;
	InstrumentClip clip;
	SoundDrum first, last;
	NoteRow first_row;
	auto last_row = std::make_unique<NoteRow>();
	first_row.drum = &first;
	last_row->drum = &last;
	clip.noteRows.rows = {&first_row, last_row.get()};
	clip.output = &kit;
	kit.activeClip = &clip;
	kit.drumsWithRenderingActive.drums = {&first, &last};
	on_render = [&] {
		clip.noteRows.rows.pop_back();
		last_row.reset();
	};
	ModelStackWithTimelineCounter stack;
	stack.clip = &clip;
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0);
	LONGS_EQUAL(1, render_calls);
}
TEST(kit_inner_render, missing_backup_manager_does_not_render) {
	Kit kit;
	SoundDrum drum;
	kit.drumsWithRenderingActive.drums = {&drum};
	song.backup_manager = nullptr;
	ModelStackWithTimelineCounter stack;
	StereoSample samples[1];
	CHECK_FALSE(kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, false, 0, 0, 0));
	LONGS_EQUAL(0, render_calls);
}
TEST(kit_inner_render, backup_manager_destruction_cancels_remaining_drums) {
	Kit kit;
	SoundDrum first, last;
	kit.drumsWithRenderingActive.drums = {&first, &last};
	auto manager = std::make_unique<ParamManager>();
	song.backup_manager = manager.get();
	on_render = [&] {
		song.backup_manager = nullptr;
		manager.reset();
	};
	ModelStackWithTimelineCounter stack;
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, false, 0, 0, 0);
	LONGS_EQUAL(1, render_calls);
}
TEST(kit_inner_render, row_drum_reassignment_stops_parameter_ticks) {
	Kit kit;
	InstrumentClip clip;
	SoundDrum first, replacement;
	NoteRow row, next;
	row.drum = next.drum = &first;
	row.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	next.paramManager.summaries[1].whichParamsAreInterpolating[0] = 1;
	clip.noteRows.rows = {&row, &next};
	kit.activeClip = &clip;
	clip.output = &kit;
	on_tick = [&] { row.drum = &replacement; };
	ModelStackWithTimelineCounter stack;
	stack.clip = &clip;
	StereoSample samples[1];
	kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0);
	LONGS_EQUAL(1, tick_calls);
}
