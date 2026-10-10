#include "CppUTest/TestHarness.h"
#include "util/finally.h"
#include "util/try.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#define ALPHA_OR_BETA_VERSION 1
#define D_PRINT(...)
#define D_PRINTLN(...)
namespace folder_sample_loading_test {
enum class Error { NONE, SD_CARD, INSUFFICIENT_RAM };
enum class AudioFileType { SAMPLE };
constexpr int MIDI_NOTE_ERROR = -1000, AM_DIR = 16;
std::function<void()> on_audio, on_append, on_pitch, on_sort_allocation;
int fail_owned_slot = -1;
bool fail_sort_allocation = false;
std::vector<std::string> requested_paths;
int read_failure_after = -1;
struct String {
	std::string value;
	void set(const String* other) { value = other->value; }
	const char* get() const { return value.c_str(); }
	int32_t getLength() const { return value.size(); }
	void concatenateAtPos(const char* text, int32_t position) {
		value.resize(position);
		if (on_append)
			on_append();
		value += text;
	}
};
struct FileItem {
	bool isFolder = false;
	String filename;
};
std::unique_ptr<FileItem> selected_file;
String directory;
String& current_dir_for_session() {
	return directory;
}
struct FilePointer {};
struct FileInfo {
	char fname[64]{};
	int fattrib = 0;
} staticFNO;
std::vector<std::string> directory_entries;
template <typename T>
struct Result {
	T payload;
	bool valid = true;
	bool has_value() const { return valid; }
	T& value() {
		CHECK_TRUE(valid);
		return payload;
	}
	Error error() const { return Error::SD_CARD; }
};
namespace FatFS {
using FileInfo = ::folder_sample_loading_test::FileInfo;
struct Directory {
	size_t position = 0;
	bool closed = false;
	std::vector<std::string> entries;
	static Result<Directory> open(const char*) { return {Directory{0, false, directory_entries}}; }
	Result<std::pair<FileInfo, FilePointer>> read_and_get_filepointer() {
		if (static_cast<int>(position) == read_failure_after)
			return {{}, false};
		FileInfo info;
		if (!closed && position < entries.size())
			std::strncpy(info.fname, entries[position++].c_str(), sizeof(info.fname) - 1);
		return {{info, {}}};
	}
	void close() { closed = true; }
};
} // namespace FatFS
FatFS::Directory staticDIR;
struct AudioFile {
	AudioFileType type = AudioFileType::SAMPLE;
};
struct Sample : AudioFile {
	int numReasonsToBeLoaded = 0;
	float midiNote = 60, midiNoteFromFile = -1;
	void addReason() { ++numReasonsToBeLoaded; }
	void removeReason(const char*) { --numReasonsToBeLoaded; }
	int32_t getLengthInMSec() const { return 100; }
	void workOutMIDINote(bool, float = 0, float = 0, bool = true) {
		if (on_pitch) {
			auto callback = std::exchange(on_pitch, {});
			callback();
		}
	}
};
std::vector<Sample*> loaded_samples;
struct AudioFileManager {
	struct Files {
		int32_t getNumElements() const { return loaded_samples.size(); }
		Sample* getElement(int32_t index) { return loaded_samples.at(index); }
	} audioFiles;
	size_t next_sample = 0;
	void loadAnyEnqueuedClusters() {}
	AudioFile* getAudioFileFromFilename(String& path, bool, Error* error, FilePointer*, AudioFileType) {
		requested_paths.push_back(path.get());
		*error = Error::NONE;
		++next_sample;
		return loaded_samples.at(path.value.find("snare") != std::string::npos ? 1 : 0);
	}
} audioFileManager;
namespace AudioEngine {
void routineWithClusterLoading() {
	if (on_audio)
		on_audio();
}
} // namespace AudioEngine
struct Display {
	Error last_error = Error::NONE;
	void displayError(Error error) { last_error = error; }
} display_instance;
auto* display = &display_instance;
struct GeneralMemoryAllocator {
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t bytes) {
		if (on_sort_allocation) {
			auto callback = std::exchange(on_sort_allocation, {});
			callback();
		}
		return fail_sort_allocation ? nullptr : std::malloc(bytes);
	}
};
void FREEZE_WITH_ERROR(const char*) {
	FAIL("invalid sample reference count");
}
bool isAudioFilename(const char*) {
	return true;
}
bool filenameGreaterOrEqual(Sample*, Sample*) {
	return true;
}
bool pitchGreaterOrEqual(Sample* first, Sample* second) {
	return first->midiNote >= second->midiNote;
}
void sortSamples(bool (*)(Sample*, Sample*), int32_t, Sample***, int32_t*, int32_t*) {
}
int32_t getNumTimesIncorrectSampleOrderSeen(int32_t, Sample**) {
	return 0;
}
void delugeDealloc(void* memory) {
	std::free(memory);
}
struct ResizeableArray {
	std::vector<Sample*> entries;
	explicit ResizeableArray(int32_t) {}
	Error insertAtIndex(int32_t index) {
		if (index == fail_owned_slot)
			return Error::INSUFFICIENT_RAM;
		entries.push_back(nullptr);
		return Error::NONE;
	}
	void* getElementAddress(int32_t index) { return &entries.at(index); }
};
struct SampleBrowser {
	FileItem* getCurrentFileItem() { return selected_file.get(); }
	Error getCurrentFilePath(String* result) {
		result->set(&directory);
		return Error::NONE;
	}
	bool loadAllSamplesInFolder(bool, int32_t*, Sample***, bool*, int32_t*);
};
#include "folder_sample_loading.inc"
} // namespace folder_sample_loading_test
using namespace folder_sample_loading_test;
TEST_GROUP(FolderSampleLoading) {
	Sample first, second;
	SampleBrowser browser;
	Sample** table = nullptr;
	void setup() override {
		selected_file = std::make_unique<FileItem>();
		selected_file->filename.value = "orig-kick.wav";
		directory.value = "/samples";
		directory_entries = {"orig-kick.wav", "orig-snare.wav"};
		loaded_samples = {&first, &second};
		audioFileManager.next_sample = 0;
		read_failure_after = -1;
		display_instance = {};
		on_audio = on_append = on_pitch = on_sort_allocation = {};
		fail_owned_slot = -1;
		fail_sort_allocation = false;
		requested_paths.clear();
	}
	void teardown() override {
		std::free(table);
		loaded_samples.clear();
		selected_file.reset();
		on_audio = on_append = on_pitch = on_sort_allocation = {};
		fail_owned_slot = -1;
		fail_sort_allocation = false;
		requested_paths.clear();
	}
};
TEST(FolderSampleLoading, changed_selection_filename_does_not_change_original_prefix_calculation) {
	on_audio = [] { selected_file->filename.value = "newx-kick.wav"; };
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(2, count);
	LONGS_EQUAL(14, prefix); // /samples/ plus orig-
	CHECK_FALSE(single_cycle);
	LONGS_EQUAL(1, first.numReasonsToBeLoaded);
	LONGS_EQUAL(1, second.numReasonsToBeLoaded);
}
TEST(FolderSampleLoading, normal_selection_keeps_existing_prefix_calculation) {
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(14, prefix);
	LONGS_EQUAL(2, count);
}

