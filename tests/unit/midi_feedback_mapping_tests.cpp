#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace midi_feedback_mapping_test {
constexpr int param_id_none = 255;
constexpr int PARAM_ID_NONE = param_id_none;
static void intToString(int value, char* buffer) {
	std::snprintf(buffer, 10, "%d", value);
}
namespace params {
using namespace deluge::modulation::params;
static char const* paramNameForFile(Kind kind, int id, bool) {
	if ((kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL) && id == UNPATCHED_START + 2)
		return "shared";
	if (kind == Kind::PATCHED && id == 1)
		return "patched";
	if (kind == Kind::UNPATCHED_SOUND && id == UNPATCHED_START + 1)
		return "sound";
	if (kind == Kind::UNPATCHED_GLOBAL && id == UNPATCHED_START + 1)
		return "global";
	return "unused";
}
} // namespace params
struct Deserializer {
	std::vector<std::pair<char const*, int>> entries;
	size_t index = 0;
	int exits = 0;
	char const* readNextTagOrAttributeName() { return index < entries.size() ? entries[index].first : ""; }
	int readTagOrAttributeValueInt() { return entries[index].second; }
	void exitTag() {
		++index;
		++exits;
	}
};
constexpr char MIDI_DEFAULTS_SETTINGS_CHANNEL_TAG[] = "channel";
constexpr char MIDI_DEFAULTS_SETTINGS_DEVICE_TAG[] = "device";
struct Serializer {
	std::vector<std::pair<char const*, int>> entries;
	void writeOpeningTagBeginning(char const*) {}
	void writeOpeningTagEnd() {}
	void writeClosingTag(char const*) {}
	void writeTag(char const* tag, int value) { entries.emplace_back(tag, value); }
	void writeTag(char const* tag, char const* value) { entries.emplace_back(tag, std::atoi(value)); }
};
struct cable_fixture {
	void writeReferenceToFile(Serializer&, char const*) {}
};
using MIDICable = cable_fixture;
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MASTER };
struct channel_fixture {
	MIDIMatchType match = MIDIMatchType::NO_MATCH;
	int match_calls = 0;
	MIDICable* matched_cable = nullptr;
	uint8_t matched_channel = 0;
	MIDIMatchType checkMatch(MIDICable* cable, uint8_t channel) {
		++match_calls;
		matched_cable = cable;
		matched_channel = channel;
		return match;
	}
	uint8_t channelOrZone = MIDI_CHANNEL_NONE;
	cable_fixture* cable = nullptr;
};
using LearnedMIDI = channel_fixture;
static struct {
	std::array<channel_fixture, kNumMIDIFollowChannelTypesIncludingTracks> midiFollowChannelType;
	MIDIFollowFeedbackChannelType midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::NONE;
} midiEngine;
namespace MIDIDeviceManager {
static cable_fixture* readDeviceReferenceFromFile(Deserializer&) {
	return nullptr;
}
} // namespace MIDIDeviceManager
struct MidiFollow {
	using FeedbackChannelTypes = std::array<MIDIFollowChannelType, 2>;
	MIDIFollowChannelType track_target = MIDIFollowChannelType::Track1;
	MIDIFollowChannelType getChannelTypeForTrackFeedback() { return track_target; }
	size_t getChannelTypesForFeedback(FeedbackChannelTypes&);
	bool addChannelTypeForFeedback(FeedbackChannelTypes&, size_t&, MIDIFollowChannelType);
	bool global_context = false;
	MIDIMatchType checkMidiFollowMatch(MIDICable&, uint8_t);
	MIDIMatchType checkMidiFollowMatchForSpecificTrack(MIDICable&, uint8_t, int32_t);
	void writeDefaultMappingsToFile(Serializer&);
	char const* getNameFromChannelType(MIDIFollowChannelType) { return "a"; }
	void writeSpecificChannelSettingsToFile(Serializer&, MIDIFollowChannelType);
	std::array<uint8_t, kMaxMIDIValue + 1> ccToSoundParam, ccToGlobalParam;
	void readDefaultMappingsFromFile(Deserializer&);
	void readSpecificChannelSettingsFromFile(Deserializer&, MIDIFollowChannelType);
	std::array<uint8_t, params::UNPATCHED_START + params::UNPATCHED_SOUND_MAX_NUM> soundParamToCC;
	std::array<uint8_t, params::UNPATCHED_GLOBAL_MAX_NUM> globalParamToCC;
	bool isGlobalEffectableContext() { return global_context; }
	int32_t getCCFromParam(params::Kind, int32_t);
};
#include "midi_feedback_mapping.inc"
} // namespace midi_feedback_mapping_test
using namespace midi_feedback_mapping_test;
TEST_GROUP(MidiFeedbackMapping) {
	MidiFollow follow;
	void setup() override {
		midiEngine = {};
		follow.ccToSoundParam.fill(param_id_none);
		follow.ccToGlobalParam.fill(param_id_none);
		follow.soundParamToCC.fill(17);
		follow.globalParamToCC.fill(29);
	}
};
TEST(MidiFeedbackMapping, invalid_ids_do_not_index_feedback_tables) {
	for (bool global_context : {false, true}) {
		follow.global_context = global_context;
		for (auto kind : {params::Kind::PATCHED, params::Kind::UNPATCHED_SOUND, params::Kind::UNPATCHED_GLOBAL}) {
			for (int id : {-1, INT_MIN, INT_MAX})
				LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(kind, id));
		}
	}
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_GLOBAL_MAX_NUM));
	follow.global_context = false;
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::PATCHED, params::UNPATCHED_START));
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SOUND_MAX_NUM));
}
TEST(MidiFeedbackMapping, valid_table_boundaries_keep_mapped_values) {
	LONGS_EQUAL(17, follow.getCCFromParam(params::Kind::PATCHED, 0));
	LONGS_EQUAL(17, follow.getCCFromParam(params::Kind::PATCHED, params::UNPATCHED_START - 1));
	follow.soundParamToCC[params::UNPATCHED_START] = 91;
	LONGS_EQUAL(91, follow.getCCFromParam(params::Kind::UNPATCHED_SOUND, 0));
	LONGS_EQUAL(17, follow.getCCFromParam(params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SOUND_MAX_NUM - 1));
	follow.global_context = true;
	LONGS_EQUAL(29, follow.getCCFromParam(params::Kind::UNPATCHED_GLOBAL, 0));
	LONGS_EQUAL(29, follow.getCCFromParam(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_GLOBAL_MAX_NUM - 1));
}
TEST(MidiFeedbackMapping, incompatible_context_and_unlearned_entries_have_no_cc) {
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::UNPATCHED_GLOBAL, 0));
	follow.soundParamToCC[0] = MIDI_CC_NONE;
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::PATCHED, 0));
	follow.global_context = true;
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::PATCHED, 0));
	LONGS_EQUAL(MIDI_CC_NONE, follow.getCCFromParam(params::Kind::UNPATCHED_SOUND, 0));
}

