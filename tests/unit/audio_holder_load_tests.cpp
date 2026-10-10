#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <string>

namespace audio_holder_load_test {
namespace session = deluge::gui::ui_session;
enum class Error { NONE, ABORTED_BY_USER, FILE_NOT_FOUND };
enum class AudioFileType { SAMPLE, WAVETABLE };
constexpr int CLUSTER_ENQUEUE = 1;
struct FilePointer {};
struct AudioFile {
} loaded_file, replacement_file;
struct String {
	std::shared_ptr<std::string> value = std::make_shared<std::string>();
	const char* get() const { return value->c_str(); }
	bool isEmpty() const { return value->empty(); }
	void set(const String* other) { value = other->value; }
	void set(const char* text) { value = std::make_shared<std::string>(text); }
};
std::function<void()> on_lookup, on_assign;
int lookups = 0, assignments = 0, errors = 0;
Error lookup_error = Error::NONE;
struct {
	AudioFile* getAudioFileFromFilename(String& path, bool, Error* error, FilePointer*, AudioFileType, bool) {
		++lookups;
		if (on_lookup)
			on_lookup();
		// Storage is allowed to read and normalize the path after servicing callbacks.
		STRCMP_EQUAL("original.wav", path.get());
		path.set("resolved.wav");
		*error = lookup_error;
		return lookup_error == Error::NONE ? &loaded_file : nullptr;
	}
} audioFileManager;
struct AudioFileHolder {
	String filePath;
	AudioFile* audioFile = nullptr;
	AudioFileType audioFileType = AudioFileType::SAMPLE;
	Error loadFile(bool, bool, bool, int = CLUSTER_ENQUEUE, FilePointer* = nullptr, bool = false,
	               const deluge::lifetime::callback_validation* = nullptr);
	void setAudioFile(AudioFile* file, bool, bool, int) {
		++assignments;
		audioFile = file;
		if (on_assign)
			on_assign();
	}
};
#include "audio_holder_load.inc"
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
Song* currentSong = nullptr;
struct Output {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct AudioClip {
	deluge::lifetime::lifetime_source lifetime;
	Output* output = nullptr;
	AudioFileHolder sampleHolder;
	String name;
	struct {
		bool isCurrentlyReversed() { return false; }
	} sampleControls;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	void loadSample(bool);
};
struct Display {
	void displayError(Error) { ++errors; }
} display_instance;
auto* display = &display_instance;
#include "audio_clip_load.inc"
} // namespace audio_holder_load_test
using namespace audio_holder_load_test;
TEST_GROUP(AudioHolderLoad) {
	AudioClip clip;
	Song song;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		clip.sampleHolder.filePath.set("original.wav");
		clip.name.set("old name");
		on_lookup = on_assign = {};
		lookups = assignments = errors = 0;
		lookup_error = Error::NONE;
	}
	void teardown() override {
		on_lookup = on_assign = {};
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
	}
};

TEST(AudioHolderLoad, invalidated_storage_result_is_not_assigned) {
	bool valid = true;
	auto check = [&] { return valid; };
	deluge::lifetime::callback_validation validation(check);
	on_lookup = [&] { valid = false; };
	CHECK(clip.sampleHolder.loadFile(false, false, true, CLUSTER_ENQUEUE, nullptr, false, &validation)
	      == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, assignments);
	STRCMP_EQUAL("original.wav", clip.sampleHolder.filePath.get());
}

TEST(AudioHolderLoad, clip_retirement_during_storage_does_not_publish_name_or_error) {
	on_lookup = [&] { clip.lifetime.retire(); };
	lookup_error = Error::FILE_NOT_FOUND;
	clip.loadSample(true);
	STRCMP_EQUAL("old name", clip.name.get());
	LONGS_EQUAL(0, errors);
}

TEST(AudioHolderLoad, successful_load_publishes_normalized_path_and_name) {
	clip.loadSample(true);
	LONGS_EQUAL(1, assignments);
	STRCMP_EQUAL("resolved.wav", clip.sampleHolder.filePath.get());
	STRCMP_EQUAL("resolved.wav", clip.name.get());
	LONGS_EQUAL(0, errors);
}

