#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include <array>
#include <climits>
#include <cstring>
#include <vector>
namespace midi_feedback_mapping_test {
constexpr int param_id_none = 255;
namespace params {
using namespace deluge::modulation::params;
static char const* paramNameForFile(Kind kind, int id, bool) {
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
struct MidiFollow {
	bool global_context = false;
	std::array<uint8_t, kMaxMIDIValue + 1> ccToSoundParam, ccToGlobalParam;
	void readDefaultMappingsFromFile(Deserializer&);
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
