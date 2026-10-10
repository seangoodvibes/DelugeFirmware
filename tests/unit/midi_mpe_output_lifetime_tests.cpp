#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
namespace midi_mpe_output_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, kNumExpressionDimensions = 3;
constexpr int MIDI_CHANNEL_MPE_LOWER_ZONE = 16, MIDI_CHANNEL_NONE = 255, kMIDIOutputFilterNoMPE = -1;
constexpr int X_PITCH_BEND = 0, Y_SLIDE_TIMBRE = 1, Z_PRESSURE = 2;
constexpr int BEND_RANGE_MAIN = 0, BEND_RANGE_FINGER_LEVEL = 1;
enum class PgmChangeSend { NEVER, ALWAYS };
enum class ArpMode { OFF, ON };
enum class MIDICharacteristic { NOTE, CHANNEL };
namespace util {
template <class T>
int to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
namespace MIDIDeviceManager {
int highestLastMemberChannelOfUpperZoneOnConnectedOutput = 14;
int lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 1;
} // namespace MIDIDeviceManager
int song;
int* currentSong = &song;
uint16_t lastNoteOffOrder = 1;
constexpr int shiftAmountsFrom16Bit[3]{2, 9, 8};
struct ArpNote {
	int outputMemberChannel[3]{MIDI_CHANNEL_NONE, MIDI_CHANNEL_NONE, MIDI_CHANNEL_NONE};
	int16_t inputCharacteristics[2]{60, 2};
	int16_t mpeValues[3]{400, 512, 768};
	uint8_t velocity = 99;
};
struct MIDIInstrument;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	MIDIInstrument* output = nullptr;
	int midiPGM = 0, midiSub = 0, midiBank = 0;
	struct expression_set {
		int bendRanges[2]{2, 48};
	};
	struct {
		expression_set expression;
		expression_set* getExpressionParamSet() { return &expression; }
	} paramManager;
	struct {
		ArpMode mode = ArpMode::OFF;
	} arpSettings;
};
using ArpeggiatorSettings = decltype(InstrumentClip::arpSettings);
std::function<void()> on_output;
int outputs = 0, notes = 0, combines = 0, last_note_velocity = 0;
int pitch_value = 0, slide_value = 0, pressure_value = 0;
std::vector<int> all_off_channels;
struct {
	void sendAllNotesOff(MIDIInstrument*, int channel, int) {
		all_off_channels.push_back(channel);
		++outputs;
		if (on_output)
			on_output();
	}
	void sendPitchBend(MIDIInstrument*, int, int value, int) {
		pitch_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendCC(MIDIInstrument*, int, int, int value, int) {
		slide_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendPolyphonicAftertouch(MIDIInstrument*, int, int value, int, int) {
		pressure_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendChannelAftertouch(MIDIInstrument*, int, int value, int) {
		pressure_value = value;
		++outputs;
		if (on_output)
			on_output();
	}
	void sendNote(MIDIInstrument*, bool, int, int velocity, int, int) {
		last_note_velocity = velocity;
		++notes;
		++outputs;
		if (on_output)
			on_output();
	}
} midiEngine;
struct ModelStackWithTimelineCounter {
	int* song = currentSong;
	InstrumentClip* clip = nullptr;
	InstrumentClip* getTimelineCounter() { return clip; }
};
std::function<void()> on_activate;
int programs = 0, expressions = 0;
struct NonAudioInstrument {
	InstrumentClip* activeClip = nullptr;
	bool setActiveClip(ModelStackWithTimelineCounter* stack, PgmChangeSend) {
		auto* target = stack ? stack->clip : nullptr;
		bool changed = !stack || target != activeClip;
		activeClip = target;
		if (on_activate)
			on_activate();
		return changed;
	}
};
struct MIDIInstrument : NonAudioInstrument {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	int channel = MIDI_CHANNEL_MPE_LOWER_ZONE, outputMPEY = 74;
	bool mpe = true, internal = false, collapseAftertouch = false, collapseMPE = false;
	int getChannel() const { return channel; }
	bool sendsToMPE() const { return mpe; }
	bool sendsToInternal() const { return internal; }
	struct {
		uint64_t revision = 0;
		uint64_t instruction_revision() const { return revision; }
		void reset() {
			++revision;
			notes.entries.clear();
		}
		struct {
			std::vector<ArpNote*> entries;
			int getNumElements() const { return entries.size(); }
			void* getElementAddress(int index) { return entries.at(index); }
		} notes;
	} arpeggiator;
	struct member {
		int lastNoteCode = -1;
		uint16_t noteOffOrder = 0;
		int lastXValueSent = 0, lastYAndZValuesSent[2]{};
	} mpeOutputMemberChannels[16];
	void sendNoteToInternal(bool, int, int, int) {
		++notes;
		++outputs;
		if (on_output)
			on_output();
	}
	void combineMPEtoMono(int, int) {
		++combines;
		if (on_output)
			on_output();
	}
	bool outputAllMPEValuesOnMemberChannel(const int16_t*, int32_t);
	void noteOnPostArp(int32_t, ArpNote*, int32_t);
	ArpeggiatorSettings* getArpSettings() { return activeClip ? &activeClip->arpSettings : nullptr; }
	void polyphonicExpressionEventPostArpeggiator(int32_t, int32_t, int32_t, ArpNote*, int32_t);
	int cachedBendRanges[2]{}, lastCombinedPolyExpression[3]{11, 22, 33};
	float ratio = 0;
	void sendMIDIPGM() {
		++programs;
		if (on_output)
			on_output();
	}
	void sendMonophonicExpressionEvent(int) {
		++expressions;
		if (on_output)
			on_output();
	}
	bool setActiveClip(ModelStackWithTimelineCounter*, PgmChangeSend);
	bool stop_all_notes();
	void allNotesOff();
	void noteOffPostArp(int32_t, int32_t, int32_t, int32_t);
};
#include "midi_mpe_output_lifetime.inc"
} // namespace midi_mpe_output_lifetime_test
using namespace midi_mpe_output_lifetime_test;
TEST_GROUP(midi_mpe_output_lifetime) {
	std::unique_ptr<MIDIInstrument> instrument;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<ArpNote> note;
	void reset() {
		instrument = std::make_unique<MIDIInstrument>();
		clip = std::make_unique<InstrumentClip>();
		note = std::make_unique<ArpNote>();
		instrument->activeClip = clip.get();
		clip->output = instrument.get();
		instrument->arpeggiator.notes.entries = {note.get()};
		on_output = on_activate = {};
		programs = expressions = 0;
		all_off_channels.clear();
		outputs = notes = combines = 0;
		pitch_value = slide_value = pressure_value = 0;
		currentSong = &song;
		MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 1;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_output = on_activate = {};
		currentSong = &song;
	}
	void send() {
		instrument->noteOnPostArp(60, note.get(), 0);
	}
};
TEST(midi_mpe_output_lifetime, live_mpe_values_precede_note_output) {
	send();
	LONGS_EQUAL(4, outputs);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(8292, pitch_value);
	LONGS_EQUAL(65, slide_value);
	LONGS_EQUAL(3, pressure_value);
	LONGS_EQUAL(1, note->outputMemberChannel[0]);
}
TEST(midi_mpe_output_lifetime, every_expression_output_can_destroy_owners_and_cancel_note) {
	for (int stage = 1; stage <= 3; ++stage) {
		reset();
		on_output = [&] {
			if (outputs == stage) {
				note.reset();
				clip.reset();
				instrument.reset();
			}
		};
		send();
		LONGS_EQUAL(stage, outputs);
		LONGS_EQUAL(0, notes);
	}
}
TEST(midi_mpe_output_lifetime, revision_change_can_free_note_without_destroying_output) {
	for (int stage = 1; stage <= 3; ++stage) {
		reset();
		on_output = [&] {
			if (outputs == stage) {
				++instrument->arpeggiator.revision;
				instrument->arpeggiator.notes.entries.clear();
				note.reset();
			}
		};
		send();
		LONGS_EQUAL(stage, outputs);
		LONGS_EQUAL(0, notes);
	}
}
TEST(midi_mpe_output_lifetime, expression_sequence_uses_stable_input_values) {
	on_output = [&] {
		note->mpeValues[1] = 2048;
		note->mpeValues[2] = 4096;
	};
	CHECK(instrument->outputAllMPEValuesOnMemberChannel(note->mpeValues, 1));
	LONGS_EQUAL(65, slide_value);
	LONGS_EQUAL(3, pressure_value);
}
TEST(midi_mpe_output_lifetime, channel_and_y_assignment_changes_cancel_expression_sequence) {
	for (bool change_channel : {false, true}) {
		reset();
		on_output = [&] {
			if (change_channel)
				++instrument->channel;
			else
				++instrument->outputMPEY;
		};
		send();
		LONGS_EQUAL(1, outputs);
		LONGS_EQUAL(0, notes);
	}
}
TEST(midi_mpe_output_lifetime, invalid_member_channels_and_zone_bounds_do_not_access_arrays) {
	CHECK_FALSE(instrument->outputAllMPEValuesOnMemberChannel(note->mpeValues, -1));
	CHECK_FALSE(instrument->outputAllMPEValuesOnMemberChannel(note->mpeValues, 16));
	MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 0;
	send();
	LONGS_EQUAL(0, outputs);
}
TEST(midi_mpe_output_lifetime, non_mpe_and_internal_notes_keep_single_output_behavior) {
	for (bool internal : {false, true}) {
		reset();
		instrument->mpe = false;
		instrument->internal = internal;
		instrument->channel = 1;
		send();
		LONGS_EQUAL(1, outputs);
		LONGS_EQUAL(1, notes);
	}
}
TEST(midi_mpe_output_lifetime, note_off_destruction_stops_mpe_traversal_and_mono_collapse) {
	for (bool mpe : {false, true}) {
		reset();
		instrument->mpe = mpe;
		instrument->collapseAftertouch = instrument->collapseMPE = true;
		on_output = [&] {
			note.reset();
			clip.reset();
			instrument.reset();
		};
		instrument->noteOffPostArp(60, 1, 64, 0);
		LONGS_EQUAL(1, notes);
		LONGS_EQUAL(0, combines);
	}
}
TEST(midi_mpe_output_lifetime, nested_reset_stops_mono_collapse_sequence) {
	instrument->mpe = false;
	instrument->collapseAftertouch = instrument->collapseMPE = true;
	on_output = [&] {
		if (combines == 1)
			++instrument->arpeggiator.revision;
	};
	instrument->noteOffPostArp(60, 1, 64, 0);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(1, combines);
}
TEST(midi_mpe_output_lifetime, shared_member_note_off_recomputes_expression_values) {
	note->outputMemberChannel[0] = 1;
	instrument->noteOffPostArp(61, 1, 64, 0);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(4, outputs);
	LONGS_EQUAL(8292, pitch_value);
	LONGS_EQUAL(65, slide_value);
	LONGS_EQUAL(3, pressure_value);
}
TEST(midi_mpe_output_lifetime, shared_member_note_on_preserves_mpe_averaging) {
	ArpNote existing;
	existing.outputMemberChannel[0] = 1;
	existing.mpeValues[0] = 800;
	existing.mpeValues[1] = 1536;
	existing.mpeValues[2] = 1280;
	instrument->arpeggiator.notes.entries.push_back(&existing);
	send();
	LONGS_EQUAL(4, outputs);
	LONGS_EQUAL(8342, pitch_value);
	LONGS_EQUAL(66, slide_value);
	LONGS_EQUAL(4, pressure_value);
}

TEST(midi_mpe_output_lifetime, note_velocity_is_snapshotted_before_expression_callbacks) {
	on_output = [&] { note->velocity = 17; };
	send();
	LONGS_EQUAL(99, last_note_velocity);
}
TEST(midi_mpe_output_lifetime, zone_reconfiguration_cancels_expression_sequence) {
	on_output = [&] { ++MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput; };
	send();
	LONGS_EQUAL(1, outputs);
	LONGS_EQUAL(0, notes);
}
TEST(midi_mpe_output_lifetime, collapse_setting_change_does_not_start_new_cleanup_sequence) {
	instrument->mpe = false;
	on_output = [&] { instrument->collapseMPE = true; };
	instrument->noteOffPostArp(60, 1, 64, 0);
	LONGS_EQUAL(1, notes);
	LONGS_EQUAL(0, combines);
}

TEST(midi_mpe_output_lifetime, all_notes_off_covers_master_and_both_zone_ranges) {
	for (int master : {16, 17}) {
		reset();
		instrument->channel = master;
		instrument->allNotesOff();
		LONGS_EQUAL(2, all_off_channels.size());
		LONGS_EQUAL(master == 16 ? 0 : 14, all_off_channels[0]);
		LONGS_EQUAL(master == 16 ? 1 : 15, all_off_channels[1]);
	}
}
TEST(midi_mpe_output_lifetime, all_notes_off_owner_destruction_stops_channel_sweep) {
	on_output = [&] {
		note.reset();
		clip.reset();
		instrument.reset();
	};
	instrument->allNotesOff();
	LONGS_EQUAL(1, all_off_channels.size());
}
TEST(midi_mpe_output_lifetime, all_notes_off_preserves_nested_replacement_event) {
	on_output = [&] { ++instrument->arpeggiator.revision; };
	instrument->allNotesOff();
	LONGS_EQUAL(1, all_off_channels.size());
}
TEST(midi_mpe_output_lifetime, all_notes_off_zone_and_channel_changes_cancel_sweep) {
	for (bool change_zone : {false, true}) {
		reset();
		on_output = [&] {
			if (change_zone)
				++MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
			else
				++instrument->channel;
		};
		instrument->allNotesOff();
		LONGS_EQUAL(1, all_off_channels.size());
	}
}
TEST(midi_mpe_output_lifetime, all_notes_off_rejects_invalid_zone_and_retired_entry) {
	MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 16;
	instrument->allNotesOff();
	LONGS_EQUAL(0, all_off_channels.size());
	instrument->lifetime.retire();
	const auto revision = instrument->arpeggiator.revision;
	instrument->allNotesOff();
	CHECK(instrument->arpeggiator.revision == revision);
}
TEST(midi_mpe_output_lifetime, clipless_mono_all_notes_off_still_sends_once) {
	instrument->activeClip = nullptr;
	instrument->mpe = false;
	instrument->channel = 3;
	instrument->allNotesOff();
	LONGS_EQUAL(1, all_off_channels.size());
	LONGS_EQUAL(3, all_off_channels[0]);
}

TEST(midi_mpe_output_lifetime, expression_rejects_invalid_dimensions_indices_and_channels) {
	for (int dimension : {-1, 3})
		instrument->polyphonicExpressionEventPostArpeggiator(0, 60, dimension, note.get(), 0);
	for (int index : {-1, ARP_MAX_INSTRUCTION_NOTES})
		instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), index);
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, nullptr, 0);
	for (int channel : {-1, 16, MIDI_CHANNEL_NONE}) {
		note->outputMemberChannel[0] = channel;
		instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), 0);
	}
	LONGS_EQUAL(0, outputs);
}
TEST(midi_mpe_output_lifetime, expression_rejects_retired_or_reassigned_owners) {
	note->outputMemberChannel[0] = 1;
	clip->output = nullptr;
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), 0);
	clip->output = instrument.get();
	clip->lifetime.retire();
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), 0);
	instrument->activeClip = nullptr;
	instrument->lifetime.retire();
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), 0);
	LONGS_EQUAL(0, outputs);
}
TEST(midi_mpe_output_lifetime, expression_averages_negative_pitch_and_suppresses_unchanged_output) {
	ArpNote second;
	second.outputMemberChannel[0] = note->outputMemberChannel[0] = 1;
	second.mpeValues[0] = note->mpeValues[0] = INT16_MIN;
	instrument->arpeggiator.notes.entries.push_back(&second);
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), 0);
	LONGS_EQUAL(1, outputs);
	LONGS_EQUAL(0, pitch_value);
	LONGS_EQUAL(-8192, instrument->mpeOutputMemberChannels[1].lastXValueSent);
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, note.get(), 0);
	LONGS_EQUAL(1, outputs);
}
TEST(midi_mpe_output_lifetime, expression_outputs_each_dimension_and_allows_final_callback_deletion) {
	for (int dimension = 0; dimension < 3; ++dimension) {
		reset();
		note->outputMemberChannel[0] = 1;
		on_output = [&] {
			instrument.reset();
			clip.reset();
			note.reset();
		};
		instrument->polyphonicExpressionEventPostArpeggiator(1 << 26, 60, dimension, note.get(), 0);
		LONGS_EQUAL(1, outputs);
	}
}
TEST(midi_mpe_output_lifetime, expression_preserves_mono_and_internal_routing_without_arp_note) {
	instrument->mpe = false;
	instrument->polyphonicExpressionEventPostArpeggiator(1 << 26, 60, 2, nullptr, 0);
	LONGS_EQUAL(1, outputs);
	LONGS_EQUAL(4, pressure_value);
	instrument->collapseMPE = true;
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, nullptr, 0);
	LONGS_EQUAL(1, combines);
	instrument->internal = true;
	instrument->polyphonicExpressionEventPostArpeggiator(0, 60, 0, nullptr, 0);
	LONGS_EQUAL(1, combines);
}

