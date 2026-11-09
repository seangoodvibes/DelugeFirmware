// SPDX-License-Identifier: GPL-3.0-or-later

#include "model/clip/clip_freeze.h"
#include "model/clip/freeze_cycle.h"
#include "model/clip/instrument_clip.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster_priority_queue.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {
constexpr int32_t crossfadeSamples = 2048; // 46 ms at 44.1 kHz.
constexpr int32_t tailGuardSamples = SSI_TX_BUFFER_NUM_SAMPLES;
constexpr int32_t priority = static_cast<int32_t>(ClusterPriority::forAudioClip(VoicePriority::MEDIUM));
} // namespace

ClipFreeze::~ClipFreeze() {
	clear();
}

void ClipFreeze::Stream::stopOutgoing() {
	if (outgoing) {
		outgoing->beenUnassigned(false);
		AudioEngine::voiceSampleUnassigned(outgoing);
		outgoing = nullptr;
	}
}

void ClipFreeze::Stream::stop() {
	stopOutgoing();
	if (voice) {
		voice->beenUnassigned(false);
		AudioEngine::voiceSampleUnassigned(voice);
		voice = nullptr;
	}
}

void ClipFreeze::Stream::clear() {
	stop();
	holder.setAudioFile(nullptr);
	if (recorder) {
		// Defer file deletion and destruction to the card routine, including when called from an audio callback.
		recorder->abort();
		recorder->pointerHeldElsewhere = false;
		recorder = nullptr;
	}
}

void ClipFreeze::clear() {
	dry_.clear();
	reverb_.clear();
	state_ = State::OFF;
	haveBoundary_ = false;
	resumePending_ = false;
	crossfadeLength_ = crossfadeRemaining_ = 0;
	loopSamples_ = 0;
	tailRemaining_ = -1;
}

void ClipFreeze::fail() {
	clear();
	state_ = State::FAILED;
}

Error ClipFreeze::arm(InstrumentClip& clip) {
	int32_t period;
	bool continuous, snapshot;
	Error error = clip.getFreezeCycleLength(period, continuous, snapshot);
	if (error != Error::NONE) {
		clear();
		return error;
	}
	return arm(clip, period, continuous, snapshot);
}

Error ClipFreeze::arm(Clip& clip, int32_t period, bool continuousPlayback, bool snapshot) {
	clear();
	output_ = clip.output;
	length_ = clip.loopLength;
	stereoRendering_ = AudioEngine::renderInStereo;
	// Keep snapshots within the stream's signed byte-offset limit too. A long clip
	// may fit once but not eight times; retain as many complete patterns as fit.
	constexpr uint64_t maxSamples = (INT32_MAX - 44) / 6 - crossfadeSamples - tailGuardSamples;
	tickTime_ = playbackHandler.getTimePerInternalTickBig();
	if (!tickTime_) {
		return Error::UNSPECIFIED;
	}
	FreezeCycle cycle(length_);
	if (!cycle.include(period, 1) || (snapshot && !cycle.extendForSnapshot((maxSamples << 32) / tickTime_))) {
		return Error::FILE_TOO_BIG;
	}
	period_ = cycle.ticks();
	snapshot_ = snapshot;
	continuousPlayback_ = continuousPlayback;
	// VoiceSample addresses file bytes with signed 32-bit offsets.
	if (period_ <= 0 || playbackHandler.getTimePerInternalTickBig() > (maxSamples << 32) / period_) {
		return Error::FILE_TOO_BIG;
	}
	tickTime_ = playbackHandler.getTimePerInternalTickBig();
	for (auto* stream : {&dry_, &reverb_}) {
		// OFFLINE_OUTPUT recorders are fed explicitly, never by the input routine. CLIPS uses the temporary folder.
		stream->recorder = AudioEngine::getNewRecorder(
		    stream == &dry_ ? 2 : 1, AudioRecordingFolder::CLIPS, AudioInputChannel::OFFLINE_OUTPUT, true, false, 0,
		    false, nullptr, {.neverUseThreshold = true, .forceStereoRendering = false});
		if (!stream->recorder) {
			fail();
			return Error::INSUFFICIENT_RAM;
		}
		stream->recorder->numSamplesToRunBeforeBeginningCapturing = 0;
		stream->recorder->numSamplesExtraToCaptureAtEndSyncingWise = 0;
		stream->recorder->allowFileAlterationAfter = false;
	}
	state_ = State::ARMED;
	return Error::NONE;
}

