#include "CppUTest/TestHarness.h"
#include <cstdint>
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
	DrumType type = DrumType::SOUND;
};
struct ModelStackWithThreeMainThings {};
struct ParamCollectionSummary {
	int whichParamsAreInterpolating[3] = {};
};
int tick_calls = 0, render_calls = 0, row_index = -1;
struct ParamManager {
	ParamCollectionSummary summaries[4];
	void tickSamples(size_t, ModelStackWithThreeMainThings*) { ++tick_calls; }
} backup;
struct NoteRow {
	Drum* drum = nullptr;
	ParamManager paramManager;
};
struct NoteRowVector {
	std::vector<NoteRow*> rows;
	int getNumElements() { return rows.size(); }
	NoteRow* getElement(int index) { return rows.at(index); }
};
struct InstrumentClip {
	NoteRowVector noteRows;
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
	void killAllVoices() {}
	template <class... Args>
	void render(Args&&...) {
		++render_calls;
	}
};
struct Song {
	ParamManager* getBackedUpParamManagerPreferablyWithClip(SoundDrum*, void*) { return &backup; }
} song;
struct ModelStackWithTimelineCounter {
	Song* song = &kit_inner_render_test::song;
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
	kit.drumsWithRenderingActive.drums.push_back(&drum);
	ModelStackWithTimelineCounter stack;
	StereoSample samples[1];
	CHECK(kit.renderGlobalEffectableForClip(&stack, samples, nullptr, nullptr, 0, 0, false, true, 0, 0, 0));
	LONGS_EQUAL(1, render_calls);
	LONGS_EQUAL(1, tick_calls);
}
