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
constexpr int UNPATCHED_ARP_GATE = 0, GLOBAL_ARP_RATE = 0, FIRST_GLOBAL = 0, LOCAL_NOISE_VOLUME = 1;
}
struct ModelStackWithSoundFlags {
	bool checkSourceEverActive(int) { return true; }
};
struct UnpatchedParamSet {
	int getValue(int) { return 0; }
} unpatched;
enum class SynthMode { RINGMOD, OTHER };
struct PatchedParamSet {
	bool containsSomething(int, int) { return true; }
};
struct ParamManagerForTimeline {
	UnpatchedParamSet* unpatched_set = &unpatched;
	PatchedParamSet patched;
	UnpatchedParamSet* getUnpatchedParamSet() { return unpatched_set; }
	PatchedParamSet* getPatchedParamSet() { return &patched; }
};
struct ModelStackWithThreeMainThings {
	ParamManagerForTimeline* paramManager = nullptr;
	ModelStackWithSoundFlags flags;
	ModelStackWithSoundFlags* addSoundFlags() { return &flags; }
};
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
std::function<void()> on_generation, on_off, on_start, on_rewind, on_reassess;
int reassessments = 0;
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
	void noteOn(ArpeggiatorSettings*, int, int, ArpReturnInstruction* instruction, int, const int16_t*) {
		++generated;
		generate(instruction);
	}
	bool hasAnyInputNotesActive() { return true; }

	void render(ArpeggiatorSettings*, ArpReturnInstruction* instruction, uint32_t, uint32_t, uint32_t) {
		++generated;
		generate(instruction);
	}
	void handlePendingNotes(ArpeggiatorSettings*, ArpReturnInstruction* instruction) {
		++pending;
		generate(instruction);
	}
};
using ArpeggiatorBase = Arpeggiator;
struct Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	Arpeggiator arp;
	ArpeggiatorSettings settings;
	ArpeggiatorSettings* selected_settings = &settings;
	Arpeggiator* getArp() { return &arp; }
	ArpeggiatorSettings* getArpSettings() { return selected_settings; }
	int paramFinalValues[1]{};
	SynthMode synthMode = SynthMode::RINGMOD;
	void getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings*) {
		if (on_rewind)
			on_rewind();
	}
	void reassessRenderSkippingStatus(ModelStackWithSoundFlags*) {
		++reassessments;
		if (on_reassess)
			on_reassess();
	}
	void noteOn(ModelStackWithThreeMainThings*, Arpeggiator*, int32_t, const int16_t*, uint32_t, int32_t, uint32_t,
	            int32_t, int32_t, const deluge::lifetime::callback_validation*);
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
		on_generation = on_off = on_start = on_rewind = on_reassess = {};
		generated = pending = stopped = started = reassessments = 0;
		voice_budget = 3;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_generation = on_off = on_start = on_rewind = on_reassess = {};
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

TEST(sound_render_arp_lifetime, direct_note_publishes_status_and_copies_input_before_generation) {
	ParamManagerForTimeline manager;
	ModelStackWithThreeMainThings model_stack{&manager};
	auto mpe = std::make_unique<int16_t[]>(3);
	mpe[0] = 11;
	mpe[1] = 22;
	mpe[2] = 33;
	on_generation = [&] { mpe.reset(); };
	on_start = [&] { CHECK(sound->arp.note->noteStatus[started - 1] == ArpNoteStatus::PLAYING); };
	deluge::lifetime::lifetime_watch watch{sound->lifetime};
	const auto valid = [&] { return watch.alive(); };
	const deluge::lifetime::callback_validation validation{valid};
	sound->noteOn(&model_stack, &sound->arp, 50, mpe.get(), 16, 0, 0, 99, 2, &validation);
	LONGS_EQUAL(2, started);
}
TEST(sound_render_arp_lifetime, direct_note_owner_deletion_cancels_at_each_boundary) {
	for (int boundary = 0; boundary < 4; ++boundary) {
		reset();
		ParamManagerForTimeline manager;
		ModelStackWithThreeMainThings model_stack{&manager};
		int16_t mpe[3]{11, 22, 33};
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive(); };
		const deluge::lifetime::callback_validation validation{valid};
		if (boundary == 0)
			on_rewind = [&] { sound.reset(); };
		else if (boundary == 1)
			on_generation = [&] { sound.reset(); };
		else
			on_start = [&] {
				if (started == boundary - 1)
					sound.reset();
			};
		sound->noteOn(&model_stack, &sound->arp, 50, mpe, 16, 0, 0, 99, 2, &validation);
		LONGS_EQUAL(boundary < 2 ? 0 : boundary - 1, started);
	}
}
TEST(sound_render_arp_lifetime, direct_note_stops_after_instruction_replacement) {
	ParamManagerForTimeline manager;
	ModelStackWithThreeMainThings model_stack{&manager};
	int16_t mpe[3]{11, 22, 33};
	on_start = [&] {
		++sound->arp.revision;
		sound->arp.note.reset();
	};
	sound->noteOn(&model_stack, &sound->arp, 50, mpe, 16, 0, 0, 99, 2, nullptr);
	LONGS_EQUAL(1, started);
}
TEST(sound_render_arp_lifetime, direct_note_cancels_changed_parameter_association) {
	ParamManagerForTimeline manager;
	UnpatchedParamSet replacement;
	ModelStackWithThreeMainThings model_stack{&manager};
	int16_t mpe[3]{11, 22, 33};
	on_generation = [&] { manager.unpatched_set = &replacement; };
	sound->noteOn(&model_stack, &sound->arp, 50, mpe, 16, 0, 0, 99, 2, nullptr);
	LONGS_EQUAL(0, started);
}
TEST(sound_render_arp_lifetime, direct_note_budget_deferral_reassesses_rendering) {
	voice_budget = 1;
	ParamManagerForTimeline manager;
	ModelStackWithThreeMainThings model_stack{&manager};
	int16_t mpe[3]{11, 22, 33};
	sound->noteOn(&model_stack, &sound->arp, 50, mpe, 16, 0, 0, 99, 2, nullptr);
	LONGS_EQUAL(1, started);
	LONGS_EQUAL(1, reassessments);
	CHECK(sound->arp.note->noteStatus[1] == ArpNoteStatus::PENDING);
}