bool ClipFreeze::validFor(Clip& clip) const {
	return clip.output == output_ && clip.loopLength == length_ && AudioEngine::renderInStereo == stereoRendering_
	       && clip.sequenceDirectionMode == SequenceDirection::FORWARD
	       && playbackHandler.getTimePerInternalTickBig() == tickTime_ && !playbackHandler.isExternalClockActive()
	       && !clip.getCurrentlyRecordingLinearly() && playbackHandler.recording != RecordingMode::ARRANGEMENT;
}

bool ClipFreeze::healthy() const {
	for (auto* stream : {&dry_, &reverb_}) {
		if (!stream->recorder || stream->recorder->hadCardError || stream->recorder->reachedMaxFileSize
		    || stream->recorder->status >= RecorderStatus::ABORTED) {
			return false;
		}
	}
	return true;
}

void ClipFreeze::Stream::prepare() {
	holder.startPos = 0;
	holder.endPos = recorder->sample->lengthInSamples;
	holder.setAudioFile(recorder->sample);
	guide.audioFileHolder = &holder;
	guide.sequenceSyncLengthTicks = 0;
	guide.sequenceSyncStartedAtTick = 0;
	guide.setupPlaybackBounds(false);
}

bool ClipFreeze::Stream::start(int32_t pos, int32_t length, int64_t loopSamples, bool overlap) {
	stopOutgoing();
	if (overlap) {
		outgoing = voice;
		voice = nullptr;
	}
	else {
		stop();
	}
	voice = AudioEngine::solicitVoiceSample();
	if (!voice) {
		return false;
	}
	voice->noteOn(&guide, 0, priority);
	auto* sample = static_cast<Sample*>(holder.audioFile);
	int64_t offset = loopSamples * pos / length;
	return voice->setupClusersForInitialPlay(&guide, sample, offset * sample->numChannels * sample->byteDepth, false,
	                                         priority);
}

int32_t ClipFreeze::phase(int32_t pos, int32_t repeatCount) const {
	int64_t ticks = (int64_t{repeatCount} - captureStartRepeat_) * length_ + std::max<int32_t>(0, pos);
	return static_cast<int32_t>((ticks % period_ + period_) % period_);
}

bool ClipFreeze::start(int32_t pos, int32_t repeatCount) {
	if (state_ != State::READY && state_ != State::PLAYING) {
		return false;
	}
	bool overlap = state_ == State::PLAYING && crossfadeLength_ > 0;
	pos = phase(pos, repeatCount);
	if (!dry_.start(pos, period_, loopSamples_, overlap) || !reverb_.start(pos, period_, loopSamples_, overlap)) {
		fail();
		return false;
	}
	crossfadeRemaining_ = overlap ? crossfadeLength_ : 0;
	state_ = State::PLAYING;
	resumePending_ = false;
	return true;
}

void ClipFreeze::pause() {
	crossfadeRemaining_ = 0;
	if (continuousPlayback_) {
		// Global LFO phase follows the song clock, not clip repeatCount. A relaunch
		// can change their relative phase, so do not reuse this cache after a stop/seek.
		clear();
		return;
	}
	if (state_ == State::PLAYING) {
		dry_.stop();
		reverb_.stop();
		state_ = State::READY;
		resumePending_ = true;
	}
	else if (state_ == State::CAPTURING) {
		// An interrupted pass is not a valid loop.
		clear();
	}
}

