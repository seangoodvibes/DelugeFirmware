#include "CppUTest/TestHarness.h"
#include "gui/ui/recording_session.h"
#include "gui/ui/ui_session.h"
#include "util/exceptions.h"
#include "util/finally.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <optional>

namespace recorded_sample_assignment_test {
namespace deluge {
namespace lifetime = ::deluge::lifetime;
using ::deluge::exception;
namespace gui::ui_session {
using ::deluge::gui::ui_session::current;
using ::deluge::gui::ui_session::RecordingTarget;
using ::deluge::gui::ui_session::Scope;
struct Navigation {
	struct State {
		struct Refresh {
			unsigned value = 0;
			unsigned revision() const { return value; }
		} structural_refresh;
	} state;
	State& active() { return state; }
} navigation;
} // namespace gui::ui_session
} // namespace deluge
constexpr int kNumSources = 2, CLUSTER_ENQUEUE = 1;
enum class OscType { SAMPLE, WAVETABLE };
std::function<void()> on_kill, on_type, on_assignment, on_first_range;
int assignments = 0, type_changes = 0, kills = 0;
bool cancel_assignment = false, fail_first_range = false;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
enum class Error { NONE, INSUFFICIENT_RAM };
std::function<void()> on_logs, on_name, on_unique_name;
Error name_error = Error::NONE;
int log_deletions = 0, unique_names = 0, errors = 0;
::deluge::gui::ui_session::RecordingSession recording_session;
enum class AudioInputChannel { NONE, INPUT };
AudioInputChannel recordingSource = AudioInputChannel::NONE;
struct ActionLogger {
	void deleteAllLogs() {
		++log_deletions;
		if (on_logs)
			on_logs();
	}
} actionLogger;
struct Display {
	void displayError(Error) { ++errors; }
} display_instance;
auto* display = &display_instance;
struct String {
	int value = 9;
	void set(const String* other) { value = other->value; }
	Error set(const char*) {
		if (on_name)
			on_name();
		return name_error;
	}
	const char* get() { return "REC1"; }
};
struct Sample {
	String filePath;
	int reasons = 1;
	void addReason() { ++reasons; }
	void removeReason(const char*) { --reasons; }
};
struct Holder {
	String filePath;
	Sample* sample = nullptr;
	~Holder() {
		if (sample)
			sample->removeReason("test");
	}
	bool setAudioFile(Sample* value, bool, bool, int, const deluge::lifetime::callback_validation* validation) {
		if (on_assignment)
			on_assignment();
		if (!validation->valid() || cancel_assignment)
			return false;
		++assignments;
		if (sample)
			sample->removeReason("test");
		sample = value;
		sample->addReason();
		return true;
	}
};
struct MultiRange {
	Holder holder;
	Holder* getAudioFileHolder() { return &holder; }
};
using MultisampleRange = MultiRange;
struct Source : Lifetime {
	OscType oscType = OscType::SAMPLE;
	struct Ranges {
		std::unique_ptr<MultiRange> range = std::make_unique<MultiRange>();
		unsigned revision = 0;
		unsigned layout_revision() const { return revision; }
		int getNumElements() const { return range ? 1 : 0; }
		MultiRange* getElement(int) { return range.get(); }
		void replace() {
			range = std::make_unique<MultiRange>();
			++revision;
		}
	} ranges;
	struct Controls {
		bool reversed = false;
		bool isCurrentlyReversed() const { return reversed; }
	} sampleControls;
	MultiRange* getOrCreateFirstRange() {
		if (on_first_range)
			on_first_range();
		if (fail_first_range)
			return nullptr;
		ranges.replace();
		return ranges.getElement(0);
	}
	void setOscType(OscType value) {
		++type_changes;
		oscType = value;
		if (on_type)
			on_type();
	}
};
struct Kit;
struct Sound : Lifetime {
	Kit* kit = nullptr;
	std::string drumName = "Original";
	Source sources[kNumSources];
	bool voices = false;
	~Sound() { lifetime.retire(); }
	auto watch_sound_lifetime() { return watch_lifetime(); }
	void killAllVoices() {
		++kills;
		if (on_kill)
			on_kill();
	}
	bool hasActiveVoices() const { return voices; }
};
using SoundDrum = Sound;
struct Kit : Sound {
	bool contains_drum = true;
	int getDrumIndex(Sound*) { return contains_drum ? 0 : -1; }
	Error makeDrumNameUnique(String*, int) {
		++unique_names;
		if (on_unique_name)
			on_unique_name();
		return name_error;
	}
};
enum class OutputType { SYNTH, KIT };
OutputType output_type = OutputType::SYNTH;
Kit* active_kit = nullptr;
OutputType getCurrentOutputType() {
	return output_type;
}
Kit* getCurrentKit() {
	return active_kit;
}
struct Clip : Lifetime {
	Sound* output = nullptr;
};
struct Song : Lifetime {
	Clip* clip = nullptr;
	Clip* getCurrentClip() { return clip; }
};
Song* currentSong = nullptr;
struct Editor {
	Sound* currentSound = nullptr;
	Source* currentSource = nullptr;
	int currentSourceIndex = 0, currentMultiRangeIndex = 0;
	MultiRange* currentMultiRange = nullptr;
} editor;
Editor& sound_editor_for_session() {
	return editor;
}
int screen;
void* active_ui = &screen;
void* getCurrentUI() {
	return active_ui;
}
struct AudioRecorder {
	deluge::gui::ui_session::RecordingTarget target;
	deluge::lifetime::lifetime_watch target_song_lifetime_, target_clip_lifetime_, target_output_lifetime_;
	deluge::lifetime::lifetime_watch target_sound_lifetime_, target_source_lifetime_;
	Clip* target_clip_ = nullptr;
	Sound* target_output_ = nullptr;
	int32_t target_source_index_ = -1, target_range_index_ = -1;
	uint32_t target_range_layout_ = 0;
	::deluge::gui::ui_session::Id target_owner_ = ::deluge::gui::ui_session::Id::Local;
	bool capture_recording_target();
	bool prepare_recording_target();
	bool recording_target_current() const;
	bool try_assign_recorded_sample(Sample* sample);
};
#include "recorded_sample_assignment.inc"

#include "recording_target_lifetime.inc"

#include "recording_prepare.inc"
} // namespace recorded_sample_assignment_test
using namespace recorded_sample_assignment_test;
TEST_GROUP(RecordedSampleAssignment) {
	Sample sample;
	Sound sound;
	Clip clip;
	Song song;
	AudioRecorder recorder;
	void setup() override {
		on_kill = on_type = on_assignment = on_first_range = {};
		assignments = type_changes = kills = 0;
		cancel_assignment = fail_first_range = false;
		on_logs = on_name = on_unique_name = {};
		name_error = Error::NONE;
		log_deletions = unique_names = errors = 0;
		recording_session = {};
		recordingSource = AudioInputChannel::NONE;
		output_type = OutputType::SYNTH;
		active_kit = nullptr;
		currentSong = &song;
		song.clip = &clip;
		clip.output = &sound;
		editor = {&sound, &sound.sources[0], 0, 0, sound.sources[0].ranges.getElement(0)};
		active_ui = &screen;
		recorded_sample_assignment_test::deluge::gui::ui_session::navigation = {};
		::deluge::gui::ui_session::detail::active = ::deluge::gui::ui_session::Id::Local;
	}
	void teardown() override {
		on_logs = on_name = on_unique_name = {};
		recording_session.release();
		on_kill = on_type = on_assignment = on_first_range = {};
		::deluge::gui::ui_session::detail::active = ::deluge::gui::ui_session::Id::Local;
	}
};
TEST(RecordedSampleAssignment, successful_attachment_transfers_reference_to_holder) {
	CHECK_TRUE(recorder.try_assign_recorded_sample(&sample));
	POINTERS_EQUAL(&sample, editor.currentMultiRange->holder.sample);
	LONGS_EQUAL(2, sample.reasons);
	LONGS_EQUAL(1, assignments);
}
TEST(RecordedSampleAssignment, voice_cleanup_selection_change_cancels_conversion) {
	Sound replacement;
	on_kill = [&] { editor.currentSound = &replacement; };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, type_changes);
	LONGS_EQUAL(1, sample.reasons);
}
TEST(RecordedSampleAssignment, destroyed_sound_during_voice_cleanup_stops_attachment) {
	auto owner = std::make_unique<Sound>();
	editor = {owner.get(), &owner->sources[0], 0, 0, owner->sources[0].ranges.getElement(0)};
	on_kill = [&] { owner.reset(); };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, type_changes);
	LONGS_EQUAL(1, sample.reasons);
}
TEST(RecordedSampleAssignment, conversion_replacement_uses_new_selected_range) {
	sound.sources[0].oscType = OscType::WAVETABLE;
	on_type = [&] {
		sound.sources[0].ranges.replace();
		editor.currentMultiRange = sound.sources[0].ranges.getElement(0);
	};
	CHECK_TRUE(recorder.try_assign_recorded_sample(&sample));
	POINTERS_EQUAL(&sample, editor.currentMultiRange->holder.sample);
}
TEST(RecordedSampleAssignment, stale_selection_after_conversion_is_rejected) {
	on_type = [&] { sound.sources[0].ranges.replace(); };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, assignments);
	LONGS_EQUAL(1, sample.reasons);
}
TEST(RecordedSampleAssignment, range_destruction_during_assignment_cancels_before_holder_access) {
	on_assignment = [&] { sound.sources[0].ranges.replace(); };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, assignments);
	LONGS_EQUAL(1, sample.reasons);
}
TEST(RecordedSampleAssignment, song_retirement_during_conversion_stops_assignment) {
	on_type = [&] { song.lifetime.retire(); };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, assignments);
}
TEST(RecordedSampleAssignment, structural_invalidation_during_voice_cleanup_stops_conversion) {
	on_kill = [&] {
		++recorded_sample_assignment_test::deluge::gui::ui_session::navigation.state.structural_refresh.value;
	};
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, type_changes);
}
TEST(RecordedSampleAssignment, remaining_voices_prevent_assignment) {
	on_kill = [&] { sound.voices = true; };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, type_changes);
}
TEST(RecordedSampleAssignment, rejected_holder_assignment_releases_temporary_pin) {
	cancel_assignment = true;
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(1, sample.reasons);
}
TEST(RecordedSampleAssignment, invalid_range_rejects_entry_without_voice_cleanup) {
	editor.currentMultiRangeIndex = -1;
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, kills);
	LONGS_EQUAL(1, sample.reasons);
}

