#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
namespace kit_shortcut_routing_test {
using namespace deluge::gui::ui_session;
enum class OutputType { KIT, AUDIO, SYNTH };
struct InstrumentClip {
	State<bool> affect_entire;
	bool affect_entire_for_session() const { return affect_entire.active(); }
};
static OutputType output_type;
static InstrumentClip* current_clip;
static int instrument_lookups;
OutputType getCurrentOutputType() {
	return output_type;
}
InstrumentClip* getCurrentInstrumentClip() {
	++instrument_lookups;
	return current_clip;
}
struct SoundEditor {
	static bool shouldEditKitAffectEntire();
};
#include "kit_shortcut_routing.inc"
TEST_GROUP(KitShortcutRouting){void setup() override{output_type = OutputType::KIT;
current_clip = nullptr;
instrument_lookups = 0;
} // namespace kit_shortcut_routing_test
}
;
TEST(KitShortcutRouting, audio_and_synth_do_not_request_an_instrument_clip) {
	output_type = OutputType::AUDIO;
	CHECK_FALSE(SoundEditor::shouldEditKitAffectEntire());
	output_type = OutputType::SYNTH;
	CHECK_FALSE(SoundEditor::shouldEditKitAffectEntire());
	LONGS_EQUAL(0, instrument_lookups);
}
TEST(KitShortcutRouting, missing_clip_is_not_kit_affect_entire) {
	CHECK_FALSE(SoundEditor::shouldEditKitAffectEntire());
}
TEST(KitShortcutRouting, reads_the_active_panels_affect_entire_state) {
	InstrumentClip clip;
	current_clip = &clip;
	clip.affect_entire.for_owner(Id::Local) = true;
	clip.affect_entire.for_owner(Id::Remote) = false;
	{
		Scope scope(Id::Local);
		CHECK_TRUE(SoundEditor::shouldEditKitAffectEntire());
	}
	{
		Scope scope(Id::Remote);
		CHECK_FALSE(SoundEditor::shouldEditKitAffectEntire());
	}
}
} // namespace kit_shortcut_routing_test