TEST(FolderSampleLoading, destroyed_selection_keeps_original_filename_alive_for_prefix_calculation) {
	on_audio = [] { selected_file.reset(); };
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(14, prefix);
	LONGS_EQUAL(2, count);
}
TEST(FolderSampleLoading, missing_selection_rejects_entry_without_acquiring_samples) {
	selected_file.reset();
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_FALSE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	CHECK(table == nullptr);
	LONGS_EQUAL(0, first.numReasonsToBeLoaded);
	LONGS_EQUAL(0, second.numReasonsToBeLoaded);
}
TEST(FolderSampleLoading, folder_selection_keeps_the_existing_empty_basename_behavior) {
	selected_file->isFolder = true;
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(9, prefix);
	LONGS_EQUAL(2, count);
}

TEST(FolderSampleLoading, nested_browser_directory_does_not_replace_loader_cursor) {
	on_audio = [] {
		directory_entries = {"unrelated.wav"};
		staticDIR = FatFS::Directory::open("/other").value();
	};
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(2, count);
	LONGS_EQUAL(0, staticDIR.position);
	CHECK_FALSE(staticDIR.closed);
	STRCMP_EQUAL("/samples/orig-kick.wav", requested_paths[0].c_str());
	STRCMP_EQUAL("/samples/orig-snare.wav", requested_paths[1].c_str());
}
TEST(FolderSampleLoading, nested_filename_buffer_changes_do_not_replace_current_directory_entry) {
	on_append = [] { std::strcpy(staticFNO.fname, "unrelated.wav"); };
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(2, count);
	STRCMP_EQUAL("/samples/orig-kick.wav", requested_paths[0].c_str());
	STRCMP_EQUAL("/samples/orig-snare.wav", requested_paths[1].c_str());
}

