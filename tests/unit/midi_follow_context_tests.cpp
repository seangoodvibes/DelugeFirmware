#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
namespace midi_follow_context_test {
namespace session = deluge::gui::ui_session;
struct Output {
	OutputType type = OutputType::SYNTH;
};
struct Clip {
	Output* output = nullptr;
	ClipType type = ClipType::AUDIO;
};
struct InstrumentClip : Clip {
	InstrumentClip() { type = ClipType::INSTRUMENT; }
	session::State<bool> affect_entire;
	bool affect_entire_for_session() { return affect_entire.active(); }
};
struct MidiFollow {
	session::State<Clip*> clips;
	Clip* getSelectedOrActiveClip() { return clips.active(); }
	bool isGlobalEffectableContext();
};
#include "midi_follow_context.inc"
} // namespace midi_follow_context_test
using namespace midi_follow_context_test;
TEST_GROUP(MidiFollowContext) {
	MidiFollow follow;
	Output output;
	InstrumentClip instrument;
	Clip audio;
	void setup() override {
		session::detail::active = session::Id::Local;
		instrument.output = &output;
		audio.output = &output;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(MidiFollowContext, missing_clip_or_output_is_not_global_effectable) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK_FALSE(follow.isGlobalEffectableContext());
		follow.clips.active() = &instrument;
		instrument.output = nullptr;
		CHECK_FALSE(follow.isGlobalEffectableContext());
	}
}
TEST(MidiFollowContext, kit_context_uses_each_panels_affect_entire_state) {
	output.type = OutputType::KIT;
	instrument.affect_entire.for_owner(session::Id::Remote) = true;
	follow.clips.for_owner(session::Id::Local) = &instrument;
	follow.clips.for_owner(session::Id::Remote) = &instrument;
	CHECK_FALSE(follow.isGlobalEffectableContext());
	{
		session::Scope scope(session::Id::Remote);
		CHECK(follow.isGlobalEffectableContext());
	}
	CHECK_FALSE(follow.isGlobalEffectableContext());
}
TEST(MidiFollowContext, audio_is_global_synth_is_not_and_mismatched_kit_is_safe) {
	follow.clips.active() = &audio;
	output.type = OutputType::AUDIO;
	CHECK(follow.isGlobalEffectableContext());
	output.type = OutputType::KIT;
	CHECK_FALSE(follow.isGlobalEffectableContext());
	follow.clips.active() = &instrument;
	instrument.affect_entire.active() = true;
	output.type = OutputType::SYNTH;
	CHECK_FALSE(follow.isGlobalEffectableContext());
}
