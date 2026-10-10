#include "CppUTest/TestHarness.h"
#include "util/finally.h"
#include "util/lifetime.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#define ALPHA_OR_BETA_VERSION 1
#define D_PRINT(...)
#define D_PRINTLN(...)
namespace kit_sample_import_test {
enum class Error { NONE, INSUFFICIENT_RAM };
enum class OscType { SAMPLE, OTHER };
enum class SampleRepeatMode { ONCE, CUT, LOOP };
constexpr int32_t MIDI_NOTE_ERROR = -1;
int transpose_calls = 0, audio_publications = 0;
bool reserve_success = true;
void uartPrintlnFloat(float) {
}
namespace deluge {
namespace lifetime = ::deluge::lifetime;
namespace l10n {
enum class String { STRING_FOR_ERROR_CREATING_MULTISAMPLED_INSTRUMENT };
String get(String value) {
	return value;
}
} // namespace l10n
} // namespace deluge
int screen;
int* getCurrentUI() {
	return &screen;
}
constexpr int MODEL_STACK_MAX_SIZE = 128;
int range_calls = 0, fail_range = 0, allocation_calls = 0, fail_allocation = 0;
int parameter_calls = 0, fail_parameter = 0, assignment_calls = 0, fail_assignment = 0;
int sidechains = 0, row_assignments = 0, edits = 0, exits = 0, redraws = 0, table_frees = 0;
void* sample_table = nullptr;
struct String {
	std::string value = "sample.wav";
	Error set(const String* other) {
		value = other->value;
		return Error::NONE;
	}
	Error set(const char* text) {
		value = text;
		return Error::NONE;
	}
	const char* get() const { return value.c_str(); }
	void shorten(int32_t count) { value.resize(count); }
};
struct Sample {
	String filePath;
	int32_t numReasonsToBeLoaded = 1;
	float midiNote = 60;
	int32_t fileLoopEndSamples = 0;
	void removeReason(const char*) { --numReasonsToBeLoaded; }
	int32_t getLengthInMSec() const { return 100; }
};
std::vector<Sample*> loaded_samples;
struct AudioFileHolder {
	String filePath;
	Sample* sample = nullptr;
	int32_t loopEndPos = 0;
	void setTransposeAccordingToSamplePitch(bool, bool, bool, int32_t) { ++transpose_calls; }
	~AudioFileHolder() {
		if (sample)
			sample->removeReason("fixture");
	}
	bool setAudioFile(Sample* replacement, bool = false, bool = false) {
		if (++assignment_calls == fail_assignment)
			return false;
		if (sample)
			sample->removeReason("fixture");
		sample = replacement;
		if (sample)
			++sample->numReasonsToBeLoaded;
		return true;
	}
};
struct MultiRange {
	int32_t topNote = 32767;
	AudioFileHolder sampleHolder;
	AudioFileHolder* getAudioFileHolder() { return &sampleHolder; }
};
using MultisampleRange = MultiRange;
struct Source {
	struct RangeArray {
		MultiRange entries[4];
		int32_t count = 1;
		int32_t getNumElements() { return count; }
		int32_t element_size() { return sizeof(MultisampleRange); }
		bool ensureEnoughSpaceAllocated(int32_t) { return reserve_success; }
		MultiRange* getElement(int32_t index) { return &entries[index]; }
		MultiRange* insertMultiRange(int32_t index) {
			++count;
			return &entries[index];
		}
	} ranges;
	MultiRange& range = ranges.entries[0];
	OscType oscType = OscType::SAMPLE;
	SampleRepeatMode repeatMode = SampleRepeatMode::CUT;
	struct {
		bool isCurrentlyReversed() const { return false; }
	} sampleControls;
	MultiRange* getOrCreateFirstRange() { return ++range_calls == fail_range ? nullptr : &range; }
	void setOscType(OscType type) { oscType = type; }
};
struct ParamSet {
	bool isAutomated(int32_t) { return false; }
	void set_current_value(void*, int32_t, int32_t) {}
};
struct ParamCollectionSummary {
	void* paramCollection;
};
struct ParamManagerForTimeline {
	ParamSet parameters;
	ParamCollectionSummary summary{&parameters};
	Error setupWithPatching() { return ++parameter_calls == fail_parameter ? Error::INSUFFICIENT_RAM : Error::NONE; }
	ParamCollectionSummary* getPatchedParamSetSummary() { return &summary; }
};
struct Sound {
	Source sources[1];
	void killAllVoices() {}
	bool try_delete_multi_range(int32_t index, int32_t, const deluge::lifetime::callback_validation* validation) {
		if (!validation->valid())
			return false;
		--sources[index].ranges.count;
		return true;
	}
	static void initParams(ParamManagerForTimeline*) {}
};
struct SoundDrum : Sound {
	bool nameIsDiscardable = false;
	int noteRowAssignedTemp = 0;
	std::string drumName;
	void setupAsSample(ParamManagerForTimeline*) {}
};
struct Kit {
	std::vector<SoundDrum*> drums;
	void addDrum(SoundDrum* drum) { drums.push_back(drum); }
	bool getDrumFromName(const char*) { return false; }
	Error makeDrumNameUnique(String*, int) { return Error::NONE; }
	void resetDrumTempValues() {}
};
Kit* active_kit = nullptr;
struct Song {
	void backUpParamManager(SoundDrum*, void*, ParamManagerForTimeline*, bool) {}
} song;
auto* currentSong = &song;
struct ModelStackWithTimelineCounter {};
struct ModelStackWithThreeMainThings {
	ParamManagerForTimeline* paramManager;
	void* addParamCollection(ParamSet*, ParamCollectionSummary*) { return nullptr; }
};
struct Editor {
	Sound* currentSound = nullptr;
	Source* currentSource = nullptr;
	int currentSourceIndex = 0;
	MultiRange* currentMultiRange = nullptr;
	void setCurrentMultiRange(int32_t index) { currentMultiRange = currentSource->ranges.getElement(index); }
	ParamManagerForTimeline manager;
	ModelStackWithThreeMainThings stack{&manager};
	ModelStackWithThreeMainThings* getCurrentModelStack(char*) { return &stack; }
} editor;
Editor& sound_editor_for_session() {
	return editor;
}
namespace params {
constexpr int LOCAL_OSC_A_VOLUME = 0;
}
namespace AudioEngine {
bool audioRoutineLocked = false;
void routineWithClusterLoading() {
}
void stopAnyPreviewing() {
}
} // namespace AudioEngine
struct Display {
	int errors = 0, removals = 0;
	void displayLoadingAnimationText(const char*) {}
	void removeWorkingAnimation() { ++removals; }
	void displayError(Error) { ++errors; }
	void displayPopup(deluge::l10n::String) { ++errors; }
} display_instance;
auto* display = &display_instance;
struct {
	void close() {}
} staticDIR;
struct GeneralMemoryAllocator {
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t size) { return ++allocation_calls == fail_allocation ? nullptr : std::malloc(size); }
};
void delugeDealloc(void* memory) {
	if (memory == sample_table) {
		++table_frees;
		sample_table = nullptr;
	}
	std::free(memory);
}
void FREEZE_WITH_ERROR(const char*) {
	FAIL("invalid sample ownership");
}
struct Clip {
	void assignDrumsToNoteRows(ModelStackWithTimelineCounter*) { ++row_assignments; }
} clip;
struct Instrument {
	void beenEdited() { ++edits; }
} instrument;
Clip* getCurrentInstrumentClip() {
	return &clip;
}
Instrument* getCurrentInstrument() {
	return &instrument;
}
void* getCurrentClip() {
	return &clip;
}
Kit* getCurrentKit() {
	return active_kit;
}
int clip_view;
int& instrument_clip_view_for_session() {
	return clip_view;
}
void uiNeedsRendering(int*) {
	++redraws;
}
struct SampleBrowser {
	bool loadAllSamplesInFolder(bool, int32_t* count, Sample*** table, bool* single_cycle, int32_t* prefix = nullptr) {
		if (single_cycle)
			*single_cycle = false;
		*count = loaded_samples.size();
		if (prefix)
			*prefix = 0;
		*table = static_cast<Sample**>(std::malloc(loaded_samples.size() * sizeof(Sample*)));
		sample_table = *table;
		for (size_t index = 0; index < loaded_samples.size(); ++index)
			(*table)[index] = loaded_samples[index];
		return true;
	}
	void autoDetectSideChainSending(SoundDrum*, Source*, const char*) { ++sidechains; }
	void exitAndNeverDeleteDrum() { ++exits; }
	bool importFolderAsKit();
	bool importFolderAsMultisamples();
	void audioFileIsNowSet() { ++audio_publications; }
};
#include "multisample_import_cleanup.inc"

