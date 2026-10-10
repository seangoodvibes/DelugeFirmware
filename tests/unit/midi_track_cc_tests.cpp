#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
namespace midi_track_cc_test {
namespace panels = deluge::gui::ui_session;
constexpr int MIDI_CC_MUTE = 89, MIDI_CC_SOLO = 90;
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MASTER };
struct MIDICable {};
struct Clip;
struct ModelStackWithTimelineCounter {};
struct ModelStack {
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip*) { return &timeline; }
};
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	Output* next = nullptr;
	Clip* active_clip = nullptr;
	OutputType type = OutputType::SYNTH;
	Clip* getActiveClip() { return active_clip; }
};
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	Output* output = nullptr;
	ClipType type = ClipType::INSTRUMENT;
};
static Clip* current_clip = nullptr;
static Clip* getCurrentClip() {
	return current_clip;
}
static int instrument_calls = 0;
static std::function<void()> on_expression;
struct Kit : Output {
	template <class... Args>
	void receivedPitchBendForKit(Args&&...) {
		++instrument_calls;
		if (on_expression)
			on_expression();
	}
	template <class... Args>
	void receivedAftertouchForKit(Args&&...) {
		++instrument_calls;
		if (on_expression)
			on_expression();
	}
	template <class... Args>
	void receivedCCForKit(Args&&...) {
		++instrument_calls;
	}
};
struct MelodicInstrument : Output {
	template <class... Args>
	void receivedPitchBend(Args&&...) {
		++instrument_calls;
		if (on_expression)
			on_expression();
	}
	template <class... Args>
	void receivedAftertouch(Args&&...) {
		++instrument_calls;
		if (on_expression)
			on_expression();
	}
	template <class... Args>
	void receivedCC(Args&&...) {
		++instrument_calls;
	}
};
struct song_fixture {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	Output* firstOutput = nullptr;
	bool owns_output_for_undo(const Output* target, bool) const {
		for (auto* output = firstOutput; output; output = output->next) {
			if (output == target)
				return true;
		}
		return false;
	}
};
static song_fixture song;
static song_fixture* currentSong = &song;
static struct {
	int mute_calls = 0, solo_calls = 0;
	template <class... Args>
	void toggleClipStatus(Args&&...) {
		++mute_calls;
	}
	template <class... Args>
	void soloClipAction(Args&&...) {
		++solo_calls;
	}
} session;
namespace Buttons {
static bool isShiftButtonPressed() {
	return false;
}
} // namespace Buttons
namespace AudioEngine {
static uint32_t audioSampleTimer = 0;
}
static struct {
	bool midiFollowFeedbackFilter = false;
} midiEngine;
struct MidiFollow {
	void pitchBendReceivedForSpecificTrack(MIDICable&, uint8_t, uint8_t, uint8_t, bool*, ModelStack*, Output*, int32_t);
	void aftertouchReceivedForSpecificTrack(MIDICable&, int32_t, int32_t, int32_t, bool*, ModelStack*, Output*,
	                                        int32_t);
	Output* pitchBendReceivedForSelectedOrActiveClip(MIDICable&, uint8_t, uint8_t, uint8_t, bool*, ModelStack*);
	Output* aftertouchReceivedForSelectedOrActiveClip(MIDICable&, int32_t, int32_t, int32_t, bool*, ModelStack*);
	int parameter_calls = 0;
	int activation_calls = 0;
	Clip* selected_clip = nullptr;
	Clip* active_clip = nullptr;
	std::function<void()> on_activation;
	MIDIMatchType checkMidiFollowMatch(MIDICable&, uint8_t) { return match; }
	Clip* getSelectedOrActiveClip() { return selected_clip; }
	Clip* getActiveClip(ModelStack*) {
		++activation_calls;
		if (on_activation)
			on_activation();
		return active_clip;
	}
	Output* midiCCReceivedForSelectedOrActiveClip(MIDICable&, uint8_t, uint8_t, uint8_t, bool*, ModelStack*);
	bool feedback = false;
	MIDIMatchType match = MIDIMatchType::CHANNEL;
	uint32_t timeLastCCSent[kMaxMIDIValue + 1]{};
	std::function<void()> on_parameter;
	MIDIMatchType checkMidiFollowMatchForSpecificTrack(MIDICable&, uint8_t, int32_t) { return match; }
	bool isFeedbackEnabled() { return feedback; }
	void handleReceivedCC(MIDICable&, ModelStackWithTimelineCounter&, Clip*, int, int) {
		++parameter_calls;
		if (on_parameter)
			on_parameter();
	}
	void midiCCReceivedForSpecificTrack(MIDICable&, uint8_t, uint8_t, uint8_t, bool*, ModelStack*, Output*, int32_t);
};
#include "midi_track_cc.inc"
} // namespace midi_track_cc_test
using namespace midi_track_cc_test;
TEST_GROUP(MidiTrackCC) {
	MidiFollow follow;
	Clip clip;
	MelodicInstrument output;
	ModelStack stack;
	MIDICable cable;
	bool thru = true;
	void send(int cc = 7, int value = 100) {
		follow.midiCCReceivedForSpecificTrack(cable, 0, cc, value, &thru, &stack, &output, 0);
	}
	Output* send_selected(int cc = 7, int value = 100) {
		return follow.midiCCReceivedForSelectedOrActiveClip(cable, 0, cc, value, &thru, &stack);
	}
	void send_expression(bool selected, ModelStack* target_stack, Output* target_output) {
		if (selected) {
			follow.pitchBendReceivedForSelectedOrActiveClip(cable, 0, 0, 64, &thru, target_stack);
			follow.aftertouchReceivedForSelectedOrActiveClip(cable, 0, 64, -1, &thru, target_stack);
		}
		else {
			follow.pitchBendReceivedForSpecificTrack(cable, 0, 0, 64, &thru, target_stack, target_output, 0);
			follow.aftertouchReceivedForSpecificTrack(cable, 0, 64, -1, &thru, target_stack, target_output, 0);
		}
	}
	void setup() override {
		panels::detail::active = panels::Id::Local;
		currentSong = &song;
		song.firstOutput = &output;
		output.active_clip = &clip;
		clip.output = &output;
		current_clip = &clip;
		follow.selected_clip = &clip;
		follow.active_clip = &clip;
		instrument_calls = 0;
		on_expression = {};
		session = {};
		midiEngine = {};
		AudioEngine::audioSampleTimer = 0;
	}
	void teardown() override {
		follow.on_parameter = {};
		panels::detail::active = panels::Id::Local;
	}
};
TEST(MidiTrackCC, parameter_callback_can_delete_clip_without_instrument_followup) {
	auto* target = new Clip;
	target->output = &output;
	output.active_clip = target;
	follow.on_parameter = [&] {
		output.active_clip = nullptr;
		delete target;
	};
	send();
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, parameter_callback_can_remove_output_or_replace_song) {
	for (bool replace_song : {false, true}) {
		currentSong = &song;
		song.firstOutput = &output;
		follow.on_parameter = [&] {
			if (replace_song)
				currentSong = nullptr;
			else
				song.firstOutput = nullptr;
		};
		send();
		LONGS_EQUAL(0, instrument_calls);
	}
}
TEST(MidiTrackCC, parameter_owner_change_stops_delivery_and_restores_panel) {
	for (auto owner : {panels::Id::Local, panels::Id::Remote}) {
		panels::Scope scope(owner);
		follow.on_parameter = [owner] {
			panels::detail::active = owner == panels::Id::Local ? panels::Id::Remote : panels::Id::Local;
		};
		send();
		CHECK(panels::current() == owner);
		LONGS_EQUAL(0, instrument_calls);
	}
}
TEST(MidiTrackCC, normal_internal_and_external_instrument_routing_is_preserved) {
	send();
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(1, instrument_calls);
	output.type = OutputType::MIDI_OUT;
	send();
	output.type = OutputType::CV;
	send();
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(3, instrument_calls);
}
TEST(MidiTrackCC, mute_solo_and_invalid_values_do_not_dispatch_parameters) {
	send(MIDI_CC_MUTE, 0);
	send(MIDI_CC_SOLO, 0);
	LONGS_EQUAL(0, session.mute_calls);
	LONGS_EQUAL(0, session.solo_calls);
	send(MIDI_CC_MUTE);
	send(MIDI_CC_SOLO);
	LONGS_EQUAL(1, session.mute_calls);
	LONGS_EQUAL(1, session.solo_calls);
	send(128);
	send(7, 128);
	LONGS_EQUAL(0, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, feedback_filter_skips_recent_internal_parameter_echo) {
	follow.feedback = true;
	midiEngine.midiFollowFeedbackFilter = true;
	send();
	LONGS_EQUAL(0, follow.parameter_calls);
	LONGS_EQUAL(1, instrument_calls);
	AudioEngine::audioSampleTimer = kSampleRate;
	send();
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(2, instrument_calls);
}

TEST(MidiTrackCC, parameter_callback_can_remove_and_delete_output) {
	auto* target = new MelodicInstrument;
	target->active_clip = &clip;
	clip.output = target;
	song.firstOutput = target;
	follow.on_parameter = [&] {
		song.firstOutput = nullptr;
		delete target;
	};
	follow.midiCCReceivedForSpecificTrack(cable, 0, 7, 100, &thru, &stack, target, 0);
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, detached_or_mismatched_track_targets_do_not_receive_cc) {
	song.firstOutput = nullptr;
	send();
	song.firstOutput = &output;
	clip.output = nullptr;
	send();
	clip.output = &output;
	follow.midiCCReceivedForSpecificTrack(cable, 0, 7, 100, &thru, nullptr, &output, 0);
	follow.midiCCReceivedForSpecificTrack(cable, 0, 7, 100, &thru, &stack, nullptr, 0);
	LONGS_EQUAL(0, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}

TEST(MidiTrackCC, selected_parameter_context_change_prevents_activation) {
	for (int change = 0; change < 3; ++change) {
		currentSong = &song;
		current_clip = &clip;
		follow.on_parameter = [&] {
			if (change == 0)
				currentSong = nullptr;
			if (change == 1)
				current_clip = nullptr;
			if (change == 2)
				panels::detail::active = panels::Id::Remote;
		};
		POINTERS_EQUAL(nullptr, send_selected());
		LONGS_EQUAL(0, follow.activation_calls);
		LONGS_EQUAL(0, instrument_calls);
		CHECK(panels::current() == panels::Id::Local);
	}
}
TEST(MidiTrackCC, selected_activation_context_change_prevents_instrument_delivery) {
	follow.on_activation = [&] { current_clip = nullptr; };
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(1, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, selected_and_active_clips_can_differ) {
	Clip active_clip;
	active_clip.output = &output;
	follow.active_clip = &active_clip;
	POINTERS_EQUAL(&output, send_selected());
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(1, instrument_calls);
}
TEST(MidiTrackCC, selected_invalid_input_does_not_activate) {
	send_selected(128);
	send_selected(7, 128);
	currentSong = nullptr;
	send_selected();
	LONGS_EQUAL(0, follow.parameter_calls);
	LONGS_EQUAL(0, follow.activation_calls);
}

TEST(MidiTrackCC, expression_routes_only_supported_instrument_outputs) {
	for (bool selected : {false, true}) {
		instrument_calls = 0;
		for (auto type : {OutputType::SYNTH, OutputType::MIDI_OUT, OutputType::CV}) {
			output.type = type;
			send_expression(selected, &stack, &output);
		}
		LONGS_EQUAL(6, instrument_calls);
		output.type = OutputType::AUDIO;
		send_expression(selected, &stack, &output);
		LONGS_EQUAL(6, instrument_calls);
	}
}
TEST(MidiTrackCC, expression_missing_context_does_not_dispatch) {
	for (bool selected : {false, true}) {
		send_expression(selected, nullptr, &output);
		currentSong = nullptr;
		send_expression(selected, &stack, &output);
		currentSong = &song;
	}
	send_expression(false, &stack, nullptr);
	LONGS_EQUAL(0, instrument_calls);
	LONGS_EQUAL(0, follow.activation_calls);
}
TEST(MidiTrackCC, expression_missing_and_mismatched_clip_outputs_are_rejected) {
	clip.output = nullptr;
	send_expression(false, &stack, &output);
	send_expression(true, &stack, &output);
	MelodicInstrument other_output;
	clip.output = &other_output;
	send_expression(false, &stack, &output);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, expression_kit_delivery_and_audio_clip_exclusion) {
	Kit kit;
	song.firstOutput = &kit;
	kit.type = OutputType::KIT;
	kit.active_clip = &clip;
	clip.output = &kit;
	for (bool selected : {false, true})
		send_expression(selected, &stack, &kit);
	LONGS_EQUAL(4, instrument_calls);
	clip.type = ClipType::AUDIO;
	for (bool selected : {false, true})
		send_expression(selected, &stack, &kit);
	LONGS_EQUAL(4, instrument_calls);
}

TEST(MidiTrackCC, destroyed_clip_without_active_pointer_cleanup_stops_cc) {
	auto* target = new Clip;
	target->output = &output;
	output.active_clip = target;
	follow.on_parameter = [&] { delete target; };
	send();
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, replacement_at_same_clip_address_does_not_receive_old_cc) {
	auto* target = new Clip;
	target->output = &output;
	output.active_clip = target;
	follow.on_parameter = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
		target->output = &output;
	};
	send();
	LONGS_EQUAL(0, instrument_calls);
	delete target;
}

TEST(MidiTrackCC, output_deleted_without_list_cleanup_stops_instrument_followup) {
	auto* target = new MelodicInstrument;
	target->active_clip = &clip;
	clip.output = target;
	song.firstOutput = target;
	follow.on_parameter = [&] { delete target; };
	follow.midiCCReceivedForSpecificTrack(cable, 0, 7, 100, &thru, &stack, target, 0);
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, output_address_reuse_stops_instrument_followup) {
	auto* target = new MelodicInstrument;
	target->active_clip = &clip;
	clip.output = target;
	song.firstOutput = target;
	follow.on_parameter = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
		target->active_clip = &clip;
	};
	follow.midiCCReceivedForSpecificTrack(cable, 0, 7, 100, &thru, &stack, target, 0);
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
	delete target;
}
TEST(MidiTrackCC, retired_track_does_not_receive_cc) {
	output.lifetime_source.retire();
	send();
	LONGS_EQUAL(0, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}

TEST(MidiTrackCC, selected_clip_destroyed_by_parameter_callback_prevents_activation) {
	auto* target = new Clip;
	target->output = &output;
	follow.selected_clip = target;
	follow.on_parameter = [&] { delete target; };
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(0, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, current_clip_destroyed_by_parameter_callback_prevents_activation) {
	auto* target = new Clip;
	target->output = &output;
	current_clip = target;
	follow.on_parameter = [&] { delete target; };
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(0, follow.activation_calls);
}
TEST(MidiTrackCC, selected_output_destroyed_by_parameter_callback_prevents_activation) {
	auto* target = new MelodicInstrument;
	Clip selected;
	selected.output = target;
	follow.selected_clip = &selected;
	follow.on_parameter = [&] { delete target; };
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(0, follow.activation_calls);
}
TEST(MidiTrackCC, current_output_reused_by_parameter_callback_prevents_activation) {
	auto* target = new MelodicInstrument;
	Clip current;
	current.output = target;
	current_clip = &current;
	follow.on_parameter = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
	};
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(0, follow.activation_calls);
	delete target;
}
TEST(MidiTrackCC, outputless_selected_clip_does_not_dereference_output) {
	clip.output = nullptr;
	follow.active_clip = nullptr;
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
}

TEST(MidiTrackCC, retiring_clip_rejects_selected_and_track_expression) {
	clip.lifetime_source.retire();
	for (bool selected : {false, true})
		send_expression(selected, &stack, &output);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, retiring_output_rejects_selected_and_track_expression) {
	output.lifetime_source.retire();
	for (bool selected : {false, true})
		send_expression(selected, &stack, &output);
	LONGS_EQUAL(0, instrument_calls);
}

TEST(MidiTrackCC, selected_parameter_song_reuse_prevents_activation) {
	follow.on_parameter = [&] {
		song.~song_fixture();
		new (&song) song_fixture;
		song.firstOutput = &output;
	};
	POINTERS_EQUAL(nullptr, send_selected());
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, track_parameter_song_retirement_prevents_delivery) {
	song_fixture retiring_song;
	retiring_song.firstOutput = &output;
	currentSong = &retiring_song;
	follow.on_parameter = [&] { retiring_song.lifetime.retire(); };
	send();
	LONGS_EQUAL(1, follow.parameter_calls);
	LONGS_EQUAL(0, instrument_calls);
	currentSong = &song;
}
TEST(MidiTrackCC, retired_song_rejects_selected_and_track_cc) {
	song_fixture retiring_song;
	retiring_song.firstOutput = &output;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	POINTERS_EQUAL(nullptr, send_selected());
	send();
	LONGS_EQUAL(0, follow.parameter_calls);
	LONGS_EQUAL(0, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
	currentSong = &song;
}

TEST(MidiTrackCC, selected_expression_song_reuse_during_activation_prevents_delivery) {
	follow.on_activation = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	send_expression(true, &stack, &output);
	LONGS_EQUAL(2, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, selected_expression_owner_change_during_activation_cancels_and_restores) {
	follow.on_activation = [] { panels::detail::active = panels::Id::Remote; };
	send_expression(true, &stack, &output);
	LONGS_EQUAL(2, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
	CHECK(panels::current() == panels::Id::Local);
}
TEST(MidiTrackCC, retired_song_prevents_selected_expression_activation) {
	song_fixture retiring_song;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	send_expression(true, &stack, &output);
	LONGS_EQUAL(0, follow.activation_calls);
	LONGS_EQUAL(0, instrument_calls);
	currentSong = &song;
}

TEST(MidiTrackCC, selected_expression_deleted_clip_is_not_returned) {
	for (bool pitch : {false, true}) {
		auto target = std::make_unique<Clip>();
		target->output = &output;
		follow.active_clip = target.get();
		on_expression = [&] { target.reset(); };
		Output* result = pitch ? follow.pitchBendReceivedForSelectedOrActiveClip(cable, 0, 0, 64, &thru, &stack)
		                       : follow.aftertouchReceivedForSelectedOrActiveClip(cable, 0, 64, -1, &thru, &stack);
		POINTERS_EQUAL(nullptr, result);
	}
	LONGS_EQUAL(2, instrument_calls);
}
TEST(MidiTrackCC, selected_expression_reused_output_is_not_returned) {
	for (bool pitch : {false, true}) {
		auto target = std::make_unique<MelodicInstrument>();
		clip.output = target.get();
		on_expression = [&] {
			target->~MelodicInstrument();
			new (target.get()) MelodicInstrument;
		};
		Output* result = pitch ? follow.pitchBendReceivedForSelectedOrActiveClip(cable, 0, 0, 64, &thru, &stack)
		                       : follow.aftertouchReceivedForSelectedOrActiveClip(cable, 0, 64, -1, &thru, &stack);
		POINTERS_EQUAL(nullptr, result);
	}
	clip.output = &output;
	LONGS_EQUAL(2, instrument_calls);
}

TEST(MidiTrackCC, detached_track_does_not_receive_expression) {
	song.firstOutput = nullptr;
	send_expression(false, &stack, &output);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, freed_track_is_rejected_before_lifetime_watch) {
	auto* target = new MelodicInstrument;
	delete target;
	send_expression(false, &stack, target);
	LONGS_EQUAL(0, instrument_calls);
}
TEST(MidiTrackCC, retired_song_does_not_receive_track_expression) {
	song_fixture retiring_song;
	retiring_song.firstOutput = &output;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	send_expression(false, &stack, &output);
	LONGS_EQUAL(0, instrument_calls);
	currentSong = &song;
}