TEST(AudioHolderLoad, destroyed_clip_does_not_invalidate_storage_path_or_receive_assignment) {
	auto owned_clip = std::make_unique<AudioClip>();
	owned_clip->sampleHolder.filePath.set("original.wav");
	on_lookup = [&] { owned_clip.reset(); };
	owned_clip->loadSample(true);
	LONGS_EQUAL(1, lookups);
	LONGS_EQUAL(0, assignments);
	LONGS_EQUAL(0, errors);
}

TEST(AudioHolderLoad, callback_file_path_change_is_not_overwritten_by_normalization) {
	on_lookup = [&] { clip.sampleHolder.filePath.set("new selection.wav"); };
	clip.loadSample(true);
	LONGS_EQUAL(0, assignments);
	STRCMP_EQUAL("new selection.wav", clip.sampleHolder.filePath.get());
	STRCMP_EQUAL("old name", clip.name.get());
}

TEST(AudioHolderLoad, callback_type_or_file_change_cancels_assignment) {
	for (bool change_type : {false, true}) {
		clip.sampleHolder.audioFile = nullptr;
		clip.sampleHolder.audioFileType = AudioFileType::SAMPLE;
		on_lookup = [&] {
			if (change_type)
				clip.sampleHolder.audioFileType = AudioFileType::WAVETABLE;
			else
				clip.sampleHolder.audioFile = &replacement_file;
		};
		clip.loadSample(true);
		LONGS_EQUAL(0, assignments);
		STRCMP_EQUAL("old name", clip.name.get());
	}
}

TEST(AudioHolderLoad, destroyed_output_cancels_clip_sample_publication) {
	auto owned_output = std::make_unique<Output>();
	clip.output = owned_output.get();
	on_lookup = [&] { owned_output.reset(); };
	clip.loadSample(true);
	LONGS_EQUAL(0, assignments);
	STRCMP_EQUAL("old name", clip.name.get());
}

TEST(AudioHolderLoad, storage_owner_change_cancels_and_restores_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		on_lookup = [=] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		clip.loadSample(true);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, assignments);
		STRCMP_EQUAL("old name", clip.name.get());
	}
}

TEST(AudioHolderLoad, assignment_callback_clip_destruction_does_not_publish_name) {
	auto owned_clip = std::make_unique<AudioClip>();
	owned_clip->sampleHolder.filePath.set("original.wav");
	on_assign = [&] { owned_clip.reset(); };
	owned_clip->loadSample(true);
	LONGS_EQUAL(1, assignments);
	LONGS_EQUAL(0, errors);
}

TEST(AudioHolderLoad, assignment_callback_new_selection_is_preserved) {
	on_assign = [&] {
		clip.sampleHolder.filePath.set("new selection.wav");
		clip.sampleHolder.audioFile = &replacement_file;
		clip.name.set("new name");
	};
	clip.loadSample(true);
	STRCMP_EQUAL("new name", clip.name.get());
	STRCMP_EQUAL("new selection.wav", clip.sampleHolder.filePath.get());
	POINTERS_EQUAL(&replacement_file, clip.sampleHolder.audioFile);
}

TEST(AudioHolderLoad, invalid_entry_does_not_access_storage) {
	bool valid = false;
	auto check = [&] { return valid; };
	deluge::lifetime::callback_validation validation(check);
	CHECK(clip.sampleHolder.loadFile(false, false, true, CLUSTER_ENQUEUE, nullptr, false, &validation)
	      == Error::ABORTED_BY_USER);
	clip.lifetime.retire();
	clip.loadSample(true);
	LONGS_EQUAL(0, lookups);
}

TEST(AudioHolderLoad, valid_storage_error_preserves_normalization_and_reports_error) {
	lookup_error = Error::FILE_NOT_FOUND;
	clip.loadSample(true);
	LONGS_EQUAL(0, assignments);
	LONGS_EQUAL(1, errors);
	STRCMP_EQUAL("resolved.wav", clip.name.get());
}

TEST(AudioHolderLoad, existing_file_and_empty_path_skip_storage_with_legacy_defaults) {
	clip.sampleHolder.audioFile = &loaded_file;
	CHECK(clip.sampleHolder.loadFile(false, false, true) == Error::NONE);
	clip.sampleHolder.audioFile = nullptr;
	clip.sampleHolder.filePath.set("");
	CHECK(clip.sampleHolder.loadFile(false, false, true) == Error::NONE);
	LONGS_EQUAL(0, lookups);
}
