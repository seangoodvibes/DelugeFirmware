#include "utils.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

SideChain* getSidechain(bool forReverb) {
	if (forReverb) {
		return &AudioEngine::reverbSidechain;
	}
	return &sound_editor_for_session().currentModControllable->sidechain;
}