#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
namespace midi_follow_context_test {
namespace session = deluge::gui::ui_session;
struct Clip;
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	Output* next = nullptr;
	Clip* active_clip = nullptr;
	Clip* getActiveClip() { return active_clip; }
	OutputType type = OutputType::SYNTH;
};
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
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
	Output* firstOutput = nullptr;
	session::State<int> positions;
	int last_clip_instance_entered_start_pos_for_session() { return positions.active(); }
};
static song_fixture song;
static song_fixture* currentSong = &song;
struct ModelStack {};
static std::function<void()> on_activation;
static int activation_calls = 0;
struct InstrumentClipMinder {
	static bool makeCurrentClipActiveOnInstrumentIfPossible(ModelStack*) {
		++activation_calls;
		if (on_activation)
			on_activation();
		return true;
	}
};
struct MidiFollow {
	Clip* getSelectedClip();
	Clip* getActiveClip(ModelStack*);
	const size_t getTrackCount() const;
	Output* getTrackFromIndex(uint32_t, uint32_t);
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
		on_activation = {};
		activation_calls = 0;
		current_clips = {};
		selected_clips = {};
		arranger_clips = {};
		roots = {};
		automation = {};
		currentSong = &song;
		song.firstOutput = nullptr;
		song.positions.for_owner(session::Id::Local) = -1;
		song.positions.for_owner(session::Id::Remote) = -1;
		instrument.output = &output;
		audio.output = &output;
	}
	void teardown() override {
		on_activation = {};
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
	active_clip.output = &output;
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

TEST(MidiFollowContext, empty_track_range_rejects_wrapped_index) {
	song.firstOutput = &output;
	output.active_clip = &instrument;
	POINTERS_EQUAL(nullptr, follow.getTrackFromIndex(UINT32_MAX, 0));
}
TEST(MidiFollowContext, missing_song_and_empty_list_have_no_tracks) {
	currentSong = nullptr;
	LONGS_EQUAL(0, follow.getTrackCount());
	POINTERS_EQUAL(nullptr, follow.getTrackFromIndex(0, 1));
	currentSong = &song;
	LONGS_EQUAL(0, follow.getTrackCount());
	POINTERS_EQUAL(nullptr, follow.getTrackFromIndex(0, 1));
}
TEST(MidiFollowContext, track_lookup_reverses_active_outputs_and_skips_inactive_ones) {
	Output middle, last;
	song.firstOutput = &output;
	output.next = &middle;
	middle.next = &last;
	output.active_clip = &instrument;
	last.active_clip = &audio;
	audio.output = &last;
	LONGS_EQUAL(2, follow.getTrackCount());
	POINTERS_EQUAL(&last, follow.getTrackFromIndex(0, 2));
	POINTERS_EQUAL(&output, follow.getTrackFromIndex(1, 2));
	POINTERS_EQUAL(nullptr, follow.getTrackFromIndex(2, 2));
	POINTERS_EQUAL(nullptr, follow.getTrackFromIndex(UINT32_MAX, 2));
}

TEST(MidiFollowContext, activation_cannot_return_target_after_song_replacement) {
	ModelStack stack;
	current_clips.active() = &instrument;
	output.active_clip = &instrument;
	on_activation = [] { currentSong = nullptr; };
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
}
TEST(MidiFollowContext, activation_owner_change_restores_panel_without_target) {
	ModelStack stack;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		current_clips.active() = &instrument;
		output.active_clip = &instrument;
		on_activation = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
		CHECK(session::current() == owner);
	}
}
TEST(MidiFollowContext, activation_rejects_replaced_clip_or_output) {
	ModelStack stack;
	Output replacement;
	replacement.active_clip = &audio;
	for (bool replace_output : {false, true}) {
		current_clips.active() = &instrument;
		instrument.output = &output;
		output.active_clip = &instrument;
		on_activation = [&] {
			if (replace_output)
				instrument.output = &replacement;
			else
				current_clips.active() = &audio;
		};
		POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	}
}
TEST(MidiFollowContext, activation_requires_targets_and_returns_normal_active_clip) {
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	current_clips.active() = &audio;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	current_clips.active() = &instrument;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(nullptr));
	instrument.output = nullptr;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	LONGS_EQUAL(0, activation_calls);
	instrument.output = &output;
	output.active_clip = &instrument;
	POINTERS_EQUAL(&instrument, follow.getActiveClip(&stack));
	LONGS_EQUAL(1, activation_calls);
}