TEST(FolderSampleLoading, directory_read_failure_rejects_partial_sample_list_and_releases_references) {
	read_failure_after = 1;
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_FALSE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	CHECK(table == nullptr);
	LONGS_EQUAL(0, count);
	LONGS_EQUAL(0, first.numReasonsToBeLoaded);
	LONGS_EQUAL(0, second.numReasonsToBeLoaded);
	CHECK_TRUE(display_instance.last_error == Error::SD_CARD);
}
TEST(FolderSampleLoading, initial_directory_read_failure_does_not_allocate_or_publish_samples) {
	read_failure_after = 0;
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_FALSE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	CHECK(table == nullptr);
	LONGS_EQUAL(0, audioFileManager.next_sample);
	CHECK_TRUE(display_instance.last_error == Error::SD_CARD);
}

TEST(FolderSampleLoading, nested_import_keeps_each_loads_temporary_sample_ownership_separate) {
	int32_t nested_count = 0;
	on_pitch = [&] {
		on_pitch = {};
		Sample** nested_table = nullptr;
		bool single_cycle = false;
		int32_t prefix = 0;
		CHECK_TRUE(browser.loadAllSamplesInFolder(false, &nested_count, &nested_table, &single_cycle, &prefix));
		for (int32_t index = 0; index < nested_count; ++index)
			nested_table[index]->removeReason("nested");
		std::free(nested_table);
	};
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(true, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(2, nested_count);
	LONGS_EQUAL(2, count);
	LONGS_EQUAL(1, first.numReasonsToBeLoaded);
	LONGS_EQUAL(1, second.numReasonsToBeLoaded);
}
TEST(FolderSampleLoading, owned_pointer_allocation_failure_releases_new_and_prior_references) {
	fail_owned_slot = 1;
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_FALSE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	CHECK(table == nullptr);
	LONGS_EQUAL(0, first.numReasonsToBeLoaded);
	LONGS_EQUAL(0, second.numReasonsToBeLoaded);
	CHECK_TRUE(display_instance.last_error == Error::INSUFFICIENT_RAM);
}
TEST(FolderSampleLoading, sort_table_allocation_failure_releases_all_owned_references) {
	fail_sort_allocation = true;
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_FALSE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	CHECK(table == nullptr);
	LONGS_EQUAL(0, first.numReasonsToBeLoaded);
	LONGS_EQUAL(0, second.numReasonsToBeLoaded);
}
TEST(FolderSampleLoading, duplicate_cached_samples_keep_one_reason_for_each_returned_entry) {
	loaded_samples[1] = &first;
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(2, count);
	CHECK(table[0] == &first);
	CHECK(table[1] == &first);
	LONGS_EQUAL(2, first.numReasonsToBeLoaded);
}

TEST(FolderSampleLoading, nested_load_during_sort_allocation_preserves_outer_sample_references) {
	int32_t nested_count = 0;
	on_sort_allocation = [&] {
		Sample** nested_table = nullptr;
		bool single_cycle = false;
		int32_t prefix = 0;
		CHECK_TRUE(browser.loadAllSamplesInFolder(false, &nested_count, &nested_table, &single_cycle, &prefix));
		for (int32_t index = 0; index < nested_count; ++index)
			nested_table[index]->removeReason("nested");
		std::free(nested_table);
	};
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, &table, &single_cycle, &prefix));
	LONGS_EQUAL(2, nested_count);
	LONGS_EQUAL(2, count);
	LONGS_EQUAL(1, first.numReasonsToBeLoaded);
	LONGS_EQUAL(1, second.numReasonsToBeLoaded);
}
TEST(FolderSampleLoading, omitted_output_table_releases_temporary_sample_ownership) {
	int32_t count = 0, prefix = 0;
	bool single_cycle = true;
	CHECK_TRUE(browser.loadAllSamplesInFolder(false, &count, nullptr, &single_cycle, &prefix));
	LONGS_EQUAL(2, count);
	LONGS_EQUAL(0, first.numReasonsToBeLoaded);
	LONGS_EQUAL(0, second.numReasonsToBeLoaded);
}
