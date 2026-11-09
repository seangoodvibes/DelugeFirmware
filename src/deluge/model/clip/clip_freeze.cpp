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

int32_t ClipFreeze::Stream::remainingSamples() {
	if (!voice)
		return 0;
	auto* sample = static_cast<Sample*>(holder.audioFile);
	return std::max<int64_t>(0, sample->lengthInSamples - voice->getPlaySample(sample, &guide));
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
	releaseDry_.clear();
	releaseReverb_.clear();
	capturingRelease_ = releasing_ = entering_ = clearAfterRelease_ = false;
	releaseSamples_ = releaseQuietSamples_ = releasePosition_ = 0;
	releaseHeadPower_ = playbackPower_ = 0;
	state_ = State::OFF;
	haveBoundary_ = false;
	resumePending_ = false;
	crossfadeLength_ = 0;
	loopFade_.reset(0);
	handoffFade_.reset(0);
	thawing_ = waitingForNativeStart_ = syncedStretch_ = false;
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
	       && clip.sequenceDirectionMode == SequenceDirection::FORWARD && !clip.getCurrentlyRecordingLinearly()
	       && playbackHandler.recording != RecordingMode::ARRANGEMENT;
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

void ClipFreeze::Stream::prepare(int32_t length, int64_t loopSamples) {
	holder.startPos = 0;
	holder.endPos = loopSamples;
	holder.setAudioFile(recorder->sample);
	guide.audioFileHolder = &holder;
	guide.sequenceSyncLengthTicks = length;
	guide.sequenceSyncStartedAtTick = 0;
	guide.setupPlaybackBounds(false);
	// Sync uses only the musical loop. Playback may read the recorded continuation
	// beyond it during a wrap crossfade, without stretching that margin into the beat.
	auto* sample = recorder->sample;
	guide.endPlaybackAtByte =
	    sample->audioDataStartPosBytes + sample->lengthInSamples * sample->numChannels * sample->byteDepth;
}

bool ClipFreeze::Stream::start(int32_t pos, int32_t length, int64_t loopSamples, bool overlap) {
	stopOutgoing();
	if (overlap) {
		outgoing = voice;
		outgoingGuide = guide;
		voice = nullptr;
	}
	else {
		stop();
	}
	voice = AudioEngine::solicitVoiceSample();
	if (!voice) {
		return false;
	}
	// As with AudioClip, anchor sync to the sequence's ticks, including mid-loop
	// launches. The outgoing voice keeps its previous anchor through the overlap.
	int64_t now = playbackHandler.getCurrentInternalTickCount();
	int64_t elapsed = now - (playbackHandler.lastSwungTickActioned - pos);
	elapsed = length ? std::max<int64_t>(0, elapsed) % length : 0;
	guide.sequenceSyncStartedAtTick = now - elapsed;
	guide.sequenceSyncLengthTicks = length;
	auto* sample = static_cast<Sample*>(holder.audioFile);
	guide.endPlaybackAtByte =
	    sample->audioDataStartPosBytes + sample->lengthInSamples * sample->numChannels * sample->byteDepth;
	voice->noteOn(&guide, 0, priority);
	int64_t offset = length ? loopSamples * pos / length : 0;
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
	if (releasing_) {
		finishRelease();
		if (state_ != State::READY)
			return false; // A pending thaw or song-relative cache was discarded.
	}
	pos = phase(pos, repeatCount);
	if (state_ == State::PLAYING && syncedStretch_ && pos == 0 && dry_.voice->canContinueTimeStretching()
	    && reverb_.voice->canContinueTimeStretching()) {
		// AudioClip keeps its stretched voices across wraps and moves the sync
		// anchor. The next grain returns to the loop start with its native fade.
		dry_.guide.sequenceSyncStartedAtTick = playbackHandler.lastSwungTickActioned;
		reverb_.guide.sequenceSyncStartedAtTick = playbackHandler.lastSwungTickActioned;
		return true;
	}
	bool firstStart = state_ == State::READY && !resumePending_;
	// Near unity speed the native fast path may drift slightly ahead of the
	// clock. Only fade across continuation that is actually still unread.
	int32_t availableTail = std::min(dry_.remainingSamples(), reverb_.remainingSamples()) - tailGuardSamples;
	int32_t sourceFade = std::min(crossfadeLength_, availableTail);
	bool overlap = state_ == State::PLAYING && !syncedStretch_ && sourceFade > 0;
	if (!dry_.start(pos, period_, loopSamples_, overlap) || !reverb_.start(pos, period_, loopSamples_, overlap)) {
		fail();
		return false;
	}
	// The tail is measured in source frames. Speeding up shortens the output
	// duration available for the fade; never read beyond the recorded margin.
	int32_t fadeSamples = (int64_t{std::max<int32_t>(0, sourceFade)} << 24) / std::max(kMaxSampleValue, stretchRatio());
	loopFade_.reset(overlap ? std::max<int32_t>(1, fadeSamples) : 0);
	if (firstStart) {
		entering_ = true;
		resetHandoff();
		if (!snapshot_)
			handoffFade_.reset(crossfadeSamples);
	}
	state_ = State::PLAYING;
	resumePending_ = false;
	return true;
}

void ClipFreeze::pause(bool release) {
	if (release && state_ == State::PLAYING && !entering_) {
		if (releasing_)
			return;
		if (capturingRelease_) {
			capturingRelease_ = false;
			releaseDry_.clear();
			releaseReverb_.clear();
		}
		clearAfterRelease_ = thawing_;
		thawing_ = waitingForNativeStart_ = false;
		releasing_ = true;
		// The transport clock may stop; let the short bridge run freely.
		dry_.guide.sequenceSyncLengthTicks = reverb_.guide.sequenceSyncLengthTicks = 0;
		releasePosition_ = 0;
		// End the bridge before its source continuation runs out when stopping
		// near a loop boundary. The recorded release itself remains untruncated.
		int32_t available = std::min(dry_.remainingSamples(), reverb_.remainingSamples()) - tailGuardSamples;
		int32_t bridge = (int64_t{std::max<int32_t>(1, available)} << 24) / std::max(kMaxSampleValue, stretchRatio());
		releaseFade_.reset(std::clamp<int32_t>(bridge, 1, 4410));
		releaseGain_ = std::sqrt(std::clamp(playbackPower_ / std::max(1.0f, releaseHeadPower_), 0.0625f, 16.0f));
		bool ready = releaseDry_.recorder && releaseReverb_.recorder
		             && releaseDry_.recorder->status == RecorderStatus::COMPLETE
		             && releaseReverb_.recorder->status == RecorderStatus::COMPLETE;
		if (ready) {
			releaseDry_.prepare(0, releaseSamples_);
			releaseReverb_.prepare(0, releaseSamples_);
			ready =
			    releaseDry_.start(0, 0, releaseSamples_, false) && releaseReverb_.start(0, 0, releaseSamples_, false);
		}
		if (!ready) {
			releaseDry_.stop();
			releaseReverb_.stop();
		}
		return;
	}
	if (releasing_)
		finishRelease();
	entering_ = false;
	if (capturingRelease_) {
		capturingRelease_ = false;
		releaseDry_.clear();
		releaseReverb_.clear();
	}
	syncedStretch_ = false;
	loopFade_.reset(0);
	handoffFade_.reset(0);
	if (thawing_) {
		clear();
		return;
	}
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
	if (!enabled() && !thawing_) {
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
			dry_.prepare(period_, loopSamples_);
			reverb_.prepare(period_, loopSamples_);
			state_ = State::READY; // Give the card routine a pass to preload the loop starts before switching.
		}
		break;
	case State::READY:
	case State::PLAYING:
		// Restart through the ordinary sequencer at a complete musical pattern
		// boundary. Starting a held note halfway through a cycle offsets local
		// synced modulation even if its level has already recovered.
		if (waitingForNativeStart_ && repeatCount % (period_ / length_) == 0) {
			waitingForNativeStart_ = false;
		}
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

int32_t ClipFreeze::stretchRatio() const {
	// Use the measured recording duration, not the tempo at arm time: the
	// external clock may have settled or changed during capture.
	double duration = double(playbackHandler.getTimePerInternalTickBig()) / 4294967296.0 * period_;
	if (duration <= 0)
		return kMaxSampleValue;
	return static_cast<int32_t>(std::clamp(double(loopSamples_) * kMaxSampleValue / duration, 1.0, double(INT32_MAX)));
}

bool ClipFreeze::Stream::render(int32_t* buffer, int32_t count, int32_t channels, int32_t stretch, bool syncedStretch,
                                bool tail) {
	auto* player = tail ? outgoing : voice;
	auto* playbackGuide = tail ? &outgoingGuide : &guide;
	if (!player)
		return false;
	if (syncedStretch) {
		// Native time-stretcher looping uses the musical marker, not the extra
		// continuation recorded for non-stretched crossfades.
		auto* sample = static_cast<Sample*>(holder.audioFile);
		playbackGuide->endPlaybackAtByte =
		    sample->audioDataStartPosBytes + holder.endPos * sample->numChannels * sample->byteDepth;
	}
	// VoiceSample doubles its gain for time stretching. Quarter gain leaves room
	// for that doubling; restore the common half-gain contract after rendering.
	bool success =
	    player->render(playbackGuide, buffer, count, static_cast<Sample*>(holder.audioFile), channels,
	                   syncedStretch ? LoopType::TIMESTRETCHER_LEVEL_IF_ACTIVE : LoopType::NONE, kMaxSampleValue,
	                   stretch, (INT32_MAX >> 4), 0, 2, InterpolationMode::LINEAR, priority);
	int32_t outputChannels = channels == 2 && AudioEngine::renderInStereo ? 2 : 1;
	for (int32_t i = 0; i < count * outputChannels; ++i)
		buffer[i] *= 2;
	return success;
}

bool ClipFreeze::render(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder) {
	return releasing_ ? renderRelease(dry, reverb, recorder) : renderLoop(dry, reverb, recorder);
}

bool ClipFreeze::renderLoop(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder) {
	if (releaseDry_.recorder && releaseReverb_.recorder && !releaseDry_.holder.audioFile
	    && releaseDry_.recorder->status == RecorderStatus::COMPLETE
	    && releaseReverb_.recorder->status == RecorderStatus::COMPLETE) {
		releaseDry_.prepare(0, releaseSamples_);
		releaseReverb_.prepare(0, releaseSamples_);
	}
	if (state_ != State::PLAYING) {
		return false;
	}
	static StereoSample audio[SSI_TX_BUFFER_NUM_SAMPLES];
	static int32_t send[SSI_TX_BUFFER_NUM_SAMPLES];
	static StereoSample tailAudio[SSI_TX_BUFFER_NUM_SAMPLES];
	static int32_t tailSend[SSI_TX_BUFFER_NUM_SAMPLES];
	int32_t overlap = std::min<int32_t>(loopFade_.remaining, dry.size());
	std::fill_n(tailAudio, overlap, StereoSample{0, 0});
	std::fill_n(tailSend, overlap, 0);
	std::fill_n(audio, dry.size(), StereoSample{0, 0});
	std::fill_n(send, dry.size(), 0);
	int32_t stretch = stretchRatio();
	if (!syncedStretch_ && !releasing_) {
		// Use the same native-rate tolerance as AudioClip, but make one shared
		// decision so dry audio and reverb send retain the same musical clock.
		syncedStretch_ = stretch < 16661327 || stretch >= 16893911
		                 || std::abs(dry_.guide.getNumSamplesLaggingBehindSync(dry_.voice)) >= (44100 >> 7)
		                 || std::abs(reverb_.guide.getNumSamplesLaggingBehindSync(reverb_.voice)) >= (44100 >> 7);
	}
	if (!syncedStretch_)
		stretch = kMaxSampleValue;
	else if (stretch == kMaxSampleValue)
		++stretch; // Retain both grain streams through unity and release them on pause/clear.
	if (!dry_.render(reinterpret_cast<int32_t*>(audio), dry.size(), 2, stretch, syncedStretch_)
	    || !reverb_.render(send, dry.size(), 1, stretch, syncedStretch_)) {
		fail();
		return false;
	}
	if (overlap
	    && (!dry_.render(reinterpret_cast<int32_t*>(tailAudio), overlap, 2, kMaxSampleValue, false, true)
	        || !reverb_.render(tailSend, overlap, 1, kMaxSampleValue, false, true))) {
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
	for (size_t i = 0; i < dry.size(); ++i) {
		audio[i].l *= 2;
		audio[i].r *= 2;
		send[i] *= 2;
	}
	for (int32_t i = 0; i < overlap; ++i) {
		tailAudio[i].l *= 2;
		tailAudio[i].r *= 2;
		tailSend[i] *= 2;
	}
	loopFade_.apply({audio, dry.size()}, {send, dry.size()}, {tailAudio, size_t(overlap)}, {tailSend, size_t(overlap)});
	float power = 0;
	for (size_t i = 0; i < dry.size(); ++i) {
		power += float(audio[i].l) * audio[i].l + float(audio[i].r) * audio[i].r;
		dry[i] += audio[i];
		reverb[i] += send[i];
	}
	playbackPower_ += float(dry.size()) / (dry.size() + 2205) * (power / dry.size() - playbackPower_);
	if (!loopFade_.remaining) {
		dry_.stopOutgoing();
		reverb_.stopOutgoing();
	}
	if (waitingForNativeStart_)
		nativeWaitSamples_ += dry.size();
	if (recorder && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		recorder->feedAudio({audio, dry.size()}, true, 2); // Match Sound::render's output recording gain.
	}
	return true;
}

void ClipFreeze::Fade::apply(std::span<StereoSample> incoming, std::span<int32_t> send,
                             std::span<const StereoSample> outgoing, std::span<const int32_t> outgoingSend) {
	int32_t count = std::min<int32_t>(remaining, incoming.size());
	if (!count)
		return;
	float oldIncoming = incomingPower, oldOutgoing = outgoingPower, oldCross = crossPower;
	float in = 0, out = 0, cross = 0;
	for (int32_t i = 0; i < count; ++i) {
		float il = incoming[i].l, ir = incoming[i].r;
		float ol = outgoing[i].l, oright = outgoing[i].r;
		in += il * il + ir * ir;
		out += ol * ol + oright * oright;
		cross += il * ol + ir * oright;
	}
	if (remaining == length) {
		oldIncoming = incomingPower = in / count;
		oldOutgoing = outgoingPower = out / count;
		oldCross = crossPower = cross / count;
	}
	else {
		float alpha = float(count) / (count + 256);
		incomingPower += alpha * (in / count - incomingPower);
		outgoingPower += alpha * (out / count - outgoingPower);
		crossPower += alpha * (cross / count - crossPower);
	}
	for (int32_t i = 0; i < count; ++i) {
		// Smoothstep endpoints and bounded correlation compensation. Interpolate
		// power estimates within the block so updates cannot introduce gain steps.
		int64_t x = int64_t{length - remaining} * 65536 / length;
		int64_t weight = ((x * x >> 16) * (3 * 65536 - 2 * x)) >> 16;
		float w = float(weight) / 65536;
		float progress = float(i + 1) / count;
		float inPower = oldIncoming + progress * (incomingPower - oldIncoming);
		float outPower = oldOutgoing + progress * (outgoingPower - oldOutgoing);
		float correlation = oldCross + progress * (crossPower - oldCross);
		float target = (1 - w) * outPower + w * inPower;
		float mixed = (1 - w) * (1 - w) * outPower + w * w * inPower + 2 * w * (1 - w) * correlation;
		float gain = target > 1 ? std::sqrt(std::clamp(target / std::max(mixed, target / 16), 1.0f, 16.0f)) : 1;
		auto blend = [&](int32_t a, int32_t b) {
			int64_t value = (int64_t{a} * weight + int64_t{b} * (65536 - weight)) >> 16;
			return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(value * gain), INT32_MIN, INT32_MAX));
		};
		incoming[i].l = blend(incoming[i].l, outgoing[i].l);
		incoming[i].r = blend(incoming[i].r, outgoing[i].r);
		send[i] = blend(send[i], outgoingSend[i]);
		--remaining;
	}
}

void ClipFreeze::beginThaw(bool alignToPattern) {
	if (state_ != State::PLAYING) {
		clear();
		return;
	}
	entering_ = false;
	if (capturingRelease_) {
		capturingRelease_ = false;
		releaseDry_.clear();
		releaseReverb_.clear();
	}
	thawing_ = true;
	waitingForNativeStart_ = alignToPattern;
	nativeWaitSamples_ = 0;
	resetHandoff();
}

void ClipFreeze::resetHandoff() {
	warmupSamples_ = 0;
	warmLivePower_ = warmCachePower_ = 0;
	matchedSamples_ = 0;
	thawMatched_ = false;
	for (auto& stream : handoffLowPass_)
		for (auto& channel : stream)
			std::fill_n(channel, 3, 0);
	for (auto& stream : handoffBandPower_)
		std::fill_n(stream, 4, 0);
	handoffFade_.reset(0);
}

void ClipFreeze::cancelThaw() {
	if (!thawing_)
		return;
	thawing_ = waitingForNativeStart_ = false;
	handoffFade_.reset(0);
}

void ClipFreeze::mixHandoff(std::span<StereoSample> dry, std::span<int32_t> reverb) {
	static StereoSample cached[SSI_TX_BUFFER_NUM_SAMPLES];
	static int32_t cachedSend[SSI_TX_BUFFER_NUM_SAMPLES];
	std::fill_n(cached, dry.size(), StereoSample{0, 0});
	std::fill_n(cachedSend, dry.size(), 0);
	if (!render({cached, dry.size()}, {cachedSend, dry.size()}))
		return;
	if (thawing_ || entering_) {
		if (!handoffFade_.length) {
			float live = 0, cache = 0;
			for (size_t i = 0; i < dry.size(); ++i) {
				float l = dry[i].l, r = dry[i].r, cl = cached[i].l, cr = cached[i].r;
				live += l * l + r * r;
				cache += cl * cl + cr * cr;
			}
			float alpha = float(dry.size()) / (dry.size() + 882);
			warmLivePower_ += alpha * (live / dry.size() - warmLivePower_);
			warmCachePower_ += alpha * (cache / dry.size() - warmCachePower_);
			warmupSamples_ += dry.size();
			// Match spectral balance too: a loud restarted modulation envelope can
			// have a completely different timbre. Four broad stereo bands, with
			// 50 ms averaging, must track the cached sound for at least 250 ms.
			constexpr float coefficients[3] = {0.028093f, 0.107735f, 0.366131f};
			float powers[2][4]{};
			for (size_t i = 0; i < dry.size(); ++i) {
				for (int stream = 0; stream < 2; ++stream) {
					const auto& frame = stream ? cached[i] : dry[i];
					for (int channel = 0; channel < 2; ++channel) {
						float value = channel ? frame.r : frame.l;
						auto& lows = handoffLowPass_[stream][channel];
						for (int band = 0; band < 3; ++band)
							lows[band] += coefficients[band] * (value - lows[band]);
						float bands[4] = {lows[0], lows[1] - lows[0], lows[2] - lows[1], value - lows[2]};
						for (int band = 0; band < 4; ++band)
							powers[stream][band] += bands[band] * bands[band];
					}
				}
			}
			float spectralAlpha = float(dry.size()) / (dry.size() + 2205);
			float totals[2]{};
			for (int stream = 0; stream < 2; ++stream) {
				for (int band = 0; band < 4; ++band) {
					auto& power = handoffBandPower_[stream][band];
					power += spectralAlpha * (powers[stream][band] / dry.size() - power);
					totals[stream] += power;
				}
			}
			bool matches = warmLivePower_ >= warmCachePower_ * 0.81f && warmLivePower_ <= warmCachePower_ * 1.235f;
			for (int band = 0; band < 4 && matches; ++band) {
				float target = handoffBandPower_[1][band] / std::max(1.0f, totals[1]);
				float actual = handoffBandPower_[0][band] / std::max(1.0f, totals[0]);
				if (handoffBandPower_[1][band] > totals[1] * 0.0001f) {
					matches = actual >= target * 0.63f && actual <= target * 1.585f;
				}
			}
			matchedSamples_ = matches ? matchedSamples_ + dry.size() : 0;
			bool matched = warmupSamples_ >= 11025 && matchedSamples_ >= 11025;
			// Entry already has a running native renderer. A long search for an
			// arbitrary modulation match postpones the CPU saving without ensuring
			// a better match. Keep the established slow fade after a short search.
			// Thaw must match both level and spectrum before handing off early.
			// Recovered power alone can still expose a restarted modulation envelope;
			// retain the original rebuild timeout when the timbre does not match.
			bool fallback = (!thawing_ && warmupSamples_ >= 44100) || warmupSamples_ >= 44100 * 30;
			if (matched || fallback) {
				thawMatched_ = matched;
				handoffFade_.reset(thawMatched_ ? 44100 : 132300);
			}
			else {
				if (thawing_) {
					std::copy_n(cached, dry.size(), dry.begin());
					std::copy_n(cachedSend, dry.size(), reverb.begin());
				}
				return;
			}
		}
	}
	if (thawing_) {
		handoffFade_.apply(dry, reverb, {cached, dry.size()}, {cachedSend, dry.size()});
		if (!handoffFade_.remaining)
			clear();
	}
	else {
		handoffFade_.apply({cached, dry.size()}, {cachedSend, dry.size()}, dry, reverb);
		if (!handoffFade_.remaining)
			entering_ = false;
		std::copy_n(cached, dry.size(), dry.begin());
		std::copy_n(cachedSend, dry.size(), reverb.begin());
	}
}

bool ClipFreeze::beginReleaseCapture() {
	// Render an ordinary note-off once, while the listener hears the cache.
	// These temporary recorders and native voices are not part of frozen playback.
	releaseDry_.clear();
	releaseReverb_.clear();
	releaseSamples_ = releaseQuietSamples_ = 0;
	releaseHeadPower_ = 0;
	for (auto* stream : {&releaseDry_, &releaseReverb_}) {
		stream->recorder = AudioEngine::getNewRecorder(
		    stream == &releaseDry_ ? 2 : 1, AudioRecordingFolder::CLIPS, AudioInputChannel::OFFLINE_OUTPUT, true, false,
		    0, false, nullptr, {.neverUseThreshold = true, .forceStereoRendering = false});
		if (!stream->recorder) {
			releaseDry_.clear();
			releaseReverb_.clear();
			return false;
		}
		stream->recorder->numSamplesToRunBeforeBeginningCapturing = 0;
		stream->recorder->numSamplesExtraToCaptureAtEndSyncingWise = 0;
		stream->recorder->allowFileAlterationAfter = false;
	}
	capturingRelease_ = true;
	return true;
}

void ClipFreeze::captureRelease(std::span<StereoSample> dry, std::span<int32_t> reverb) {
	if (!capturingRelease_)
		return;
	for (auto* stream : {&releaseDry_, &releaseReverb_}) {
		if (stream->recorder->hadCardError || stream->recorder->reachedMaxFileSize
		    || stream->recorder->status >= RecorderStatus::ABORTED) {
			capturingRelease_ = false;
			releaseDry_.clear();
			releaseReverb_.clear();
			return;
		}
	}
	float power = 0, sendPower = 0;
	static StereoSample send[SSI_TX_BUFFER_NUM_SAMPLES];
	for (size_t i = 0; i < dry.size(); ++i) {
		power += float(dry[i].l) * dry[i].l + float(dry[i].r) * dry[i].r;
		send[i] = {reverb[i], reverb[i]};
		sendPower += float(reverb[i]) * reverb[i];
	}
	power /= dry.size();
	sendPower /= dry.size();
	if (releaseSamples_ < 441)
		releaseHeadPower_ = (releaseHeadPower_ * releaseSamples_ + power * dry.size()) / (releaseSamples_ + dry.size());
	releaseDry_.recorder->feedAudio(dry);
	releaseReverb_.recorder->feedAudio({send, dry.size()});
	releaseSamples_ += dry.size();
	// Keep long delay gaps; bound infinite releases/feedback to thirty seconds.
	releaseQuietSamples_ =
	    power < std::max(1.0f, releaseHeadPower_ * 0.000001f) ? releaseQuietSamples_ + dry.size() : 0;
	if (releaseQuietSamples_ >= 44100 * 2 || releaseSamples_ >= 44100 * 30) {
		releaseDry_.recorder->endSyncedRecording(0);
		releaseReverb_.recorder->endSyncedRecording(0);
		capturingRelease_ = false;
	}
}

void ClipFreeze::finishRelease() {
	releaseDry_.stop();
	releaseReverb_.stop();
	releasing_ = false;
	if (clearAfterRelease_) {
		clear();
		return;
	}
	// An unsynchronised loop can retain its cache for the next launch. Song-
	// relative modulation still follows the existing invalidation rule.
	pause();
}

bool ClipFreeze::renderRelease(std::span<StereoSample> dry, std::span<int32_t> reverb, SampleRecorder* recorder) {
	static StereoSample audio[SSI_TX_BUFFER_NUM_SAMPLES], continuation[SSI_TX_BUFFER_NUM_SAMPLES];
	static int32_t send[SSI_TX_BUFFER_NUM_SAMPLES], continuationSend[SSI_TX_BUFFER_NUM_SAMPLES];
	std::fill_n(audio, dry.size(), StereoSample{0, 0});
	std::fill_n(send, dry.size(), 0);
	std::fill_n(continuation, dry.size(), StereoSample{0, 0});
	std::fill_n(continuationSend, dry.size(), 0);
	bool haveTail = releaseDry_.voice && releaseReverb_.voice;
	int32_t count = haveTail ? std::min<int32_t>(dry.size(), releaseSamples_ - releasePosition_) : 0;
	if (count > 0) {
		bool ok = releaseDry_.render(reinterpret_cast<int32_t*>(audio), count, 2, kMaxSampleValue, false)
		          && releaseReverb_.render(send, count, 1, kMaxSampleValue, false);
		if (!ok) {
			releaseDry_.stop();
			releaseReverb_.stop();
			haveTail = false;
			std::fill_n(audio, dry.size(), StereoSample{0, 0});
			std::fill_n(send, dry.size(), 0);
		}
		// Convert VoiceSample's half-gain contract and mono packing exactly as
		// for the loop, with a short fade at a bounded/truncated release end.
		if (!AudioEngine::renderInStereo) {
			auto* mono = reinterpret_cast<int32_t*>(audio);
			for (int i = count - 1; i >= 0; --i)
				audio[i] = {mono[i], mono[i]};
		}
		for (int32_t i = 0; i < count; ++i) {
			float gain = 2 * releaseGain_ * std::min(1.0f, float(releaseSamples_ - releasePosition_ - i) / 2205);
			auto scale = [gain](int32_t value) {
				return static_cast<int32_t>(std::clamp(double(value) * gain, double(INT32_MIN), double(INT32_MAX)));
			};
			audio[i] = {scale(audio[i].l), scale(audio[i].r)};
			send[i] = scale(send[i]);
		}
	}
	if (releaseFade_.remaining) {
		// No sequencer ticks arrive after a stop. Keep the short bridge inside
		// available source data, wrapping only if the stop fell at the file end.
		if (std::min(dry_.remainingSamples(), reverb_.remainingSamples()) < int32_t(dry.size()) + tailGuardSamples) {
			loopFade_.reset(0);
			dry_.start(0, period_, loopSamples_, false);
			reverb_.start(0, period_, loopSamples_, false);
			dry_.guide.sequenceSyncLengthTicks = reverb_.guide.sequenceSyncLengthTicks = 0;
		}
		if (!renderLoop({continuation, dry.size()}, {continuationSend, dry.size()}, nullptr)) {
			return false;
		}
		// A missing/incomplete release recording still gets a bounded fade-out.
		releaseFade_.apply({audio, dry.size()}, {send, dry.size()}, {continuation, dry.size()},
		                   {continuationSend, dry.size()});
		if (!releaseFade_.remaining) {
			dry_.stop();
			reverb_.stop();
		}
	}
	for (size_t i = 0; i < dry.size(); ++i) {
		dry[i] += audio[i];
		reverb[i] += send[i];
	}
	if (recorder && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING)
		recorder->feedAudio({audio, dry.size()}, true, 2);
	releasePosition_ += dry.size();
	if (!releaseFade_.remaining && (!haveTail || releasePosition_ >= releaseSamples_))
		finishRelease();
	return true;
}