TEST(MidiFeedbackMapping, invalid_file_cc_values_preserve_mappings_and_consume_tags) {
	Deserializer reader;
	for (auto tag : {"patched", "sound", "global"})
		for (int value : {-1, INT_MIN, 128, 255, INT_MAX})
			reader.entries.emplace_back(tag, value);
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(15, reader.exits);
	for (auto value : follow.soundParamToCC)
		LONGS_EQUAL(17, value);
	for (auto value : follow.globalParamToCC)
		LONGS_EQUAL(29, value);
	for (auto value : follow.ccToSoundParam)
		LONGS_EQUAL(param_id_none, value);
	for (auto value : follow.ccToGlobalParam)
		LONGS_EQUAL(param_id_none, value);
}
TEST(MidiFeedbackMapping, file_mapping_recovers_after_invalid_entry_and_accepts_boundaries) {
	Deserializer reader;
	reader.entries = {{"patched", -1}, {"patched", 0}, {"sound", 127}, {"global", 64}};
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(4, reader.exits);
	LONGS_EQUAL(0, follow.soundParamToCC[1]);
	LONGS_EQUAL(1, follow.ccToSoundParam[0]);
	LONGS_EQUAL(127, follow.soundParamToCC[params::UNPATCHED_START + 1]);
	LONGS_EQUAL(params::UNPATCHED_START + 1, follow.ccToSoundParam[127]);
	LONGS_EQUAL(64, follow.globalParamToCC[1]);
	LONGS_EQUAL(1, follow.ccToGlobalParam[64]);
}
TEST(MidiFeedbackMapping, unknown_file_mapping_does_not_change_tables) {
	Deserializer reader;
	reader.entries = {{"unknown", 7}};
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(1, reader.exits);
	LONGS_EQUAL(param_id_none, follow.ccToSoundParam[7]);
	LONGS_EQUAL(param_id_none, follow.ccToGlobalParam[7]);
}

