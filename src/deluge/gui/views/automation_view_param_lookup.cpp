#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/output.h"

namespace params = deluge::modulation::params;
using params::kNoParamID;

ModelStackWithAutoParam* AutomationView::getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack,
                                                                       Clip* clip, int32_t paramID,
                                                                       params::Kind paramKind) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (paramID == kNoParamID) {
		paramID = clip->last_selected_param_id_for_session();
		paramKind = clip->last_selected_param_kind_for_session();
	}

	// check if we're in the sound menu and not the settings menu
	// because in the settings menu, the menu mod controllable's aren't setup, so we don't want to use those
	bool inSoundMenu = getCurrentUI() == &sound_editor_for_session() && !sound_editor_for_session().inSettingsMenu();

	modelStackWithParam =
	    clip->output->getModelStackWithParam(modelStack, clip, paramID, paramKind, getAffectEntire(), inSoundMenu);

	return modelStackWithParam;
}
