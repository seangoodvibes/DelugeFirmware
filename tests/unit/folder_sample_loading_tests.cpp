#include "CppUTest/TestHarness.h"
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
#define D_TRY_CATCH(expression, error, failure) (expression)
namespace folder_sample_loading_test {
enum class Error { NONE, SD_CARD, INSUFFICIENT_RAM };
enum class AudioFileType { SAMPLE };
constexpr int MIDI_NOTE_ERROR = -1000, AM_DIR = 16;
std::function<void()> on_audio;
struct String {
	std::string value;
	void set(const String* other) { value = other->value; }
	const char* get() const { return value.c_str(); }
	int32_t getLength() const { return value.size(); }
	void concatenateAtPos(const char* text, int32_t position) {
		value.resize(position);
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
namespace FatFS {
struct Directory {
	size_t position = 0;
	static Directory open(const char*) { return {}; }
	std::pair<FileInfo, FilePointer> read_and_get_filepointer() {
		FileInfo info;
		if (position < directory_entries.size())
			std::strncpy(info.fname, directory_entries[position++].c_str(), sizeof(info.fname) - 1);
		return {info, {}};
	}
	void close() {}
};
} // namespace FatFS
FatFS::Directory staticDIR;
struct AudioFile {
	AudioFileType type = AudioFileType::SAMPLE;
};
struct Sample : AudioFile {
	int numReasonsToBeLoaded = 0;
	bool partOfFolderBeingLoaded = false;
	float midiNote = 60, midiNoteFromFile = -1;
	void addReason() { ++numReasonsToBeLoaded; }
	void removeReason(const char*) { --numReasonsToBeLoaded; }
	int32_t getLengthInMSec() const { return 100; }
	void workOutMIDINote(bool, float = 0, float = 0, bool = true) {}
};
std::vector<Sample*> loaded_samples;
struct AudioFileManager {
	struct Files {
		int32_t getNumElements() const { return loaded_samples.size(); }
		Sample* getElement(int32_t index) { return loaded_samples.at(index); }
	} audioFiles;
	size_t next_sample = 0;
	void loadAnyEnqueuedClusters() {}
	AudioFile* getAudioFileFromFilename(String&, bool, Error* error, FilePointer*, AudioFileType) {
		*error = Error::NONE;
		return loaded_samples.at(next_sample++);
	}
} audioFileManager;
namespace AudioEngine {
void routineWithClusterLoading() {
	if (on_audio)
		on_audio();
}
} // namespace AudioEngine
struct Display {
	void displayError(Error) {}
} display_instance;
auto* display = &display_instance;
struct GeneralMemoryAllocator {
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t bytes) { return std::malloc(bytes); }
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
		on_audio = {};
	}
	void teardown() override {
		std::free(table);
		loaded_samples.clear();
		selected_file.reset();
		on_audio = {};
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
