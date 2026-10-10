/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/sample/sample_holder.h"
#include "gui/ui/browser/sample_browser.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "util/functions.h"
#include "util/lifetime.h"

SampleHolder::SampleHolder() {
	startPos = 0;
	endPos = 9999999;
	waveformViewZoom = 0;
	audioFileType = AudioFileType::SAMPLE;

	for (int32_t l = 0; l < kNumClustersLoadedAhead; l++) {
		clustersForStart[l] = nullptr;
	}
}

SampleHolder::~SampleHolder() {

	// Don't call setSample() - that does writing to variables which isn't necessary

	if ((Sample*)audioFile) {
		unassignAllClusterReasons(true);
#if ALPHA_OR_BETA_VERSION
		if (audioFile->numReasonsToBeLoaded <= 0) {
			FREEZE_WITH_ERROR("E219"); // I put this here to try and catch an E004 Luc got
		}
#endif
		audioFile->removeReason("E396");
	}
}

void SampleHolder::beenClonedFrom(SampleHolder const* other, bool reversed) {
	filePath.set(&other->filePath);
	// Sample assignment can service callbacks. Keep the source settings without retaining its storage.
	const auto start_pos = other->startPos;
	const auto end_pos = other->endPos;
	const auto waveform_scroll = other->waveformViewScroll;
	const auto waveform_zoom = other->waveformViewZoom;
	if (other->audioFile) {
		setAudioFile(other->audioFile, reversed);
	}

	startPos = start_pos;
	endPos = end_pos;
	waveformViewScroll = waveform_scroll;
	waveformViewZoom = waveform_zoom;
}

void SampleHolder::unassignAllClusterReasons(bool beingDestructed) {
	for (int32_t l = 0; l < kNumClustersLoadedAhead; l++) {
		if (clustersForStart[l] != nullptr) {
			audioFileManager.removeReasonFromCluster(*clustersForStart[l], "E123");
			if (!beingDestructed) {
				clustersForStart[l] = nullptr;
			}
		}
	}
}

int64_t SampleHolder::getEndPos(bool forTimeStretching) {
	if (forTimeStretching) {
		return endPos;
	}
	else {
		return std::min(endPos, ((Sample*)audioFile)->lengthInSamples);
	}
}

int64_t SampleHolder::getDurationInSamples(bool forTimeStretching) {
	return getEndPos(forTimeStretching) - startPos;
}

int32_t SampleHolder::getLengthInSamplesAtSystemSampleRate(bool forTimeStretching) {
	uint64_t lengthInSamples = getDurationInSamples(forTimeStretching);
	if (neutralPhaseIncrement == kMaxSampleValue) {
		return lengthInSamples;
	}
	else {
		return (lengthInSamples << 24) / neutralPhaseIncrement;
	}
}

// returns loop length in ticks from the sample waveform start and end positions selected
int32_t SampleHolder::getLoopLengthAtSystemSampleRate(bool forTimeStretching) {
	if (audioFile) {
		double loopLength = (double)getLengthInSamplesAtSystemSampleRate(forTimeStretching)
		                    / playbackHandler.getTimePerInternalTickFloat();

		return static_cast<int32_t>(loopLength);
	}
	return getCurrentClip()->loopLength;
}

bool SampleHolder::setAudioFile(AudioFile* newSample, bool reversed, bool manuallySelected,
                                int32_t clusterLoadInstruction,
                                const deluge::lifetime::callback_validation* validation) {

	if (!AudioFileHolder::setAudioFile(newSample, reversed, manuallySelected, clusterLoadInstruction, validation))
		return false;

	if (audioFile) {

		if (manuallySelected && ((Sample*)audioFile)->tempFilePathForRecording.isEmpty()) {
			auto& browser = sample_browser_for_session();
			if ((validation && !validation->valid()) || audioFile != newSample)
				return false;
			browser.lastFilePathLoaded.set(&filePath);
		}

		uint32_t lengthInSamples = ((Sample*)audioFile)->lengthInSamples;

		// If we're here as a result of the user having manually selected a new file, set the zone to its actual length.
		if (manuallySelected) {
			startPos = 0;
			endPos = lengthInSamples;
		}

		// Otherwise, simply make sure that the zone doesn't exceed the length of the sample
		else {
			startPos = std::min<uint64_t>(startPos, lengthInSamples);
			if (endPos == 0 || endPos == 9999999) {
				endPos = lengthInSamples;
			}
			if (endPos <= startPos) {
				startPos = 0;
			}
		}

		sampleBeenSet(reversed, manuallySelected);
		if ((validation && !validation->valid()) || audioFile != newSample)
			return false;

#if 1 || ALPHA_OR_BETA_VERSION
		if (!audioFile) {
			FREEZE_WITH_ERROR("i031"); // Trying to narrow down E368 that Kevin F got
		}
#endif

		return claimClusterReasons(reversed, clusterLoadInstruction, validation);
	}
	return true;
}

