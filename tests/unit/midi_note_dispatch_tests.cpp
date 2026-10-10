#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <array>
#include <climits>
#include <functional>
#include <memory>
namespace midi_note_dispatch_test {
namespace session = deluge::gui::ui_session;
enum class MIDIMatchType { CHANNEL, NO_MATCH };
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
	Clip* getActiveClip() { return active_clip; }
	OutputType type = OutputType::SYNTH;
};
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	Output* output = nullptr;
	ClipType type = ClipType::INSTRUMENT;
};
using InstrumentClip = Clip;
static int sends = 0, last_note = -1;
static bool last_on = false, last_record = false;
static std::function<void()> on_note;
struct Kit : Output {
	void receivedNoteForKit(ModelStackWithTimelineCounter*, MIDICable&, bool on, int, int note, int, bool record, bool*,
	                        InstrumentClip*) {
		++sends;
		last_note = note;
		last_on = on;
		last_record = record;
		if (on_note)
			on_note();
	}
};
struct MelodicInstrument : Output {
	void receivedNote(ModelStackWithTimelineCounter*, MIDICable&, bool on, int, MIDIMatchType, int note, int,
	                  bool record, bool*) {
		++sends;
		last_note = note;
		last_on = on;
		last_record = record;
		if (on_note)
			on_note();
	}
};
static struct {
	int midiFollowKitRootNote = 36;
} midiEngine;
struct song_fixture {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	Output* firstOutput = nullptr;
	bool active = true;
	bool isOutputActiveInArrangement(Output*) { return active; }
};
static song_fixture song;
static song_fixture* currentSong = &song;
static Clip* clipForLastNoteReceived[kMaxMIDIValue + 1]{};
struct MidiFollow {
	MIDIMatchType match = MIDIMatchType::CHANNEL;
	MIDIMatchType checkMidiFollowMatch(MIDICable&, uint8_t) { return match; }
	Clip* getActiveClip(ModelStack*) { return nullptr; }
	Output* noteMessageReceivedForSelectedOrActiveClip(MIDICable&, bool, int32_t, int32_t, int32_t, bool*, bool,
	                                                   ModelStack*);
	MIDIMatchType checkMidiFollowMatchForSpecificTrack(MIDICable&, uint8_t, int32_t) { return match; }
	void noteMessageReceivedForSpecificTrack(MIDICable&, bool, int32_t, int32_t, int32_t, bool*, bool, ModelStack*,
	                                         Output*, int32_t);
	void clearStoredClips();
	void removeClip(Clip*);
	Output* sendNoteToClip(MIDICable&, Clip*, MIDIMatchType, bool, int32_t, int32_t, int32_t, bool*, bool, ModelStack*,
	                       bool = true);
};
#include "midi_note_dispatch.inc"
} // namespace midi_note_dispatch_test
using namespace midi_note_dispatch_test;
TEST_GROUP(MidiNoteDispatch) {
	MidiFollow follow;
	Clip clip;
	MelodicInstrument output;
	ModelStack stack;
	MIDICable cable;
	bool thru = true;
	Output* send(bool on, int note = 60, bool remember = true) {
		return follow.sendNoteToClip(cable, &clip, MIDIMatchType::CHANNEL, on, 0, note, 100, &thru, true, &stack,
		                             remember);
	}
	void setup() override {
		on_note = {};
		session::detail::active = session::Id::Local;
		clip.output = &output;
		song.active = true;
		song.firstOutput = &output;
		output.active_clip = &clip;
		currentSong = &song;
		sends = 0;
		last_note = -1;
		last_on = last_record = false;
		for (auto& target : clipForLastNoteReceived)
			target = nullptr;
	}
	void teardown() override {
		on_note = {};
	}
};
TEST(MidiNoteDispatch, invalid_notes_and_missing_inputs_do_not_send) {
	for (int note : {-1, INT_MIN, 128, INT_MAX})
		POINTERS_EQUAL(nullptr, send(true, note));
	clip.output = nullptr;
	POINTERS_EQUAL(nullptr, send(true));
	clip.output = &output;
	currentSong = nullptr;
	POINTERS_EQUAL(nullptr, send(false));
	currentSong = &song;
	POINTERS_EQUAL(nullptr, follow.sendNoteToClip(cable, &clip, MIDIMatchType::CHANNEL, true, 0, 60, 100, &thru, true,
	                                              nullptr, true));
	POINTERS_EQUAL(nullptr, follow.sendNoteToClip(cable, nullptr, MIDIMatchType::CHANNEL, true, 0, 60, 100, &thru, true,
	                                              &stack, true));
	LONGS_EQUAL(0, sends);
}
TEST(MidiNoteDispatch, muted_note_off_is_delivered_without_recording) {
	POINTERS_EQUAL(&output, send(true));
	POINTERS_EQUAL(&clip, clipForLastNoteReceived[60]);
	CHECK(last_record);
	song.active = false;
	POINTERS_EQUAL(nullptr, send(true));
	LONGS_EQUAL(1, sends);
	POINTERS_EQUAL(&output, send(false));
	LONGS_EQUAL(2, sends);
	CHECK_FALSE(last_on);
	CHECK_FALSE(last_record);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}