TEST(RecordedSampleAssignment, captured_recording_target_keeps_independent_lifetime_observers) {
	CHECK_TRUE(recorder.capture_recording_target());
	CHECK_TRUE(recorder.recording_target_current());
	sound.sources[0].lifetime.retire();
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, reconstructed_song_at_same_address_does_not_revive_recording_target) {
	std::optional<Song> owner(std::in_place);
	owner->clip = &clip;
	currentSong = &*owner;
	CHECK_TRUE(recorder.capture_recording_target());
	auto* original = currentSong;
	owner.reset();
	owner.emplace();
	owner->clip = &clip;
	currentSong = &*owner;
	POINTERS_EQUAL(original, currentSong);
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, reconstructed_clip_at_same_address_does_not_revive_recording_target) {
	std::optional<Clip> owner(std::in_place);
	owner->output = &sound;
	song.clip = &*owner;
	CHECK_TRUE(recorder.capture_recording_target());
	auto* original = song.clip;
	owner.reset();
	owner.emplace();
	owner->output = &sound;
	song.clip = &*owner;
	POINTERS_EQUAL(original, song.clip);
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, retired_output_cancels_recording_target_even_when_sound_remains_alive) {
	Sound output;
	clip.output = &output;
	CHECK_TRUE(recorder.capture_recording_target());
	output.lifetime.retire();
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, changed_range_layout_cancels_long_lived_recording_target) {
	CHECK_TRUE(recorder.capture_recording_target());
	++sound.sources[0].ranges.revision;
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, changed_panel_cannot_use_another_panels_recording_target) {
	CHECK_TRUE(recorder.capture_recording_target());
	::deluge::gui::ui_session::Scope remote(::deluge::gui::ui_session::Id::Remote);
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, failed_target_capture_clears_previous_watches) {
	CHECK_TRUE(recorder.capture_recording_target());
	currentSong = nullptr;
	CHECK_FALSE(recorder.capture_recording_target());
	currentSong = &song;
	CHECK_FALSE(recorder.recording_target_current());
}
TEST(RecordedSampleAssignment, capturing_new_recording_replaces_previous_target_observers) {
	CHECK_TRUE(recorder.capture_recording_target());
	Song replacement;
	replacement.clip = &clip;
	currentSong = &replacement;
	CHECK_TRUE(recorder.capture_recording_target());
	song.lifetime.retire();
	CHECK_TRUE(recorder.recording_target_current());
}

