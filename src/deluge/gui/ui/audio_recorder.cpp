/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "gui/ui/audio_recorder.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/recording_session.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/mirror.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/finally.h"
#include <string.h>

namespace {
AudioRecorder local_audio_recorder{};
PLACE_SDRAM_BSS deluge::gui::ui_session::RemoteInstance<AudioRecorder> remote_audio_recorder;
deluge::gui::ui_session::RecordingSession recording_session;
} // namespace
AudioRecorder& audio_recorder_for_session() {
	return remote_audio_recorder.get(local_audio_recorder);
}
AudioInputChannel AudioRecorder::recordingSource = AudioInputChannel::NONE;
SampleRecorder* AudioRecorder::recorder = nullptr;

extern "C" void routineForSD(void);

extern "C" {

#include "RZA1/spibsc/r_spibsc_flash_api.h"

void oledRoutine();
}

bool AudioRecorder::capture_recording_target() {
	target_song_lifetime_.reset();
	target_clip_lifetime_.reset();
	target_output_lifetime_.reset();
	target_sound_lifetime_.reset();
	target_source_lifetime_.reset();
	target = {};
	target_clip_ = nullptr;
	target_output_ = nullptr;
	auto* source_song = currentSong;
	auto* selected_sound = sound_editor_for_session().currentSound;
	const auto source_index = sound_editor_for_session().currentSourceIndex;
	if (!source_song || !selected_sound || source_index < 0 || source_index >= kNumSources)
		return false;
	target_song_lifetime_.observe(source_song->watch_lifetime());
	target_sound_lifetime_.observe(selected_sound->watch_sound_lifetime());
	if (!target_song_lifetime_.alive() || !target_sound_lifetime_.alive())
		return false;
	target_clip_ = source_song->getCurrentClip();
	if (!target_clip_)
		return false;
	target_clip_lifetime_.observe(target_clip_->watch_lifetime());
	if (!target_clip_lifetime_.alive() || !target_clip_->output)
		return false;
	target_output_ = target_clip_->output;
	target_output_lifetime_.observe(target_output_->watch_lifetime());
	auto* selected_source = &selected_sound->sources[source_index];
	target_source_lifetime_.observe(selected_source->watch_lifetime());
	if (!target_output_lifetime_.alive() || !target_source_lifetime_.alive()
	    || sound_editor_for_session().currentSource != selected_source)
		return false;
	const auto range_index = sound_editor_for_session().currentMultiRangeIndex;
	if (range_index < 0 || range_index >= selected_source->ranges.getNumElements())
		return false;
	auto* selected_range = selected_source->ranges.getElement(range_index);
	if (sound_editor_for_session().currentMultiRange != selected_range)
		return false;
	target_source_index_ = source_index;
	target_range_index_ = range_index;
	target_range_layout_ = selected_source->ranges.layout_revision();
	target_owner_ = deluge::gui::ui_session::current();
	target = {source_song, selected_sound, selected_source, selected_range,
	          deluge::gui::ui_session::navigation.active().structural_refresh.revision()};
	return true;
}

bool AudioRecorder::recording_target_current() const {
	if (!target_song_lifetime_.alive() || !target_clip_lifetime_.alive() || !target_output_lifetime_.alive()
	    || !target_sound_lifetime_.alive() || !target_source_lifetime_.alive())
		return false;
	auto* selected_source = sound_editor_for_session().currentSource;
	return deluge::gui::ui_session::current() == target_owner_ && currentSong == target.song
	       && currentSong->getCurrentClip() == target_clip_ && target_clip_->output == target_output_
	       && sound_editor_for_session().currentSound == target.sound && selected_source == target.source
	       && sound_editor_for_session().currentMultiRange == target.range
	       && sound_editor_for_session().currentSourceIndex == target_source_index_
	       && sound_editor_for_session().currentMultiRangeIndex == target_range_index_
	       && selected_source->ranges.layout_revision() == target_range_layout_
	       && deluge::gui::ui_session::navigation.active().structural_refresh.revision() == target.revision;
}