TEST(MidiNoteDispatch, kit_translation_and_specific_track_delivery_preserve_retained_note) {
	Kit kit;
	kit.type = OutputType::KIT;
	clip.output = &kit;
	POINTERS_EQUAL(&kit, send(true, 38, false));
	LONGS_EQUAL(2, last_note);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[38]);
	CHECK(last_on);
	CHECK(last_record);
}
TEST(MidiNoteDispatch, audio_clip_returns_output_without_instrument_dispatch) {
	clip.type = ClipType::AUDIO;
	POINTERS_EQUAL(&output, send(true));
	LONGS_EQUAL(0, sends);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}

TEST(MidiNoteDispatch, incompatible_output_types_are_not_cast_to_instruments) {
	for (auto type : {OutputType::AUDIO, OutputType::NONE}) {
		output.type = type;
		POINTERS_EQUAL(&output, send(true));
		LONGS_EQUAL(0, sends);
		POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
	}
}
TEST(MidiNoteDispatch, supported_melodic_types_deliver_boundary_notes_and_release_retention) {
	for (auto type : {OutputType::SYNTH, OutputType::MIDI_OUT, OutputType::CV}) {
		output.type = type;
		for (int note : {0, kMaxMIDIValue}) {
			POINTERS_EQUAL(&output, send(true, note));
			LONGS_EQUAL(note, last_note);
			POINTERS_EQUAL(&clip, clipForLastNoteReceived[note]);
			POINTERS_EQUAL(&output, send(false, note));
			POINTERS_EQUAL(nullptr, clipForLastNoteReceived[note]);
		}
	}
	LONGS_EQUAL(12, sends);
}

TEST(MidiNoteDispatch, callback_cleanup_is_not_overwritten_by_note_on_completion) {
	for (bool remove_only : {false, true}) {
		on_note = [&] {
			if (remove_only)
				follow.removeClip(&clip);
			else
				follow.clearStoredClips();
		};
		send(true);
		POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
	}
}
TEST(MidiNoteDispatch, nested_note_on_survives_outer_note_off_completion) {
	Clip replacement;
	replacement.output = &output;
	send(true);
	bool nested = false;
	on_note = [&] {
		if (nested)
			return;
		nested = true;
		follow.sendNoteToClip(cable, &replacement, MIDIMatchType::CHANNEL, true, 0, 60, 100, &thru, true, &stack, true);
	};
	send(false);
	POINTERS_EQUAL(&replacement, clipForLastNoteReceived[60]);
}
TEST(MidiNoteDispatch, unrelated_note_off_does_not_clear_newer_target) {
	Clip replacement;
	replacement.output = &output;
	clipForLastNoteReceived[60] = &replacement;
	send(false);
	POINTERS_EQUAL(&replacement, clipForLastNoteReceived[60]);
}

TEST(MidiNoteDispatch, all_notes_off_stops_after_song_replacement) {
	song_fixture replacement;
	clipForLastNoteReceived[0] = &clip;
	clipForLastNoteReceived[1] = &clip;
	on_note = [&] { currentSong = &replacement; };
	POINTERS_EQUAL(nullptr, follow.noteMessageReceivedForSelectedOrActiveClip(cable, false, 0, ALL_NOTES_OFF, 0, &thru,
	                                                                          false, &stack));
	LONGS_EQUAL(1, sends);
}
TEST(MidiNoteDispatch, all_notes_off_clears_retained_notes_in_normal_context) {
	for (auto& target : clipForLastNoteReceived)
		target = &clip;
	POINTERS_EQUAL(&output, follow.noteMessageReceivedForSelectedOrActiveClip(cable, false, 0, ALL_NOTES_OFF, 0, &thru,
	                                                                          false, &stack));
	LONGS_EQUAL(128, sends);
	for (auto target : clipForLastNoteReceived)
		POINTERS_EQUAL(nullptr, target);
}
TEST(MidiNoteDispatch, all_notes_off_owner_change_stops_and_restores_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		sends = 0;
		for (auto& target : clipForLastNoteReceived)
			target = &clip;
		on_note = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		POINTERS_EQUAL(nullptr, follow.noteMessageReceivedForSelectedOrActiveClip(cable, false, 0, ALL_NOTES_OFF, 0,
		                                                                          &thru, false, &stack));
		LONGS_EQUAL(1, sends);
		CHECK(session::current() == owner);
	}
}