void ClipFreeze::loop(int32_t repeatCount) {
	if (!enabled()) {
		return;
	}
	if (haveBoundary_ && lastBoundary_ == playbackHandler.lastSwungTickActioned) {
		return; // Edits may reschedule this same musical tick after more audio has rendered.
	}
	haveBoundary_ = true;
	lastBoundary_ = playbackHandler.lastSwungTickActioned;
	if (!healthy()) {
		fail();
		return;
	}
	switch (state_) {
	case State::ARMED:
		captureStartRepeat_ = repeatCount;
		loopsRemaining_ = period_ / length_;
		state_ = State::CAPTURING;
		break;
	case State::CAPTURING:
		if (tailRemaining_ >= 0) {
			break;
		}
		if (--loopsRemaining_ > 0) {
			break;
		}
		loopSamples_ = dry_.recorder->numSamplesCaptured;
		if (!loopSamples_ || reverb_.recorder->numSamplesCaptured != loopSamples_) {
			fail();
			break;
		}
		crossfadeLength_ = std::min<int64_t>(crossfadeSamples, loopSamples_ / 4);
		// Continue the native performance beyond the musical loop. At each wrap,
		// the outgoing voice plays this continuation while the incoming voice fades
		// in at the original start, preserving the exact musical loop duration.
		tailRemaining_ = crossfadeLength_ + tailGuardSamples;
		break;
	case State::WRITING:
		if (dry_.recorder->status == RecorderStatus::COMPLETE && reverb_.recorder->status == RecorderStatus::COMPLETE) {
			dry_.prepare();
			reverb_.prepare();
			state_ = State::READY; // Give the card routine a pass to preload the loop starts before switching.
		}
		break;
	case State::READY:
	case State::PLAYING:
		// Wait for the recorded phase to come around again. Do not restart at each ordinary clip loop:
		// the file contains the complete multi-loop pattern, including intentionally silent iterations.
		// Transport starts may reach the first tick without resumePlayback(). A
		// paused cache can start at any recorded phase, unlike the initial handoff.
		if (resumePending_ || phase(0, repeatCount) == 0) {
			start(0, repeatCount);
		}
		break;
	default:
		break;
	}
}

void ClipFreeze::capture(std::span<StereoSample> dry, std::span<int32_t> reverb) {
	if (!capturing()) {
		return;
	}
	if (!healthy()) {
		fail();
		return;
	}
	if (tailRemaining_ >= 0) {
		size_t count = std::min<size_t>(dry.size(), tailRemaining_);
		dry = dry.first(count);
		reverb = reverb.first(count);
		tailRemaining_ -= count;
	}
	dry_.recorder->feedAudio(dry);
	static StereoSample send[SSI_TX_BUFFER_NUM_SAMPLES];
	for (size_t i = 0; i < reverb.size(); ++i) {
		send[i] = {reverb[i], reverb[i]};
	}
	reverb_.recorder->feedAudio({send, reverb.size()});
	if (tailRemaining_ == 0) {
		dry_.recorder->endSyncedRecording(0);
		reverb_.recorder->endSyncedRecording(0);
		state_ = State::WRITING;
	}
}

bool ClipFreeze::Stream::render(int32_t* buffer, int32_t count, int32_t channels, bool tail) {
	// VoiceSample shifts amplitude left by three before its Q32 multiply. Use half gain, then restore it
	// after reading. This preserves headroom and avoids clipping the recorded data.
	auto* player = tail ? outgoing : voice;
	return player
	       && player->render(&guide, buffer, count, static_cast<Sample*>(holder.audioFile), channels, LoopType::NONE,
	                         kMaxSampleValue, kMaxSampleValue, (INT32_MAX >> 3), 0, 2, InterpolationMode::LINEAR,
	                         priority);
}

