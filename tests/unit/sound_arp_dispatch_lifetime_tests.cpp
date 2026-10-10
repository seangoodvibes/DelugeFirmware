#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
namespace sound_arp_dispatch_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, ARP_NOTE_NONE = -1, kNumExpressionDimensions = 3;
enum class ArpNoteStatus { OFF, PENDING, PLAYING };
enum class MIDICharacteristic { NOTE, CHANNEL };
namespace util {
template <class T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
struct ModelStackWithSoundFlags {};
struct UnpatchedParamSet {
} unpatched;
struct ParamManager {
	UnpatchedParamSet* unpatched_set = &unpatched;
	UnpatchedParamSet* getUnpatchedParamSet() { return unpatched_set; }
};
struct ArpeggiatorSettings {
	void updateParamsFromUnpatchedParamSet(UnpatchedParamSet*) {}
};
struct ArpNote {
	int noteCodeOnPostArp[3]{60, 64, ARP_NOTE_NONE};
	ArpNoteStatus noteStatus[3]{ArpNoteStatus::PENDING, ArpNoteStatus::PENDING, ArpNoteStatus::OFF};
	int16_t inputCharacteristics[2]{50, 2};
	int16_t mpeValues[3]{11, 22, 33};
	uint8_t velocity = 99;
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	bool invertReversed = true;
	int sampleSyncLengthOn = 16;
	int glideNoteCodeOffPostArp[3]{60, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int noteCodeOffPostArp[3]{64, ARP_NOTE_NONE, ARP_NOTE_NONE};
};
int started = 0, voice_budget = 3;
std::function<void()> on_start, on_off, on_generation;
int stopped = 0;
int song;
int* currentSong = &song;
namespace AudioEngine {
bool allowedToStartVoice() {
	return started < voice_budget;
}
} // namespace AudioEngine
struct Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	bool invertReversed = false;
	void noteOffPostArpeggiator(ModelStackWithSoundFlags*, int) {
		++stopped;
		if (on_off)
			on_off();
	}
	void noteOnPostArpeggiator(ModelStackWithSoundFlags*, int input_note, int, int velocity, const int16_t* mpe,
	                           int length, int, int, int channel) {
		++started;
		LONGS_EQUAL(50, input_note);
		LONGS_EQUAL(99, velocity);
		LONGS_EQUAL(16, length);
		LONGS_EQUAL(2, channel);
		if (on_start)
			on_start();
		// Voice creation may consume these values after allocation or other callbacks.
		LONGS_EQUAL(11, mpe[0]);
		LONGS_EQUAL(22, mpe[1]);
		LONGS_EQUAL(33, mpe[2]);
	}
	void process_postarp_notes(ModelStackWithSoundFlags*, ArpeggiatorSettings*, ArpReturnInstruction,
	                           const deluge::lifetime::callback_validation* = nullptr);
};

struct SoundInstrument;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	SoundInstrument* output = nullptr;
	ParamManager paramManager;
	ArpeggiatorSettings settings;
	bool currentlyPlayingReversed = false;
};
struct ModelStackWithThreeMainThings {
	Clip* clip = nullptr;
	ParamManager* paramManager = nullptr;
	ModelStackWithSoundFlags flags;
	Clip* getTimelineCounterAllowNull() { return clip; }
	ModelStackWithSoundFlags* addSoundFlags() { return &flags; }
	ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(SoundInstrument*, ParamManager* manager) {
		paramManager = manager;
		return this;
	}
};
struct ModelStack {
	int* song = &sound_arp_dispatch_lifetime_test::song;
	ModelStackWithThreeMainThings main;
	ModelStackWithThreeMainThings* addTimelineCounter(Clip* clip) {
		main.clip = clip;
		return &main;
	}
};
struct SoundInstrument : Sound {
	Clip* activeClip = nullptr;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	ParamManager* getParamManager(int*) { return &activeClip->paramManager; }
	ArpeggiatorSettings* getArpSettings() { return &activeClip->settings; }
	struct {
		std::unique_ptr<ArpNote> note = std::make_unique<ArpNote>();
		uint64_t revision = 0;
		uint64_t instruction_revision() const { return revision; }
		int32_t doTickForward(ArpeggiatorSettings*, ArpReturnInstruction* instruction, int32_t, bool) {
			instruction->arpNoteOn = note.get();
			if (on_generation)
				on_generation();
			return 7;
		}
	} arpeggiator;
	int32_t doTickForwardForArp(ModelStack*, int32_t);
};
#include "sound_arp_dispatch_lifetime.inc"
#include "sound_instrument_tick_lifetime.inc"
} // namespace sound_arp_dispatch_lifetime_test
using namespace sound_arp_dispatch_lifetime_test;
TEST_GROUP(sound_arp_dispatch_lifetime) {
	ModelStackWithSoundFlags stack;
	void setup() override {
		started = 0;
		voice_budget = 3;
		on_start = {};
	}
	void teardown() override {
		on_start = {};
	}
};
TEST(sound_arp_dispatch_lifetime, live_chord_publishes_status_before_voice_creation) {
	Sound sound;
	ArpNote note;
	on_start = [&] { CHECK(note.noteStatus[started - 1] == ArpNoteStatus::PLAYING); };
	sound.process_postarp_notes(&stack, nullptr, {&note});
	LONGS_EQUAL(2, started);
	CHECK(sound.invertReversed);
}
TEST(sound_arp_dispatch_lifetime, voice_budget_preserves_remaining_pending_notes) {
	Sound sound;
	ArpNote note;
	voice_budget = 1;
	sound.process_postarp_notes(&stack, nullptr, {&note});
	LONGS_EQUAL(1, started);
	CHECK(note.noteStatus[0] == ArpNoteStatus::PLAYING);
	CHECK(note.noteStatus[1] == ArpNoteStatus::PENDING);
}
TEST(sound_arp_dispatch_lifetime, destroyed_sound_and_note_cancel_without_stale_reads) {
	auto sound = std::make_unique<Sound>();
	auto note = std::make_unique<ArpNote>();
	auto lifetime = deluge::lifetime::lifetime_watch{sound->lifetime};
	const auto valid = [&] { return lifetime.alive(); };
	deluge::lifetime::callback_validation validation{valid};
	on_start = [&] {
		note.reset();
		sound.reset();
	};
	sound->process_postarp_notes(&stack, nullptr, {note.get()}, &validation);
	LONGS_EQUAL(1, started);
}
TEST(sound_arp_dispatch_lifetime, replaced_instruction_preserves_nested_status) {
	Sound sound;
	ArpNote note;
	bool current = true;
	const auto valid = [&] { return current; };
	deluge::lifetime::callback_validation validation{valid};
	on_start = [&] {
		note.noteStatus[0] = ArpNoteStatus::OFF;
		current = false;
	};
	sound.process_postarp_notes(&stack, nullptr, {&note}, &validation);
	LONGS_EQUAL(1, started);
	CHECK(note.noteStatus[0] == ArpNoteStatus::OFF);
}
TEST(sound_arp_dispatch_lifetime, invalid_entry_does_not_consume_voice_budget_or_publish) {
	Sound sound;
	ArpNote note;
	const auto valid = [] { return false; };
	deluge::lifetime::callback_validation validation{valid};
	sound.process_postarp_notes(&stack, nullptr, {&note}, &validation);
	LONGS_EQUAL(0, started);
	CHECK(note.noteStatus[0] == ArpNoteStatus::PENDING);
	CHECK_FALSE(sound.invertReversed);
}
TEST(sound_arp_dispatch_lifetime, already_playing_notes_are_not_retriggered) {
	Sound sound;
	ArpNote note;
	note.noteStatus[0] = ArpNoteStatus::PLAYING;
	sound.process_postarp_notes(&stack, nullptr, {&note});
	LONGS_EQUAL(1, started);
}

