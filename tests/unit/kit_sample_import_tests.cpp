#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/finally.h"
#include "util/lifetime.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
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
constexpr int CLUSTER_ENQUEUE = 0;
std::function<void()> on_assignment, on_reserve, on_kill, on_audio, on_exit;
int audio_calls = 0, directory_closes = 0;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
void uartPrintlnFloat(float) {
}
namespace deluge {
namespace lifetime = ::deluge::lifetime;
namespace gui = ::deluge::gui;
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
	void set(const String* other) { value = other->value; }
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
	int32_t applied_correction = 0;
	void setTransposeAccordingToSamplePitch(bool, bool, bool, int32_t, int32_t correction = 0) {
		++transpose_calls;
		applied_correction = correction;
	}
	~AudioFileHolder() {
		if (sample)
			sample->removeReason("fixture");
	}
	bool setAudioFile(Sample* replacement, bool = false, bool = false, int32_t = CLUSTER_ENQUEUE,
	                  const ::deluge::lifetime::callback_validation* validation = nullptr) {
		if (on_assignment)
			on_assignment();
		if (validation && !validation->valid())
			return false;
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
struct MultiRangeArray {
	MultiRange* entries = nullptr;
	int32_t count = 0;
	uint32_t revision = 0;
	~MultiRangeArray() { std::free(entries); }
	int32_t getNumElements() const { return count; }
	uint32_t layout_revision() const { return revision; }
	int32_t element_size() const { return sizeof(MultisampleRange); }
	bool ensureEnoughSpaceAllocated(int32_t) {
		if (on_reserve)
			on_reserve();
		if (!reserve_success)
			return false;
		if (!entries)
			entries = static_cast<MultiRange*>(std::malloc(4 * sizeof(MultiRange)));
		return entries != nullptr;
	}
	MultiRange* getElement(int32_t index) { return &entries[index]; }
	MultiRange* insertMultiRange(int32_t index) {
		if (!entries)
			entries = static_cast<MultiRange*>(std::malloc(4 * sizeof(MultiRange)));
		++count;
		++revision;
		return new (&entries[index]) MultiRange;
	}
	void swapStateWith(MultiRangeArray* other) {
		std::swap(entries, other->entries);
		std::swap(count, other->count);
		++revision;
		++other->revision;
	}
};
struct Source : Lifetime {
	MultiRangeArray ranges;
	Source() { ranges.insertMultiRange(0); }
	~Source() {
		for (int32_t index = 0; index < ranges.count; ++index)
			ranges.getElement(index)->~MultiRange();
	}
	OscType oscType = OscType::SAMPLE;
	int defaultRangeI = 0;
	SampleRepeatMode repeatMode = SampleRepeatMode::CUT;
	struct {
		bool isCurrentlyReversed() const { return false; }
	} sampleControls;
	MultiRange* getOrCreateFirstRange() { return ++range_calls == fail_range ? nullptr : ranges.getElement(0); }
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
struct Sound : Lifetime {
	Source sources[1];
	bool active_voices = false;
	bool hasActiveVoices() const { return active_voices; }
	auto watch_sound_lifetime() { return watch_lifetime(); }
	void killAllVoices() {
		if (on_kill)
			on_kill();
	}
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
struct Clip;
Clip* current_clip = nullptr;
struct Song : Lifetime {
	Clip* getCurrentClip() { return current_clip; }
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
	++audio_calls;
	if (on_audio)
		on_audio();
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
	void close() { ++directory_closes; }
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
struct Instrument;
struct Clip : Lifetime {
	Instrument* output = nullptr;
	void assignDrumsToNoteRows(ModelStackWithTimelineCounter*) { ++row_assignments; }
} clip;
struct Instrument : Lifetime {
	int own_edits = 0;
	void beenEdited() {
		++edits;
		++own_edits;
	}
} instrument;
Clip* getCurrentInstrumentClip() {
	return &clip;
}
Instrument* getCurrentInstrument() {
	return current_clip ? current_clip->output : nullptr;
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
	void exitAndNeverDeleteDrum() {
		++exits;
		if (on_exit)
			on_exit();
	}
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
	Sample original_sample;
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
		on_assignment = on_reserve = on_kill = on_audio = on_exit = {};
		audio_calls = directory_closes = 0;
		::deluge::gui::ui_session::detail::active = ::deluge::gui::ui_session::Id::Local;
		AudioEngine::audioRoutineLocked = false;
		for (int32_t index = 0; index < 3; ++index)
			samples[index].midiNote = 60 + index;
		display_instance = {};
		loaded_samples = {&samples[0], &samples[1], &samples[2]};
		active_kit = &kit;
		current_clip = &clip;
		clip.output = &instrument;
		instrument.own_edits = 0;
		editor.currentSound = &first_drum;
		editor.currentSource = &first_drum.sources[0];
	}
	void teardown() override {
		on_assignment = on_reserve = on_kill = on_audio = on_exit = {};
		audio_calls = directory_closes = 0;
		::deluge::gui::ui_session::detail::active = ::deluge::gui::ui_session::Id::Local;
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
TEST(KitSampleImport, later_multisample_cancellation_discards_detached_ranges) {
	fail_assignment = 2;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(2, assignment_calls);
	LONGS_EQUAL(1, transpose_calls);
	LONGS_EQUAL(0, samples[0].numReasonsToBeLoaded);
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

TEST(KitSampleImport, multisample_reservation_failure_preserves_original_mapping) {
	auto& ranges = first_drum.sources[0].ranges;
	ranges.insertMultiRange(1)->topNote = 96;
	auto* original = ranges.getElement(1);
	reserve_success = false;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(2, ranges.getNumElements());
	CHECK(original == ranges.getElement(1));
	LONGS_EQUAL(96, original->topNote);
}
TEST(KitSampleImport, multisample_assignment_failure_preserves_original_mapping_and_type) {
	auto& source = first_drum.sources[0];
	source.oscType = OscType::OTHER;
	source.ranges.getElement(0)->topNote = 90;
	auto* original = source.ranges.getElement(0);
	fail_assignment = 2;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	CHECK_TRUE(source.oscType == OscType::OTHER);
	CHECK(original == source.ranges.getElement(0));
	LONGS_EQUAL(90, original->topNote);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, multisample_source_retirement_during_assignment_cancels_commit) {
	on_assignment = [&] { first_drum.sources[0].lifetime.retire(); };
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(1, first_drum.sources[0].ranges.getNumElements());
	LONGS_EQUAL(0, audio_publications);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, multisample_owner_destruction_during_reservation_cleans_only_detached_resources) {
	auto owner = std::make_unique<SoundDrum>();
	editor.currentSound = owner.get();
	editor.currentSource = &owner->sources[0];
	on_reserve = [&] { owner.reset(); };
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(0, assignment_calls);
	LONGS_EQUAL(1, table_frees);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, multisample_nested_voices_cancel_commit_and_preserve_original_ranges) {
	auto* original = first_drum.sources[0].ranges.getElement(0);
	on_kill = [&] { first_drum.active_voices = true; };
	CHECK_FALSE(browser.importFolderAsMultisamples());
	CHECK(original == first_drum.sources[0].ranges.getElement(0));
	LONGS_EQUAL(0, audio_publications);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, multisample_commit_preserves_caller_audio_lock) {
	AudioEngine::audioRoutineLocked = true;
	CHECK_TRUE(browser.importFolderAsMultisamples());
	CHECK_TRUE(AudioEngine::audioRoutineLocked);
}

TEST(KitSampleImport, multisample_owner_destruction_during_retired_range_cleanup_stops_ui_publication) {
	auto owner = std::make_unique<SoundDrum>();
	editor.currentSound = owner.get();
	editor.currentSource = &owner->sources[0];
	on_audio = [&] {
		if (audio_calls == 2)
			owner.reset();
	};
	CHECK_FALSE(browser.importFolderAsMultisamples());
	CHECK(owner == nullptr);
	LONGS_EQUAL(0, audio_publications);
	LONGS_EQUAL(0, edits);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}

TEST(KitSampleImport, failed_multisample_replacement_preserves_original_sample_reference) {
	original_sample.numReasonsToBeLoaded = 0;
	auto* original_range = first_drum.sources[0].ranges.getElement(0);
	CHECK_TRUE(original_range->sampleHolder.setAudioFile(&original_sample));
	assignment_calls = 0;
	fail_assignment = 2;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	CHECK(original_range == first_drum.sources[0].ranges.getElement(0));
	CHECK(original_range->sampleHolder.sample == &original_sample);
	LONGS_EQUAL(1, original_sample.numReasonsToBeLoaded);
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, completed_multisample_replacement_releases_original_sample_reference_once) {
	original_sample.numReasonsToBeLoaded = 0;
	CHECK_TRUE(first_drum.sources[0].ranges.getElement(0)->sampleHolder.setAudioFile(&original_sample));
	assignment_calls = 0;
	CHECK_TRUE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(0, original_sample.numReasonsToBeLoaded);
	for (auto& sample : samples)
		LONGS_EQUAL(1, sample.numReasonsToBeLoaded);
}

TEST(KitSampleImport, multisample_clip_change_during_assignment_cancels_commit) {
	Clip replacement;
	replacement.output = &instrument;
	on_assignment = [&] { current_clip = &replacement; };
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(0, audio_publications);
	LONGS_EQUAL(1, first_drum.sources[0].ranges.getNumElements());
	for (auto& sample : samples)
		LONGS_EQUAL(0, sample.numReasonsToBeLoaded);
}
TEST(KitSampleImport, multisample_output_rebinding_during_assignment_cancels_commit) {
	Instrument replacement;
	on_assignment = [&] { clip.output = &replacement; };
	CHECK_FALSE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(0, audio_publications);
	LONGS_EQUAL(0, replacement.own_edits);
	LONGS_EQUAL(1, first_drum.sources[0].ranges.getNumElements());
}
TEST(KitSampleImport, multisample_navigation_does_not_mark_newly_selected_instrument_edited) {
	Instrument replacement_instrument;
	Clip replacement_clip;
	replacement_clip.output = &replacement_instrument;
	on_exit = [&] { current_clip = &replacement_clip; };
	CHECK_TRUE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(1, instrument.own_edits);
	LONGS_EQUAL(0, replacement_instrument.own_edits);
	LONGS_EQUAL(1, display_instance.removals);
}
TEST(KitSampleImport, multisample_navigation_does_not_remove_new_working_animation) {
	on_exit = [&] { display_instance.removals = 0; };
	CHECK_TRUE(browser.importFolderAsMultisamples());
	LONGS_EQUAL(0, display_instance.removals);
}
TEST(KitSampleImport, multisample_missing_clip_rejects_entry_without_loading_samples) {
	current_clip = nullptr;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	CHECK(sample_table == nullptr);
	LONGS_EQUAL(0, assignment_calls);
}

TEST(KitSampleImport, octave_correction_preserves_shared_pitch_when_assignment_fails) {
	samples[0].midiNote = 48;
	samples[1].midiNote = 61;
	samples[2].midiNote = 62;
	fail_assignment = 2;
	CHECK_FALSE(browser.importFolderAsMultisamples());
	DOUBLES_EQUAL(48, samples[0].midiNote, 0.001);
	DOUBLES_EQUAL(61, samples[1].midiNote, 0.001);
	DOUBLES_EQUAL(62, samples[2].midiNote, 0.001);
}
TEST(KitSampleImport, lower_octave_correction_changes_only_imported_ranges) {
	samples[0].midiNote = 48;
	samples[1].midiNote = 61;
	samples[2].midiNote = 62;
	CHECK_TRUE(browser.importFolderAsMultisamples());
	auto& ranges = first_drum.sources[0].ranges;
	LONGS_EQUAL(60, ranges.getElement(0)->topNote);
	LONGS_EQUAL(61, ranges.getElement(1)->topNote);
	LONGS_EQUAL(12, ranges.getElement(0)->sampleHolder.applied_correction);
	LONGS_EQUAL(0, ranges.getElement(1)->sampleHolder.applied_correction);
	DOUBLES_EQUAL(48, samples[0].midiNote, 0.001);
}
TEST(KitSampleImport, upper_octave_correction_changes_only_imported_ranges) {
	samples[0].midiNote = 60;
	samples[1].midiNote = 61;
	samples[2].midiNote = 74;
	CHECK_TRUE(browser.importFolderAsMultisamples());
	auto& ranges = first_drum.sources[0].ranges;
	LONGS_EQUAL(60, ranges.getElement(0)->topNote);
	LONGS_EQUAL(61, ranges.getElement(1)->topNote);
	LONGS_EQUAL(-12, ranges.getElement(2)->sampleHolder.applied_correction);
	DOUBLES_EQUAL(74, samples[2].midiNote, 0.001);
}
TEST(KitSampleImport, ambiguous_intervals_cancel_octave_correction) {
	samples[0].midiNote = 48;
	samples[1].midiNote = 61;
	samples[2].midiNote = 65;
	CHECK_TRUE(browser.importFolderAsMultisamples());
	auto& ranges = first_drum.sources[0].ranges;
	for (int32_t index = 0; index < 3; ++index)
		LONGS_EQUAL(0, ranges.getElement(index)->sampleHolder.applied_correction);
	DOUBLES_EQUAL(48, samples[0].midiNote, 0.001);
}

TEST(KitSampleImport, failed_kit_import_does_not_close_an_unrelated_directory) {
	fail_parameter = 1;
	CHECK_FALSE(browser.importFolderAsKit());
	LONGS_EQUAL(0, directory_closes);
}