#include "kit_sample_import.inc"

#include "multisample_folder_import.inc"
} // namespace kit_sample_import_test
using namespace kit_sample_import_test;
TEST_GROUP(KitSampleImport) {
	Sample samples[3];
	SoundDrum first_drum;
	Kit kit;
	SampleBrowser browser;
	void setup() override {
		range_calls = fail_range = allocation_calls = fail_allocation = 0;
		parameter_calls = fail_parameter = assignment_calls = fail_assignment = 0;
		sidechains = row_assignments = edits = exits = redraws = table_frees = 0;
		sample_table = nullptr;
		transpose_calls = audio_publications = 0;
		reserve_success = true;
		AudioEngine::audioRoutineLocked = false;
		for (int32_t index = 0; index < 3; ++index)
			samples[index].midiNote = 60 + index;
		display_instance = {};
		loaded_samples = {&samples[0], &samples[1], &samples[2]};
		active_kit = &kit;
		editor.currentSound = &first_drum;
		editor.currentSource = &first_drum.sources[0];
	}
	void teardown() override {
		for (auto* drum : kit.drums) {
			drum->~SoundDrum();
			std::free(drum);
		}
		if (sample_table) {
			std::free(sample_table);
			sample_table = nullptr;
		}
		loaded_samples.clear();
		active_kit = nullptr;
	}
};
TEST(KitSampleImport, initial_range_failure_releases_all_temporary_samples) {
	fail_range = 1;
	CHECK_FALSE(browser.importFolderAsKit());
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(0, row_assignments);
}
TEST(KitSampleImport, parameter_failure_preserves_completed_holder_and_releases_unprocessed_samples) {
	fail_parameter = 1;
	CHECK_FALSE(browser.importFolderAsKit());
	LONGS_EQUAL(1, samples[0].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[1].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[2].numReasonsToBeLoaded);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(0, edits);
}
TEST(KitSampleImport, later_drum_allocation_failure_preserves_completed_drums) {
	fail_allocation = 2;
	CHECK_FALSE(browser.importFolderAsKit());
	LONGS_EQUAL(1, samples[0].numReasonsToBeLoaded);
	LONGS_EQUAL(1, samples[1].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[2].numReasonsToBeLoaded);
	LONGS_EQUAL(1, kit.drums.size());
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(0, row_assignments);
}
TEST(KitSampleImport, new_drum_range_failure_releases_unprocessed_samples) {
	fail_range = 2;
	CHECK_FALSE(browser.importFolderAsKit());
	LONGS_EQUAL(1, samples[0].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[1].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[2].numReasonsToBeLoaded);
	LONGS_EQUAL(0, kit.drums.size());
	LONGS_EQUAL(1, table_frees);
}
TEST(KitSampleImport, completed_import_retains_holder_references_and_publishes_rows) {
	CHECK_TRUE(browser.importFolderAsKit());
	for (auto& sample : samples)
		LONGS_EQUAL(1, sample.numReasonsToBeLoaded);
	LONGS_EQUAL(2, kit.drums.size());
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(1, row_assignments);
	LONGS_EQUAL(1, edits);
	LONGS_EQUAL(1, exits);
	LONGS_EQUAL(1, redraws);
}
TEST(KitSampleImport, cancelled_detachment_stops_import_before_sample_assignment) {
	fail_assignment = 1;
	CHECK_FALSE(browser.importFolderAsKit());
	LONGS_EQUAL(1, assignment_calls);
	LONGS_EQUAL(0, sidechains);
	LONGS_EQUAL(0, row_assignments);
	LONGS_EQUAL(1, table_frees);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, cancelled_sample_assignment_stops_import_before_publication) {
	fail_assignment = 2;
	CHECK_FALSE(browser.importFolderAsKit());
	LONGS_EQUAL(2, assignment_calls);
	LONGS_EQUAL(0, sidechains);
	LONGS_EQUAL(0, row_assignments);
	LONGS_EQUAL(1, table_frees);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}