TEST(RecordedSampleAssignment, empty_source_is_a_valid_recording_target) {
	sound.sources[0].ranges.range.reset();
	editor.currentMultiRange = nullptr;
	CHECK_TRUE(recorder.capture_recording_target());
	CHECK_TRUE(recorder.recording_target_current());
	CHECK_TRUE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(1, assignments);
	POINTERS_EQUAL(sound.sources[0].ranges.getElement(0), editor.currentMultiRange);
}
TEST(RecordedSampleAssignment, empty_source_allocation_failure_does_not_attach_sample) {
	sound.sources[0].ranges.range.reset();
	editor.currentMultiRange = nullptr;
	fail_first_range = true;
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, assignments);
	LONGS_EQUAL(1, sample.reasons);
	POINTERS_EQUAL(nullptr, editor.currentMultiRange);
}
TEST(RecordedSampleAssignment, first_range_allocation_navigation_cannot_publish_editor_selection) {
	sound.sources[0].ranges.range.reset();
	editor.currentMultiRange = nullptr;
	on_first_range = [&] { currentSong = nullptr; };
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, assignments);
	LONGS_EQUAL(1, sample.reasons);
	POINTERS_EQUAL(nullptr, editor.currentMultiRange);
}
TEST(RecordedSampleAssignment, empty_source_with_stale_range_selection_is_rejected) {
	sound.sources[0].ranges.range.reset();
	CHECK_FALSE(recorder.capture_recording_target());
	CHECK_FALSE(recorder.try_assign_recorded_sample(&sample));
	LONGS_EQUAL(0, type_changes);
}

