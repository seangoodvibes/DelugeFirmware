#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
namespace sound_render_arp_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, ARP_NOTE_NONE = -1, kNumExpressionDimensions = 3;
enum class ArpMode { OFF, ON };
enum class ArpNoteStatus { OFF, PENDING, PLAYING };
enum class MIDICharacteristic { NOTE, CHANNEL };
namespace util {
template <class T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
namespace params {
constexpr int UNPATCHED_ARP_GATE = 0, GLOBAL_ARP_RATE = 0, FIRST_GLOBAL = 0;
}
struct ModelStackWithSoundFlags {};
struct UnpatchedParamSet {
	int getValue(int) { return 0; }
} unpatched;
struct ArpeggiatorSettings {
	ArpMode mode = ArpMode::ON;
	void updateParamsFromUnpatchedParamSet(UnpatchedParamSet*) {}
	uint32_t getPhaseIncrement(int) { return 1; }
};
struct ArpNote {
	int noteCodeOnPostArp[3]{60, 64, ARP_NOTE_NONE};
	ArpNoteStatus noteStatus[3]{ArpNoteStatus::PENDING, ArpNoteStatus::PENDING, ArpNoteStatus::OFF};
	int16_t inputCharacteristics[2]{50, 2}, mpeValues[3]{11, 22, 33};
	uint8_t velocity = 99;
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	bool invertReversed = true;
	int sampleSyncLengthOn = 16;
	int glideNoteCodeOffPostArp[3]{60, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int noteCodeOffPostArp[3]{64, ARP_NOTE_NONE, ARP_NOTE_NONE};
};
std::function<void()> on_generation, on_off, on_start;
int generated = 0, pending = 0, stopped = 0, started = 0, voice_budget = 3;
namespace AudioEngine {
bool allowedToStartVoice() {
	return started < voice_budget;
}
} // namespace AudioEngine
struct Arpeggiator {
	std::unique_ptr<ArpNote> note = std::make_unique<ArpNote>();
	uint64_t revision = 0;
	uint64_t instruction_revision() const { return revision; }
	void generate(ArpReturnInstruction* instruction) {
		++revision;
		instruction->arpNoteOn = note.get();
		if (on_generation)
			on_generation();
	}
	void render(ArpeggiatorSettings*, ArpReturnInstruction* instruction, uint32_t, uint32_t, uint32_t) {
		++generated;
		generate(instruction);
	}
	void handlePendingNotes(ArpeggiatorSettings*, ArpReturnInstruction* instruction) {
		++pending;
		generate(instruction);
	}
};
struct Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	Arpeggiator arp;
	ArpeggiatorSettings settings;
	ArpeggiatorSettings* selected_settings = &settings;
	Arpeggiator* getArp() { return &arp; }
	ArpeggiatorSettings* getArpSettings() { return selected_settings; }
	int paramFinalValues[1]{};
	bool invertReversed = false;
	void noteOffPostArpeggiator(ModelStackWithSoundFlags*, int) {
		++stopped;
		if (on_off)
			on_off();
	}
	void noteOnPostArpeggiator(ModelStackWithSoundFlags*, int, int, int, const int16_t* mpe, int, int, int, int) {
		++started;
		if (on_start)
			on_start();
		LONGS_EQUAL(11, mpe[0]);
		LONGS_EQUAL(22, mpe[1]);
		LONGS_EQUAL(33, mpe[2]);
	}
	void process_postarp_notes(ModelStackWithSoundFlags*, ArpeggiatorSettings*, ArpReturnInstruction,
	                           const deluge::lifetime::callback_validation*);
	bool process_render_arp(ModelStackWithSoundFlags*, UnpatchedParamSet*, uint32_t,
	                        const deluge::lifetime::callback_validation*);
};
#include "sound_render_arp_lifetime.inc"
} // namespace sound_render_arp_lifetime_test
using namespace sound_render_arp_lifetime_test;
TEST_GROUP(sound_render_arp_lifetime) {
	std::unique_ptr<Sound> sound;
	ModelStackWithSoundFlags stack;
	void reset() {
		sound = std::make_unique<Sound>();
		on_generation = on_off = on_start = {};
		generated = pending = stopped = started = 0;
		voice_budget = 3;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_generation = on_off = on_start = {};
	}
	bool render() {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive(); };
		const deluge::lifetime::callback_validation validation{valid};
		return sound->process_render_arp(&stack, &unpatched, 32, &validation);
	}
};
TEST(sound_render_arp_lifetime, live_arp_and_pending_modes_dispatch_complete_batch) {
	for (int mode = 0; mode < 3; ++mode) {
		reset();
		if (mode == 1)
			sound->settings.mode = ArpMode::OFF;
		if (mode == 2)
			sound->selected_settings = nullptr;
		CHECK(render());
		LONGS_EQUAL(mode == 0 ? 1 : 0, generated);
		LONGS_EQUAL(mode == 0 ? 0 : 1, pending);
		LONGS_EQUAL(2, stopped);
		LONGS_EQUAL(2, started);
	}
}
TEST(sound_render_arp_lifetime, generation_deletion_stops_before_instruction_access) {
	on_generation = [&] { sound.reset(); };
	CHECK_FALSE(render());
	LONGS_EQUAL(0, stopped);
	LONGS_EQUAL(0, started);
}
TEST(sound_render_arp_lifetime, each_note_callback_can_delete_the_sound) {
	for (int boundary = 1; boundary <= 4; ++boundary) {
		reset();
		auto destroy = [&] {
			if (stopped + started == boundary)
				sound.reset();
		};
		on_off = on_start = destroy;
		CHECK_FALSE(render());
		LONGS_EQUAL(boundary, stopped + started);
	}
}
TEST(sound_render_arp_lifetime, replacement_instruction_cancels_before_using_freed_pending_note) {
	for (int boundary = 1; boundary <= 4; ++boundary) {
		reset();
		auto replace = [&] {
			if (stopped + started == boundary) {
				++sound->arp.revision;
				sound->arp.note.reset();
			}
		};
		on_off = on_start = replace;
		CHECK_FALSE(render());
		LONGS_EQUAL(boundary, stopped + started);
	}
}
TEST(sound_render_arp_lifetime, changed_settings_cancel_generated_instruction) {
	on_generation = [&] { sound->selected_settings = nullptr; };
	CHECK_FALSE(render());
	LONGS_EQUAL(0, stopped);
}
TEST(sound_render_arp_lifetime, rejected_owner_does_not_generate) {
	sound->lifetime.retire();
	CHECK_FALSE(render());
	LONGS_EQUAL(0, generated);
}
TEST(sound_render_arp_lifetime, voice_budget_keeps_remaining_note_pending) {
	voice_budget = 1;
	CHECK(render());
	LONGS_EQUAL(1, started);
	CHECK(sound->arp.note->noteStatus[1] == ArpNoteStatus::PENDING);
}
