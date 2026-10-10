#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include <array>
namespace note_recording_clone_test {
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MASTER, MPE_MEMBER };
enum class RecordingMode { OFF, ARRANGEMENT };
enum class RuntimeFeatureSettingType { HighlightIncomingNotes };
enum class RuntimeFeatureStateToggle { Off, On };
enum class ActionType { RECORD };
enum class ActionAddition { ALLOWED };
constexpr int UI_MODE_RECORD_COUNT_IN = 1, MIDI_DIRECTION_INPUT_TO_DELUGE = 0;
static int currentUIMode = 0;
static int16_t zeroMPEValues[3]{};
struct MIDICable {
	struct {
		std::array<int16_t, 3> defaultInputMPEValues{};
	} inputChannels[16];
	struct {
		bool isChannelPartOfAnMPEZone(int) { return false; }
	} ports[2];
};
static struct {
	RuntimeFeatureStateToggle get(RuntimeFeatureSettingType) { return RuntimeFeatureStateToggle::Off; }
} runtimeFeatureSettings;
struct ExpressionParamSet {
	void cancelAllOverriding() {}
};
struct NoteRow {
	bool sequenced = false;
	struct {
		bool matches_type(int) { return true; }
		ExpressionParamSet* getExpressionParamSet() { return nullptr; }
	} paramManager;
};
struct Clip;
using InstrumentClip = Clip;
struct ModelStackWithNoteRow {
	NoteRow* row = nullptr;
	NoteRow* getNoteRowAllowNull() { return row; }
};
struct ModelStackWithTimelineCounter {
	Clip* timeline = nullptr;
	ModelStackWithNoteRow row_stack;
	Clip* getTimelineCounter() { return timeline; }
	Clip* getTimelineCounterAllowNull() { return timeline; }
	ModelStackWithNoteRow* addNoteRow(int, NoteRow* row) {
		row_stack.row = row;
		return &row_stack;
	}
	ModelStackWithTimelineCounter* toWithSong() { return this; }
};
struct Action {
	void updateYScrollClipViewAfter() {}
};
static struct {
	int calls = 0;
	Action action;
	Action* getNewAction(ActionType, ActionAddition) {
		++calls;
		return &action;
	}
} actionLogger;
struct Clip {
	bool armedForRecording = true;
	bool arrangement_only = false;
	Error clone_error = Error::NONE;
	Clip* clone_target = nullptr;
	NoteRow row;
	int records_on = 0, records_off = 0, clone_calls = 0;
	bool isArrangementOnlyClip() { return arrangement_only; }
	bool getCurrentlyRecordingLinearly() { return false; }
	bool allowNoteTails(ModelStackWithNoteRow*) { return true; }
	ModelStackWithNoteRow* getNoteRowForYNote(int, ModelStackWithTimelineCounter* stack) {
		return stack->addNoteRow(0, &row);
	}
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter* stack, void*) {
		return stack->addNoteRow(0, &row);
	}
	ModelStackWithNoteRow* getOrCreateNoteRowForYNote(int, ModelStackWithTimelineCounter* stack, Action*, bool*) {
		return stack->addNoteRow(0, &row);
	}
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* stack, Error* error) {
		++clone_calls;
		*error = clone_error;
		if (clone_error == Error::NONE && clone_target) {
			stack->timeline = clone_target;
			return true;
		}
		return false;
	}
	template <class... Args>
	void recordNoteOn(Args&&...) {
		++records_on;
	}
	template <class... Args>
	void recordNoteOff(Args&&...) {
		++records_off;
	}
};
static Clip* current_clip = nullptr;
static Clip* getCurrentInstrumentClip() {
	return current_clip;
}
static struct {
	bool isClipActive(Clip*) { return true; }
	Clip* getClipWithOutputAboutToBeginLinearRecording(void*) { return nullptr; }
} song;
static auto* currentSong = &song;
static struct {
	RecordingMode recording = RecordingMode::ARRANGEMENT;
	int getActualSwungTickCount() { return 0; }
	int getTimePerInternalTick() { return 1; }
} playbackHandler;
static struct {
	int launchEventAtSwungTickCount = 0;
} session;
static void* currentPlaybackMode = &session;
static struct {
	template <class... Args>
	void reportMPEInitialValuesForNoteEditing(Args&&...) {}
	void reportNoteOffForMPEEditing(ModelStackWithNoteRow*) {}
} clip_view;
static auto& instrument_clip_view_for_session() {
	return clip_view;
}
struct root_fixture {
	void noteRowChanged(Clip*, NoteRow*) {}
};
static root_fixture* getRootUI() {
	return nullptr;
}
static struct {
	int highlightedNotes[128]{};
	void requestRendering() {}
} keyboard;
static auto& keyboard_screen_for_session() {
	return keyboard;
}
struct audition_fixture {
	int starts = 0, ends = 0;
};
struct MelodicInstrument : audition_fixture {
	Clip* activeClip = nullptr;
	struct {
		bool contains(int) { return true; }
	} notesAuditioned;
	struct early_note {
		uint8_t velocity = 0;
		bool still_active = false;
	};
	std::array<early_note, 128> earlyNotes;
	template <class... Args>
	void beginAuditioningForNote(Args&&...) {
		++starts;
	}
	template <class... Args>
	void endAuditioningForNote(Args&&...) {
		++ends;
	}
	void receivedNote(ModelStackWithTimelineCounter*, MIDICable&, bool, int32_t, MIDIMatchType, int32_t, int32_t, bool,
	                  bool*);
};
struct Drum {
	DrumType type = DrumType::SOUND;
	bool auditioned = true;
	int16_t lastExpressionInputsReceived[2][3]{};
	void recordNoteOnEarly(int, bool) {}
	void getCombinedExpressionInputs(int16_t* values) {
		for (int i = 0; i < 3; ++i)
			values[i] = 0;
	}
	Drum* toModControllable() { return this; }
	int required_param_manager_type() { return 0; }
};
struct MIDIDrum : Drum {
	int channel = 0, note = 60;
};
static void freezeWithError(const char*) {
	FAIL("Unexpected parameter-manager mismatch");
}
struct Kit : audition_fixture {
	void possiblySetSelectedDrumAndRefreshUI(Drum*) {}
	template <class... Args>
	void beginAuditioningforDrum(Args&&...) {
		++starts;
	}
	template <class... Args>
	void endAuditioningForDrum(Args&&...) {
		++ends;
	}
	void receivedNoteForDrum(ModelStackWithTimelineCounter*, MIDICable&, bool, int32_t, int32_t, int32_t, bool, bool*,
	                         Drum*);
};
#include "kit_note_recording.inc"
#include "melodic_note_recording.inc"
} // namespace note_recording_clone_test
using namespace note_recording_clone_test;
TEST_GROUP(NoteRecordingClone) {
	Clip original, cloned;
	ModelStackWithTimelineCounter stack;
	MIDICable cable;
	MelodicInstrument melodic;
	Kit kit;
	Drum drum;
	bool thru = true;
	void setup() override {
		stack.timeline = &original;
		melodic.activeClip = &original;
		current_clip = &original;
		cloned.arrangement_only = true;
		actionLogger.calls = 0;
		playbackHandler = {};
		currentUIMode = 0;
	}
	void send(bool is_kit, bool on = true, bool record = true) {
		if (is_kit)
			kit.receivedNoteForDrum(&stack, cable, on, 0, 60, 100, record, &thru, &drum);
		else
			melodic.receivedNote(&stack, cable, on, 0, MIDIMatchType::CHANNEL, 60, 100, record, &thru);
	}
};
TEST(NoteRecordingClone, failed_clone_preserves_original_and_still_auditions) {
	original.clone_error = Error::INSUFFICIENT_RAM;
	send(false);
	send(true);
	LONGS_EQUAL(0, original.records_on);
	LONGS_EQUAL(0, cloned.records_on);
	LONGS_EQUAL(0, actionLogger.calls);
	LONGS_EQUAL(1, melodic.starts);
	LONGS_EQUAL(1, kit.starts);
	POINTERS_EQUAL(&original, stack.timeline);
	send(false, false);
	send(true, false);
	LONGS_EQUAL(1, melodic.ends);
	LONGS_EQUAL(1, kit.ends);
	LONGS_EQUAL(0, original.records_off);
}
TEST(NoteRecordingClone, successful_clone_receives_notes_and_original_is_untouched) {
	original.clone_target = &cloned;
	send(false);
	stack.timeline = &original;
	send(true);
	LONGS_EQUAL(0, original.records_on);
	LONGS_EQUAL(2, cloned.records_on);
	LONGS_EQUAL(1, melodic.starts);
	LONGS_EQUAL(1, kit.starts);
}
TEST(NoteRecordingClone, existing_arrangement_clip_keeps_recording_when_no_clone_needed) {
	original.arrangement_only = true;
	send(false);
	send(true);
	LONGS_EQUAL(2, original.records_on);
	LONGS_EQUAL(1, actionLogger.calls);
}
TEST(NoteRecordingClone, nonrecording_notes_bypass_clone_and_keep_auditioning) {
	send(false, true, false);
	send(true, true, false);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, original.records_on);
	LONGS_EQUAL(1, melodic.starts);
	LONGS_EQUAL(1, kit.starts);
}