bool AudioRecorder::try_assign_recorded_sample(Sample* sample) {
	auto* source_song = currentSong;
	auto* selected_sound = sound_editor_for_session().currentSound;
	const auto source_index = sound_editor_for_session().currentSourceIndex;
	if (!sample || !source_song || !selected_sound || source_index < 0 || source_index >= kNumSources)
		return false;
	auto song_lifetime = source_song->watch_lifetime();
	auto sound_lifetime = selected_sound->watch_sound_lifetime();
	if (!song_lifetime.alive() || !sound_lifetime.alive())
		return false;
	auto* source_clip = source_song->getCurrentClip();
	if (!source_clip)
		return false;
	auto clip_lifetime = source_clip->watch_lifetime();
	if (!clip_lifetime.alive())
		return false;
	auto* source_output = source_clip->output;
	auto* selected_source = &selected_sound->sources[source_index];
	auto source_lifetime = selected_source->watch_lifetime();
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	auto* source_ui = getCurrentUI();
	const auto structure_revision = deluge::gui::ui_session::navigation.active().structural_refresh.revision();
	const auto range_index = sound_editor_for_session().currentMultiRangeIndex;
	const auto context_valid = [&] {
		return song_lifetime.alive() && sound_lifetime.alive() && clip_lifetime.alive() && source_lifetime.alive()
		       && currentSong == source_song && source_song->getCurrentClip() == source_clip
		       && source_clip->output == source_output && deluge::gui::ui_session::current() == source_owner
		       && getCurrentUI() == source_ui
		       && deluge::gui::ui_session::navigation.active().structural_refresh.revision() == structure_revision
		       && sound_editor_for_session().currentSound == selected_sound
		       && sound_editor_for_session().currentSource == selected_source
		       && sound_editor_for_session().currentSourceIndex == source_index
		       && sound_editor_for_session().currentMultiRangeIndex == range_index;
	};
	if (!context_valid() || range_index < 0 || range_index >= selected_source->ranges.getNumElements()
	    || sound_editor_for_session().currentMultiRange != selected_source->ranges.getElement(range_index))
		return false;
	sample->addReason();
	auto sample_cleanup = util::finally([sample] { sample->removeReason("recorded sample attachment"); });
	const auto initial_layout = selected_source->ranges.layout_revision();
	const auto initial_type = selected_source->oscType;
	selected_sound->killAllVoices();
	if (!context_valid() || selected_sound->hasActiveVoices()
	    || selected_source->ranges.layout_revision() != initial_layout || selected_source->oscType != initial_type)
		return false;
	selected_source->setOscType(OscType::SAMPLE);
	if (!context_valid() || selected_source->oscType != OscType::SAMPLE || selected_sound->hasActiveVoices()
	    || range_index >= selected_source->ranges.getNumElements())
		return false;
	auto* selected_range = selected_source->ranges.getElement(range_index);
	const auto assigned_layout = selected_source->ranges.layout_revision();
	const auto reversed = selected_source->sampleControls.isCurrentlyReversed();
	const auto assignment_valid = [&] {
		return context_valid() && selected_source->ranges.layout_revision() == assigned_layout
		       && selected_source->oscType == OscType::SAMPLE && !selected_sound->hasActiveVoices()
		       && selected_source->sampleControls.isCurrentlyReversed() == reversed
		       && sound_editor_for_session().currentMultiRange == selected_range;
	};
	if (!assignment_valid())
		return false;
	auto* holder = selected_range->getAudioFileHolder();
	holder->filePath.set(&sample->filePath);
	if (!assignment_valid())
		return false;
	deluge::lifetime::callback_validation validation(assignment_valid);
	return holder->setAudioFile(sample, reversed, true, CLUSTER_ENQUEUE, &validation) && assignment_valid();
}

