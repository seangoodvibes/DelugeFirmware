#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <functional>
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
	Output* next = nullptr;
	Clip* active_clip = nullptr;
	OutputType type = OutputType::SYNTH;
	Clip* getActiveClip() { return active_clip; }
};
struct Clip {
	Output* output = nullptr;
	ClipType type = ClipType::INSTRUMENT;
};
static int instrument_calls = 0;
struct Kit : Output {
	template <class... Args>
	void receivedCCForKit(Args&&...) {
		++instrument_calls;
	}
};
struct MelodicInstrument : Output {
	template <class... Args>
	void receivedCC(Args&&...) {
		++instrument_calls;
	}
};
struct song_fixture {
	Output* firstOutput = nullptr;
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
	int parameter_calls = 0;
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
	void setup() override {
		panels::detail::active = panels::Id::Local;
		currentSong = &song;
		song.firstOutput = &output;
		output.active_clip = &clip;
		clip.output = &output;
		instrument_calls = 0;
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
