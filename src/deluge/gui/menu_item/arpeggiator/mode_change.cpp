#include "gui/menu_item/arpeggiator/mode_change.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui_session.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "util/lifetime.h"

namespace deluge::gui::menu_item::arpeggiator {
void apply_mode_change(ArpMode mode, ArpPreset preset, bool use_preset) {
	auto& editor = sound_editor_for_session();
	auto* clip = getCurrentClip();
	if (!clip)
		return;
	auto clip_lifetime = clip->watch_lifetime();
	if (!clip_lifetime.alive() || !clip->output)
		return;
	auto* output = clip->output;
	auto output_lifetime = output->watch_lifetime();
	if (!output_lifetime.alive())
		return;
	auto* settings = editor.currentArpSettings;
	if (!settings)
		return;
	auto* sound = editor.currentSound;
	auto* manager = editor.currentParamManager;
	const bool affect_entire = editor.editingKitAffectEntire();
	auto* kit = editor.editingKit() ? static_cast<Kit*>(output) : nullptr;
	auto* drum = kit ? kit->selected_drum_for_session() : nullptr;
	auto drum_lifetime = drum ? drum->watch_lifetime() : deluge::lifetime::lifetime_watch{};
	if (drum && !drum_lifetime.alive())
		return;
	const auto source_owner = deluge::gui::ui_session::current();
	const auto source_mode = currentUIMode;
	auto* source_song = currentSong;
	const auto previous_mode = settings->mode;
	const auto previous_preset = settings->preset;
	const auto valid = [&] {
		if (!clip_lifetime.alive() || !output_lifetime.alive() || (drum && !drum_lifetime.alive())
		    || currentSong != source_song || deluge::gui::ui_session::current() != source_owner
		    || currentUIMode != source_mode || getCurrentClip() != clip || clip->output != output)
			return false;
		if (kit && (kit->selected_drum_for_session() != drum || (drum && kit->getDrumIndex(drum) < 0)))
			return false;
		return editor.currentArpSettings == settings && editor.currentSound == sound
		       && editor.currentParamManager == manager && editor.editingKitAffectEntire() == affect_entire
		       && settings->mode == previous_mode && settings->preset == previous_preset;
	};
	const deluge::lifetime::callback_validation validation{valid};
	if (!valid())
		return;
	const bool whole_kit = source_mode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && editor.editingKitRow();
	const bool turning_off = use_preset ? preset == ArpPreset::OFF : mode == ArpMode::OFF;
	if ((previous_mode == ArpMode::OFF || turning_off) && clip->isActiveOnOutput()
	    && !editor.editingKitAffectEntire()) {
		if (whole_kit) {
			kit->cutAllSound();
		}
		else {
			char model_stack_memory[MODEL_STACK_MAX_SIZE];
			auto* model_stack = editor.getCurrentModelStack(model_stack_memory);
			if (!valid() || !model_stack)
				return;
			if (kit) {
				if (drum)
					drum->killAllVoices();
			}
			else if (editor.editingCVOrMIDIClip()) {
				getCurrentInstrumentClip()->stopAllNotesForMIDIOrCV(model_stack->toWithTimelineCounter());
			}
			else {
				if (!sound || !sound->allNotesOff(model_stack, sound->getArp(), &validation))
					return;
				if (!valid())
					return;
				sound->reassessRenderSkippingStatus(model_stack->addSoundFlags());
			}
		}
		if (!valid())
			return;
	}
	const auto apply = [&](ArpeggiatorSettings* target) {
		if (use_preset) {
			target->preset = preset;
			target->updateSettingsFromCurrentPreset();
			target->flagForceArpRestart = true;
		}
		else {
			target->mode = mode;
			target->updatePresetFromCurrentSettings();
		}
	};
	if (whole_kit) {
		for (auto* current_drum = kit->firstDrum; current_drum; current_drum = current_drum->next) {
			apply(&current_drum->arpSettings);
		}
	}
	else
		apply(settings);
}
} // namespace deluge::gui::menu_item::arpeggiator
