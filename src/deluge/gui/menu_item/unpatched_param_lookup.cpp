#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"

namespace params = deluge::modulation::params;
using params::kNoParamID;

namespace deluge::gui::menu_item {
ModelStackWithAutoParam* UnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = sound_editor_for_session().getCurrentModelStack(memory);
	return modelStack ? modelStack->getUnpatchedAutoParamFromId(getP()) : nullptr;
}
deluge::modulation::params::Kind UnpatchedParam::getParamKind() {
	char model_stack_memory[MODEL_STACK_MAX_SIZE];
	auto* model_stack = getModelStack(model_stack_memory);
	if (!model_stack || !model_stack->autoParam || !model_stack->paramCollection) {
		return params::Kind::NONE;
	}
	return model_stack->paramCollection->getParamKind();
}
} // namespace deluge::gui::menu_item
