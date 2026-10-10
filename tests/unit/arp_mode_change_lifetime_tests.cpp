#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
namespace arp_mode_change_test {
enum class ArpMode { OFF, ON };
enum class ArpPreset { OFF, UP, DOWN };
constexpr int MODEL_STACK_MAX_SIZE = 128, UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR = 1;
int currentUIMode = 0;
int song;
int* currentSong = &song;
struct ArpeggiatorSettings {
	ArpMode mode = ArpMode::ON;
	ArpPreset preset = ArpPreset::UP;
	bool flagForceArpRestart = false;
	void updatePresetFromCurrentSettings() { preset = mode == ArpMode::OFF ? ArpPreset::OFF : ArpPreset::UP; }
	void updateSettingsFromCurrentPreset() { mode = preset == ArpPreset::OFF ? ArpMode::OFF : ArpMode::ON; }
};
struct Owner {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
};
std::function<void()> on_stop, on_reassess, on_stack;
int stops, reassessments;
void stop() {
	++stops;
	if (on_stop)
		on_stop();
}
struct Drum : Owner {
	Drum* next = nullptr;
	ArpeggiatorSettings arpSettings;
	void killAllVoices() { stop(); }
};
struct Output : Owner {
	virtual ~Output() = default;
};
struct Kit : Output {
	Drum* selected = nullptr;
	Drum* firstDrum = nullptr;
	Drum* selected_drum_for_session() { return selected; }
	int getDrumIndex(Drum* target) {
		int i = 0;
		for (auto* d = firstDrum; d; d = d->next, ++i)
			if (d == target)
				return i;
		return -1;
	}
	void cutAllSound() { stop(); }
};
struct ModelStack {
	ModelStack* toWithTimelineCounter() { return this; }
	ModelStack* addSoundFlags() { return this; }
} stack;
struct Clip : Owner {
	Output* output = nullptr;
	bool active = true;
	bool isActiveOnOutput() { return active; }
	void stopAllNotesForMIDIOrCV(ModelStack*) { stop(); }
};
struct Sound {
	ArpeggiatorSettings settings;
	int arp;
	bool stop_result = true;
	int* getArp() { return &arp; }
	bool allNotesOff(ModelStack*, int*, const deluge::lifetime::callback_validation* validation) {
		const bool result = stop_result;
		stop();
		return validation->valid() && result;
	}
	void reassessRenderSkippingStatus(ModelStack*) {
		++reassessments;
		if (on_reassess)
			on_reassess();
	}
};
struct Editor {
	ArpeggiatorSettings* currentArpSettings = nullptr;
	Sound* currentSound = nullptr;
	int* currentParamManager = nullptr;
	bool kit = false, entire = false, midi = false;
	bool editingKit() { return kit; }
	bool editingKitRow() { return kit && !entire; }
	bool editingKitAffectEntire() { return kit && entire; }
	bool editingCVOrMIDIClip() { return midi; }
	ModelStack* getCurrentModelStack(char*) {
		if (on_stack)
			on_stack();
		return &stack;
	}
} editor;
Clip* current_clip = nullptr;
Clip* getCurrentClip() {
	return current_clip;
}
Clip* getCurrentInstrumentClip() {
	return current_clip;
}
Editor& sound_editor_for_session() {
	return editor;
}
} // namespace arp_mode_change_test
namespace arp_mode_change_test {
#include "arp_mode_change_lifetime.inc"
}
using namespace arp_mode_change_test;
TEST_GROUP(arp_mode_change_lifetime) {
	std::unique_ptr<Clip> clip;
	std::unique_ptr<Kit> output;
	std::unique_ptr<Drum> drum;
	std::unique_ptr<Sound> sound;
	void setup() override {
		clip = std::make_unique<Clip>();
		output = std::make_unique<Kit>();
		drum = std::make_unique<Drum>();
		sound = std::make_unique<Sound>();
		clip->output = output.get();
		current_clip = clip.get();
		output->selected = output->firstDrum = drum.get();
		editor = {};
		editor.currentSound = sound.get();
		editor.currentArpSettings = &sound->settings;
		currentSong = &song;
		currentUIMode = 0;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
		stops = reassessments = 0;
		on_stop = on_reassess = on_stack = {};
	}
	void teardown() override {
		on_stop = on_reassess = on_stack = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void apply(bool preset = false) {
		apply_mode_change(ArpMode::OFF, ArpPreset::OFF, preset);
	}
	void kit() {
		editor.kit = true;
		editor.currentSound = nullptr;
		editor.currentArpSettings = &drum->arpSettings;
	}
};
TEST(arp_mode_change_lifetime, synth_mode_and_preset_keep_existing_behavior) {
	apply();
	CHECK(sound->settings.mode == ArpMode::OFF);
	LONGS_EQUAL(1, stops);
	LONGS_EQUAL(1, reassessments);
	apply(true);
	CHECK(sound->settings.preset == ArpPreset::OFF);
	CHECK(sound->settings.flagForceArpRestart);
}
TEST(arp_mode_change_lifetime, cancelled_sound_stop_does_not_reassess_or_write) {
	sound->stop_result = false;
	apply();
	LONGS_EQUAL(0, reassessments);
	CHECK(sound->settings.mode == ArpMode::ON);
}
TEST(arp_mode_change_lifetime, deleted_clip_cancels_after_note_stop) {
	on_stop = [&] { clip.reset(); };
	apply();
	LONGS_EQUAL(0, reassessments);
	CHECK(sound->settings.mode == ArpMode::ON);
}
TEST(arp_mode_change_lifetime, deleted_output_and_sound_cancel_before_reassessment) {
	on_stop = [&] {
		output.reset();
		sound.reset();
	};
	apply();
	LONGS_EQUAL(0, reassessments);
}
TEST(arp_mode_change_lifetime, deleted_sound_at_reassessment_cancels_before_settings_access) {
	on_reassess = [&] {
		output.reset();
		sound.reset();
	};
	apply(true);
	LONGS_EQUAL(1, reassessments);
}
TEST(arp_mode_change_lifetime, editor_retarget_preserves_new_settings) {
	ArpeggiatorSettings replacement;
	on_stop = [&] { editor.currentArpSettings = &replacement; };
	apply(true);
	CHECK(replacement.preset == ArpPreset::UP);
	CHECK(sound->settings.mode == ArpMode::ON);
	LONGS_EQUAL(0, reassessments);
}
TEST(arp_mode_change_lifetime, session_switch_cancels_settings_write) {
	on_stop = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	apply();
	CHECK(sound->settings.mode == ArpMode::ON);
}
TEST(arp_mode_change_lifetime, reentrant_setting_change_is_preserved) {
	on_stop = [&] { sound->settings.preset = ArpPreset::DOWN; };
	apply(true);
	CHECK(sound->settings.preset == ArpPreset::DOWN);
}
TEST(arp_mode_change_lifetime, drum_deletion_during_stop_cancels_settings_access) {
	kit();
	on_stop = [&] { drum.reset(); };
	apply();
	LONGS_EQUAL(1, stops);
}
TEST(arp_mode_change_lifetime, detached_drum_is_not_updated) {
	kit();
	on_stop = [&] { output->firstDrum = nullptr; };
	apply();
	CHECK(drum->arpSettings.mode == ArpMode::ON);
}
TEST(arp_mode_change_lifetime, whole_kit_applies_preset_to_all_drums) {
	kit();
	Drum second;
	drum->next = &second;
	currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
	apply(true);
	LONGS_EQUAL(1, stops);
	CHECK(drum->arpSettings.preset == ArpPreset::OFF);
	CHECK(second.arpSettings.preset == ArpPreset::OFF);
	CHECK(second.arpSettings.flagForceArpRestart);
}
TEST(arp_mode_change_lifetime, whole_kit_deletion_during_cut_cancels_traversal) {
	kit();
	currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
	on_stop = [&] {
		drum.reset();
		output.reset();
	};
	apply(true);
	LONGS_EQUAL(1, stops);
}
TEST(arp_mode_change_lifetime, midi_stop_retarget_cancels_write) {
	editor.midi = true;
	on_stop = [&] { current_clip = nullptr; };
	apply();
	CHECK(sound->settings.mode == ArpMode::ON);
}
TEST(arp_mode_change_lifetime, inactive_clip_updates_without_stopping) {
	clip->active = false;
	apply();
	LONGS_EQUAL(0, stops);
	CHECK(sound->settings.mode == ArpMode::OFF);
}
TEST(arp_mode_change_lifetime, retired_entry_is_rejected) {
	output->lifetime.retire();
	apply();
	LONGS_EQUAL(0, stops);
	CHECK(sound->settings.mode == ArpMode::ON);
}
TEST(arp_mode_change_lifetime, stack_callback_cancellation_prevents_stop) {
	on_stack = [&] { clip.reset(); };
	apply();
	LONGS_EQUAL(0, stops);
}

TEST(arp_mode_change_lifetime, parameter_context_change_cancels_write) {
	int replacement;
	on_stop = [&] { editor.currentParamManager = &replacement; };
	apply();
	CHECK(sound->settings.mode == ArpMode::ON);
	LONGS_EQUAL(0, reassessments);
}
TEST(arp_mode_change_lifetime, retired_drum_entry_is_rejected) {
	kit();
	drum->lifetime.retire();
	apply();
	LONGS_EQUAL(0, stops);
}
