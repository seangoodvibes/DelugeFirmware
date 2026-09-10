#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include <cstdint>
#include <functional>
namespace audio_start_tests {
constexpr int BEFORE = 0;
enum class ActionType { CLIP_LENGTH_DECREASE, CLIP_LENGTH_INCREASE };
enum class ActionAddition { NOT_ALLOWED };
struct Consequence {
	enum { CLIP_LENGTH, PARAM_CHANGE };
	Consequence* next = nullptr;
	int type = CLIP_LENGTH;
};
struct ConsequenceClipLength : Consequence {
	void* clip = nullptr;
	enum class SampleMarker { NONE, START, END };
	SampleMarker sample_marker = SampleMarker::NONE;
	uint64_t markerValueToRevertTo = 999;
};
struct Action {
	deluge::gui::ui_session::Id navigation_owner = deluge::gui::ui_session::Id::Local;
	void* captured_song = nullptr;
	void* captured_output = nullptr;
	void* currentClip = nullptr;
	Consequence* firstConsequence = nullptr;
	ConsequenceClipLength fallback_consequence;
	bool record_succeeds = true;
	std::function<void()> on_record;
	bool recordClipLengthChange(void* target_clip, int32_t) {
		if (!record_succeeds)
			return false;
		if (!firstConsequence)
			firstConsequence = &fallback_consequence;
		if (firstConsequence->type == Consequence::CLIP_LENGTH)
			static_cast<ConsequenceClipLength*>(firstConsequence)->clip = target_clip;
		auto callback = on_record;
		if (callback)
			callback();
		return true;
	}
};
static struct Logger {
	Action action;
	Action* firstAction[1]{};
	Action* override_action = nullptr;
	bool allocation_succeeds = true;
	std::function<void()> on_allocate;
	int closed = 0;
	Action* getNewAction(ActionType, ActionAddition);
	void closeAction(ActionType) { ++closed; }
} actionLogger;
struct AudioClip;
static AudioClip* selected_clip;
enum class ClipType { AUDIO, INSTRUMENT };
struct AudioClip {
	ClipType type = ClipType::AUDIO;
	int32_t loopLength = 96;
	void* output = nullptr;
	struct {
		uint64_t startPos = 10, endPos = 106;
		void* audioFile = nullptr;
	} sampleHolder;
	struct {
		bool reversed = false;
		bool isCurrentlyReversed() { return reversed; }
	} sampleControls;
};
struct Sample {
	uint64_t lengthInSamples = 1000;
	uint64_t fileLoopStartSamples = 0;
};
struct Song {
	bool success = true;
	bool registered = true;
	bool contains_clip_for_undo(AudioClip*) { return registered; }
	int resize_calls = 0;
	std::function<void()> callback;
	bool setClipLength(AudioClip* clip, int32_t new_length, Action* action) {
		if (action && action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH)
			static_cast<ConsequenceClipLength*>(action->firstConsequence)->clip = clip;
		clip->loopLength = new_length;
		++resize_calls;
		if (callback)
			callback();
		return success;
	}
};
static Song* currentSong;
Action* Logger::getNewAction(ActionType, ActionAddition) {
	if (!allocation_succeeds) {
		if (on_allocate)
			on_allocate();
		return nullptr;
	}
	Action* result = override_action ? override_action : &action;
	firstAction[BEFORE] = result;
	result->navigation_owner = deluge::gui::ui_session::current();
	result->captured_song = currentSong;
	result->captured_output = selected_clip->output;
	result->currentClip = selected_clip;
	if (on_allocate)
		on_allocate();
	return result;
}
struct AudioClipView {
	bool changeUnderlyingSampleStart(AudioClip&, const Sample*, int32_t, int32_t, uint64_t) const;
	bool changeUnderlyingSampleLength(AudioClip&, const Sample*, int32_t, int32_t, uint64_t) const;
};
#include "audio_start_method.inc"
TEST_GROUP(AudioStartResult) {
	Song song;
	int output = 0;
	Sample sample;
	AudioClipView view;
	ConsequenceClipLength consequence;
	void setup() override {
		currentSong = &song;
		actionLogger = {};
		actionLogger.action.firstConsequence = &consequence;
	}
	void teardown() override {
		currentSong = nullptr;
	}
};
TEST(AudioStartResult, failed_resize_does_not_update_or_close_undo_after_clip_deletion) {
	for (bool reversed : {false, true}) {
		auto* clip = new AudioClip;
		selected_clip = clip;
		selected_clip->output = &output;
		selected_clip->sampleHolder.audioFile = &sample;
		clip->sampleControls.reversed = reversed;
		song.success = false;
		song.callback = [&] { delete clip; };
		CHECK_FALSE(view.changeUnderlyingSampleStart(*clip, &sample, 24, 96, 96));
		CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
		LONGS_EQUAL(999, consequence.markerValueToRevertTo);
		LONGS_EQUAL(0, actionLogger.closed);
	}
}
TEST(AudioStartResult, successful_resize_records_original_marker_for_both_directions) {
	for (bool reversed : {false, true}) {
		AudioClip clip;
		selected_clip = &clip;
		selected_clip->output = &output;
		selected_clip->sampleHolder.audioFile = &sample;
		clip.sampleControls.reversed = reversed;
		CHECK_TRUE(view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96));
		CHECK_TRUE(
		    consequence.sample_marker
		    == (reversed ? ConsequenceClipLength::SampleMarker::END : ConsequenceClipLength::SampleMarker::START));
		LONGS_EQUAL(reversed ? 106 : 10, consequence.markerValueToRevertTo);
	}
	LONGS_EQUAL(2, actionLogger.closed);
}
TEST(AudioStartResult, failed_end_resize_does_not_update_or_close_undo_after_clip_deletion) {
	for (bool reversed : {false, true}) {
		auto* clip = new AudioClip;
		selected_clip = clip;
		selected_clip->output = &output;
		selected_clip->sampleHolder.audioFile = &sample;
		clip->sampleControls.reversed = reversed;
		song.success = false;
		song.callback = [&] { delete clip; };
		CHECK_FALSE(view.changeUnderlyingSampleLength(*clip, &sample, 48, 96, 96));
		CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
		LONGS_EQUAL(999, consequence.markerValueToRevertTo);
		LONGS_EQUAL(0, actionLogger.closed);
	}
}
TEST(AudioStartResult, successful_end_resize_records_original_marker_for_both_directions) {
	for (bool reversed : {false, true}) {
		AudioClip clip;
		selected_clip = &clip;
		selected_clip->output = &output;
		selected_clip->sampleHolder.audioFile = &sample;
		clip.sampleControls.reversed = reversed;
		CHECK_TRUE(view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
		CHECK_TRUE(
		    consequence.sample_marker
		    == (reversed ? ConsequenceClipLength::SampleMarker::START : ConsequenceClipLength::SampleMarker::END));
		LONGS_EQUAL(reversed ? 10 : 106, consequence.markerValueToRevertTo);
		LONGS_EQUAL(58, reversed ? clip.sampleHolder.startPos : clip.sampleHolder.endPos);
	}
	LONGS_EQUAL(2, actionLogger.closed);
}
TEST(AudioStartResult, removed_actions_stop_marker_edits_before_consequence_access) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool during_resize : {false, true}) {
				AudioClip clip;
				selected_clip = &clip;
				selected_clip->output = &output;
				selected_clip->sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				actionLogger = {};
				song = {};
				auto* allocated_action = new Action;
				allocated_action->firstConsequence = &consequence;
				actionLogger.override_action = allocated_action;
				auto remove_action = [&] {
					actionLogger.firstAction[BEFORE] = nullptr;
					delete allocated_action;
				};
				if (during_resize)
					song.callback = remove_action;
				else
					actionLogger.on_allocate = remove_action;
				const bool result = start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                                 : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96);
				CHECK_FALSE(result);
				LONGS_EQUAL(during_resize ? 1 : 0, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
				CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
				LONGS_EQUAL(999, consequence.markerValueToRevertTo);
			}
		}
	}
}
TEST(AudioStartResult, allocation_cannot_redirect_song_or_resize_removed_clip) {
	Song replacement_song;
	for (bool start_marker : {false, true}) {
		for (bool remove_clip : {false, true}) {
			auto* clip = new AudioClip;
			selected_clip = clip;
			selected_clip->output = &output;
			selected_clip->sampleHolder.audioFile = &sample;
			song = {};
			currentSong = &song;
			actionLogger.on_allocate = [&] {
				if (remove_clip) {
					song.registered = false;
					delete clip;
				}
				else
					currentSong = &replacement_song;
			};
			const bool result = start_marker ? view.changeUnderlyingSampleStart(*clip, &sample, 24, 96, 96)
			                                 : view.changeUnderlyingSampleLength(*clip, &sample, 48, 96, 96);
			currentSong = &song;
			if (!remove_clip)
				delete clip;
			CHECK_FALSE(result);
			LONGS_EQUAL(0, song.resize_calls);
			LONGS_EQUAL(0, replacement_song.resize_calls);
			LONGS_EQUAL(0, actionLogger.closed);
		}
	}
}
TEST(AudioStartResult, invalid_original_length_rejects_before_marker_arithmetic) {
	AudioClip clip;
	selected_clip = &clip;
	selected_clip->output = &output;
	selected_clip->sampleHolder.audioFile = &sample;
	for (int32_t old_length : {0, -1}) {
		CHECK_FALSE(view.changeUnderlyingSampleStart(clip, &sample, 24, old_length, 96));
		CHECK_FALSE(view.changeUnderlyingSampleLength(clip, &sample, 48, old_length, 96));
	}
	LONGS_EQUAL(10, clip.sampleHolder.startPos);
	LONGS_EQUAL(106, clip.sampleHolder.endPos);
	LONGS_EQUAL(0, song.resize_calls);
}
TEST(AudioStartResult, unrepresentable_marker_lengths_fail_before_mutation) {
	AudioClip clip;
	selected_clip = &clip;
	selected_clip->output = &output;
	selected_clip->sampleHolder.audioFile = &sample;
	CHECK_FALSE(view.changeUnderlyingSampleStart(clip, &sample, INT32_MIN, 96, 96));
	CHECK_FALSE(view.changeUnderlyingSampleStart(clip, &sample, 0, 96, UINT64_MAX));
	CHECK_FALSE(view.changeUnderlyingSampleLength(clip, &sample, 96, 96, UINT64_MAX));
	LONGS_EQUAL(10, clip.sampleHolder.startPos);
	LONGS_EQUAL(106, clip.sampleHolder.endPos);
	LONGS_EQUAL(0, song.resize_calls);
	LONGS_EQUAL(0, actionLogger.closed);
}
TEST(AudioStartResult, marker_positions_clamp_without_unsigned_wraparound) {
	for (bool start_marker : {false, true}) {
		AudioClip clip;
		selected_clip = &clip;
		selected_clip->output = &output;
		selected_clip->sampleHolder.audioFile = &sample;
		clip.sampleControls.reversed = !start_marker;
		CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 0, 1, UINT64_MAX)
		                        : view.changeUnderlyingSampleLength(clip, &sample, 1, 1, UINT64_MAX));
		LONGS_EQUAL(0, clip.sampleHolder.startPos);
		clip = {};
		selected_clip = &clip;
		selected_clip->output = &output;
		selected_clip->sampleHolder.audioFile = &sample;
		clip.sampleControls.reversed = start_marker;
		CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 0, 1, UINT64_MAX)
		                        : view.changeUnderlyingSampleLength(clip, &sample, 1, 1, UINT64_MAX));
		LONGS_EQUAL(1000, clip.sampleHolder.endPos);
	}
}
TEST(AudioStartResult, changed_output_sample_or_direction_rejects_marker_result) {
	int replacement = 0;
	for (bool start_marker : {false, true}) {
		for (bool during_resize : {false, true}) {
			for (int change = 0; change < 3; ++change) {
				AudioClip clip;
				selected_clip = &clip;
				selected_clip->output = &output;
				selected_clip->sampleHolder.audioFile = &sample;
				actionLogger = {};
				actionLogger.action.firstConsequence = &consequence;
				song = {};
				auto change_context = [&] {
					if (change == 0)
						clip.output = &replacement;
					else if (change == 1)
						clip.sampleHolder.audioFile = &replacement;
					else
						clip.sampleControls.reversed = true;
				};
				if (during_resize)
					song.callback = change_context;
				else
					actionLogger.on_allocate = change_context;
				const bool result = start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                                 : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96);
				CHECK_FALSE(result);
				LONGS_EQUAL(during_resize ? 1 : 0, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
				CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
				LONGS_EQUAL(999, consequence.markerValueToRevertTo);
			}
		}
	}
}
TEST(AudioStartResult, callback_marker_changes_reject_edit_before_undo_update) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool during_resize : {false, true}) {
				for (bool change_start : {false, true}) {
					AudioClip clip;
					selected_clip = &clip;
					selected_clip->output = &output;
					selected_clip->sampleHolder.audioFile = &sample;
					clip.sampleControls.reversed = reversed;
					actionLogger = {};
					actionLogger.action.firstConsequence = &consequence;
					song = {};
					auto change_marker = [&] {
						if (change_start)
							++clip.sampleHolder.startPos;
						else
							++clip.sampleHolder.endPos;
					};
					if (during_resize)
						song.callback = change_marker;
					else
						actionLogger.on_allocate = change_marker;
					CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
					                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
					LONGS_EQUAL(during_resize ? 1 : 0, song.resize_calls);
					LONGS_EQUAL(0, actionLogger.closed);
					CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
					LONGS_EQUAL(999, consequence.markerValueToRevertTo);
				}
			}
		}
	}
}
TEST(AudioStartResult, matching_remote_action_ownership_allows_marker_edit) {
	deluge::gui::ui_session::Scope scope(deluge::gui::ui_session::Id::Remote);
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			AudioClip clip;
			selected_clip = &clip;
			selected_clip->output = &output;
			selected_clip->sampleHolder.audioFile = &sample;
			clip.sampleControls.reversed = reversed;
			actionLogger = {};
			actionLogger.action.firstConsequence = &consequence;
			song = {};
			CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
			                        : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
			LONGS_EQUAL(1, song.resize_calls);
			LONGS_EQUAL(1, actionLogger.closed);
			CHECK_TRUE(consequence.sample_marker != ConsequenceClipLength::SampleMarker::NONE);
			LONGS_EQUAL(start_marker == reversed ? 106 : 10, consequence.markerValueToRevertTo);
		}
	}
}
TEST(AudioStartResult, changed_action_ownership_rejects_marker_undo_update) {
	int replacement = 0;
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool during_resize : {false, true}) {
				for (int changed_field = 0; changed_field < 4; ++changed_field) {
					AudioClip clip;
					selected_clip = &clip;
					selected_clip->output = &output;
					selected_clip->sampleHolder.audioFile = &sample;
					clip.sampleControls.reversed = reversed;
					actionLogger = {};
					actionLogger.action.firstConsequence = &consequence;
					song = {};
					auto change_owner = [&] {
						auto& action = actionLogger.action;
						switch (changed_field) {
						case 0:
							action.currentClip = &replacement;
							break;
						case 1:
							action.captured_song = &replacement;
							break;
						case 2:
							action.captured_output = &replacement;
							break;
						case 3:
							action.navigation_owner = deluge::gui::ui_session::Id::Remote;
							break;
						}
					};
					if (during_resize)
						song.callback = change_owner;
					else
						actionLogger.on_allocate = change_owner;
					CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
					                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
					POINTERS_EQUAL(&actionLogger.action, actionLogger.firstAction[BEFORE]);
					LONGS_EQUAL(during_resize ? 1 : 0, song.resize_calls);
					LONGS_EQUAL(0, actionLogger.closed);
					CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
					LONGS_EQUAL(999, consequence.markerValueToRevertTo);
				}
			}
		}
	}
}
TEST(AudioStartResult, callback_length_changes_reject_marker_edit) {
	for (bool with_action : {false, true}) {
		for (bool start_marker : {false, true}) {
			for (bool reversed : {false, true}) {
				for (bool during_resize : {false, true}) {
					AudioClip clip;
					selected_clip = &clip;
					selected_clip->output = &output;
					selected_clip->sampleHolder.audioFile = &sample;
					clip.sampleControls.reversed = reversed;
					actionLogger = {};
					actionLogger.allocation_succeeds = with_action;
					actionLogger.action.firstConsequence = &consequence;
					song = {};
					auto change_length = [&] { clip.loopLength = 123; };
					if (during_resize)
						song.callback = change_length;
					else
						actionLogger.on_allocate = change_length;
					CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
					                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
					LONGS_EQUAL(123, clip.loopLength);
					LONGS_EQUAL(during_resize ? 1 : 0, song.resize_calls);
					LONGS_EQUAL(0, actionLogger.closed);
					CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
					LONGS_EQUAL(999, consequence.markerValueToRevertTo);
				}
			}
		}
	}
}
TEST(AudioStartResult, successful_marker_resize_without_undo_allocation) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			AudioClip clip;
			selected_clip = &clip;
			selected_clip->output = &output;
			selected_clip->sampleHolder.audioFile = &sample;
			clip.sampleControls.reversed = reversed;
			actionLogger = {};
			actionLogger.allocation_succeeds = false;
			song = {};
			CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
			                        : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
			LONGS_EQUAL(start_marker ? 72 : 48, clip.loopLength);
			LONGS_EQUAL(1, song.resize_calls);
			LONGS_EQUAL(0, actionLogger.closed);
		}
	}
}
TEST(AudioStartResult, unrelated_sample_is_rejected_before_marker_mutation) {
	Sample replacement_sample;
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool empty_holder : {false, true}) {
				AudioClip clip;
				selected_clip = &clip;
				selected_clip->output = &output;
				clip.sampleHolder.audioFile = empty_holder ? nullptr : &replacement_sample;
				clip.sampleControls.reversed = reversed;
				CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(10, clip.sampleHolder.startPos);
				LONGS_EQUAL(106, clip.sampleHolder.endPos);
				LONGS_EQUAL(96, clip.loopLength);
				POINTERS_EQUAL(nullptr, actionLogger.firstAction[BEFORE]);
				LONGS_EQUAL(0, song.resize_calls);
			}
		}
	}
}
TEST(AudioStartResult, file_marker_snapping_stays_within_sample) {
	for (uint64_t file_marker : {uint64_t(995), uint64_t(1000), uint64_t(1001), UINT64_MAX}) {
		AudioClip clip;
		selected_clip = &clip;
		selected_clip->output = &output;
		clip.sampleHolder.audioFile = &sample;
		sample.fileLoopStartSamples = file_marker;
		CHECK_TRUE(view.changeUnderlyingSampleLength(clip, &sample, 1000, 96, 96));
		UNSIGNED_LONGS_EQUAL(file_marker <= 1000 ? file_marker : 1000, clip.sampleHolder.endPos);
		LONGS_EQUAL(106, consequence.markerValueToRevertTo);
	}
}
TEST(AudioStartResult, allocation_failure_does_not_publish_marker_change) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool callback_edits_marker : {false, true}) {
				AudioClip clip;
				selected_clip = &clip;
				selected_clip->output = &output;
				clip.sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				actionLogger = {};
				song = {};
				int allocation_calls = 0;
				actionLogger.on_allocate = [&] {
					++allocation_calls;
					LONGS_EQUAL(10, clip.sampleHolder.startPos);
					LONGS_EQUAL(106, clip.sampleHolder.endPos);
					if (callback_edits_marker) {
						clip.sampleHolder.startPos = 20;
						clip.sampleHolder.endPos = 100;
					}
					else
						actionLogger.firstAction[BEFORE] = nullptr;
				};
				CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(1, allocation_calls);
				LONGS_EQUAL(callback_edits_marker ? 20 : 10, clip.sampleHolder.startPos);
				LONGS_EQUAL(callback_edits_marker ? 100 : 106, clip.sampleHolder.endPos);
				LONGS_EQUAL(96, clip.loopLength);
				LONGS_EQUAL(0, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
			}
		}
	}
}
TEST(AudioStartResult, resize_observes_published_marker_after_allocation) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			AudioClip clip;
			selected_clip = &clip;
			selected_clip->output = &output;
			clip.sampleHolder.audioFile = &sample;
			clip.sampleControls.reversed = reversed;
			actionLogger = {};
			song = {};
			actionLogger.on_allocate = [&] {
				LONGS_EQUAL(10, clip.sampleHolder.startPos);
				LONGS_EQUAL(106, clip.sampleHolder.endPos);
			};
			song.callback = [&] {
				const bool changes_start = start_marker != reversed;
				LONGS_EQUAL(changes_start ? (start_marker ? 34 : 58) : 10, clip.sampleHolder.startPos);
				LONGS_EQUAL(changes_start ? 106 : (start_marker ? 82 : 58), clip.sampleHolder.endPos);
			};
			CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
			                        : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
			LONGS_EQUAL(1, song.resize_calls);
		}
	}
}
TEST(AudioStartResult, invalid_resize_target_is_rejected_before_allocating_or_changing_markers) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (int invalid_case = 0; invalid_case < 3; ++invalid_case) {
				AudioClip clip;
				selected_clip = &clip;
				clip.output = invalid_case == 0 ? nullptr : &output;
				clip.loopLength = invalid_case == 0 ? 96 : (invalid_case == 1 ? 0 : -1);
				const int32_t original_length = clip.loopLength;
				clip.sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				int allocation_calls = 0;
				actionLogger = {};
				actionLogger.on_allocate = [&] { ++allocation_calls; };
				song = {};
				CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(original_length, clip.loopLength);
				LONGS_EQUAL(10, clip.sampleHolder.startPos);
				LONGS_EQUAL(106, clip.sampleHolder.endPos);
				LONGS_EQUAL(0, allocation_calls);
				LONGS_EQUAL(0, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
				// Retry the same edit once its prerequisites are restored.
				clip.output = &output;
				clip.loopLength = 96;
				CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                        : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(1, allocation_calls);
				LONGS_EQUAL(1, song.resize_calls);
			}
		}
	}
}
TEST(AudioStartResult, changed_clip_type_rejects_audio_marker_edit) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (int change_stage = 0; change_stage < 3; ++change_stage) {
				AudioClip clip;
				selected_clip = &clip;
				clip.output = &output;
				clip.sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				actionLogger = {};
				actionLogger.action.firstConsequence = &consequence;
				song = {};
				int allocation_calls = 0;
				actionLogger.on_allocate = [&] {
					++allocation_calls;
					if (change_stage == 1)
						clip.type = ClipType::INSTRUMENT;
				};
				if (change_stage == 0)
					clip.type = ClipType::INSTRUMENT;
				if (change_stage == 2)
					song.callback = [&] { clip.type = ClipType::INSTRUMENT; };
				CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(change_stage == 0 ? 0 : 1, allocation_calls);
				LONGS_EQUAL(change_stage == 2 ? 1 : 0, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
				CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
				LONGS_EQUAL(999, consequence.markerValueToRevertTo);
				if (change_stage < 2) {
					LONGS_EQUAL(96, clip.loopLength);
					LONGS_EQUAL(10, clip.sampleHolder.startPos);
					LONGS_EQUAL(106, clip.sampleHolder.endPos);
				}
			}
		}
	}
}
TEST(AudioStartResult, wrong_consequence_target_does_not_receive_marker_history) {
	AudioClip replacement_clip;
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool null_target : {false, true}) {
				AudioClip clip;
				selected_clip = &clip;
				clip.output = &output;
				clip.sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				actionLogger = {};
				actionLogger.action.firstConsequence = &consequence;
				song = {};
				song.callback = [&] { consequence.clip = null_target ? nullptr : &replacement_clip; };
				CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(1, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
				CHECK_TRUE(consequence.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
				LONGS_EQUAL(999, consequence.markerValueToRevertTo);
				POINTERS_EQUAL(&clip, actionLogger.action.currentClip);
			}
		}
	}
}
TEST(AudioStartResult, marker_history_finds_target_length_behind_other_consequences) {
	AudioClip other_clip;
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool other_length : {false, true}) {
				AudioClip clip;
				selected_clip = &clip;
				clip.output = &output;
				clip.sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				Consequence parameter_change;
				parameter_change.type = Consequence::PARAM_CHANGE;
				ConsequenceClipLength unrelated_length;
				unrelated_length.clip = &other_clip;
				unrelated_length.next = &consequence;
				parameter_change.next = other_length ? &unrelated_length : &consequence;
				actionLogger = {};
				actionLogger.action.firstConsequence = &consequence;
				song = {};
				song.callback = [&] { actionLogger.action.firstConsequence = &parameter_change; };
				CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                        : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				const bool changes_start = start_marker != reversed;
				CHECK_TRUE(consequence.sample_marker
				           == (changes_start ? ConsequenceClipLength::SampleMarker::START
				                             : ConsequenceClipLength::SampleMarker::END));
				LONGS_EQUAL(changes_start ? 10 : 106, consequence.markerValueToRevertTo);
				CHECK_TRUE(unrelated_length.sample_marker == ConsequenceClipLength::SampleMarker::NONE);
				LONGS_EQUAL(999, unrelated_length.markerValueToRevertTo);
				POINTERS_EQUAL(&parameter_change, actionLogger.action.firstConsequence);
				LONGS_EQUAL(1, actionLogger.closed);
			}
		}
	}
}
TEST(AudioStartResult, missing_length_undo_aborts_before_marker_publication) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			AudioClip clip;
			selected_clip = &clip;
			clip.output = &output;
			clip.sampleHolder.audioFile = &sample;
			clip.sampleControls.reversed = reversed;
			actionLogger = {};
			actionLogger.action.record_succeeds = false;
			song = {};
			CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
			                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
			LONGS_EQUAL(10, clip.sampleHolder.startPos);
			LONGS_EQUAL(106, clip.sampleHolder.endPos);
			LONGS_EQUAL(96, clip.loopLength);
			LONGS_EQUAL(0, song.resize_calls);
			LONGS_EQUAL(0, actionLogger.closed);
			actionLogger.action.record_succeeds = true;
			CHECK_TRUE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
			                        : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
			LONGS_EQUAL(1, song.resize_calls);
		}
	}
}
TEST(AudioStartResult, length_undo_preparation_revalidates_callback_changes) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			for (bool invalidate_history : {false, true}) {
				AudioClip clip;
				selected_clip = &clip;
				clip.output = &output;
				clip.sampleHolder.audioFile = &sample;
				clip.sampleControls.reversed = reversed;
				actionLogger = {};
				song = {};
				actionLogger.action.on_record = [&] {
					if (invalidate_history)
						actionLogger.firstAction[BEFORE] = nullptr;
					else
						clip.loopLength = 123;
				};
				CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
				                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
				LONGS_EQUAL(10, clip.sampleHolder.startPos);
				LONGS_EQUAL(106, clip.sampleHolder.endPos);
				LONGS_EQUAL(invalidate_history ? 96 : 123, clip.loopLength);
				LONGS_EQUAL(0, song.resize_calls);
				LONGS_EQUAL(0, actionLogger.closed);
			}
		}
	}
}
TEST(AudioStartResult, removed_prepared_consequence_is_not_reported_as_success) {
	for (bool start_marker : {false, true}) {
		for (bool reversed : {false, true}) {
			AudioClip clip;
			selected_clip = &clip;
			clip.output = &output;
			clip.sampleHolder.audioFile = &sample;
			clip.sampleControls.reversed = reversed;
			actionLogger = {};
			song = {};
			song.callback = [&] { actionLogger.action.firstConsequence = nullptr; };
			CHECK_FALSE(start_marker ? view.changeUnderlyingSampleStart(clip, &sample, 24, 96, 96)
			                         : view.changeUnderlyingSampleLength(clip, &sample, 48, 96, 96));
			LONGS_EQUAL(1, song.resize_calls);
			LONGS_EQUAL(0, actionLogger.closed);
			CHECK_TRUE(actionLogger.action.fallback_consequence.sample_marker
			           == ConsequenceClipLength::SampleMarker::NONE);
		}
	}
}
} // namespace audio_start_tests
