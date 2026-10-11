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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/recording_session.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "util/lifetime.h"

extern "C" {
#include "fatfs/ff.h"
}

class SampleRecorder;
class Sample;
class Clip;
class Output;

class AudioRecorder final : public UI {
public:
	AudioRecorder() = default;
	bool opened() override;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	bool beginOutputRecording(AudioRecordingFolder folder = AudioRecordingFolder::RESAMPLE,
	                          AudioInputChannel channel = AudioInputChannel::OUTPUT, bool writeLoopPoints = false,
	                          bool shouldNormalize = true);
	void process();
	void slowRoutine();
	bool isCurrentlyResampling();

	static AudioInputChannel recordingSource;

	static SampleRecorder* recorder;

	void endRecordingSoon(int32_t buttonLatency = 0);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	bool updatedRecordingStatus = false;

	// ui
	UIType getUIType() override { return UIType::AUDIO_RECORDER; }

private:
	deluge::gui::ui_session::RecordingTarget target;
	deluge::lifetime::lifetime_watch target_song_lifetime_, target_clip_lifetime_, target_output_lifetime_;
	deluge::lifetime::lifetime_watch target_sound_lifetime_, target_source_lifetime_;
	Clip* target_clip_ = nullptr;
	Output* target_output_ = nullptr;
	int32_t target_source_index_ = -1, target_range_index_ = -1;
	uint32_t target_range_layout_ = 0;
	deluge::gui::ui_session::Id target_owner_ = deluge::gui::ui_session::Id::Local;
	bool capture_recording_target();
	bool prepare_recording_target();
	bool start_prepared_recording();
	bool recording_target_current() const;
	bool finishRecording(bool remove_animation = true);
	bool try_assign_recorded_sample(Sample* sample);
	bool setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels, AudioRecordingFolder folderID,
	                          bool writeLoopPoints = false, bool shouldNormalize = true);
};

AudioRecorder& audio_recorder_for_session();
