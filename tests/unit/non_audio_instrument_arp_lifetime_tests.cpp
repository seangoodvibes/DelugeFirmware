#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
namespace deluge::modulation::params {
constexpr int GLOBAL_ARP_RATE = 0;
}
namespace non_audio_instrument_arp_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, ARP_NOTE_NONE = -1, kDefaultLiftValue = 64;
int song;
int* currentSong = &song;
int paramNeutralValues[1]{};
int cableToExpParamShortcut(int value) {
	return value;
}
int getFinalParameterValueExp(int, int value) {
	return value;
}
struct StereoSample {};
struct ModelStack {
	int* song = &non_audio_instrument_arp_lifetime_test::song;
};
using ModelStackWithThreeMainThings = ModelStack;
enum class ArpNoteStatus { OFF, PENDING, PLAYING };
enum class ArpMode { OFF, ON };
enum class OutputType { MIDI_OUT, CV };
struct ArpeggiatorSettings {
	ArpMode mode = ArpMode::ON;
	int gate = 0, rate = 0;
	int getPhaseIncrement(int value) { return value; }
};
struct ArpNote {
	int noteCodeOnPostArp[3]{60, 64, ARP_NOTE_NONE};
	ArpNoteStatus noteStatus[3]{ArpNoteStatus::PENDING, ArpNoteStatus::PENDING, ArpNoteStatus::OFF};
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	int glideNoteCodeOffPostArp[3]{60, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int noteCodeOffPostArp[3]{61, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int glideOutputMIDIChannelOff[3]{1, 0, 0};
	int outputMIDIChannelOff[3]{2, 0, 0};
};
std::function<void()> on_generation;
std::function<void(bool)> on_dispatch;
int generated = 0, dispatched = 0, ons = 0, offs = 0, last_velocity = 0;
struct Arpeggiator {
	uint64_t revision = 0;
	uint64_t instruction_revision() const { return revision; }
	std::unique_ptr<ArpNote> note = std::make_unique<ArpNote>();
	void generate(ArpReturnInstruction* instruction) {
		++generated;
		++revision;
		instruction->arpNoteOn = note.get();
		if (on_generation)
			on_generation();
	}
	void noteOn(ArpeggiatorSettings*, int, uint8_t, ArpReturnInstruction* instruction, int, const int16_t*) {
		generate(instruction);
	}
	void noteOff(ArpeggiatorSettings*, int, ArpReturnInstruction* instruction) { generate(instruction); }
	void render(ArpeggiatorSettings*, ArpReturnInstruction* instruction, size_t, uint32_t, uint32_t) {
		generate(instruction);
	}
	int32_t doTickForward(ArpeggiatorSettings*, ArpReturnInstruction* instruction, int32_t, bool) {
		generate(instruction);
		return 7;
	}
};
struct NonAudioInstrument;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	NonAudioInstrument* output = nullptr;
	ArpeggiatorSettings arpSettings;
	bool currentlyPlayingReversed = false;
};
struct NonAudioInstrument {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	InstrumentClip* activeClip = nullptr;
	Arpeggiator arpeggiator;
	OutputType type = OutputType::MIDI_OUT;
	int channel = 1;
	int getChannel() const { return channel; }
	void noteOnPostArp(int, ArpNote* note, int index) {
		CHECK(note->noteStatus[index] == ArpNoteStatus::PLAYING);
		++ons;
		++dispatched;
		if (on_dispatch)
			on_dispatch(true);
	}
	void noteOffPostArp(int, int, int velocity, int) {
		++offs;
		++dispatched;
		last_velocity = velocity;
		if (on_dispatch)
			on_dispatch(false);
	}
	void renderOutput(ModelStack*, std::span<StereoSample>, int32_t*, int32_t, int32_t, bool, bool);
	void sendNote(ModelStackWithThreeMainThings*, bool, int32_t, const int16_t*, int32_t, uint8_t, uint32_t, int32_t,
	              uint32_t);
	int32_t doTickForwardForArp(ModelStack*, int32_t);
};
#include "non_audio_instrument_arp_lifetime.inc"
} // namespace non_audio_instrument_arp_lifetime_test
using namespace non_audio_instrument_arp_lifetime_test;
TEST_GROUP(non_audio_instrument_arp_lifetime) {
	std::unique_ptr<NonAudioInstrument> instrument;
	std::unique_ptr<InstrumentClip> clip;
	ModelStack stack;
	void reset() {
		instrument = std::make_unique<NonAudioInstrument>();
		clip = std::make_unique<InstrumentClip>();
		instrument->activeClip = clip.get();
		clip->output = instrument.get();
		on_generation = {};
		on_dispatch = {};
		generated = dispatched = ons = offs = last_velocity = 0;
		currentSong = &song;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_generation = {};
		on_dispatch = {};
		currentSong = &song;
	}
	int run(int mode) {
		if (mode == 0)
			return instrument->doTickForwardForArp(&stack, 0);
		if (mode == 1) {
			StereoSample samples[1];
			instrument->renderOutput(&stack, samples, nullptr, 0, 0, false, true);
		}
		else
			instrument->sendNote(&stack, mode == 2, 60, nullptr, 0, 13, 0, 0, 0);
		return 0;
	}
};
TEST(non_audio_instrument_arp_lifetime, live_tick_render_and_direct_note_routes) {
	for (int mode = 0; mode < 4; ++mode) {
		reset();
		LONGS_EQUAL(mode == 0 ? 7 : 0, run(mode));
		LONGS_EQUAL(mode < 2 ? 4 : 2, dispatched);
		LONGS_EQUAL(mode == 3 ? 0 : 2, ons);
		LONGS_EQUAL(mode == 2 ? 0 : 2, offs);
		if (mode != 2)
			LONGS_EQUAL(mode == 3 ? 13 : kDefaultLiftValue, last_velocity);
	}
}
TEST(non_audio_instrument_arp_lifetime, cv_note_release_can_glide_to_another_note) {
	instrument->type = OutputType::CV;
	run(3);
	LONGS_EQUAL(2, offs);
	LONGS_EQUAL(2, ons);
	LONGS_EQUAL(13, last_velocity);
}
TEST(non_audio_instrument_arp_lifetime, generation_can_destroy_owners_on_every_route) {
	for (int mode = 0; mode < 4; ++mode) {
		reset();
		on_generation = [&] {
			clip.reset();
			instrument.reset();
		};
		run(mode);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(non_audio_instrument_arp_lifetime, dispatch_destruction_cancels_all_remaining_events) {
	for (int mode = 0; mode < 4; ++mode) {
		for (int stop_after = 1; stop_after <= (mode < 2 ? 4 : 2); ++stop_after) {
			reset();
			on_dispatch = [&](bool) {
				if (dispatched == stop_after) {
					clip.reset();
					instrument.reset();
				}
			};
			run(mode);
			LONGS_EQUAL(stop_after, dispatched);
		}
	}
}
TEST(non_audio_instrument_arp_lifetime, revision_change_can_free_pending_storage_with_owners_alive) {
	for (int mode = 0; mode < 4; ++mode) {
		reset();
		on_dispatch = [&](bool) {
			++instrument->arpeggiator.revision;
			instrument->arpeggiator.note.reset();
		};
		run(mode);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(non_audio_instrument_arp_lifetime, channel_retarget_stops_original_batch) {
	for (int mode = 0; mode < 4; ++mode) {
		reset();
		on_dispatch = [&](bool) { ++instrument->channel; };
		run(mode);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(non_audio_instrument_arp_lifetime, clip_detachment_stops_original_batch) {
	for (int mode = 0; mode < 4; ++mode) {
		reset();
		on_dispatch = [&](bool) { clip->output = nullptr; };
		run(mode);
		LONGS_EQUAL(1, dispatched);
	}
}
TEST(non_audio_instrument_arp_lifetime, clipless_direct_notes_remain_supported) {
	instrument->activeClip = nullptr;
	instrument->sendNote(nullptr, true, 60, nullptr, 0, 13, 0, 0, 0);
	LONGS_EQUAL(2, ons);
}
TEST(non_audio_instrument_arp_lifetime, retired_entry_and_disabled_render_do_not_generate) {
	clip->arpSettings.mode = ArpMode::OFF;
	run(1);
	LONGS_EQUAL(0, generated);
	instrument->lifetime.retire();
	for (int mode = 0; mode < 4; ++mode)
		run(mode);
	LONGS_EQUAL(0, generated);
}
TEST(non_audio_instrument_arp_lifetime, note_status_does_not_overwrite_nested_reset) {
	on_dispatch = [&](bool) {
		++instrument->arpeggiator.revision;
		instrument->arpeggiator.note->noteStatus[0] = ArpNoteStatus::OFF;
	};
	run(2);
	LONGS_EQUAL(1, ons);
	CHECK(instrument->arpeggiator.note->noteStatus[0] == ArpNoteStatus::OFF);
}
