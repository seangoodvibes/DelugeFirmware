#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <array>
#include <functional>
#include <memory>
namespace note_recording_clone_test {
static std::function<void()> on_selection, on_clone, on_record, on_view, on_root;
static bool root_enabled = false;
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
	uint64_t undo_identity = 1;
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
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	void* output = nullptr;
	bool row_present = true;
	NoteRow* getNoteRowForDrum(void*) { return row_present ? &row : nullptr; }
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
		return stack->addNoteRow(0, row_present ? &row : nullptr);
	}
	ModelStackWithNoteRow* getOrCreateNoteRowForYNote(int, ModelStackWithTimelineCounter* stack, Action*, bool*) {
		return stack->addNoteRow(0, &row);
	}
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* stack, Error* error) {
		++clone_calls;
		*error = clone_error;
		auto callback = on_clone;
		const auto saved_error = clone_error;
		auto* saved_target = clone_target;
		if (callback)
			callback();
		if (saved_error == Error::NONE && saved_target) {
			stack->timeline = saved_target;
			return true;
		}
		return false;
	}
	template <class... Args>
	void recordNoteOn(Args&&...) {
		++records_on;
		auto callback = on_record;
		if (callback)
			callback();
	}
	template <class... Args>
	void recordNoteOff(Args&&...) {
		++records_off;
		auto callback = on_record;
		if (callback)
			callback();
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
	void reportMPEInitialValuesForNoteEditing(Args&&...) {
		auto callback = on_view;
		if (callback)
			callback();
	}
	void reportNoteOffForMPEEditing(ModelStackWithNoteRow*) {
		auto callback = on_view;
		if (callback)
			callback();
	}
} clip_view;
static auto& instrument_clip_view_for_session() {
	return clip_view;
}
struct root_fixture {
	void noteRowChanged(Clip*, NoteRow*) {
		auto callback = on_root;
		if (callback)
			callback();
	}
};
static root_fixture root;
static root_fixture* getRootUI() {
	return root_enabled ? &root : nullptr;
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
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
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
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	bool linked = true;
	int32_t getDrumIndex(Drum*) { return linked ? 0 : -1; }
	void possiblySetSelectedDrumAndRefreshUI(Drum*) {
		auto callback = on_selection;
		if (callback)
			callback();
	}
	template <class... Args>
	void beginAuditioningforDrum(Args&&...) {
		++starts;
	}
	template <class... Args>
	void endAuditioningForDrum(Args&&...) {
		++ends;
	}
	bool receivedNoteForDrum(ModelStackWithTimelineCounter*, MIDICable&, bool, int32_t, int32_t, int32_t, bool, bool*,
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
		on_selection = on_clone = on_record = on_view = on_root = {};
		root_enabled = false;
		original.output = cloned.output = &kit;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
		stack.timeline = &original;
		melodic.activeClip = &original;
		current_clip = &original;
		cloned.arrangement_only = true;
		actionLogger.calls = 0;
		playbackHandler = {};
		currentUIMode = 0;
	}
	void teardown() override {
		on_selection = on_clone = on_record = on_view = on_root = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
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

TEST(NoteRecordingClone, selection_callback_destroying_kit_stops_note_processing) {
	auto target = std::make_unique<Kit>();
	original.output = target.get();
	on_selection = [&] { target.reset(); };
	target->receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, &drum);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, original.records_on);
}
TEST(NoteRecordingClone, selection_callback_destroying_drum_stops_note_processing) {
	auto target = std::make_unique<Drum>();
	on_selection = [&] { target.reset(); };
	kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, target.get());
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, selection_callback_destroying_clip_stops_before_recording_decisions) {
	auto target = std::make_unique<Clip>();
	target->output = &kit;
	stack.timeline = target.get();
	on_selection = [&] { target.reset(); };
	send(true);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, clone_callback_destroying_drum_stops_before_row_lookup) {
	auto target = std::make_unique<Drum>();
	on_clone = [&] { target.reset(); };
	kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, target.get());
	LONGS_EQUAL(0, original.records_on);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, clone_failure_after_source_destruction_does_not_audition_stale_clip) {
	auto target = std::make_unique<Clip>();
	target->output = &kit;
	target->clone_error = Error::BUG;
	stack.timeline = target.get();
	on_clone = [&] { target.reset(); };
	send(true);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, record_callback_destroying_drum_stops_before_audition) {
	auto target = std::make_unique<Drum>();
	original.arrangement_only = true;
	on_record = [&] { target.reset(); };
	kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, target.get());
	LONGS_EQUAL(1, original.records_on);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, record_callback_destroying_clip_stops_before_notification) {
	auto target = std::make_unique<Clip>();
	target->output = &kit;
	target->arrangement_only = true;
	stack.timeline = target.get();
	root_enabled = true;
	on_root = [] { FAIL("Retired clip must not be sent to the UI"); };
	on_record = [&] { target.reset(); };
	send(true);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, record_callback_reusing_row_identity_stops_follow_up_work) {
	original.arrangement_only = true;
	on_record = [&] { ++original.row.undo_identity; };
	send(true);
	LONGS_EQUAL(1, original.records_on);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, view_callback_removing_row_stops_before_recording) {
	original.arrangement_only = true;
	on_view = [&] { original.row_present = false; };
	send(true);
	LONGS_EQUAL(0, original.records_on);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, notification_callback_reassigning_output_stops_audition) {
	original.arrangement_only = true;
	root_enabled = true;
	on_root = [&] { original.output = nullptr; };
	send(true);
	LONGS_EQUAL(1, original.records_on);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, note_off_record_callback_invalidating_row_stops_follow_up) {
	original.arrangement_only = true;
	on_record = [&] { ++original.row.undo_identity; };
	send(true, false);
	LONGS_EQUAL(1, original.records_off);
	LONGS_EQUAL(0, kit.ends);
}
TEST(NoteRecordingClone, view_callback_retargeting_stack_is_preserved) {
	original.arrangement_only = true;
	on_view = [&] { stack.timeline = &cloned; };
	send(true);
	POINTERS_EQUAL(&cloned, stack.timeline);
	LONGS_EQUAL(0, original.records_on);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, selection_callback_detaching_drum_stops_before_clone) {
	on_selection = [&] { kit.linked = false; };
	send(true);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, retiring_kit_drum_and_clip_skip_selection_and_recording) {
	kit.lifetime.retire();
	drum.lifetime.retire();
	original.lifetime.retire();
	on_selection = [] { FAIL("Retiring target must not reach selection"); };
	send(true);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, kit.starts);
}
TEST(NoteRecordingClone, changed_panel_during_selection_cancels_note_event) {
	on_selection = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	send(true);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, kit.starts);
	CHECK(deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Remote);
}

TEST(NoteRecordingClone, retired_or_unlinked_kit_context_does_not_acquire_stale_drum_watch) {
	auto* stale_drum = new Drum;
	delete stale_drum;
	kit.linked = false;
	kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, stale_drum);
	kit.linked = true;
	kit.lifetime.retire();
	kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, stale_drum);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, kit.starts);
}

TEST(NoteRecordingClone, note_handler_reports_surviving_success_and_missing_row_as_safe_to_continue) {
	CHECK(kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, false, &thru, &drum));
	original.row_present = false;
	CHECK(kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, false, &thru, &drum));
	LONGS_EQUAL(1, kit.starts);
}
TEST(NoteRecordingClone, note_handler_reports_callback_row_replacement_as_cancellation) {
	original.arrangement_only = true;
	on_record = [&] { ++original.row.undo_identity; };
	CHECK_FALSE(kit.receivedNoteForDrum(&stack, cable, true, 0, 60, 100, true, &thru, &drum));
	LONGS_EQUAL(0, kit.starts);
}
