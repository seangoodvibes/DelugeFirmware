#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <string>
#include <string_view>
#include <vector>
namespace rename_clip_test {
namespace session = deluge::gui::ui_session;
struct name_fixture {
	std::string value = "original";
	const char* get() const { return value.c_str(); }
	Error set(std::string_view name) {
		value = name;
		return Error::NONE;
	}
};
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
namespace deluge::l10n {
enum class String { STRING_FOR_DUPLICATE_NAMES };
static const char* get(String) {
	return "Duplicate names";
}
} // namespace deluge::l10n
struct display_fixture {
	int popups = 0;
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
	}
	void teardown() override {
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