TEST(MidiFeedbackMapping, all_saved_channels_and_mpe_zones_restore) {
	for (auto type : {MIDIFollowChannelType::A, MIDIFollowChannelType::Track16}) {
		auto& channel = midiEngine.midiFollowChannelType[util::to_underlying(type)].channelOrZone;
		for (int value = 1; value <= NUM_CHANNELS; ++value) {
			channel = MIDI_CHANNEL_NONE;
			Deserializer reader;
			reader.entries = {{"channel", value}};
			follow.readSpecificChannelSettingsFromFile(reader, type);
			LONGS_EQUAL(value - 1, channel);
			LONGS_EQUAL(1, reader.exits);
		}
	}
}
TEST(MidiFeedbackMapping, invalid_saved_channels_preserve_previous_selection) {
	auto& channel = midiEngine.midiFollowChannelType[0].channelOrZone;
	channel = 7;
	Deserializer reader;
	reader.entries = {{"channel", -1}, {"channel", NUM_CHANNELS + 1}, {"channel", INT_MAX}};
	follow.readSpecificChannelSettingsFromFile(reader, MIDIFollowChannelType::A);
	LONGS_EQUAL(7, channel);
	LONGS_EQUAL(3, reader.exits);
	reader = {};
	reader.entries = {{"channel", 0}};
	follow.readSpecificChannelSettingsFromFile(reader, MIDIFollowChannelType::A);
	LONGS_EQUAL(MIDI_CHANNEL_NONE, channel);
}

TEST(MidiFeedbackMapping, saved_channel_round_trip_includes_unassigned_and_both_mpe_zones) {
	auto& channel = midiEngine.midiFollowChannelType[0].channelOrZone;
	for (int value = -1; value < NUM_CHANNELS; ++value) {
		channel = value < 0 ? MIDI_CHANNEL_NONE : value;
		Serializer writer;
		follow.writeSpecificChannelSettingsToFile(writer, MIDIFollowChannelType::A);
		LONGS_EQUAL(1, writer.entries.size());
		LONGS_EQUAL(value + 1, writer.entries[0].second);
		channel = 9;
		Deserializer reader;
		reader.entries = writer.entries;
		follow.readSpecificChannelSettingsFromFile(reader, MIDIFollowChannelType::A);
		LONGS_EQUAL(value < 0 ? MIDI_CHANNEL_NONE : value, channel);
	}
}
TEST(MidiFeedbackMapping, legacy_unassigned_channel_value_clears_previous_selection) {
	auto& channel = midiEngine.midiFollowChannelType[0].channelOrZone;
	channel = 7;
	Deserializer reader;
	reader.entries = {{"channel", MIDI_CHANNEL_NONE + 1}};
	follow.readSpecificChannelSettingsFromFile(reader, MIDIFollowChannelType::A);
	LONGS_EQUAL(MIDI_CHANNEL_NONE, channel);
}

