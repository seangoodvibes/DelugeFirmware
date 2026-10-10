#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include <array>
#include <climits>
namespace midi_feedback_mapping_test {
namespace params = deluge::modulation::params;
struct MidiFollow {
	bool global_context = false;
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
