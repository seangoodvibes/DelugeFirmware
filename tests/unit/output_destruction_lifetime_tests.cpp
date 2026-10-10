#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "util/lifetime.h"
#include <functional>
#include <optional>
namespace output_destruction_lifetime_test {
static std::function<void()> on_cleanup;
struct member_fixture {
	~member_fixture() {
		if (on_cleanup)
			on_cleanup();
	}
};
struct Song;
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime_source_;
	Output* outputRecordingThisOutput = nullptr;
	virtual ~Output();
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source_}; }
	void retire_lifetime();
	void removeRecorder() {
		if (on_cleanup)
			on_cleanup();
	}
	void deleteBackedUpParamManagers(Song*) {
		if (on_cleanup)
			on_cleanup();
	}
	void prepareForHibernationOrDeletion() {
		if (on_cleanup)
			on_cleanup();
	}
	void clearRecordingFrom() {
		if (on_cleanup)
			on_cleanup();
	}
};
struct Clip {
	Output* output = nullptr;
};
static Clip* clipForLastNoteReceived[128]{};
struct MidiFollow {
	void remove_output(Output*);
};
static MidiFollow midiFollow;
#include "output_retained_notes.inc"
struct AudioOutput : Output {
	~AudioOutput() override;
	void releaseMonitoringClaim() {
		if (on_cleanup)
			on_cleanup();
	}
};
struct SoundInstrument : Output {
	member_fixture member;
	~SoundInstrument() override;
};
struct MIDIInstrument : Output {
	member_fixture member;
	~MIDIInstrument() override;
};
struct CVInstrument : Output {
	member_fixture member;
	~CVInstrument() override;
};
#include "audio_output_lifetime.inc"
#include "cv_output_lifetime.inc"
#include "midi_output_lifetime.inc"
struct Drum {
	Drum* next = nullptr;
	virtual ~Drum() = default;
};
static void delugeDealloc(void* ptr) {
	::operator delete(ptr);
}
namespace AudioEngine {
void logAction(const char*) {
}
void routineWithClusterLoading() {
	if (on_cleanup)
		on_cleanup();
}
} // namespace AudioEngine
struct Kit : Output {
	~Kit() override;
	deluge::gui::ui_session::State<Drum*> selected_drums;
	Drum* firstDrum = nullptr;
	struct {
		void reset() {
			if (on_cleanup)
				on_cleanup();
		}
	} arpeggiator;
};
enum { NO_MACRO, OUTPUT_CYCLE };
struct Song {
	struct {
		void release(Output*) {
			if (on_cleanup)
				on_cleanup();
		}
	} undo_detached_outputs;
	struct {
		int kind = NO_MACRO;
		Output* output = nullptr;
	} sessionMacros[8];
	void clearRecordingFromReferencesTo(Output*) {
		if (on_cleanup)
			on_cleanup();
	}
	void removeOutputFromMainList(Output*, bool) {
		if (on_cleanup)
			on_cleanup();
	}
	void deleteOutput(Output*);
	void deleteOutputThatIsInMainList(Output*, bool);
};
#include "kit_output_lifetime.inc"
#include "output_lifetime.inc"
#include "song_output_lifetime.inc"
#include "sound_output_lifetime.inc"
template <class T>
void check_retirement() {
	std::optional<T> output(std::in_place);
	auto watch = output->watch_lifetime();
	int callbacks = 0;
	on_cleanup = [&] {
		++callbacks;
		CHECK_FALSE(watch.alive());
	};
	output.reset();
	CHECK_FALSE(watch.alive());
	CHECK(callbacks > 0);
	on_cleanup = {};
}
} // namespace output_destruction_lifetime_test
using namespace output_destruction_lifetime_test;
TEST_GROUP(OutputDestructionLifetime){void setup() override{for (auto& clip : clipForLastNoteReceived) clip = nullptr;
}
void teardown() override {
	on_cleanup = {};
	for (auto& clip : clipForLastNoteReceived)
		clip = nullptr;
}
}
;
TEST(OutputDestructionLifetime, base_retires_before_recorder_cleanup) {
	check_retirement<Output>();
}
TEST(OutputDestructionLifetime, audio_retires_before_monitoring_cleanup) {
	check_retirement<AudioOutput>();
}
TEST(OutputDestructionLifetime, sound_retires_before_member_cleanup) {
	check_retirement<SoundInstrument>();
}
TEST(OutputDestructionLifetime, midi_retires_before_member_cleanup) {
	check_retirement<MIDIInstrument>();
}
TEST(OutputDestructionLifetime, cv_retires_before_member_cleanup) {
	check_retirement<CVInstrument>();
}

TEST(OutputDestructionLifetime, kit_retires_before_arpeggiator_cleanup) {
	check_retirement<Kit>();
}
TEST(OutputDestructionLifetime, song_retires_before_registry_cleanup) {
	Song song;
	auto* output = new Output;
	auto watch = output->watch_lifetime();
	on_cleanup = [&] { CHECK_FALSE(watch.alive()); };
	song.deleteOutput(output);
	CHECK_FALSE(watch.alive());
}
TEST(OutputDestructionLifetime, main_list_deletion_retires_before_audition_cleanup) {
	Song song;
	auto* output = new Output;
	auto watch = output->watch_lifetime();
	on_cleanup = [&] { CHECK_FALSE(watch.alive()); };
	song.deleteOutputThatIsInMainList(output, true);
	CHECK_FALSE(watch.alive());
}

TEST(OutputDestructionLifetime, retirement_clears_only_notes_for_its_output) {
	Clip target, other;
	Output output, other_output;
	target.output = &output;
	other.output = &other_output;
	clipForLastNoteReceived[0] = &target;
	clipForLastNoteReceived[127] = &target;
	clipForLastNoteReceived[60] = &other;
	output.retire_lifetime();
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[0]);
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[127]);
	POINTERS_EQUAL(&other, clipForLastNoteReceived[60]);
}
TEST(OutputDestructionLifetime, deletion_clears_retained_notes_before_cleanup_callback) {
	Clip target;
	auto* output = new AudioOutput;
	target.output = output;
	clipForLastNoteReceived[60] = &target;
	on_cleanup = [&] { POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]); };
	delete output;
	POINTERS_EQUAL(nullptr, clipForLastNoteReceived[60]);
}
TEST(OutputDestructionLifetime, later_destructor_does_not_clear_reassigned_notes) {
	Clip target;
	Output other_output;
	auto* output = new Output;
	target.output = output;
	clipForLastNoteReceived[60] = &target;
	output->retire_lifetime();
	target.output = &other_output;
	clipForLastNoteReceived[60] = &target;
	delete output;
	POINTERS_EQUAL(&target, clipForLastNoteReceived[60]);
	clipForLastNoteReceived[60] = nullptr;
}
