#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace rename_clip_test {
namespace session = deluge::gui::ui_session;
static Error next_error = Error::NONE;
static std::function<void()> on_name_set;
struct name_fixture {
	std::string value = "original";
	const char* get() const { return value.c_str(); }
	void set(const name_fixture* other) { value = other->value; }
	Error set(std::string_view name) {
		value.clear();
		if (on_name_set)
			on_name_set();
		if (next_error != Error::NONE)
			return next_error;
		value = name;
		return Error::NONE;
	}
};
using String = name_fixture;
struct Clip;
struct Output {
	Output* next = nullptr;
	Clip* duplicate = nullptr;
	int lookups = 0;
	Clip* getClipFromName(std::string_view) {
		++lookups;
		return duplicate;
	}
};
struct Clip {
	Output* output = nullptr;
	name_fixture name;
};
struct clip_list_fixture {
	std::vector<Clip*> entries;
	int32_t getNumElements() const { return entries.size(); }
	Clip* getClipAtIndex(int32_t index) const { return entries[index]; }
};
struct Song {
	Output* firstOutput = nullptr;
	clip_list_fixture sessionClips, arrangementOnlyClips;
	bool contains_clip_for_undo(const Clip* clip);
};
static Song* currentSong;
namespace deluge::gui {
namespace ui_session = ::deluge::gui::ui_session;
}
namespace deluge::l10n {
enum class String { STRING_FOR_DUPLICATE_NAMES };
static const char* get(String) {
	return "Duplicate names";
}
} // namespace deluge::l10n
struct display_fixture {
	int popups = 0;
	Error error = Error::NONE;
	void displayError(Error value) { error = value; }
	void displayPopup(const char*) { ++popups; }
};
static display_fixture display_instance;
static auto* display = &display_instance;
class RenameClipUI {
public:
	Clip* clip = nullptr;
	bool canRename() const;
	std::string_view getCurrentName() const;
	bool trySetName(std::string_view);
};
#include "rename_clip_membership.inc"
#include "rename_clip_methods.inc"
} // namespace rename_clip_test
using namespace rename_clip_test;
TEST_GROUP(RenameClipTargets) {
	Song song;
	Clip clip, other;
	Output output;
	RenameClipUI menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		song.firstOutput = &output;
		song.sessionClips.entries = {&clip};
		clip.output = &output;
		menu.clip = &clip;
		display_instance = {};
		next_error = Error::NONE;
		on_name_set = {};
	}
	void teardown() override {
		on_name_set = {};
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
	}
};
TEST(RenameClipTargets, departed_clip_cannot_be_read_or_renamed_on_either_owner) {
	song.sessionClips.entries.clear();
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK(menu.getCurrentName().empty());
		CHECK_FALSE(menu.trySetName("changed"));
		STRCMP_EQUAL("original", clip.name.get());
		LONGS_EQUAL(0, output.lookups);
	}
}
TEST(RenameClipTargets, departed_output_cannot_be_used_for_duplicate_lookup) {
	song.firstOutput = nullptr;
	CHECK_FALSE(menu.trySetName("changed"));
	LONGS_EQUAL(0, output.lookups);
	STRCMP_EQUAL("original", clip.name.get());
}
TEST(RenameClipTargets, live_clip_renames_and_duplicate_is_rejected) {
	CHECK(menu.trySetName("changed"));
	STRCMP_EQUAL("changed", clip.name.get());
	output.duplicate = &other;
	CHECK_FALSE(menu.trySetName("duplicate"));
	STRCMP_EQUAL("changed", clip.name.get());
	LONGS_EQUAL(1, display_instance.popups);
}

TEST(RenameClipTargets, missing_context_rejects_and_arrangement_reattachment_allows_rename) {
	currentSong = nullptr;
	CHECK_FALSE(menu.canRename());
	CHECK_FALSE(menu.trySetName("changed"));
	currentSong = &song;
	menu.clip = nullptr;
	CHECK_FALSE(menu.canRename());
	CHECK(menu.getCurrentName().empty());
	menu.clip = &clip;
	clip.output = nullptr;
	CHECK_FALSE(menu.canRename());
	clip.output = &output;
	song.sessionClips.entries.clear();
	song.arrangementOnlyClips.entries = {&clip};
	CHECK(menu.canRename());
	CHECK(menu.trySetName("arrangement"));
	STRCMP_EQUAL("arrangement", clip.name.get());
}

TEST(RenameClipTargets, allocation_failure_preserves_original_name_and_reports_error) {
	next_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(menu.trySetName("replacement"));
	STRCMP_EQUAL("original", clip.name.get());
	CHECK(display_instance.error == Error::INSUFFICIENT_RAM);
	next_error = Error::NONE;
	CHECK(menu.trySetName("retry"));
	STRCMP_EQUAL("retry", clip.name.get());
}
TEST(RenameClipTargets, allocation_callback_departure_cancels_commit) {
	on_name_set = [&] { song.sessionClips.entries.clear(); };
	CHECK_FALSE(menu.trySetName("replacement"));
	STRCMP_EQUAL("original", clip.name.get());
}
TEST(RenameClipTargets, allocation_callback_newer_name_is_preserved) {
	on_name_set = [&] { clip.name.value = "newer"; };
	CHECK_FALSE(menu.trySetName("replacement"));
	STRCMP_EQUAL("newer", clip.name.get());
}
TEST(RenameClipTargets, allocation_context_changes_and_new_duplicates_cancel_commit) {
	Song replacement_song;
	Output replacement_output;
	output.next = &replacement_output;
	other.output = &output;
	song.sessionClips.entries.push_back(&other);
	for (int scenario = 0; scenario < 5; ++scenario) {
		currentSong = &song;
		menu.clip = &clip;
		clip.output = &output;
		output.duplicate = nullptr;
		session::detail::active = session::Id::Local;
		on_name_set = [&] {
			switch (scenario) {
			case 0:
				currentSong = &replacement_song;
				break;
			case 1:
				menu.clip = &other;
				break;
			case 2:
				clip.output = &replacement_output;
				break;
			case 3:
				session::detail::active = session::Id::Remote;
				break;
			case 4:
				output.duplicate = &other;
				break;
			}
		};
		CHECK_FALSE(menu.trySetName("replacement"));
		STRCMP_EQUAL("original", clip.name.get());
		STRCMP_EQUAL("original", other.name.get());
	}
}