TEST(MidiFeedbackMapping, distinct_sound_and_global_mappings_on_same_cc_round_trip) {
	follow.ccToSoundParam[7] = 1;
	follow.ccToGlobalParam[7] = 1;
	Serializer writer;
	follow.writeDefaultMappingsToFile(writer);
	LONGS_EQUAL(2, writer.entries.size());
	follow.ccToSoundParam.fill(param_id_none);
	follow.ccToGlobalParam.fill(param_id_none);
	Deserializer reader;
	reader.entries = writer.entries;
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(1, follow.ccToSoundParam[7]);
	LONGS_EQUAL(1, follow.ccToGlobalParam[7]);
	LONGS_EQUAL(7, follow.soundParamToCC[1]);
	LONGS_EQUAL(7, follow.globalParamToCC[1]);
}
TEST(MidiFeedbackMapping, shared_mapping_name_is_saved_once_and_restores_both_contexts) {
	follow.ccToSoundParam[127] = params::UNPATCHED_START + 2;
	follow.ccToGlobalParam[127] = 2;
	Serializer writer;
	follow.writeDefaultMappingsToFile(writer);
	LONGS_EQUAL(1, writer.entries.size());
	STRCMP_EQUAL("shared", writer.entries[0].first);
	Deserializer reader;
	reader.entries = writer.entries;
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(127, follow.soundParamToCC[params::UNPATCHED_START + 2]);
	LONGS_EQUAL(127, follow.globalParamToCC[2]);
}
TEST(MidiFeedbackMapping, global_only_and_unpatched_sound_mappings_survive_save) {
	follow.ccToGlobalParam[0] = 1;
	follow.ccToSoundParam[127] = params::UNPATCHED_START + 1;
	Serializer writer;
	follow.writeDefaultMappingsToFile(writer);
	LONGS_EQUAL(2, writer.entries.size());
	STRCMP_EQUAL("global", writer.entries[0].first);
	LONGS_EQUAL(0, writer.entries[0].second);
	STRCMP_EQUAL("sound", writer.entries[1].first);
	LONGS_EQUAL(127, writer.entries[1].second);
}

TEST(MidiFeedbackMapping, repeated_parameter_releases_previous_incoming_cc) {
	Deserializer reader;
	reader.entries = {{"patched", 7}, {"patched", 9}, {"global", 10}, {"global", 11}};
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(param_id_none, follow.ccToSoundParam[7]);
	LONGS_EQUAL(1, follow.ccToSoundParam[9]);
	LONGS_EQUAL(9, follow.soundParamToCC[1]);
	LONGS_EQUAL(param_id_none, follow.ccToGlobalParam[10]);
	LONGS_EQUAL(1, follow.ccToGlobalParam[11]);
	LONGS_EQUAL(11, follow.globalParamToCC[1]);
}
TEST(MidiFeedbackMapping, reassigned_cc_releases_previous_feedback_parameter) {
	Deserializer reader;
	reader.entries = {{"patched", 7}, {"sound", 7}, {"global", 9}, {"shared", 9}};
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(MIDI_CC_NONE, follow.soundParamToCC[1]);
	LONGS_EQUAL(params::UNPATCHED_START + 1, follow.ccToSoundParam[7]);
	LONGS_EQUAL(7, follow.soundParamToCC[params::UNPATCHED_START + 1]);
	LONGS_EQUAL(MIDI_CC_NONE, follow.globalParamToCC[1]);
	LONGS_EQUAL(2, follow.ccToGlobalParam[9]);
	LONGS_EQUAL(9, follow.globalParamToCC[2]);
}
TEST(MidiFeedbackMapping, repeated_identical_entries_and_invalid_replacement_preserve_mapping) {
	Deserializer reader;
	reader.entries = {{"shared", 127}, {"shared", 127}, {"shared", -1}};
	follow.readDefaultMappingsFromFile(reader);
	LONGS_EQUAL(127, follow.soundParamToCC[params::UNPATCHED_START + 2]);
	LONGS_EQUAL(127, follow.globalParamToCC[2]);
	LONGS_EQUAL(params::UNPATCHED_START + 2, follow.ccToSoundParam[127]);
	LONGS_EQUAL(2, follow.ccToGlobalParam[127]);
}