bool AudioRecorder::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool AudioRecorder::opened() {
	// Reject competing UI entry even while recorder allocation is yielding.
	if (recording_session.active()) {
		return false;
	}
	updatedRecordingStatus = false;
	if (!capture_recording_target())
		return false;

	actionLogger.deleteAllLogs();

	// If we're already recording (probably the output) then no!
	if (recordingSource > AudioInputChannel::NONE) {
		return false;
	}

	// If recording for a Drum, set the name of the Drum
	if (getCurrentOutputType() == OutputType::KIT) {
		Kit* kit = getCurrentKit();
		SoundDrum* drum = (SoundDrum*)sound_editor_for_session().currentSound;
		String newName;

		Error error = newName.set("REC");
		if (error != Error::NONE) {
gotError:
			display->displayError(error);
			return false;
		}

		error = kit->makeDrumNameUnique(&newName, 1);
		if (error != Error::NONE) {
			goto gotError;
		}

		drum->drumName = newName.get();
	}

	PadLEDs::clearTickSquares(true);

	bool inStereo = (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn);
	int32_t newNumChannels = inStereo ? 2 : 1;
	bool success = setupRecordingToFile(inStereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT, newNumChannels,
	                                    AudioRecordingFolder::RECORD);
	if (success) {
		sound_editor_for_session().setupShortcutBlink(sound_editor_for_session().currentSourceIndex, 4, 0);
		sound_editor_for_session().blinkShortcut();

		indicator_leds::setLedState(IndicatorLED::SYNTH, !sound_editor_for_session().editingKit());
		indicator_leds::setLedState(IndicatorLED::KIT, sound_editor_for_session().editingKit());
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		indicator_leds::blinkLed(IndicatorLED::BACK);
		indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
		if (display->have7SEG()) {
			display->setNextTransitionDirection(0);
			display->setText("WAIT", false, 255, true);
		}
	}

	if (currentUIMode == UI_MODE_AUDITIONING) {
		instrument_clip_view_for_session().cancelAllAuditioning();
	}

	return success;
}

void AudioRecorder::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	const char* current_status_text = updatedRecordingStatus ? "Recording" : "Waiting";
	canvas.drawStringCentred(current_status_text, 19, kTextBigSpacingX, kTextBigSizeY);
}

bool AudioRecorder::setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels,
                                         AudioRecordingFolder folderID, bool writeLoopPoints, bool shouldNormalize) {
	if (recorder || !recording_session.acquire())
		return false;
	const auto source_owner = recording_session.owner();
	deluge::gui::ui_session::Scope owner(source_owner);
	const auto source_revision = recording_session.revision();
	auto* source_ui = getCurrentUI();
	auto* source_song = currentSong;
	auto song_lifetime = source_song ? source_song->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	const auto context_valid = [&] {
		return recording_session.active() && !recording_session.finishing()
		       && recording_session.revision() == source_revision && recording_session.owner() == source_owner
		       && deluge::gui::ui_session::current() == source_owner && getCurrentUI() == source_ui
		       && currentSong == source_song && (!source_song || song_lifetime.alive()) && recorder == nullptr;
	};
	auto* new_recorder = AudioEngine::getNewRecorder(newNumChannels, folderID, newMode, false, writeLoopPoints,
	                                                 kInternalButtonPressLatency, false, nullptr, {false});
	if (!context_valid()) {
		if (new_recorder) {
			new_recorder->pointerHeldElsewhere = false;
			AudioEngine::discardRecorder(new_recorder);
		}
		if (recording_session.revision() == source_revision)
			recording_session.release();
		return false;
	}
	if (!new_recorder) {
		recording_session.release();
		display->displayError(Error::INSUFFICIENT_RAM);
		return false;
	}
	new_recorder->allowFileAlterationAfter = true;
	new_recorder->allowNormalization = shouldNormalize;
	recorder = new_recorder;
	recordingSource = newMode;
	return true;
}