TEST(midi_mpe_output_lifetime, activation_sends_changed_program_and_caches_bend_ranges) {
	InstrumentClip next;
	next.output = instrument.get();
	next.midiPGM = 9;
	ModelStackWithTimelineCounter stack{currentSong, &next};
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
	LONGS_EQUAL(1, programs);
	LONGS_EQUAL(48, instrument->cachedBendRanges[1]);
	DOUBLES_EQUAL(24, instrument->ratio, 0.001);
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
	LONGS_EQUAL(1, programs);
}
TEST(midi_mpe_output_lifetime, activation_respects_program_suppression) {
	InstrumentClip next;
	next.output = instrument.get();
	next.midiPGM = 9;
	ModelStackWithTimelineCounter stack{currentSong, &next};
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	LONGS_EQUAL(0, programs);
}
TEST(midi_mpe_output_lifetime, activation_stops_after_base_or_program_callback_deletion) {
	for (int boundary = 0; boundary < 2; ++boundary) {
		reset();
		auto next = std::make_unique<InstrumentClip>();
		next->output = instrument.get();
		next->midiPGM = 9;
		ModelStackWithTimelineCounter stack{currentSong, next.get()};
		auto destroy = [&] {
			instrument.reset();
			clip.reset();
			next.reset();
		};
		if (boundary == 0)
			on_activate = destroy;
		else
			on_output = destroy;
		CHECK(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
		LONGS_EQUAL(boundary, programs);
	}
}
TEST(midi_mpe_output_lifetime, activation_does_not_cache_after_stack_retargeting) {
	InstrumentClip next;
	next.output = instrument.get();
	next.midiPGM = 9;
	ModelStackWithTimelineCounter stack{currentSong, &next};
	on_output = [&] { stack.clip = nullptr; };
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
	LONGS_EQUAL(0, instrument->cachedBendRanges[1]);
}
TEST(midi_mpe_output_lifetime, deactivation_stops_expression_reset_after_cancelled_note_sweep) {
	on_output = [&] { ++instrument->arpeggiator.revision; };
	CHECK(instrument->setActiveClip(nullptr, PgmChangeSend::NEVER));
	LONGS_EQUAL(1, outputs);
	LONGS_EQUAL(0, expressions);
	LONGS_EQUAL(11, instrument->lastCombinedPolyExpression[0]);
}
TEST(midi_mpe_output_lifetime, deactivation_resets_expression_after_successful_sweep) {
	CHECK(instrument->setActiveClip(nullptr, PgmChangeSend::NEVER));
	LONGS_EQUAL(2, outputs);
	LONGS_EQUAL(3, expressions);
	for (int value : instrument->lastCombinedPolyExpression)
		LONGS_EQUAL(0, value);
}
TEST(midi_mpe_output_lifetime, deactivation_stops_after_each_expression_callback_deletes_owner) {
	for (int boundary = 1; boundary <= 3; ++boundary) {
		reset();
		on_output = [&] {
			if (expressions == boundary)
				instrument.reset();
		};
		CHECK(instrument->setActiveClip(nullptr, PgmChangeSend::NEVER));
		LONGS_EQUAL(boundary, expressions);
	}
}
TEST(midi_mpe_output_lifetime, deactivation_preserves_expression_suffix_after_new_event) {
	on_output = [&] {
		if (expressions == 1)
			++instrument->arpeggiator.revision;
	};
	CHECK(instrument->setActiveClip(nullptr, PgmChangeSend::NEVER));
	LONGS_EQUAL(1, expressions);
	LONGS_EQUAL(22, instrument->lastCombinedPolyExpression[1]);
}
TEST(midi_mpe_output_lifetime, activation_rejects_invalid_or_retired_target) {
	ModelStackWithTimelineCounter stack;
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
	InstrumentClip next;
	stack.clip = &next;
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
	next.output = instrument.get();
	next.lifetime.retire();
	CHECK_FALSE(instrument->setActiveClip(&stack, PgmChangeSend::ALWAYS));
	POINTERS_EQUAL(clip.get(), instrument->activeClip);
}
TEST(midi_mpe_output_lifetime, activation_accepts_zero_main_bend_range_without_infinite_ratio) {
	InstrumentClip next;
	next.output = instrument.get();
	next.paramManager.expression.bendRanges[0] = 0;
	ModelStackWithTimelineCounter stack{currentSong, &next};
	CHECK(instrument->setActiveClip(&stack, PgmChangeSend::NEVER));
	DOUBLES_EQUAL(0, instrument->ratio, 0);
	LONGS_EQUAL(48, instrument->cachedBendRanges[1]);
}