TEST(MidiFeedbackMapping, full_or_invalid_feedback_lists_remain_unchanged) {
	MidiFollow::FeedbackChannelTypes targets{MIDIFollowChannelType::NONE, MIDIFollowChannelType::NONE};
	midiEngine.midiFollowChannelType[0].channelOrZone = 0;
	for (size_t count : {size_t{2}, size_t{3}, SIZE_MAX}) {
		size_t original_count = count;
		CHECK_FALSE(follow.addChannelTypeForFeedback(targets, count, MIDIFollowChannelType::A));
		CHECK(count == original_count);
		CHECK(targets[0] == MIDIFollowChannelType::NONE);
	}
	size_t count = 1;
	CHECK_FALSE(follow.addChannelTypeForFeedback(targets, count, MIDIFollowChannelType::A));
	LONGS_EQUAL(1, count);
}
TEST(MidiFeedbackMapping, feedback_targets_validate_channel_and_target_ranges) {
	MidiFollow::FeedbackChannelTypes targets{};
	size_t count = 0;
	for (int channel : {NUM_CHANNELS, 254, MIDI_CHANNEL_NONE}) {
		midiEngine.midiFollowChannelType[0].channelOrZone = channel;
		CHECK_FALSE(follow.addChannelTypeForFeedback(targets, count, MIDIFollowChannelType::A));
	}
	for (auto type : {MIDIFollowChannelType::NONE, MIDIFollowChannelType::INVALID})
		CHECK_FALSE(follow.addChannelTypeForFeedback(targets, count, type));
	LONGS_EQUAL(0, count);
}
TEST(MidiFeedbackMapping, valid_feedback_targets_deduplicate_and_keep_mpe_zones) {
	MidiFollow::FeedbackChannelTypes targets{};
	size_t count = 0;
	midiEngine.midiFollowChannelType[0].channelOrZone = MIDI_CHANNEL_MPE_LOWER_ZONE;
	midiEngine.midiFollowChannelType[1].channelOrZone = MIDI_CHANNEL_MPE_LOWER_ZONE;
	midiEngine.midiFollowChannelType[2].channelOrZone = MIDI_CHANNEL_MPE_UPPER_ZONE;
	CHECK(follow.addChannelTypeForFeedback(targets, count, MIDIFollowChannelType::A));
	CHECK_FALSE(follow.addChannelTypeForFeedback(targets, count, MIDIFollowChannelType::B));
	CHECK(follow.addChannelTypeForFeedback(targets, count, MIDIFollowChannelType::C));
	LONGS_EQUAL(2, count);
	CHECK(targets[0] == MIDIFollowChannelType::A);
	CHECK(targets[1] == MIDIFollowChannelType::C);
}

