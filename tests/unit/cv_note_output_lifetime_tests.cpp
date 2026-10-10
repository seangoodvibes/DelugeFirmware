#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace cv_note_output_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, BEND_RANGE_MAIN = 0, BEND_RANGE_FINGER_LEVEL = 1;
enum class PgmChangeSend { NEVER };
enum class CVMode { pitch, velocity, off };
struct ArpNote {
	int16_t mpeValues[3]{11, 22, 33};
	uint8_t velocity = 99;
	int outputMemberChannel[3]{-1, -1, -1};
};
int song;
int* currentSong = &song;
std::function<void()> on_bend, on_note, on_voltage, on_activate;
int bends = 0, notes = 0, voltages = 0, last_voltage = 0;
struct {
	void sendNote(bool on, int channel, int note) {
		CHECK(on);
		LONGS_EQUAL(1, channel);
		LONGS_EQUAL(60, note);
		++notes;
		if (on_note)
			on_note();
	}
	void sendVoltageOut(int channel, int value) {
		LONGS_EQUAL(1, channel);
		++voltages;
		last_voltage = value;
		if (on_voltage)
			on_voltage();
	}
} cvEngine;
struct CVInstrument;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	CVInstrument* output = nullptr;
	struct expression_set {
		int bendRanges[2]{2, 48};
		int getValue(int) { return 1234; }
	};
	struct {
		expression_set expression;
		bool present = true;
		expression_set* getExpressionParamSet() { return present ? &expression : nullptr; }
	} paramManager;
};
struct ModelStackWithTimelineCounter {
	int* song = currentSong;
	Clip* clip = nullptr;
	Clip* getTimelineCounter() { return clip; }
};
struct NonAudioInstrument {
	Clip* activeClip = nullptr;
	bool setActiveClip(ModelStackWithTimelineCounter* stack, PgmChangeSend) {
		auto* target = stack ? stack->clip : nullptr;
		bool changed = !stack || activeClip != target;
		activeClip = target;
		if (on_activate)
			on_activate();
		return changed;
	}
};
struct CVInstrument : NonAudioInstrument {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	int channel = 1;
	CVMode cvmode[2]{CVMode::pitch, CVMode::velocity};
	int32_t polyPitchBendValue = 0, monophonicPitchBendValue = 99;
	int cachedBendRanges[2]{};
	bool setActiveClip(ModelStackWithTimelineCounter*, PgmChangeSend);
	struct {
		uint64_t revision = 0;
		uint64_t instruction_revision() const { return revision; }
	} arpeggiator;
	int getChannel() const { return channel; }
	int getPitchChannel() const { return channel; }
	void updatePitchBendOutput(bool output) {
		CHECK_FALSE(output);
		++bends;
		if (on_bend)
			on_bend();
	}
	void noteOnPostArp(int32_t, ArpNote*, int32_t);
};
#include "cv_note_output_lifetime.inc"
} // namespace cv_note_output_lifetime_test
using namespace cv_note_output_lifetime_test;
TEST_GROUP(cv_note_output_lifetime) {
	std::unique_ptr<CVInstrument> instrument;
	std::unique_ptr<Clip> clip;
	std::unique_ptr<ArpNote> note;
	void reset() {
		instrument = std::make_unique<CVInstrument>();
		clip = std::make_unique<Clip>();
		note = std::make_unique<ArpNote>();
		clip->output = instrument.get();
		instrument->activeClip = clip.get();
		on_bend = on_activate = {};
		on_note = {};
		on_voltage = {};
		bends = notes = voltages = last_voltage = 0;
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_bend = on_activate = {};
		on_note = {};
		on_voltage = {};
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void send() {
		instrument->noteOnPostArp(60, note.get(), 0);
	}
};
TEST(cv_note_output_lifetime, live_note_publishes_channel_and_velocity) {
	on_note = [&] { LONGS_EQUAL(1, note->outputMemberChannel[0]); };
	send();
	LONGS_EQUAL(1, bends);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(1, voltages);
	LONGS_EQUAL(99 << 8, last_voltage);
}
TEST(cv_note_output_lifetime, owner_destruction_at_each_output_boundary_stops_later_output) {
	for (int stage = 0; stage < 3; ++stage) {
		reset();
		auto destroy = [&] {
			note.reset();
			clip.reset();
			instrument.reset();
		};
		if (stage == 0)
			on_bend = destroy;
		else if (stage == 1)
			on_note = destroy;
		else
			on_voltage = destroy;
		send();
		LONGS_EQUAL(stage >= 1 ? 1 : 0, notes);
		LONGS_EQUAL(stage == 2 ? 1 : 0, voltages);
	}
}
TEST(cv_note_output_lifetime, event_reset_can_free_note_before_or_after_note_output) {
	for (bool during_bend : {false, true}) {
		reset();
		auto replace = [&] {
			++instrument->arpeggiator.revision;
			note.reset();
		};
		if (during_bend)
			on_bend = replace;
		else
			on_note = replace;
		send();
		LONGS_EQUAL(during_bend ? 0 : 1, notes);
		LONGS_EQUAL(0, voltages);
	}
}
TEST(cv_note_output_lifetime, channel_and_mode_changes_cancel_later_output) {
	for (bool change_channel : {false, true}) {
		reset();
		on_bend = [&] {
			if (change_channel)
				instrument->channel = 0;
			else
				instrument->cvmode[1] = CVMode::off;
		};
		send();
		LONGS_EQUAL(0, notes);
		LONGS_EQUAL(0, voltages);
	}
}
TEST(cv_note_output_lifetime, negative_pitch_conversion_is_defined_at_int16_minimum) {
	note->mpeValues[0] = INT16_MIN;
	send();
	LONGS_EQUAL(INT32_MIN, instrument->polyPitchBendValue);
}
TEST(cv_note_output_lifetime, invalid_note_indices_and_retired_entry_emit_nothing) {
	instrument->noteOnPostArp(60, nullptr, 0);
	instrument->noteOnPostArp(60, note.get(), -1);
	instrument->noteOnPostArp(60, note.get(), ARP_MAX_INSTRUCTION_NOTES);
	instrument->lifetime.retire();
	send();
	LONGS_EQUAL(0, bends);
	LONGS_EQUAL(0, notes);
	LONGS_EQUAL(0, voltages);
}
TEST(cv_note_output_lifetime, clipless_note_output_remains_supported) {
	instrument->activeClip = nullptr;
	send();
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(1, voltages);
}

TEST(cv_note_output_lifetime, activation_caches_expression_without_changing_voltage) {
	Clip next;
	next.output = instrument.get();
	ModelStackWithTimelineCounter stack{currentSong, &next};
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	LONGS_EQUAL(1234, instrument->monophonicPitchBendValue);
	LONGS_EQUAL(48, instrument->cachedBendRanges[1]);
	LONGS_EQUAL(1, bends);
	LONGS_EQUAL(0, voltages);
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	LONGS_EQUAL(1, bends);
}
TEST(cv_note_output_lifetime, deactivation_and_missing_expression_clear_cached_pitch) {
	Clip next;
	next.output = instrument.get();
	next.paramManager.present = false;
	ModelStackWithTimelineCounter stack{currentSong, &next};
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	LONGS_EQUAL(0, instrument->monophonicPitchBendValue);
	instrument->monophonicPitchBendValue = 7;
	CHECK(instrument->setActiveClip(nullptr, PgmChangeSend::NEVER));
	LONGS_EQUAL(0, instrument->monophonicPitchBendValue);
	LONGS_EQUAL(2, bends);
}
TEST(cv_note_output_lifetime, activation_owner_deletion_cancels_parameter_reads) {
	for (bool delete_output : {false, true}) {
		reset();
		auto next = std::make_unique<Clip>();
		next->output = instrument.get();
		ModelStackWithTimelineCounter stack{currentSong, next.get()};
		on_activate = [&] {
			next.reset();
			if (delete_output)
				instrument.reset();
		};
		CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
		LONGS_EQUAL(0, bends);
	}
}
TEST(cv_note_output_lifetime, activation_context_changes_cancel_parameter_reads) {
	for (int mutation = 0; mutation < 8; ++mutation) {
		reset();
		Clip next;
		next.output = instrument.get();
		ModelStackWithTimelineCounter stack{currentSong, &next};
		on_activate = [&] {
			switch (mutation) {
			case 0:
				instrument->activeClip = nullptr;
				break;
			case 1:
				next.output = nullptr;
				break;
			case 2:
				currentSong = nullptr;
				break;
			case 3:
				stack.clip = nullptr;
				break;
			case 4:
				stack.song = nullptr;
				break;
			case 5:
				instrument->channel = 0;
				break;
			case 6:
				instrument->cvmode[1] = CVMode::off;
				break;
			case 7:
				deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote;
				break;
			}
		};
		CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
		LONGS_EQUAL(0, bends);
		LONGS_EQUAL(99, instrument->monophonicPitchBendValue);
	}
}
TEST(cv_note_output_lifetime, activation_final_bend_callback_can_delete_owners) {
	Clip next;
	next.output = instrument.get();
	ModelStackWithTimelineCounter stack{currentSong, &next};
	on_bend = [&] {
		instrument.reset();
		clip.reset();
	};
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	LONGS_EQUAL(1, bends);
}
TEST(cv_note_output_lifetime, activation_rejects_invalid_or_retired_target) {
	ModelStackWithTimelineCounter stack;
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	Clip next;
	stack.clip = &next;
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	next.output = instrument.get();
	next.lifetime.retire();
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	POINTERS_EQUAL(clip.get(), instrument->activeClip);
}
