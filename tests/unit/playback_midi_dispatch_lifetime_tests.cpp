#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace playback_midi_dispatch_lifetime_test {
constexpr int MIDI_DIRECTION_INPUT_TO_DELUGE = 0, MODEL_STACK_MAX_SIZE = 256, UI_MODE_MIDI_LEARN = 1, IS_A_CC = 16;
int currentUIMode;
bool dealingWithReceivedMIDIPitchBendRightNow;
struct Owner {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
};
struct MIDICable {
	struct {
		bool mpe = false;
		bool isChannelPartOfAnMPEZone(int) { return mpe; }
		int channelToZone(int channel) { return channel; }
	} ports[1];
	struct {
		int defaultInputMPEValues[3]{};
	} inputChannels[16];
};
struct Song;
struct Output;
struct Clip : Owner {
	Output* output = nullptr;
};
struct ModelStackWithTimelineCounter;
struct ModelStack {
	Song* song = nullptr;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip*);
};
struct ModelStackWithTimelineCounter : ModelStack {
	Clip* clip = nullptr;
	Clip* getTimelineCounterAllowNull() { return clip; }
};
std::function<void()> on_ui, on_command, on_follow, on_song, on_learned, on_input;
int ui_calls, command_calls, follow_calls, song_calls, learned_calls, input_calls, learn_calls;
bool ui_used, command_used, parameter_used;
std::vector<Clip*> delivered_clips;
struct ModControllableAudio {
	void offerReceivedCCToLearnedParamsForSong(MIDICable&, int, int, int, void*) {
		++song_calls;
		if (on_song)
			on_song();
	}
};
struct ModelStackWithThreeMainThings : ModelStackWithTimelineCounter {
	ModControllableAudio* modControllable = nullptr;
} dispatch_stack;
ModelStackWithTimelineCounter* ModelStack::addTimelineCounter(Clip* clip) {
	dispatch_stack.clip = clip;
	return &dispatch_stack;
}
ModelStack* setupModelStackWithSong(char*, Song* song) {
	dispatch_stack.song = song;
	dispatch_stack.clip = nullptr;
	return &dispatch_stack;
}
struct Output : Owner {
	Output* next = nullptr;
	Clip* active = nullptr;
	Clip* getActiveClip() { return active; }
	void offerReceivedCCToLearnedParams(MIDICable&, int, int, int, ModelStackWithTimelineCounter*) {
		++learned_calls;
		if (on_learned)
			on_learned();
	}
	bool offerReceivedPitchBendToLearnedParams(MIDICable&, int, int, int, ModelStackWithTimelineCounter*) {
		++learned_calls;
		if (on_learned)
			on_learned();
		return parameter_used;
	}
	void input(ModelStackWithTimelineCounter* stack) {
		++input_calls;
		delivered_clips.push_back(stack->clip);
		if (on_input)
			on_input();
	}
	void offerReceivedCC(ModelStackWithTimelineCounter* stack, MIDICable&, int, int, int, bool*) { input(stack); }
	void offerReceivedPitchBend(ModelStackWithTimelineCounter* stack, MIDICable&, int, int, int, bool*) {
		input(stack);
	}
	void offerReceivedAftertouch(ModelStackWithTimelineCounter* stack, MIDICable&, int, int, int, bool*) {
		input(stack);
	}
};
struct Song : Owner {
	Output* firstOutput = nullptr;
	std::vector<Clip*> registered;
	ModControllableAudio global;
	ModelStackWithThreeMainThings* setupModelStackWithSongAsTimelineCounter(char*) {
		dispatch_stack.song = this;
		dispatch_stack.modControllable = &global;
		return &dispatch_stack;
	}
	bool owns_output_for_undo(Output* output, bool) {
		for (auto* current = firstOutput; current; current = current->next)
			if (current == output)
				return true;
		return false;
	}
	bool contains_clip_for_undo(Clip* clip) {
		for (auto* current : registered)
			if (current == clip)
				return true;
		return false;
	}
};
Song* currentSong;
struct UI {};
struct Editor : UI {
	bool midiCCReceived(MIDICable&, int, int, int) {
		++ui_calls;
		if (on_ui)
			on_ui();
		return ui_used;
	}
	bool pitchBendReceived(MIDICable&, int, int, int) {
		++ui_calls;
		if (on_ui)
			on_ui();
		return ui_used;
	}
} editor;
UI* current_ui;
UI* getCurrentUI() {
	return current_ui;
}
Editor& sound_editor_for_session() {
	return editor;
}
struct View {
	void ccReceivedForMIDILearn(MIDICable&, int, int, int) { ++learn_calls; }
} view;
View& view_for_session() {
	return view;
}
struct {
	void follow() {
		++follow_calls;
		if (on_follow)
			on_follow();
	}
	void midiCCReceived(MIDICable&, int, int, int, bool*, ModelStack*) { follow(); }
	void pitchBendReceived(MIDICable&, int, int, int, bool*, ModelStack*) { follow(); }
	void aftertouchReceived(MIDICable&, int, int, int, bool*, ModelStack*) { follow(); }
} midiFollow;
struct PlaybackHandler {
	enum class incoming_midi_kind { cc, pitch_bend, aftertouch };
	void midiCCReceived(MIDICable&, uint8_t, uint8_t, uint8_t, bool*);
	void pitchBendReceived(MIDICable&, uint8_t, uint8_t, uint8_t, bool*);
	void aftertouchReceived(MIDICable&, int32_t, int32_t, int32_t, bool*);
	void dispatch_midi_message(MIDICable&, incoming_midi_kind, uint8_t, int32_t, int32_t, bool, bool*);
	bool offerNoteToLearnedThings(MIDICable&, bool, int, int) {
		++command_calls;
		if (on_command)
			on_command();
		return command_used;
	}
};
#include "playback_midi_dispatch_lifetime.inc"
} // namespace playback_midi_dispatch_lifetime_test
using namespace playback_midi_dispatch_lifetime_test;
TEST_GROUP(playback_midi_dispatch_lifetime) {
	std::unique_ptr<Song> song;
	std::unique_ptr<Output> first, second;
	std::unique_ptr<Clip> first_clip, second_clip;
	MIDICable cable;
	PlaybackHandler handler;
	bool thru = true;
	void reset() {
		on_ui = on_command = on_follow = on_song = on_learned = on_input = {};
		ui_calls = command_calls = follow_calls = song_calls = learned_calls = input_calls = learn_calls = 0;
		ui_used = command_used = parameter_used = false;
		delivered_clips.clear();
		song = std::make_unique<Song>();
		first = std::make_unique<Output>();
		second = std::make_unique<Output>();
		first_clip = std::make_unique<Clip>();
		second_clip = std::make_unique<Clip>();
		first->next = second.get();
		song->firstOutput = first.get();
		first->active = first_clip.get();
		second->active = second_clip.get();
		first_clip->output = first.get();
		second_clip->output = second.get();
		song->registered = {first_clip.get(), second_clip.get()};
		currentSong = song.get();
		cable = {};
		current_ui = nullptr;
		currentUIMode = 0;
		dealingWithReceivedMIDIPitchBendRightNow = false;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_ui = on_command = on_follow = on_song = on_learned = on_input = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void send(int kind) {
		if (kind == 0)
			handler.midiCCReceived(cable, 2, 7, 64, &thru);
		else if (kind == 1)
			handler.pitchBendReceived(cable, 2, 0, 64, &thru);
		else
			handler.aftertouchReceived(cable, 2, 64, -1, &thru);
	}
};
TEST(playback_midi_dispatch_lifetime, live_messages_preserve_follow_and_output_routes) {
	for (int kind = 0; kind < 3; ++kind) {
		reset();
		send(kind);
		LONGS_EQUAL(1, follow_calls);
		LONGS_EQUAL(kind == 0 ? 1 : 0, song_calls);
		LONGS_EQUAL(kind == 2 ? 0 : 2, learned_calls);
		LONGS_EQUAL(2, input_calls);
	}
}
TEST(playback_midi_dispatch_lifetime, follow_song_deletion_cancels_all_later_routes) {
	for (int kind = 0; kind < 3; ++kind) {
		reset();
		on_follow = [&] { song.reset(); };
		send(kind);
		LONGS_EQUAL(0, song_calls);
		LONGS_EQUAL(0, learned_calls);
		LONGS_EQUAL(0, input_calls);
		CHECK_FALSE(dealingWithReceivedMIDIPitchBendRightNow);
	}
}
TEST(playback_midi_dispatch_lifetime, song_parameter_callback_deletion_cancels_outputs) {
	on_song = [&] { song.reset(); };
	send(0);
	LONGS_EQUAL(1, song_calls);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, learned_callback_output_deletion_prevents_raw_delivery) {
	for (int kind : {0, 1}) {
		reset();
		on_learned = [&] { first.reset(); };
		send(kind);
		LONGS_EQUAL(1, learned_calls);
		LONGS_EQUAL(0, input_calls);
	}
}
TEST(playback_midi_dispatch_lifetime, input_deletion_cancels_before_next_output) {
	for (int kind = 0; kind < 3; ++kind) {
		reset();
		on_input = [&] { first.reset(); };
		send(kind);
		LONGS_EQUAL(1, input_calls);
	}
}
TEST(playback_midi_dispatch_lifetime, next_output_deletion_cancels_current_continuation) {
	on_learned = [&] { second.reset(); };
	send(0);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, output_detachment_cancels_current_continuation) {
	on_learned = [&] { song->firstOutput = second.get(); };
	send(0);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, same_address_output_replacement_cancels) {
	on_learned = [&] {
		auto* output = first.get();
		output->~Output();
		new (output) Output;
	};
	send(1);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, source_clip_deletion_cancels_current_continuation) {
	on_learned = [&] { first_clip.reset(); };
	send(0);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, registered_clone_retarget_reaches_raw_handler) {
	Clip clone;
	clone.output = first.get();
	song->registered.push_back(&clone);
	on_learned = [&] {
		if (learned_calls == 1)
			dispatch_stack.clip = &clone;
	};
	send(0);
	LONGS_EQUAL(2, input_calls);
	POINTERS_EQUAL(&clone, delivered_clips[0]);
}
TEST(playback_midi_dispatch_lifetime, discarded_clone_retarget_is_rejected_without_dereference) {
	auto clone = std::make_unique<Clip>();
	on_learned = [&] {
		dispatch_stack.clip = clone.get();
		clone.reset();
	};
	send(0);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, session_switch_cancels_and_restores_dispatch_owner) {
	on_follow = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	send(0);
	LONGS_EQUAL(0, input_calls);
	CHECK(deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local);
}
TEST(playback_midi_dispatch_lifetime, editor_callback_song_deletion_stops_before_follow) {
	for (int kind : {0, 1}) {
		reset();
		current_ui = &editor;
		on_ui = [&] { song.reset(); };
		send(kind);
		LONGS_EQUAL(1, ui_calls);
		LONGS_EQUAL(0, follow_calls);
	}
}
TEST(playback_midi_dispatch_lifetime, midi_learn_and_consumed_editor_messages_do_not_dispatch) {
	currentUIMode = UI_MODE_MIDI_LEARN;
	send(0);
	LONGS_EQUAL(1, learn_calls);
	LONGS_EQUAL(0, follow_calls);
	reset();
	current_ui = &editor;
	ui_used = true;
	send(0);
	LONGS_EQUAL(0, follow_calls);
}
TEST(playback_midi_dispatch_lifetime, command_callback_song_replacement_cancels_follow) {
	on_command = [&] {
		auto* raw = song.get();
		raw->~Song();
		new (raw) Song;
	};
	send(0);
	LONGS_EQUAL(0, follow_calls);
}
TEST(playback_midi_dispatch_lifetime, mpe_members_skip_learned_output_parameters) {
	for (int kind = 0; kind < 3; ++kind) {
		reset();
		cable.ports[0].mpe = true;
		send(kind);
		LONGS_EQUAL(0, learned_calls);
		LONGS_EQUAL(2, input_calls);
	}
	cable.ports[0].mpe = true;
	handler.midiCCReceived(cable, 2, 74, 0, &thru);
	LONGS_EQUAL(-32768, cable.inputChannels[2].defaultInputMPEValues[1]);
	handler.pitchBendReceived(cable, 2, 0, 0, &thru);
	LONGS_EQUAL(-32768, cable.inputChannels[2].defaultInputMPEValues[0]);
	handler.aftertouchReceived(cable, 2, 127, -1, &thru);
	LONGS_EQUAL(32512, cable.inputChannels[2].defaultInputMPEValues[2]);
}
TEST(playback_midi_dispatch_lifetime, learned_pitch_suppresses_expression_delivery) {
	parameter_used = true;
	send(1);
	LONGS_EQUAL(2, learned_calls);
	LONGS_EQUAL(0, input_calls);
	CHECK_FALSE(dealingWithReceivedMIDIPitchBendRightNow);
}
TEST(playback_midi_dispatch_lifetime, pitch_flag_restores_nested_and_cancelled_context) {
	bool nested = false;
	on_follow = [&] {
		CHECK(dealingWithReceivedMIDIPitchBendRightNow);
		if (!nested) {
			nested = true;
			send(1);
			CHECK(dealingWithReceivedMIDIPitchBendRightNow);
		}
	};
	send(1);
	CHECK_FALSE(dealingWithReceivedMIDIPitchBendRightNow);
	reset();
	dealingWithReceivedMIDIPitchBendRightNow = true;
	on_follow = [&] { song.reset(); };
	send(1);
	CHECK(dealingWithReceivedMIDIPitchBendRightNow);
}
TEST(playback_midi_dispatch_lifetime, clipless_pitch_and_pressure_still_reach_outputs) {
	for (int kind = 0; kind < 3; ++kind) {
		reset();
		first->active = second->active = nullptr;
		send(kind);
		LONGS_EQUAL(kind == 0 ? 0 : 2, input_calls);
		LONGS_EQUAL(0, learned_calls);
	}
}
TEST(playback_midi_dispatch_lifetime, malformed_input_is_rejected_before_cable_access) {
	handler.midiCCReceived(cable, 16, 7, 64, &thru);
	handler.midiCCReceived(cable, 0, 128, 64, &thru);
	handler.pitchBendReceived(cable, 255, 0, 64, &thru);
	handler.pitchBendReceived(cable, 0, 128, 64, &thru);
	handler.aftertouchReceived(cable, -1, 64, -1, &thru);
	handler.aftertouchReceived(cable, 0, 128, -1, &thru);
	handler.aftertouchReceived(cable, 0, 64, 128, &thru);
	LONGS_EQUAL(0, follow_calls);
	LONGS_EQUAL(0, input_calls);
}

TEST(playback_midi_dispatch_lifetime, removed_source_registration_cancels_raw_delivery) {
	on_learned = [&] { song->registered.clear(); };
	send(0);
	LONGS_EQUAL(0, input_calls);
}
TEST(playback_midi_dispatch_lifetime, unrelated_active_clip_change_cancels_raw_delivery) {
	Clip replacement;
	replacement.output = first.get();
	on_learned = [&] { first->active = &replacement; };
	send(1);
	LONGS_EQUAL(0, input_calls);
}