TEST(MidiNoteDispatch, track_all_notes_off_stops_when_callback_deletes_active_clip) {
	auto* target = new Clip;
	target->output = &output;
	output.active_clip = target;
	on_note = [&] {
		output.active_clip = nullptr;
		delete target;
	};
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, &output, 0);
	LONGS_EQUAL(1, sends);
}
TEST(MidiNoteDispatch, track_all_notes_off_stops_when_callback_removes_and_deletes_output) {
	auto* target = new MelodicInstrument;
	target->active_clip = &clip;
	clip.output = target;
	song.firstOutput = target;
	on_note = [&] {
		song.firstOutput = nullptr;
		delete target;
	};
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, target, 0);
	LONGS_EQUAL(1, sends);
}
TEST(MidiNoteDispatch, track_all_notes_off_normal_delivery_does_not_change_selected_retention) {
	clipForLastNoteReceived[60] = &clip;
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, &output, 0);
	LONGS_EQUAL(128, sends);
	POINTERS_EQUAL(&clip, clipForLastNoteReceived[60]);
}
TEST(MidiNoteDispatch, track_notes_reject_missing_or_detached_outputs) {
	song.firstOutput = nullptr;
	follow.noteMessageReceivedForSpecificTrack(cable, true, 0, 60, 100, &thru, false, &stack, &output, 0);
	follow.noteMessageReceivedForSpecificTrack(cable, true, 0, 60, 100, &thru, false, &stack, nullptr, 0);
	LONGS_EQUAL(0, sends);
}

TEST(MidiNoteDispatch, retiring_clip_does_not_receive_or_retain_notes) {
	clip.lifetime_source.retire();
	send(true);
	LONGS_EQUAL(0, sends);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}

TEST(MidiNoteDispatch, track_all_notes_off_stops_without_active_pointer_cleanup) {
	auto* target = new Clip;
	target->output = &output;
	output.active_clip = target;
	on_note = [&] { delete target; };
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, &output, 0);
	LONGS_EQUAL(1, sends);
}
TEST(MidiNoteDispatch, track_all_notes_off_does_not_follow_reused_clip_address) {
	auto* target = new Clip;
	target->output = &output;
	output.active_clip = target;
	on_note = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
		target->output = &output;
	};
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, &output, 0);
	LONGS_EQUAL(1, sends);
	delete target;
}

TEST(MidiNoteDispatch, track_output_deleted_without_list_cleanup_stops_all_notes_off) {
	auto* target = new MelodicInstrument;
	target->active_clip = &clip;
	clip.output = target;
	song.firstOutput = target;
	on_note = [&] { delete target; };
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, target, 0);
	LONGS_EQUAL(1, sends);
}
TEST(MidiNoteDispatch, track_output_address_reuse_stops_all_notes_off) {
	auto* target = new MelodicInstrument;
	target->active_clip = &clip;
	clip.output = target;
	song.firstOutput = target;
	on_note = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
		target->active_clip = &clip;
	};
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, target, 0);
	LONGS_EQUAL(1, sends);
	delete target;
}
TEST(MidiNoteDispatch, retired_output_does_not_receive_or_retain_note) {
	output.lifetime_source.retire();
	POINTERS_EQUAL(nullptr, follow.sendNoteToClip(cable, &clip, MIDIMatchType::CHANNEL, true, 0, 60, 100, &thru, false,
	                                              &stack, true));
	LONGS_EQUAL(0, sends);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}

TEST(MidiNoteDispatch, selected_all_notes_off_stops_on_same_address_song_replacement) {
	clipForLastNoteReceived[0] = &clip;
	clipForLastNoteReceived[1] = &clip;
	on_note = [&] {
		song.~song_fixture();
		new (&song) song_fixture;
		song.firstOutput = &output;
	};
	POINTERS_EQUAL(nullptr, follow.noteMessageReceivedForSelectedOrActiveClip(cable, false, 0, ALL_NOTES_OFF, 0, &thru,
	                                                                          false, &stack));
	LONGS_EQUAL(1, sends);
}
TEST(MidiNoteDispatch, track_all_notes_off_stops_on_song_retirement) {
	song_fixture retiring_song;
	retiring_song.firstOutput = &output;
	currentSong = &retiring_song;
	on_note = [&] { retiring_song.lifetime.retire(); };
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, &output, 0);
	LONGS_EQUAL(1, sends);
	currentSong = &song;
}
TEST(MidiNoteDispatch, retired_song_rejects_selected_and_track_notes) {
	song_fixture retiring_song;
	retiring_song.firstOutput = &output;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	clipForLastNoteReceived[0] = &clip;
	POINTERS_EQUAL(nullptr, follow.noteMessageReceivedForSelectedOrActiveClip(cable, false, 0, ALL_NOTES_OFF, 0, &thru,
	                                                                          false, &stack));
	follow.noteMessageReceivedForSpecificTrack(cable, false, 0, ALL_NOTES_OFF, 0, &thru, false, &stack, &output, 0);
	LONGS_EQUAL(0, sends);
	currentSong = &song;
}
