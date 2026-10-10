#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
namespace sound_voice_release_lifetime_test {
constexpr int ALL_NOTES_OFF = -32768, kNumExpressionDimensions = 3;
enum class ArpMode { OFF, ON };
enum class PolyphonyMode { POLY, MONO, LEGATO };
enum class EnvelopeStage { ATTACK, RELEASE };
enum class MIDICharacteristic { NOTE, CHANNEL };
namespace util {
template <typename T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
struct ModelStackWithSoundFlags {};
struct ArpNote {
	int inputCharacteristics[2]{72, 3};
	int16_t mpeValues[3]{-11, 22, -33};
};
struct ArpeggiatorSettings {
	ArpMode mode = ArpMode::ON;
};
struct Arpeggiator {
	uint64_t revision = 0;
	int lastVelocity = 99;
	struct {
		std::unique_ptr<ArpNote> note = std::make_unique<ArpNote>();
		int getNumElements() { return note ? 1 : 0; }
		void* getElementAddress(int) { return note.get(); }
	} notes;
	uint64_t instruction_revision() const { return revision; }
	bool hasAnyInputNotesActive() { return notes.note != nullptr; }
};
std::function<void()> on_midi, on_release, on_change, on_start, on_tails;
int released, changed, started;
void check_mpe(const int16_t* mpe) {
	LONGS_EQUAL(-11, mpe[0]);
	LONGS_EQUAL(22, mpe[1]);
	LONGS_EQUAL(-33, mpe[2]);
}
struct Voice {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
	int noteCodeAfterArpeggiation = 60;
	struct {
		EnvelopeStage state = EnvelopeStage::ATTACK;
	} envelopes[1];
	void noteOff(ModelStackWithSoundFlags*) {
		++released;
		if (on_release)
			on_release();
	}
	void changeNoteCode(ModelStackWithSoundFlags*, int note, int, int channel, const int16_t* mpe) {
		LONGS_EQUAL(72, note);
		LONGS_EQUAL(3, channel);
		++changed;
		if (on_change)
			on_change();
		check_mpe(mpe);
	}
};
struct Sound {
	deluge::lifetime::lifetime_source lifetime;
	std::vector<std::unique_ptr<Voice>> voices_;
	ArpeggiatorSettings settings;
	ArpeggiatorSettings* selected_settings = &settings;
	Arpeggiator* arp = nullptr;
	PolyphonyMode polyphonic = PolyphonyMode::POLY;
	int lastNoteCode = 60;
	bool tails = true, drum = false, midi_result = true;
	ArpeggiatorSettings* getArpSettings() { return selected_settings; }
	Arpeggiator* getArp() { return arp; }
	bool isDrum() { return drum; }
	bool allowNoteTails(ModelStackWithSoundFlags*) {
		const auto result = tails;
		if (on_tails)
			on_tails();
		return result;
	}
	bool send_note_off_midi(ModelStackWithSoundFlags*, int, const deluge::lifetime::callback_validation* validation) {
		if (!validation->valid())
			return false;
		const auto result = midi_result;
		if (on_midi)
			on_midi();
		return validation->valid() && result;
	}
	void noteOnPostArpeggiator(ModelStackWithSoundFlags*, int note, int, int velocity, const int16_t* mpe, int, int,
	                           int, int channel) {
		LONGS_EQUAL(72, note);
		LONGS_EQUAL(99, velocity);
		LONGS_EQUAL(3, channel);
		++started;
		if (on_start)
			on_start();
		check_mpe(mpe);
	}
	bool noteOffPostArpeggiator(ModelStackWithSoundFlags*, int32_t, const deluge::lifetime::callback_validation*);
};
struct SoundInstrument : Sound {
	Arpeggiator arpeggiator;
	SoundInstrument() { arp = &arpeggiator; }
};
#include "sound_voice_release_lifetime.inc"
} // namespace sound_voice_release_lifetime_test
using namespace sound_voice_release_lifetime_test;
TEST_GROUP(sound_voice_release_lifetime) {
	std::unique_ptr<SoundInstrument> sound;
	ModelStackWithSoundFlags stack;
	void reset() {
		on_midi = on_release = on_change = on_start = on_tails = {};
		released = changed = started = 0;
		sound = std::make_unique<SoundInstrument>();
		for (int i = 0; i < 3; ++i)
			sound->voices_.push_back(std::make_unique<Voice>());
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_midi = on_release = on_change = on_start = on_tails = {};
	}
	bool release(int note = ALL_NOTES_OFF) {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive(); };
		const deluge::lifetime::callback_validation validation{valid};
		return sound->noteOffPostArpeggiator(&stack, note, &validation);
	}
	void mono(PolyphonyMode mode) {
		sound->polyphonic = mode;
		sound->settings.mode = ArpMode::OFF;
	}
};
TEST(sound_voice_release_lifetime, releases_only_matching_unreleased_voices) {
	sound->voices_[1]->noteCodeAfterArpeggiation = 61;
	sound->voices_[2]->envelopes[0].state = EnvelopeStage::RELEASE;
	CHECK(release(60));
	LONGS_EQUAL(1, released);
}
TEST(sound_voice_release_lifetime, every_voice_release_can_delete_owner) {
	for (int boundary = 1; boundary <= 3; ++boundary) {
		reset();
		on_release = [&] {
			if (released == boundary)
				sound.reset();
		};
		CHECK_FALSE(release());
		LONGS_EQUAL(boundary, released);
	}
}
TEST(sound_voice_release_lifetime, vector_changes_cancel_remaining_releases) {
	for (int change = 0; change < 3; ++change) {
		reset();
		on_release = [&] {
			if (change == 0)
				sound->voices_.clear();
			else if (change == 1)
				sound->voices_.push_back(std::make_unique<Voice>());
			else
				sound->voices_[0] = std::make_unique<Voice>();
		};
		CHECK_FALSE(release());
		LONGS_EQUAL(1, released);
	}
}
TEST(sound_voice_release_lifetime, same_address_voice_reconstruction_cancels) {
	on_release = [&] {
		auto* voice = sound->voices_[0].get();
		voice->~Voice();
		new (voice) Voice;
	};
	CHECK_FALSE(release());
	LONGS_EQUAL(1, released);
}
TEST(sound_voice_release_lifetime, arp_or_settings_changes_cancel_remaining_releases) {
	for (int change = 0; change < 4; ++change) {
		reset();
		on_release = [&] {
			if (change == 0)
				++sound->arpeggiator.revision;
			else if (change == 1)
				sound->selected_settings = nullptr;
			else if (change == 2)
				sound->settings.mode = ArpMode::OFF;
			else
				sound->polyphonic = PolyphonyMode::MONO;
		};
		CHECK_FALSE(release());
		LONGS_EQUAL(1, released);
	}
}
TEST(sound_voice_release_lifetime, cancelled_midi_prevents_voice_access) {
	sound->midi_result = false;
	CHECK_FALSE(release());
	LONGS_EQUAL(0, released);
	on_midi = [&] { sound.reset(); };
	CHECK_FALSE(release());
	LONGS_EQUAL(0, released);
}
TEST(sound_voice_release_lifetime, legato_returns_to_held_note) {
	mono(PolyphonyMode::LEGATO);
	CHECK(release());
	LONGS_EQUAL(3, changed);
	LONGS_EQUAL(72, sound->lastNoteCode);
	LONGS_EQUAL(0, released);
}
TEST(sound_voice_release_lifetime, legato_owner_deletion_preserves_copied_expression) {
	mono(PolyphonyMode::LEGATO);
	on_change = [&] { sound.reset(); };
	CHECK_FALSE(release());
	LONGS_EQUAL(1, changed);
}
TEST(sound_voice_release_lifetime, legato_replacement_does_not_publish_last_note) {
	mono(PolyphonyMode::LEGATO);
	on_change = [&] {
		++sound->arpeggiator.revision;
		sound->arpeggiator.notes.note.reset();
	};
	CHECK_FALSE(release());
	LONGS_EQUAL(60, sound->lastNoteCode);
}
TEST(sound_voice_release_lifetime, mono_allows_expected_voice_replacement) {
	mono(PolyphonyMode::MONO);
	on_start = [&] { sound->voices_.clear(); };
	CHECK(release());
	LONGS_EQUAL(1, started);
	LONGS_EQUAL(0, released);
}
TEST(sound_voice_release_lifetime, mono_owner_deletion_preserves_copied_expression) {
	mono(PolyphonyMode::MONO);
	on_start = [&] { sound.reset(); };
	CHECK_FALSE(release());
	LONGS_EQUAL(1, started);
}
TEST(sound_voice_release_lifetime, mono_without_held_notes_still_releases) {
	mono(PolyphonyMode::MONO);
	sound->arpeggiator.notes.note.reset();
	CHECK(release());
	LONGS_EQUAL(3, released);
}
TEST(sound_voice_release_lifetime, one_shot_mono_still_delivers_voice_note_off) {
	mono(PolyphonyMode::MONO);
	sound->tails = false;
	CHECK(release());
	LONGS_EQUAL(3, released);
	LONGS_EQUAL(0, started);
}
TEST(sound_voice_release_lifetime, tails_callback_deletion_cancels_before_voice_access) {
	mono(PolyphonyMode::LEGATO);
	on_tails = [&] { sound.reset(); };
	CHECK_FALSE(release());
	LONGS_EQUAL(0, changed);
	LONGS_EQUAL(0, released);
}