bool AudioRecorder::beginOutputRecording(AudioRecordingFolder folder, AudioInputChannel channel, bool writeLoopPoints,
                                         bool shouldNormalize) {
	if (!setupRecordingToFile(channel, 2, folder, writeLoopPoints, shouldNormalize))
		return false;
	const auto source_revision = recording_session.revision();
	auto* source_recorder = recorder;
	// Publish recording-related audio state before LED callbacks can replace the session.
	// This only applies when a recorder was actually started.
	AudioEngine::bypassCulling = true;
	indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
	return recording_session.active() && !recording_session.finishing()
	       && recording_session.revision() == source_revision && recorder == source_recorder;
}

void AudioRecorder::endRecordingSoon(int32_t buttonLatency) {
	const auto source_owner = recording_session.owner();
	deluge::gui::ui_session::Scope owner(source_owner);
	auto* source_recorder = recorder;
	const auto source_revision = recording_session.revision();
	const auto context_valid = [&] {
		return recording_session.active() && !recording_session.finishing()
		       && recording_session.revision() == source_revision && recording_session.owner() == source_owner
		       && deluge::gui::ui_session::current() == source_owner && recorder == source_recorder;
	};
	if (!source_recorder || !context_valid() || source_recorder->status != RecorderStatus::CAPTURING_DATA)
		return;
	display->displayLoadingAnimationText("Working");
	if (!context_valid() || source_recorder->status != RecorderStatus::CAPTURING_DATA)
		return;
	source_recorder->endSyncedRecording(buttonLatency);
}

void AudioRecorder::slowRoutine() {
	deluge::gui::ui_session::Scope owner(recording_session.owner());
	if (deluge::hid::mirror::is_client())
		return;
	// finishRecording() frees the SampleRecorder, which discardRecorder() forbids doing from inside the SD card
	// routine - the recorder may be suspended part-way through its own cardRoutine(), and freeing it there leaves
	// that cardRoutine() running on freed memory (then freeing it a second time). As a scheduler task we're already
	// held off by our RESOURCE_SD | RESOURCE_SD_ROUTINE registration; this guard covers the one call site that
	// bypasses the scheduler, the legacy non-USE_TASK_MANAGER mainLoop.
	if (sdRoutineLock || currentlyAccessingCard) {
		return;
	}

	if (recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		// setupRecordingToFile() always assigns recorder before recordingSource, so this can't happen - but don't let
		// it become a silent hang: StemExport's yield loop waits on recordingSource, which only finishRecording()
		// clears, so quietly skipping would wedge the export forever with nothing on screen.
		if (recorder == nullptr) {
			FREEZE_WITH_ERROR("E250");
			return;
		}
		if (recorder->status >= RecorderStatus::COMPLETE) {
			indicator_leds::setLedState(IndicatorLED::RECORD, (playbackHandler.recording == RecordingMode::NORMAL));
			finishRecording();
		}
	}
}

