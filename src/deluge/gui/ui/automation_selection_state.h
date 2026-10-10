#pragma once
#include "definitions_cxx.hpp"
#include "modulation/params/param.h"

// Presentation selection only; automation values remain in the shared model.
struct AutomationSelectionState {
	int32_t lastSelectedParamID = deluge::modulation::params::kNoParamID;
	deluge::modulation::params::Kind lastSelectedParamKind = deluge::modulation::params::Kind::NONE;
	int32_t lastSelectedParamShortcutX = kNoSelection;
	int32_t lastSelectedParamShortcutY = kNoSelection;
	int32_t lastSelectedParamArrayPosition = 0;
	OutputType lastSelectedOutputType = OutputType::NONE;
	PatchSource lastSelectedPatchSource = PatchSource::NONE;
};