TEST(KitSampleImport, multisample_assignment_cancellation_stops_range_publication) {
	fail_assignment = 1;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(1, assignment_calls);
	LONGS_EQUAL(0, transpose_calls);
	LONGS_EQUAL(0, audio_publications);
	LONGS_EQUAL(0, exits);
	LONGS_EQUAL(1, table_frees);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, later_multisample_cancellation_preserves_completed_range_reference) {
	fail_assignment = 2;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(2, assignment_calls);
	LONGS_EQUAL(1, transpose_calls);
	LONGS_EQUAL(1, samples[0].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[1].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[2].numReasonsToBeLoaded);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(0, audio_publications);
}
TEST(KitSampleImport, multisample_import_releases_duplicate_pitch_sample_references) {
	for (auto& sample : samples)
		sample.midiNote = 60;
	CHECK_TRUE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(1, samples[0].numReasonsToBeLoaded);
	LONGS_EQUAL(0, samples[1].numReasonsToBeLoaded);
	LONGS_EQUAL(1, samples[2].numReasonsToBeLoaded);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(1, audio_publications);
}
TEST(KitSampleImport, multisample_import_with_no_usable_pitches_releases_all_resources) {
	for (auto& sample : samples)
		sample.midiNote = MIDI_NOTE_ERROR;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(0, audio_publications);
}
TEST(KitSampleImport, multisample_reservation_failure_releases_all_temporary_samples) {
	reserve_success = false;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(0, assignment_calls);
}
TEST(KitSampleImport, successful_multisample_import_publishes_completed_ranges) {
	CHECK_TRUE(browser.importFolderAsMultisamples());
	for (auto& sample : samples)
		LONGS_EQUAL(1, sample.numReasonsToBeLoaded);
	LONGS_EQUAL(3, assignment_calls);
	LONGS_EQUAL(3, transpose_calls);
	LONGS_EQUAL(1, table_frees);
	LONGS_EQUAL(1, audio_publications);
	LONGS_EQUAL(1, edits);
	LONGS_EQUAL(1, exits);
}
