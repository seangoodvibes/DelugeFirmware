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
	OutputType type = OutputType::SYNTH;
	name_fixture name;
	Output* next = nullptr;
	Clip* duplicate = nullptr;
	int lookups = 0;
	Clip* getClipFromName(std::string_view) {
		++lookups;
		return duplicate;
	}
};
struct Drum {
	Drum* next = nullptr;
	std::string drumName = "original";
};
struct Kit : Output {
	Kit() { type = OutputType::KIT; }
	Drum* firstDrum = nullptr;
	Drum* duplicate_drum = nullptr;
	session::State<Drum*> selected_drums;
	Drum* selected_drum_for_session() { return selected_drums.active(); }
	Drum* getDrumFromName(std::string_view) { return duplicate_drum; }
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
	session::State<Clip*> selected_clips;
	Clip* getCurrentClip() { return selected_clips.active(); }
	Output* duplicate_output = nullptr;
	int output_lookups = 0;
	Output* getAudioOutputFromName(std::string_view) {
		++output_lookups;
		return duplicate_output;
	}
	Output* firstOutput = nullptr;
	clip_list_fixture sessionClips, arrangementOnlyClips;
	bool contains_clip_for_undo(const Clip* clip);
};
static Song* currentSong;
static int current_output_lookups = 0;
static Output* getCurrentOutput() {
	++current_output_lookups;
	return currentSong->getCurrentClip()->output;
}
static Kit* getCurrentKit() {
	return static_cast<Kit*>(getCurrentOutput());
}
static int freezes = 0;

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
class RenameOutputUI {
public:
	Output* output = nullptr;
	bool canRename() const;
	std::string_view getCurrentName() const;
	bool trySetName(std::string_view);
};
class RenameDrumUI {
public:
	bool canRename() const;
	Drum* selected_drum_for_rename() const;
	std::string_view getCurrentName() const;
	bool trySetName(std::string_view);
};
#undef FREEZE_WITH_ERROR
#define FREEZE_WITH_ERROR(...) (++freezes)
#include "rename_drum_methods.inc"
#undef FREEZE_WITH_ERROR
#include "rename_clip_membership.inc"
#include "rename_clip_methods.inc"
#include "rename_output_methods.inc"
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

TEST_GROUP(RenameOutputTargets) {
	Song song;
	Output output, other;
	RenameOutputUI menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		song.firstOutput = &output;
		output.next = &other;
		menu.output = &output;
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
TEST(RenameOutputTargets, departed_output_is_unavailable_on_both_panels) {
	song.firstOutput = &other;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK(menu.getCurrentName().empty());
		CHECK_FALSE(menu.trySetName("changed"));
		STRCMP_EQUAL("original", output.name.get());
		LONGS_EQUAL(0, song.output_lookups);
	}
}
TEST(RenameOutputTargets, live_target_allows_rename_and_rejects_duplicates) {
	CHECK(menu.trySetName("changed"));
	song.duplicate_output = &other;
	CHECK_FALSE(menu.trySetName("duplicate"));
	STRCMP_EQUAL("changed", output.name.get());
	LONGS_EQUAL(1, display_instance.popups);
	song.duplicate_output = &output;
	CHECK(menu.trySetName("changed"));
}

TEST(RenameOutputTargets, missing_context_rejects_and_reattachment_allows_rename) {
	currentSong = nullptr;
	CHECK_FALSE(menu.canRename());
	CHECK_FALSE(menu.trySetName("changed"));
	CHECK(menu.getCurrentName().empty());
	currentSong = &song;
	menu.output = nullptr;
	CHECK_FALSE(menu.canRename());
	CHECK_FALSE(menu.trySetName("changed"));
	menu.output = &output;
	song.firstOutput = &other;
	CHECK_FALSE(menu.canRename());
	song.firstOutput = &output;
	CHECK(menu.canRename());
	CHECK(menu.trySetName("reattached"));
	STRCMP_EQUAL("reattached", output.name.get());
}
TEST(RenameOutputTargets, allocation_failure_preserves_name_and_allows_retry) {
	next_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(menu.trySetName("replacement"));
	STRCMP_EQUAL("original", output.name.get());
	CHECK(display_instance.error == Error::INSUFFICIENT_RAM);
	next_error = Error::NONE;
	CHECK(menu.trySetName("retry"));
	STRCMP_EQUAL("retry", output.name.get());
}
TEST(RenameOutputTargets, allocation_callback_newer_name_is_preserved) {
	on_name_set = [&] { output.name.value = "newer"; };
	CHECK_FALSE(menu.trySetName("replacement"));
	STRCMP_EQUAL("newer", output.name.get());
}
TEST(RenameOutputTargets, allocation_context_changes_cancel_commit) {
	Song replacement_song;
	for (int scenario = 0; scenario < 5; ++scenario) {
		currentSong = &song;
		song.firstOutput = &output;
		song.duplicate_output = nullptr;
		menu.output = &output;
		session::detail::active = session::Id::Local;
		on_name_set = [&] {
			switch (scenario) {
			case 0:
				currentSong = &replacement_song;
				break;
			case 1:
				menu.output = &other;
				break;
			case 2:
				song.firstOutput = &other;
				break;
			case 3:
				session::detail::active = session::Id::Remote;
				break;
			case 4:
				song.duplicate_output = &other;
				break;
			}
		};
		CHECK_FALSE(menu.trySetName("replacement"));
		STRCMP_EQUAL("original", output.name.get());
		STRCMP_EQUAL("original", other.name.get());
	}
}
TEST(RenameClipTargets, failed_allocation_does_not_report_into_changed_context) {
	for (bool change_owner : {false, true}) {
		session::detail::active = session::Id::Local;
		menu.clip = &clip;
		next_error = Error::INSUFFICIENT_RAM;
		on_name_set = [&] {
			if (change_owner)
				session::detail::active = session::Id::Remote;
			else
				menu.clip = &other;
		};
		CHECK_FALSE(menu.trySetName("replacement"));
		CHECK(display_instance.error == Error::NONE);
		STRCMP_EQUAL("original", clip.name.get());
	}
}
TEST(RenameOutputTargets, failed_allocation_does_not_report_into_changed_context) {
	for (bool change_owner : {false, true}) {
		session::detail::active = session::Id::Local;
		menu.output = &output;
		next_error = Error::INSUFFICIENT_RAM;
		on_name_set = [&] {
			if (change_owner)
				session::detail::active = session::Id::Remote;
			else
				menu.output = &other;
		};
		CHECK_FALSE(menu.trySetName("replacement"));
		CHECK(display_instance.error == Error::NONE);
		STRCMP_EQUAL("original", output.name.get());
	}
}