constexpr int32_t kMarkerSamplesBeforeToClaim = 150;

// Reassesses which Clusters we want to be a "reason" for.
// Ensure there is a sample before you call this.
bool SampleHolder::claimClusterReasons(bool reversed, int32_t clusterLoadInstruction,
                                       const deluge::lifetime::callback_validation* validation) {
	if (validation && !validation->valid())
		return false;

	if (ALPHA_OR_BETA_VERSION && !audioFile) {
		FREEZE_WITH_ERROR("E368");
	}

	// unassignAllReasons(); // This now happens as part of reassessPosForMarker(), called below

	int32_t playDirection = reversed ? -1 : 1;
	int32_t bytesPerSample = audioFile->numChannels * ((Sample*)audioFile)->byteDepth;

	// This code basically copied from VoiceSource::setupPlaybackBounds()

	if (!reversed) {
		startPlaybackAtSample = (int64_t)startPos - kMarkerSamplesBeforeToClaim;
		if (startPlaybackAtSample < 0) {
			startPlaybackAtSample = 0;
		}
	}
	else {
		startPlaybackAtSample = getEndPos() - 1 + kMarkerSamplesBeforeToClaim;
		if (startPlaybackAtSample > ((Sample*)audioFile)->lengthInSamples - 1) {
			startPlaybackAtSample = ((Sample*)audioFile)->lengthInSamples - 1;
		}
	}

	startPlaybackAtByte = ((Sample*)audioFile)->audioDataStartPosBytes + startPlaybackAtSample * bytesPerSample;

	return claimClusterReasonsForMarker(clustersForStart, startPlaybackAtByte, playDirection, clusterLoadInstruction,
	                                    kNumClustersLoadedAhead, validation);
}

bool SampleHolder::claimClusterReasonsForMarker(Cluster** clusters, uint32_t startPlaybackAtByte, int32_t playDirection,
                                                int32_t clusterLoadInstruction, int32_t /* numClustersToClaim */,
                                                const deluge::lifetime::callback_validation* validation) {
	if (validation && !validation->valid())
		return false;
	auto* const source_sample = static_cast<Sample*>(audioFile);
	if (!source_sample)
		return false;
	const auto source_start = startPos;
	const auto source_end = endPos;
	// Preserve the existing two-cluster marker policy.
	constexpr int32_t cluster_count = 2;
	Cluster* original_clusters[cluster_count] = {clusters[0], clusters[1]};
	const auto context_valid = [&] {
		return (!validation || validation->valid()) && audioFile == source_sample && startPos == source_start
		       && endPos == source_end && clusters[0] == original_clusters[0] && clusters[1] == original_clusters[1];
	};
	// The holder can disappear while getCluster services storage. Keep the sample
	// alive until every temporary cluster reason has been transferred or released.
	source_sample->addReason();
	struct sample_reason_guard {
		Sample* sample;
		~sample_reason_guard() { sample->removeReason("E463"); }
	} sample_reason{source_sample};
	struct cluster_reason_guard {
		Cluster* values[cluster_count]{};
		~cluster_reason_guard() {
			for (auto* cluster : values)
				if (cluster)
					audioFileManager.removeReasonFromCluster(*cluster, "E146");
		}
	} acquired;

	int32_t cluster_index = startPlaybackAtByte >> Cluster::size_magnitude;
	for (int32_t index = 0; index < cluster_count; ++index) {
		if (cluster_index < source_sample->getFirstClusterIndexWithAudioData()
		    || cluster_index >= source_sample->getFirstClusterIndexWithNoAudioData())
			break;
		auto* sample_cluster = source_sample->clusters.getElement(cluster_index);
		acquired.values[index] = sample_cluster->getCluster(source_sample, cluster_index, clusterLoadInstruction);
		if (!context_valid())
			return false;
		if (!acquired.values[index])
			break;
		cluster_index += playDirection;
	}

	for (int32_t index = 0; index < cluster_count; ++index) {
		if (clusters[index])
			audioFileManager.removeReasonFromCluster(*clusters[index], "E146");
		clusters[index] = acquired.values[index];
		acquired.values[index] = nullptr;
	}
	return true;
}
