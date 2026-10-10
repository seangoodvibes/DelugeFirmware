#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/comparison.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
namespace osc_type_menu_test {
constexpr int kNumSources = 2;
enum class OscType { SINE, SQUARE, SAMPLE, WAVETABLE, DX7, INPUT_L, INPUT_R, INPUT_STEREO };
std::function<void()> on_kill, on_type;
int kills = 0, changes = 0, patches = 0;
bool cancel_type = false;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct Song : Lifetime {};
Song* currentSong = nullptr;
struct Source : Lifetime {
	OscType oscType = OscType::SQUARE;
	struct {
		unsigned revision = 0;
		unsigned layout_revision() const { return revision; }
	} ranges;
	void setOscType(OscType value) {
		++changes;
		if (!cancel_type)
			oscType = value;
		if (on_type)
			on_type();
	}
};
struct Sound : Lifetime {
	Source sources[kNumSources];
	bool voices = false;
	auto watch_sound_lifetime() { return watch_lifetime(); }
	void killAllVoices() {
		++kills;
		if (on_kill)
			on_kill();
	}
	bool hasActiveVoices() const { return voices; }
	void setupPatchingForAllParamManagers(Song*) { ++patches; }
};
struct Editor {
	Sound* currentSound = nullptr;
} editor;
Editor& sound_editor_for_session() {
	return editor;
}
int screen;
void* active_ui = &screen;
void* getCurrentUI() {
	return active_ui;
}
struct Type {
	unsigned sourceId_ = 0;
	OscType value = OscType::SAMPLE;
	bool mayUseDx() const { return true; }
	template <typename T>
	T getValue() const {
		return static_cast<T>(value);
	}
	void writeCurrentValue();
};
#include "osc_type_menu_write.inc"
} // namespace osc_type_menu_test
using namespace osc_type_menu_test;
TEST_GROUP(OscTypeMenu) {
	Song song;
	Sound sound, other;
	Type menu;
	void setup() override {
		on_kill = on_type = {};
		kills = changes = patches = 0;
		cancel_type = false;
		editor.currentSound = &sound;
		currentSong = &song;
		active_ui = &screen;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void teardown() override {
		on_kill = on_type = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
};
TEST(OscTypeMenu, successful_change_updates_selected_source_and_patching) {
	menu.sourceId_ = 1;
	menu.writeCurrentValue();
	CHECK(sound.sources[1].oscType == OscType::SAMPLE);
	CHECK(sound.sources[0].oscType == OscType::SQUARE);
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(1, patches);
}
TEST(OscTypeMenu, voice_cleanup_selection_change_does_not_edit_new_sound) {
	on_kill = [&] { editor.currentSound = &other; };
	menu.writeCurrentValue();
	LONGS_EQUAL(0, changes);
	LONGS_EQUAL(0, patches);
	CHECK(other.sources[0].oscType == OscType::SQUARE);
}
TEST(OscTypeMenu, voice_cleanup_can_destroy_original_sound) {
	auto owner = std::make_unique<Sound>();
	editor.currentSound = owner.get();
	on_kill = [&] { owner.reset(); };
	menu.writeCurrentValue();
	LONGS_EQUAL(0, changes);
	LONGS_EQUAL(0, patches);
}
TEST(OscTypeMenu, remaining_voices_cancel_type_change) {
	on_kill = [&] { sound.voices = true; };
	menu.writeCurrentValue();
	LONGS_EQUAL(0, changes);
}
TEST(OscTypeMenu, voice_cleanup_range_change_cancels_type_change) {
	on_kill = [&] { ++sound.sources[0].ranges.revision; };
	menu.writeCurrentValue();
	LONGS_EQUAL(0, changes);
}
TEST(OscTypeMenu, conversion_selection_change_does_not_patch_new_sound) {
	on_type = [&] { editor.currentSound = &other; };
	menu.writeCurrentValue();
	LONGS_EQUAL(1, changes);
	LONGS_EQUAL(0, patches);
}
TEST(OscTypeMenu, conversion_song_retirement_cancels_patching) {
	on_type = [&] { song.lifetime.retire(); };
	menu.writeCurrentValue();
	LONGS_EQUAL(0, patches);
}
TEST(OscTypeMenu, cancelled_conversion_does_not_publish_patching) {
	cancel_type = true;
	menu.writeCurrentValue();
	LONGS_EQUAL(0, patches);
}
TEST(OscTypeMenu, changed_panel_cancels_and_restores_owner) {
	on_kill = [&] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	menu.writeCurrentValue();
	LONGS_EQUAL(0, changes);
	CHECK(deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local);
}
TEST(OscTypeMenu, invalid_source_index_rejects_entry) {
	menu.sourceId_ = kNumSources;
	menu.writeCurrentValue();
	LONGS_EQUAL(0, kills);
	LONGS_EQUAL(0, changes);
}
