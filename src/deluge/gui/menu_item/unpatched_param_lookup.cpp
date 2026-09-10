#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"

namespace params = deluge::modulation::params;
using params::kNoParamID;

namespace deluge::gui::menu_item {
ModelStackWithAutoParam* UnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = sound_editor_for_session().getCurrentModelStack(memory);
	return modelStack->getUnpatchedAutoParamFromId(getP());
}
} // namespace deluge::gui::menu_item