TEST(RecordedSampleAssignment, preparation_stops_after_log_cleanup_retires_target) {
	on_logs = [&] { sound.lifetime.retire(); };
	CHECK_FALSE(recorder.prepare_recording_target());
	LONGS_EQUAL(1, log_deletions);
	LONGS_EQUAL(0, unique_names);
}
TEST(RecordedSampleAssignment, preparation_rejects_session_started_and_finished_during_log_cleanup) {
	on_logs = [&] {
		CHECK_TRUE(recording_session.acquire());
		recording_session.release();
	};
	CHECK_FALSE(recorder.prepare_recording_target());
}
TEST(RecordedSampleAssignment, preparation_rejects_navigation_during_log_cleanup) {
	int other_screen;
	on_logs = [&] { active_ui = &other_screen; };
	CHECK_FALSE(recorder.prepare_recording_target());
}
TEST(RecordedSampleAssignment, occupied_session_does_not_delete_logs) {
	CHECK_TRUE(recording_session.acquire());
	CHECK_FALSE(recorder.prepare_recording_target());
	LONGS_EQUAL(0, log_deletions);
}
TEST(RecordedSampleAssignment, kit_preparation_publishes_name_after_validation) {
	Kit kit;
	active_kit = sound.kit = &kit;
	clip.output = &kit;
	output_type = OutputType::KIT;
	CHECK_TRUE(recorder.prepare_recording_target());
	STRCMP_EQUAL("REC1", sound.drumName.c_str());
	LONGS_EQUAL(1, unique_names);
}
TEST(RecordedSampleAssignment, name_allocation_retirement_does_not_call_kit) {
	Kit kit;
	active_kit = sound.kit = &kit;
	clip.output = &kit;
	output_type = OutputType::KIT;
	on_name = [&] { kit.lifetime.retire(); };
	CHECK_FALSE(recorder.prepare_recording_target());
	LONGS_EQUAL(0, unique_names);
	STRCMP_EQUAL("Original", sound.drumName.c_str());
}
TEST(RecordedSampleAssignment, unique_name_callback_removed_drum_preserves_original_name) {
	Kit kit;
	active_kit = sound.kit = &kit;
	clip.output = &kit;
	output_type = OutputType::KIT;
	on_unique_name = [&] { kit.contains_drum = false; };
	CHECK_FALSE(recorder.prepare_recording_target());
	STRCMP_EQUAL("Original", sound.drumName.c_str());
}
TEST(RecordedSampleAssignment, kit_name_allocation_failure_preserves_original_name) {
	Kit kit;
	active_kit = sound.kit = &kit;
	clip.output = &kit;
	output_type = OutputType::KIT;
	name_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(recorder.prepare_recording_target());
	LONGS_EQUAL(1, errors);
	LONGS_EQUAL(0, unique_names);
	STRCMP_EQUAL("Original", sound.drumName.c_str());
}
