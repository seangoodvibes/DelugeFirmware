#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
namespace midi_follow_context_test {
namespace session = deluge::gui::ui_session;
struct Clip;
struct Output {
	Clip* active_clip = nullptr;
	Clip* getActiveClip() { return active_clip; }
	OutputType type = OutputType::SYNTH;
};
struct Clip {
	Output* output = nullptr;
	ClipType type = ClipType::AUDIO;
};
struct InstrumentClip : Clip {
	InstrumentClip() { type = ClipType::INSTRUMENT; }
	session::State<bool> affect_entire;
	bool affect_entire_for_session() { return affect_entire.active(); }
};
static session::State<Clip*> current_clips;
static Clip* getCurrentClip() {
	return current_clips.active();
}
static session::State<Clip*> selected_clips, arranger_clips;
struct RootUI {
	UIType type = UIType::SESSION;
	bool onArrangerView = false;
	UIType getUIType() { return type; }
	Clip* getClipForLayout() { return selected_clips.active(); }
	Clip* getClipForSelection() { return arranger_clips.active(); }
};
static session::State<RootUI> roots, automation;
static RootUI* getRootUI() {
	return &roots.active();
}
static RootUI& session_view_for_session() {
	return roots.active();
}
static RootUI& arranger_view_for_session() {
	return roots.active();
}
static RootUI& automation_view_for_session() {
	return automation.active();
}
struct song_fixture {
	session::State<int> positions;
	int last_clip_instance_entered_start_pos_for_session() { return positions.active(); }
};
static song_fixture song;
static song_fixture* currentSong = &song;
struct MidiFollow {
	Clip* getSelectedClip();
	Clip* getSelectedOrActiveClip();
	bool isGlobalEffectableContext();
};
#include "midi_follow_context.inc"
} // namespace midi_follow_context_test
using namespace midi_follow_context_test;
TEST_GROUP(MidiFollowContext) {
	MidiFollow follow;
	Output output;
	InstrumentClip instrument;
	Clip audio;
	void setup() override {
		session::detail::active = session::Id::Local;
		current_clips = {};
		selected_clips = {};
		arranger_clips = {};
		roots = {};
		automation = {};
		currentSong = &song;
		song.positions.for_owner(session::Id::Local) = -1;
		song.positions.for_owner(session::Id::Remote) = -1;
		instrument.output = &output;
		audio.output = &output;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(MidiFollowContext, missing_clip_or_output_is_not_global_effectable) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK_FALSE(follow.isGlobalEffectableContext());
		selected_clips.active() = &instrument;
		instrument.output = nullptr;
		CHECK_FALSE(follow.isGlobalEffectableContext());
	}
}
TEST(MidiFollowContext, kit_context_uses_each_panels_affect_entire_state) {
	output.type = OutputType::KIT;
	instrument.affect_entire.for_owner(session::Id::Remote) = true;
	selected_clips.for_owner(session::Id::Local) = &instrument;
	selected_clips.for_owner(session::Id::Remote) = &instrument;
	CHECK_FALSE(follow.isGlobalEffectableContext());
	{
		session::Scope scope(session::Id::Remote);
		CHECK(follow.isGlobalEffectableContext());
	}
	CHECK_FALSE(follow.isGlobalEffectableContext());
}
TEST(MidiFollowContext, audio_is_global_synth_is_not_and_mismatched_kit_is_safe) {
	selected_clips.active() = &audio;
	output.type = OutputType::AUDIO;
	CHECK(follow.isGlobalEffectableContext());
	output.type = OutputType::KIT;
	CHECK_FALSE(follow.isGlobalEffectableContext());
	selected_clips.active() = &instrument;
	instrument.affect_entire.active() = true;
	output.type = OutputType::SYNTH;
	CHECK_FALSE(follow.isGlobalEffectableContext());
}

TEST(MidiFollowContext, fallback_without_output_or_active_clip_returns_no_target) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		current_clips.active() = &instrument;
		instrument.output = nullptr;
		POINTERS_EQUAL(nullptr, follow.getSelectedOrActiveClip());
		instrument.output = &output;
		POINTERS_EQUAL(nullptr, follow.getSelectedOrActiveClip());
	}
}
TEST(MidiFollowContext, explicit_selection_wins_over_each_panels_active_clip_fallback) {
	Clip active_clip;
	output.active_clip = &active_clip;
	current_clips.for_owner(session::Id::Local) = &instrument;
	current_clips.for_owner(session::Id::Remote) = &instrument;
	selected_clips.for_owner(session::Id::Local) = &audio;
	POINTERS_EQUAL(&audio, follow.getSelectedOrActiveClip());
	{
		session::Scope scope(session::Id::Remote);
		POINTERS_EQUAL(&active_clip, follow.getSelectedOrActiveClip());
	}
	POINTERS_EQUAL(&audio, follow.getSelectedOrActiveClip());
}

TEST(MidiFollowContext, performance_selection_requires_song_and_arrangement_context) {
	roots.active().type = UIType::PERFORMANCE;
	arranger_clips.active() = &instrument;
	currentSong = nullptr;
	POINTERS_EQUAL(nullptr, follow.getSelectedClip());
	currentSong = &song;
	POINTERS_EQUAL(nullptr, follow.getSelectedClip());
	song.positions.active() = 0;
	POINTERS_EQUAL(&instrument, follow.getSelectedClip());
}
TEST(MidiFollowContext, selection_routes_each_view_to_its_expected_clip) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		selected_clips.active() = &audio;
		arranger_clips.active() = &instrument;
		Clip current_clip;
		current_clips.active() = &current_clip;
		roots.active().type = UIType::SESSION;
		POINTERS_EQUAL(&audio, follow.getSelectedClip());
		roots.active().type = UIType::ARRANGER;
		POINTERS_EQUAL(&instrument, follow.getSelectedClip());
		roots.active().type = UIType::AUTOMATION;
		automation.active().onArrangerView = true;
		POINTERS_EQUAL(&instrument, follow.getSelectedClip());
		automation.active().onArrangerView = false;
		POINTERS_EQUAL(&current_clip, follow.getSelectedClip());
		roots.active().type = UIType::INSTRUMENT_CLIP;
		POINTERS_EQUAL(&current_clip, follow.getSelectedClip());
	}
}
TEST(MidiFollowContext, panels_can_select_different_view_contexts) {
	roots.for_owner(session::Id::Local).type = UIType::SESSION;
	selected_clips.for_owner(session::Id::Local) = &audio;
	roots.for_owner(session::Id::Remote).type = UIType::PERFORMANCE;
	song.positions.for_owner(session::Id::Remote) = 0;
	arranger_clips.for_owner(session::Id::Remote) = &instrument;
	POINTERS_EQUAL(&audio, follow.getSelectedClip());
	{
		session::Scope scope(session::Id::Remote);
		POINTERS_EQUAL(&instrument, follow.getSelectedClip());
	}
	POINTERS_EQUAL(&audio, follow.getSelectedClip());
}