TEST_GROUP(sound_instrument_tick_lifetime) {
	std::unique_ptr<SoundInstrument> instrument;
	std::unique_ptr<Clip> clip;
	ModelStack stack;
	void reset() {
		instrument = std::make_unique<SoundInstrument>();
		clip = std::make_unique<Clip>();
		instrument->activeClip = clip.get();
		clip->output = instrument.get();
		on_generation = {};
		on_off = {};
		on_start = {};
		started = stopped = 0;
		voice_budget = 3;
		currentSong = &song;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_generation = {};
		on_off = {};
		on_start = {};
		currentSong = &song;
	}
	int32_t tick() {
		return instrument->doTickForwardForArp(&stack, 0);
	}
};
TEST(sound_instrument_tick_lifetime, live_tick_stops_and_starts_chord) {
	LONGS_EQUAL(7, tick());
	LONGS_EQUAL(2, stopped);
	LONGS_EQUAL(2, started);
}
TEST(sound_instrument_tick_lifetime, generation_can_destroy_output_and_clip) {
	on_generation = [&] {
		clip.reset();
		instrument.reset();
	};
	LONGS_EQUAL(2147483647, tick());
	LONGS_EQUAL(0, stopped);
	LONGS_EQUAL(0, started);
}
TEST(sound_instrument_tick_lifetime, each_output_callback_can_destroy_owners) {
	for (int stage = 1; stage <= 4; ++stage) {
		reset();
		auto destroy = [&] {
			if (started + stopped == stage) {
				clip.reset();
				instrument.reset();
			}
		};
		on_off = on_start = destroy;
		LONGS_EQUAL(2147483647, tick());
		LONGS_EQUAL(stage, started + stopped);
	}
}
TEST(sound_instrument_tick_lifetime, reset_can_free_instruction_with_owners_alive) {
	for (int stage = 1; stage <= 4; ++stage) {
		reset();
		auto replace = [&] {
			if (started + stopped == stage) {
				++instrument->arpeggiator.revision;
				instrument->arpeggiator.note.reset();
			}
		};
		on_off = on_start = replace;
		LONGS_EQUAL(2147483647, tick());
		LONGS_EQUAL(stage, started + stopped);
	}
}
TEST(sound_instrument_tick_lifetime, retargeted_clip_cancels_remaining_events) {
	on_off = [&] { instrument->activeClip = nullptr; };
	LONGS_EQUAL(2147483647, tick());
	LONGS_EQUAL(1, stopped);
	LONGS_EQUAL(0, started);
}
TEST(sound_instrument_tick_lifetime, parameter_collection_replacement_cancels_remaining_events) {
	UnpatchedParamSet replacement;
	on_off = [&] { clip->paramManager.unpatched_set = &replacement; };
	LONGS_EQUAL(2147483647, tick());
	LONGS_EQUAL(1, stopped);
	LONGS_EQUAL(0, started);
}
