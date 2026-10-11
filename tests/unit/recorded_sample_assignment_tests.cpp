#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/finally.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>

namespace recorded_sample_assignment_test {
namespace deluge {
namespace lifetime = ::deluge::lifetime;
namespace gui::ui_session {
using ::deluge::gui::ui_session::current;
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
std::function<void()> on_kill, on_type, on_assignment;
int assignments = 0, type_changes = 0, kills = 0;
bool cancel_assignment = false;
struct Lifetime {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
struct String {
	int value = 9;
	void set(const String* other) { value = other->value; }
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
	void setOscType(OscType value) {
		++type_changes;
		oscType = value;
		if (on_type)
			on_type();
	}
};
struct Sound : Lifetime {
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
struct Clip : Lifetime {
	void* output = nullptr;
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
	bool try_assign_recorded_sample(Sample* sample);
};
#include "recorded_sample_assignment.inc"
} // namespace recorded_sample_assignment_test
using namespace recorded_sample_assignment_test;
TEST_GROUP(RecordedSampleAssignment) {
	Sample sample;
	Sound sound;
	Clip clip;
	Song song;
	AudioRecorder recorder;
	void setup() override {
		on_kill = on_type = on_assignment = {};
		assignments = type_changes = kills = 0;
		cancel_assignment = false;
		currentSong = &song;
		song.clip = &clip;
		clip.output = &sound;
		editor = {&sound, &sound.sources[0], 0, 0, sound.sources[0].ranges.getElement(0)};
		active_ui = &screen;
		recorded_sample_assignment_test::deluge::gui::ui_session::navigation = {};
		::deluge::gui::ui_session::detail::active = ::deluge::gui::ui_session::Id::Local;
	}
	void teardown() override {
		on_kill = on_type = on_assignment = {};
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
