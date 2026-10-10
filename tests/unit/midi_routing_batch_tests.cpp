#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <array>
#include <functional>
namespace midi_routing_batch_test {
namespace session = deluge::gui::ui_session;
struct MIDICable {};
struct ModelStack {};
struct Output {};
static int song, replacement_song;
static int* currentSong = &song;
struct MidiFollow {
	std::array<Output, 20> outputs;
	int track_count = 3, selected_calls = 0, track_calls = 0, count_calls = 0, lookups = 0;
	Output* selected = nullptr;
	bool missing_first = false;
	std::function<void()> on_selected, on_track;
	Output* selected_event() {
		++selected_calls;
		if (on_selected)
			on_selected();
		return selected;
	}
	void track_event() {
		++track_calls;
		if (on_track)
			on_track();
	}
	template <class... Args>
	Output* noteMessageReceivedForSelectedOrActiveClip(Args&&...) {
		return selected_event();
	}
	template <class... Args>
	Output* midiCCReceivedForSelectedOrActiveClip(Args&&...) {
		return selected_event();
	}
	template <class... Args>
	Output* pitchBendReceivedForSelectedOrActiveClip(Args&&...) {
		return selected_event();
	}
	template <class... Args>
	Output* aftertouchReceivedForSelectedOrActiveClip(Args&&...) {
		return selected_event();
	}
	template <class... Args>
	void noteMessageReceivedForSpecificTrack(Args&&...) {
		track_event();
	}
	template <class... Args>
	void midiCCReceivedForSpecificTrack(Args&&...) {
		track_event();
	}
	template <class... Args>
	void pitchBendReceivedForSpecificTrack(Args&&...) {
		track_event();
	}
	template <class... Args>
	void aftertouchReceivedForSpecificTrack(Args&&...) {
		track_event();
	}
	int getTrackCount() {
		++count_calls;
		return track_count;
	}
	Output* getTrackFromIndex(int index, int) {
		++lookups;
		return missing_first && index == 0 ? nullptr : &outputs[index];
	}
	void noteMessageReceived(MIDICable&, bool, int32_t, int32_t, int32_t, bool*, bool, ModelStack*);
	void midiCCReceived(MIDICable&, uint8_t, uint8_t, uint8_t, bool*, ModelStack*);
	void pitchBendReceived(MIDICable&, uint8_t, uint8_t, uint8_t, bool*, ModelStack*);
	void aftertouchReceived(MIDICable&, int32_t, int32_t, int32_t, bool*, ModelStack*);
	void dispatch(int event, ModelStack* stack) {
		MIDICable cable;
		bool thru = true;
		switch (event) {
		case 0:
			noteMessageReceived(cable, true, 1, 60, 100, &thru, true, stack);
			break;
		case 1:
			midiCCReceived(cable, 1, 7, 100, &thru, stack);
			break;
		case 2:
			pitchBendReceived(cable, 1, 0, 64, &thru, stack);
			break;
		case 3:
			aftertouchReceived(cable, 1, 100, 60, &thru, stack);
			break;
		}
	}
};
#include "midi_routing_batch.inc"
} // namespace midi_routing_batch_test
using namespace midi_routing_batch_test;
TEST_GROUP(MidiRoutingBatch) {
	ModelStack stack;
	void setup() override {
		currentSong = &song;
		session::detail::active = session::Id::Local;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(MidiRoutingBatch, selected_delivery_song_change_stops_all_track_routing) {
	for (int event = 0; event < 4; ++event) {
		MidiFollow follow;
		currentSong = &song;
		follow.on_selected = [] { currentSong = &replacement_song; };
		follow.dispatch(event, &stack);
		LONGS_EQUAL(1, follow.selected_calls);
		LONGS_EQUAL(0, follow.count_calls);
		LONGS_EQUAL(0, follow.track_calls);
	}
}
TEST(MidiRoutingBatch, track_delivery_context_change_stops_remaining_tracks) {
	for (int event = 0; event < 4; ++event) {
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			session::Scope scope(owner);
			currentSong = &song;
			MidiFollow follow;
			follow.on_track = [owner] {
				session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
			};
			follow.dispatch(event, &stack);
			LONGS_EQUAL(1, follow.track_calls);
			CHECK(session::current() == owner);
		}
	}
}
TEST(MidiRoutingBatch, normal_routing_skips_selected_missing_and_excess_tracks) {
	for (int event = 0; event < 4; ++event) {
		MidiFollow follow;
		follow.track_count = 20;
		follow.missing_first = true;
		follow.selected = &follow.outputs[1];
		follow.dispatch(event, &stack);
		LONGS_EQUAL(16, follow.lookups);
		LONGS_EQUAL(14, follow.track_calls);
	}
}
TEST(MidiRoutingBatch, missing_song_or_stack_prevents_dispatch) {
	for (int event = 0; event < 4; ++event) {
		MidiFollow follow;
		currentSong = &song;
		follow.dispatch(event, nullptr);
		currentSong = nullptr;
		follow.dispatch(event, &stack);
		LONGS_EQUAL(0, follow.selected_calls);
		LONGS_EQUAL(0, follow.track_calls);
	}
}
