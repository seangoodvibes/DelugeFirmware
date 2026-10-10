#include "gui/ui/ui_session.h"
#include "io/midi/midi_follow.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "util/lifetime.h"

namespace params = deluge::modulation::params;

constexpr int32_t PARAM_ID_NONE = 255;

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParam(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                   int32_t soundParamId, int32_t globalParamId, bool displayError) {
	auto* const source_song = modelStackWithTimelineCounter ? modelStackWithTimelineCounter->song : nullptr;
	auto song_watch = source_song ? source_song->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (source_song && !song_watch.alive())
		return nullptr;
	auto clip_watch = clip ? clip->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (clip && !clip_watch.alive())
		return nullptr;
	auto* const source_output = clip ? clip->output : nullptr;
	auto output_watch = source_output ? source_output->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	const auto source_owner = deluge::gui::ui_session::current();
	const auto context_matches = [&] {
		return (!source_song || song_watch.alive())
		       && (!modelStackWithTimelineCounter || modelStackWithTimelineCounter->song == source_song)
		       && (!clip || (clip_watch.alive() && clip->output == source_output))
		       && (!source_output || output_watch.alive()) && deluge::gui::ui_session::current() == source_owner;
	};
	if (!context_matches())
		return nullptr;
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	// non-null clip means you're dealing with the clip context
	if (clip) {
		if (modelStackWithTimelineCounter) {
			modelStackWithParam =
			    getModelStackWithParamForClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		}
	}

	if (!context_matches())
		return nullptr;
	if (displayError && (!modelStackWithParam || !modelStackWithParam->autoParam)) {
		displayParamControlError(soundParamId, globalParamId);
	}

	return context_matches() ? modelStackWithParam : nullptr;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                          int32_t soundParamId, int32_t globalParamId) {
	if (!modelStackWithTimelineCounter || !clip || !clip->output)
		return nullptr;

	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	OutputType outputType = clip->output->type;

	switch (outputType) {
	case OutputType::SYNTH:
		modelStackWithParam =
		    getModelStackWithParamForSynthClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		break;
	case OutputType::KIT:
		modelStackWithParam =
		    getModelStackWithParamForKitClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		break;
	case OutputType::AUDIO:
		modelStackWithParam =
		    getModelStackWithParamForAudioClip(modelStackWithTimelineCounter, clip, soundParamId, globalParamId);
		break;
	// explicit fallthrough cases
	case OutputType::CV:
	case OutputType::MIDI_OUT:
	case OutputType::NONE:;
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = PARAM_ID_NONE;

	if (soundParamId != PARAM_ID_NONE && soundParamId < params::UNPATCHED_START) {
		paramKind = params::Kind::PATCHED;
		paramID = soundParamId;
	}
	else if (soundParamId != PARAM_ID_NONE && soundParamId >= params::UNPATCHED_START) {
		paramKind = params::Kind::UNPATCHED_SOUND;
		paramID = soundParamId - params::UNPATCHED_START;
	}
	if ((paramKind != params::Kind::NONE) && (paramID != PARAM_ID_NONE)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind, true, false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                             int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::NONE;
	int32_t paramID = PARAM_ID_NONE;
	InstrumentClip* instrumentClip = (InstrumentClip*)clip;

	if (!instrumentClip->affect_entire_for_session()) {
		if (soundParamId != PARAM_ID_NONE && soundParamId < params::UNPATCHED_START) {
			paramKind = params::Kind::PATCHED;
			paramID = soundParamId;
		}
		else if (soundParamId != PARAM_ID_NONE && soundParamId >= params::UNPATCHED_START) {
			// don't allow control of Portamento in Kit's
			if (soundParamId - params::UNPATCHED_START != params::UNPATCHED_PORTAMENTO) {
				paramKind = params::Kind::UNPATCHED_SOUND;
				paramID = soundParamId - params::UNPATCHED_START;
			}
		}
	}
	else {
		if (globalParamId != PARAM_ID_NONE) {
			paramKind = params::Kind::UNPATCHED_GLOBAL;
			paramID = globalParamId;
		}
	}

	if ((paramKind != params::Kind::NONE) && (paramID != PARAM_ID_NONE)) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind,
		                                         instrumentClip->affect_entire_for_session(), false);
	}

	return modelStackWithParam;
}

ModelStackWithAutoParam*
MidiFollow::getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
                                               int32_t soundParamId, int32_t globalParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	params::Kind paramKind = params::Kind::UNPATCHED_GLOBAL;
	int32_t paramID = globalParamId;

	if (paramID != PARAM_ID_NONE) {
		// Note: useMenuContext parameter will always be false for MidiFollow
		modelStackWithParam =
		    clip->output->getModelStackWithParam(modelStackWithTimelineCounter, clip, paramID, paramKind, true, false);
	}

	return modelStackWithParam;
}