TEST_GROUP(RenameDrumTargets) {
	Song song;
	Clip clip;
	Kit kit;
	Drum drum, other;
	RenameDrumUI menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		song.sessionClips.entries = {&clip};
		song.firstOutput = &kit;
		clip.output = &kit;
		kit.firstDrum = &drum;
		drum.next = &other;
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			song.selected_clips.for_owner(owner) = &clip;
			kit.selected_drums.for_owner(owner) = &drum;
		}
		freezes = current_output_lookups = 0;
		display_instance = {};
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
		currentSong = nullptr;
	}
};
TEST(RenameDrumTargets, departed_selected_drum_is_not_read_or_renamed) {
	kit.firstDrum = &other;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK(menu.getCurrentName().empty());
		CHECK_FALSE(menu.trySetName("changed"));
		STRCMP_EQUAL("original", drum.drumName.c_str());
	}
	LONGS_EQUAL(0, freezes);
}
TEST(RenameDrumTargets, departed_clip_is_rejected_before_output_lookup) {
	song.sessionClips.entries.clear();
	CHECK(menu.getCurrentName().empty());
	CHECK_FALSE(menu.trySetName("changed"));
	LONGS_EQUAL(0, current_output_lookups);
}
TEST(RenameDrumTargets, selected_drums_are_independent_and_duplicates_are_rejected) {
	kit.selected_drums.for_owner(session::Id::Remote) = &other;
	CHECK(menu.trySetName("local"));
	{
		session::Scope scope(session::Id::Remote);
		CHECK(menu.trySetName("remote"));
		kit.duplicate_drum = &drum;
		CHECK_FALSE(menu.trySetName("local"));
	}
	STRCMP_EQUAL("local", drum.drumName.c_str());
	STRCMP_EQUAL("remote", other.drumName.c_str());
	LONGS_EQUAL(1, display_instance.popups);
}

TEST(RenameDrumTargets, missing_context_is_unavailable_without_freezing) {
	currentSong = nullptr;
	CHECK_FALSE(menu.canRename());
	CHECK_FALSE(menu.trySetName("changed"));
	currentSong = &song;
	song.selected_clips.active() = nullptr;
	CHECK_FALSE(menu.canRename());
	song.selected_clips.active() = &clip;
	song.firstOutput = nullptr;
	CHECK_FALSE(menu.canRename());
	song.firstOutput = &kit;
	kit.type = OutputType::SYNTH;
	CHECK_FALSE(menu.canRename());
	kit.type = OutputType::KIT;
	kit.selected_drums.active() = nullptr;
	CHECK_FALSE(menu.canRename());
	CHECK(menu.getCurrentName().empty());
	CHECK_FALSE(menu.trySetName("changed"));
	LONGS_EQUAL(0, freezes);
}
TEST(RenameDrumTargets, arrangement_clip_and_reattached_drum_allow_rename) {
	song.sessionClips.entries.clear();
	song.arrangementOnlyClips.entries = {&clip};
	kit.firstDrum = &other;
	CHECK_FALSE(menu.canRename());
	kit.firstDrum = &drum;
	CHECK(menu.canRename());
	CHECK(menu.trySetName("reattached"));
	STRCMP_EQUAL("reattached", drum.drumName.c_str());
}
