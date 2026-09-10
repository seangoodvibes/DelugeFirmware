#include "gui/menu_item/patch_cable_strength.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"

namespace deluge::gui::menu_item {
ModelStackWithAutoParam* PatchCableStrength::getModelStackWithParam(void* memory) {
	return getModelStack(memory);
}

// May return nullptr or a model stack with no autoParam; callers must check both.
ModelStackWithAutoParam* PatchCableStrength::getModelStack(void* memory, bool allowCreation) {
	ModelStackWithThreeMainThings* modelStack = sound_editor_for_session().getCurrentModelStack(memory);
	// Reject missing model stacks, missing managers, and non-SOUND layouts before accessing the patch-cable collection.
	if (!modelStack || !modelStack->paramManager || !sound_editor_for_session().currentModControllable
	    || !modelStack->paramManager->matches_type(ParamManagerType::SOUND)
	    || !modelStack->paramManager->matches_type(
	        sound_editor_for_session().currentModControllable->required_param_manager_type())) {
		return nullptr;
	}
	ParamCollectionSummary* paramSetSummary = modelStack->paramManager->getPatchCableSetSummary();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addParamCollectionSummary(paramSetSummary);
	ModelStackWithParamId* ModelStackWithParamId = modelStackWithParamCollection->addParamId(getLearningThing().data);
	ModelStackWithAutoParam* modelStackMaybeWithAutoParam =
	    paramSetSummary->paramCollection->getAutoParamFromId(ModelStackWithParamId, allowCreation);

	if (allowCreation && modelStackMaybeWithAutoParam && modelStackMaybeWithAutoParam->autoParam
	    && !cable_exists_for_session() && !isInHorizontalMenu()) {
		// If we created a patch cable then set the polarity to match the menus
		setPatchCablePolarity(polarity_for_session());
		cable_exists_for_session() = true;
	}
	return modelStackMaybeWithAutoParam;
}
} // namespace deluge::gui::menu_item