bool ClipFreeze::render(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder) {
	if (!playing()) {
		return false;
	}
	static StereoSample audio[SSI_TX_BUFFER_NUM_SAMPLES];
	static int32_t send[SSI_TX_BUFFER_NUM_SAMPLES];
	static StereoSample tailAudio[SSI_TX_BUFFER_NUM_SAMPLES];
	static int32_t tailSend[SSI_TX_BUFFER_NUM_SAMPLES];
	int32_t overlap = std::min<int32_t>(crossfadeRemaining_, dry.size());
	std::fill_n(tailAudio, overlap, StereoSample{0, 0});
	std::fill_n(tailSend, overlap, 0);
	std::fill_n(audio, dry.size(), StereoSample{0, 0});
	std::fill_n(send, dry.size(), 0);
	if (!dry_.render(reinterpret_cast<int32_t*>(audio), dry.size(), 2) || !reverb_.render(send, dry.size(), 1)) {
		fail();
		return false;
	}
	if (overlap
	    && (!dry_.render(reinterpret_cast<int32_t*>(tailAudio), overlap, 2, true)
	        || !reverb_.render(tailSend, overlap, 1, true))) {
		fail();
		return false;
	}
	if (!AudioEngine::renderInStereo) {
		// VoiceSample condenses stereo files to packed mono in mono mode. Expand backwards
		// in place before mixing into the engine's always-stereo output buffer.
		auto* mono = reinterpret_cast<int32_t*>(audio);
		for (size_t i = dry.size(); i-- > 0;) {
			int32_t value = mono[i];
			audio[i] = {value, value};
		}
		auto* tailMono = reinterpret_cast<int32_t*>(tailAudio);
		for (int32_t i = overlap; i-- > 0;) {
			int32_t value = tailMono[i];
			tailAudio[i] = {value, value};
		}
	}
	float previousIncoming = incomingPower_, previousOutgoing = outgoingPower_, previousCross = crossPower_;
	if (overlap) {
		float incoming = 0, outgoing = 0, cross = 0;
		for (int32_t i = 0; i < overlap; ++i) {
			float il = audio[i].l, ir = audio[i].r;
			float ol = tailAudio[i].l, oright = tailAudio[i].r;
			incoming += il * il + ir * ir;
			outgoing += ol * ol + oright * oright;
			cross += il * ol + ir * oright;
		}
		if (crossfadeRemaining_ == crossfadeLength_) {
			previousIncoming = incomingPower_ = incoming / overlap;
			previousOutgoing = outgoingPower_ = outgoing / overlap;
			previousCross = crossPower_ = cross / overlap;
		}
		else {
			float alpha = float(overlap) / (overlap + 256);
			incomingPower_ += alpha * (incoming / overlap - incomingPower_);
			outgoingPower_ += alpha * (outgoing / overlap - outgoingPower_);
			crossPower_ += alpha * (cross / overlap - crossPower_);
		}
	}
	for (size_t i = 0; i < dry.size(); ++i) {
		audio[i].l *= 2;
		audio[i].r *= 2;
		int32_t sendSample = send[i] * 2;
		if (i < static_cast<size_t>(overlap)) {
			// Smoothstep weights have zero slope at each end of the overlap.
			int64_t x = int64_t{crossfadeLength_ - crossfadeRemaining_} * 65536 / crossfadeLength_;
			int64_t weight = ((x * x >> 16) * (3 * 65536 - 2 * x)) >> 16;
			// Correlated material needs unity gain; phase-shifted sustained tones need
			// more than a linear fade to avoid a hole. Estimate the mixed power from
			// both streams and their correlation, with sample-count-based smoothing.
			// Bound compensation to +12 dB for near-opposite phases, and saturate.
			float w = float(weight) / 65536;
			// Interpolate estimates to avoid gain steps at render-block boundaries.
			float progress = float(i + 1) / overlap;
			float incomingPower = previousIncoming + progress * (incomingPower_ - previousIncoming);
			float outgoingPower = previousOutgoing + progress * (outgoingPower_ - previousOutgoing);
			float crossPower = previousCross + progress * (crossPower_ - previousCross);
			float targetPower = (1 - w) * outgoingPower + w * incomingPower;
			float mixedPower = (1 - w) * (1 - w) * outgoingPower + w * w * incomingPower + 2 * w * (1 - w) * crossPower;
			float gain = targetPower > 1.0f
			                 ? std::sqrt(std::clamp(targetPower / std::max(mixedPower, targetPower / 16), 1.0f, 16.0f))
			                 : 1.0f;
			auto blend = [&](int32_t incoming, int32_t outgoing) {
				int64_t mixed = (int64_t{incoming} * weight + int64_t{outgoing} * (65536 - weight)) >> 16;
				return static_cast<int32_t>(
				    std::clamp<int64_t>(static_cast<int64_t>(mixed * gain), INT32_MIN, INT32_MAX));
			};
			audio[i].l = blend(audio[i].l, tailAudio[i].l * 2);
			audio[i].r = blend(audio[i].r, tailAudio[i].r * 2);
			sendSample = blend(sendSample, tailSend[i] * 2);
			--crossfadeRemaining_;
		}
		dry[i] += audio[i];
		reverb[i] += sendSample;
	}
	if (!crossfadeRemaining_) {
		dry_.stopOutgoing();
		reverb_.stopOutgoing();
	}
	if (recorder && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		recorder->feedAudio({audio, dry.size()}, true, 2); // Match Sound::render's output recording gain.
	}
	return true;
}