TEST(MidiFollowContext, selected_outputless_clip_is_not_a_midi_target) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		selected_clips.active() = &instrument;
		instrument.output = nullptr;
		POINTERS_EQUAL(nullptr, follow.getSelectedOrActiveClip());
	}
}
TEST(MidiFollowContext, outputless_active_clip_is_not_a_midi_target) {
	Clip detached;
	current_clips.active() = &instrument;
	output.active_clip = &detached;
	POINTERS_EQUAL(nullptr, follow.getSelectedOrActiveClip());
	detached.output = &output;
	POINTERS_EQUAL(&detached, follow.getSelectedOrActiveClip());
}

TEST(MidiFollowContext, fallback_rejects_clip_assigned_to_another_output) {
	Output other_output;
	Clip moved_clip;
	moved_clip.output = &other_output;
	current_clips.active() = &instrument;
	output.active_clip = &moved_clip;
	POINTERS_EQUAL(nullptr, follow.getSelectedOrActiveClip());
}
TEST(MidiFollowContext, activated_output_cannot_route_to_detached_or_moved_clip) {
	ModelStack stack;
	Output other_output;
	Clip moved_clip;
	current_clips.active() = &instrument;
	output.active_clip = &moved_clip;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	moved_clip.output = &other_output;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	moved_clip.output = &output;
	POINTERS_EQUAL(&moved_clip, follow.getActiveClip(&stack));
}

TEST(MidiFollowContext, track_enumeration_skips_detached_and_reassigned_active_clips) {
	Output valid_output, other_output;
	Clip valid_clip;
	valid_clip.output = &valid_output;
	valid_output.active_clip = &valid_clip;
	song.firstOutput = &output;
	output.next = &valid_output;
	output.active_clip = &instrument;
	for (auto* association : {static_cast<Output*>(nullptr), &other_output}) {
		instrument.output = association;
		LONGS_EQUAL(1, follow.getTrackCount());
		POINTERS_EQUAL(&valid_output, follow.getTrackFromIndex(0, 1));
		POINTERS_EQUAL(nullptr, follow.getTrackFromIndex(1, 1));
	}
	instrument.output = &output;
	LONGS_EQUAL(2, follow.getTrackCount());
	POINTERS_EQUAL(&output, follow.getTrackFromIndex(1, 2));
}

TEST(MidiFollowContext, activation_destroying_source_clip_does_not_return_active_target) {
	auto* target = new InstrumentClip;
	target->output = &output;
	current_clips.active() = target;
	output.active_clip = target;
	on_activation = [&] { delete target; };
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
}
TEST(MidiFollowContext, activation_reusing_source_address_does_not_return_replacement) {
	auto* target = new InstrumentClip;
	target->output = &output;
	current_clips.active() = target;
	output.active_clip = target;
	on_activation = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
		target->output = &output;
	};
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	delete target;
}
TEST(MidiFollowContext, retiring_active_clip_is_not_returned) {
	current_clips.active() = &instrument;
	output.active_clip = &audio;
	audio.lifetime_source.retire();
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
}

TEST(MidiFollowContext, output_destroyed_during_activation_cancels_lookup) {
	auto* target = new Output;
	instrument.output = target;
	target->active_clip = &instrument;
	current_clips.active() = &instrument;
	on_activation = [&] { delete target; };
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
}
TEST(MidiFollowContext, output_reused_during_activation_is_not_returned) {
	auto* target = new Output;
	instrument.output = target;
	current_clips.active() = &instrument;
	on_activation = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
		target->active_clip = &instrument;
	};
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	delete target;
}
TEST(MidiFollowContext, retired_output_prevents_activation_lookup) {
	current_clips.active() = &instrument;
	output.lifetime_source.retire();
	ModelStack stack;
	POINTERS_EQUAL(nullptr, follow.getActiveClip(&stack));
	LONGS_EQUAL(0, activation_calls);
}
