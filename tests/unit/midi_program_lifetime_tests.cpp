#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <vector>
namespace midi_program_lifetime_test {
enum class OutputType { MIDI_OUT, SYNTH };
struct InstrumentClip;
struct MIDIInstrument {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	OutputType type = OutputType::MIDI_OUT;
	InstrumentClip* activeClip = nullptr;
	int channel = 16, master_channel = 0;
	InstrumentClip* getActiveClip() const { return activeClip; }
	int getChannel() const { return channel; }
	int getOutputMasterChannel() const { return master_channel; }
};
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	MIDIInstrument* output = nullptr;
	int midiBank = 1, midiSub = 2, midiPGM = 3;
	void sendMIDIPGM();
};
int song;
int* currentSong = &song;
std::function<void()> on_send;
std::vector<int> sent;
struct {
	void send(int value) {
		sent.push_back(value);
		if (on_send)
			on_send();
	}
	void sendBank(MIDIInstrument*, int master, int value, int filter) {
		LONGS_EQUAL(0, master);
		LONGS_EQUAL(16, filter);
		send(value);
	}
	void sendSubBank(MIDIInstrument*, int, int value, int) { send(value); }
	void sendPGMChange(MIDIInstrument*, int, int value, int) { send(value); }
} midiEngine;
#include "midi_program_lifetime.inc"
} // namespace midi_program_lifetime_test
using namespace midi_program_lifetime_test;
TEST_GROUP(midi_program_lifetime) {
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<MIDIInstrument> output;
	void reset() {
		on_send = {};
		sent.clear();
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
		clip = std::make_unique<InstrumentClip>();
		output = std::make_unique<MIDIInstrument>();
		clip->output = output.get();
		output->activeClip = clip.get();
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_send = {};
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
};
TEST(midi_program_lifetime, sends_bank_sub_bank_and_program_in_order) {
	clip->sendMIDIPGM();
	LONGS_EQUAL(3, sent.size());
	for (int i = 0; i < 3; ++i)
		LONGS_EQUAL(i + 1, sent[i]);
}
TEST(midi_program_lifetime, skips_each_unset_field_independently) {
	for (int mask = 0; mask < 8; ++mask) {
		reset();
		clip->midiBank = mask & 1 ? 1 : 128;
		clip->midiSub = mask & 2 ? 2 : 128;
		clip->midiPGM = mask & 4 ? 3 : 128;
		clip->sendMIDIPGM();
		LONGS_EQUAL(!!(mask & 1) + !!(mask & 2) + !!(mask & 4), sent.size());
	}
}
TEST(midi_program_lifetime, owner_deletion_at_each_output_stops_remaining_sends) {
	for (int boundary = 1; boundary <= 3; ++boundary) {
		reset();
		on_send = [&] {
			if (sent.size() == boundary) {
				clip.reset();
				output.reset();
			}
		};
		clip->sendMIDIPGM();
		LONGS_EQUAL(boundary, sent.size());
	}
}
TEST(midi_program_lifetime, routing_or_program_changes_cancel_remaining_sends) {
	for (int mutation = 0; mutation < 9; ++mutation) {
		reset();
		on_send = [&] {
			switch (mutation) {
			case 0:
				clip->output = nullptr;
				break;
			case 1:
				output->channel = 2;
				break;
			case 2:
				output->master_channel = 15;
				break;
			case 3:
				output->activeClip = nullptr;
				break;
			case 4:
				currentSong = nullptr;
				break;
			case 5:
				deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote;
				break;
			case 6:
				clip->midiBank = 4;
				break;
			case 7:
				clip->midiSub = 4;
				break;
			case 8:
				clip->midiPGM = 4;
				break;
			}
		};
		clip->sendMIDIPGM();
		LONGS_EQUAL(1, sent.size());
	}
}
TEST(midi_program_lifetime, rejects_missing_wrong_type_and_retired_owners) {
	clip->output = nullptr;
	clip->sendMIDIPGM();
	clip->output = output.get();
	output->type = OutputType::SYNTH;
	clip->sendMIDIPGM();
	output->type = OutputType::MIDI_OUT;
	output->lifetime.retire();
	clip->sendMIDIPGM();
	clip->lifetime.retire();
	clip->sendMIDIPGM();
	LONGS_EQUAL(0, sent.size());
}
TEST(midi_program_lifetime, inactive_clip_can_send_its_program) {
	output->activeClip = nullptr;
	clip->sendMIDIPGM();
	LONGS_EQUAL(3, sent.size());
}