void AudioRecorder::process() {
	using namespace deluge::gui::ui_session;
	Scope owner(recording_session.owner());
	if (!recording_session.active() || recording_session.finishing() || recorder == nullptr) {
		return;
	}
	// Completion and UI state always belong to the panel that started recording.
	if (&audio_recorder_for_session() != this) {
		audio_recorder_for_session().process();
		return;
	}
	const auto source_owner = recording_session.owner();
	const auto source_revision = recording_session.revision();
	auto* source_recorder = recorder;
	auto* source_ui = getCurrentUI();
	const auto recording_current = [&] {
		return recording_session.active() && !recording_session.finishing() && recording_session.owner() == source_owner
		       && recording_session.revision() == source_revision && recorder == source_recorder;
	};
	const auto ui_current = [&] { return current() == source_owner && getCurrentUI() == source_ui; };
	while (true) {
		if (!recording_current())
			return;
		{
			Scope hardware(Id::Local);
			AudioEngine::routineWithClusterLoading();
			if (!recording_current())
				return;

			uiTimerManager.routine();
			if (!recording_current())
				return;

			if (display->haveOLED()) {
				oledRoutine();
				if (!recording_current())
					return;
			}
			PIC::flush();
			if (!recording_current())
				return;

			readButtonsAndPads();
			if (!recording_current())
				return;

			AudioEngine::slowRoutine();
			if (!recording_current())
				return;
			deluge::hid::mirror::transport_routine();
			if (!recording_current())
				return;
		}
		if (recording_session.owner() == Id::Remote) {
			uiTimerManager.routine();
			if (!recording_current())
				return;
		}

		// If recording has finished...
		if (recorder->status >= RecorderStatus::COMPLETE || recorder->hadCardError) {

			if (recorder->status == RecorderStatus::ABORTED || recorder->hadCardError) {}

			else if (ui_current() && recording_target_current()) {
				// Attachment also validates the destination across voice cleanup and conversion.
				try_assign_recorded_sample(recorder->sample);
			}
			if (!recording_current() || !finishRecording())
				return;
			if (ui_current() && !recording_session.active())
				close();
			return;
		}

		// Or if recording's ongoing...
		else {
			if (recorder->recordingClippedRecently) {
				recorder->recordingClippedRecently = false;

				if (ui_current() && !display->hasPopup()) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIPPING_OCCURRED));
				}
			}
			else if (ui_current() && !updatedRecordingStatus
			         && (recorder->numSamplesCaptured > recorder->minThresholdMargin)) {
				if (display->have7SEG()) {
					display->setText("REC", false, 255, true);
				}
				else {
					deluge::hid::display::OLED::clearMainImage();
					deluge::hid::display::OLED::main_for_session().drawStringCentred("Recording", 19, kTextBigSpacingX,
					                                                                 kTextBigSizeY);
					deluge::hid::display::OLED::sendMainImage();
				}
				if (!recording_current() || !ui_current())
					return;
				updatedRecordingStatus = true;
			}
		}
	}
}

// Returns whether this recording completed without being superseded.
bool AudioRecorder::finishRecording() {
	const auto source_owner = recording_session.owner();
	deluge::gui::ui_session::Scope owner(source_owner);
	if (!recorder || !recording_session.begin_finish())
		return false;
	const auto finishing_revision = recording_session.revision();
	auto* source_ui = getCurrentUI();
	auto* source_song = currentSong;
	auto* finished_recorder = recorder;
	// Detach shared state before releasing a recorder whose destructor can yield.
	// Keep the reservation until all cleanup and display callbacks have completed.
	recorder = nullptr;
	recordingSource = AudioInputChannel::NONE;
	finished_recorder->pointerHeldElsewhere = false;
	AudioEngine::discardRecorder(finished_recorder);
	if (recording_session.revision() != finishing_revision)
		return false;
	if (deluge::gui::ui_session::current() == source_owner && getCurrentUI() == source_ui && currentSong == source_song)
		display->removeLoadingAnimation();
	if (recording_session.revision() != finishing_revision)
		return false;
	recording_session.release();
	return true;
}

ActionResult AudioRecorder::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (!on) {
		// allow turning off fill mode if it was active while entering audio recorder
		if (b == SYNC_SCALING && currentSong->isFillModeActive()) {
			return getRootUI()->buttonAction(b, on, inCardRoutine);
		}
		return ActionResult::NOT_DEALT_WITH;
	}

	// We don't actually wrap up recording here, because this could be in fact called from the SD writing routines as
	// they wait - that'd be a tangle.
	if ((b == BACK) || (b == SELECT_ENC) || (b == RECORD)) {

		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		endRecordingSoon(kInternalButtonPressLatency);
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

bool AudioRecorder::isCurrentlyResampling() {
	return (recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && recorder
	        && recorder->status == RecorderStatus::CAPTURING_DATA);
}