TEST(MidiFeedbackMapping, feedback_modes_resolve_regular_channels_and_track_in_order) {
	using mode = MIDIFollowFeedbackChannelType;
	using target = MIDIFollowChannelType;
	const std::array<mode, 7> modes{mode::A,         mode::B,         mode::C,        mode::Track,
	                                mode::TrackAndA, mode::TrackAndB, mode::TrackAndC};
	const std::array<target, 7> first{target::A,      target::B,      target::C,     target::Track1,
	                                  target::Track1, target::Track1, target::Track1};
	for (int i = 0; i < 4; ++i)
		midiEngine.midiFollowChannelType[i].channelOrZone = i;
	for (size_t i = 0; i < modes.size(); ++i) {
		midiEngine.midiFollowFeedbackChannelType = modes[i];
		MidiFollow::FeedbackChannelTypes targets{};
		LONGS_EQUAL(i < 4 ? 1 : 2, follow.getChannelTypesForFeedback(targets));
		CHECK(targets[0] == first[i]);
		if (i >= 4)
			CHECK(targets[1] == static_cast<target>(i - 4));
		else
			CHECK(targets[1] == target::NONE);
	}
}
TEST(MidiFeedbackMapping, combined_feedback_deduplicates_and_falls_back_without_track) {
	midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::TrackAndA;
	midiEngine.midiFollowChannelType[0].channelOrZone = 4;
	midiEngine.midiFollowChannelType[3].channelOrZone = 4;
	MidiFollow::FeedbackChannelTypes targets{};
	LONGS_EQUAL(1, follow.getChannelTypesForFeedback(targets));
	CHECK(targets[0] == MIDIFollowChannelType::Track1);
	follow.track_target = MIDIFollowChannelType::NONE;
	LONGS_EQUAL(1, follow.getChannelTypesForFeedback(targets));
	CHECK(targets[0] == MIDIFollowChannelType::A);
	CHECK(targets[1] == MIDIFollowChannelType::NONE);
}
TEST(MidiFeedbackMapping, disabled_and_unconfigured_modes_clear_previous_targets) {
	MidiFollow::FeedbackChannelTypes targets{MIDIFollowChannelType::A, MIDIFollowChannelType::B};
	for (auto mode : {MIDIFollowFeedbackChannelType::NONE, MIDIFollowFeedbackChannelType::INVALID,
	                  MIDIFollowFeedbackChannelType::TrackAndC}) {
		midiEngine.midiFollowFeedbackChannelType = mode;
		LONGS_EQUAL(0, follow.getChannelTypesForFeedback(targets));
		for (auto target : targets)
			CHECK(target == MIDIFollowChannelType::NONE);
	}
}

TEST(MidiFeedbackMapping, invalid_track_match_does_not_alias_regular_follow_channel) {
	MIDICable cable;
	for (auto& entry : midiEngine.midiFollowChannelType)
		entry.match = MIDIMatchType::CHANNEL;
	for (int index : {-1, -3, INT_MIN, kNumMIDIFollowChannelTrackTypes, INT_MAX}) {
		CHECK(follow.checkMidiFollowMatchForSpecificTrack(cable, 4, index) == MIDIMatchType::NO_MATCH);
	}
	for (auto& entry : midiEngine.midiFollowChannelType)
		LONGS_EQUAL(0, entry.match_calls);
}
TEST(MidiFeedbackMapping, track_matching_passes_cable_channel_and_preserves_match_kind) {
	MIDICable cable;
	for (int index : {0, kNumMIDIFollowChannelTrackTypes - 1}) {
		auto& entry = midiEngine.midiFollowChannelType[kNumMIDIFollowChannelTypes + index];
		entry.match = MIDIMatchType::MPE_MASTER;
		CHECK(follow.checkMidiFollowMatchForSpecificTrack(cable, 15, index) == MIDIMatchType::MPE_MASTER);
		POINTERS_EQUAL(&cable, entry.matched_cable);
		LONGS_EQUAL(15, entry.matched_channel);
		LONGS_EQUAL(1, entry.match_calls);
	}
}
TEST(MidiFeedbackMapping, regular_matching_stops_at_first_match_and_excludes_tracks) {
	MIDICable cable;
	midiEngine.midiFollowChannelType[1].match = MIDIMatchType::CHANNEL;
	midiEngine.midiFollowChannelType[2].match = MIDIMatchType::MPE_MASTER;
	CHECK(follow.checkMidiFollowMatch(cable, 2) == MIDIMatchType::CHANNEL);
	LONGS_EQUAL(1, midiEngine.midiFollowChannelType[0].match_calls);
	LONGS_EQUAL(1, midiEngine.midiFollowChannelType[1].match_calls);
	LONGS_EQUAL(0, midiEngine.midiFollowChannelType[2].match_calls);
	for (auto& entry : midiEngine.midiFollowChannelType) {
		entry.match_calls = 0;
		entry.match = MIDIMatchType::NO_MATCH;
	}
	midiEngine.midiFollowChannelType[3].match = MIDIMatchType::CHANNEL;
	CHECK(follow.checkMidiFollowMatch(cable, 2) == MIDIMatchType::NO_MATCH);
	LONGS_EQUAL(0, midiEngine.midiFollowChannelType[3].match_calls);
}
