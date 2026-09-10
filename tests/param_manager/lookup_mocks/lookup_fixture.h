#pragma once
#include "gui/ui/ui_session.h"
#include "model/model_stack.h"
#include "modulation/params/param_set.h"
namespace params = deluge::modulation::params;
class ModControllable {
public:
	virtual ParamManagerType required_param_manager_type() const = 0;
};
class TimelineCounter {};
class Clip;
class Output : public ModControllable {
public:
	virtual ~Output() = default;
	OutputType type = OutputType::NONE;
	ModControllable* toModControllable() { return this; }
	virtual ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int32_t,
	                                                        params::Kind, bool, bool) {
		return nullptr;
	}
};
class Clip : public TimelineCounter {
public:
	Output* output = nullptr;
	ParamManagerForTimeline paramManager;
	int32_t& last_selected_param_id_for_session() {
		return deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local ? lastSelectedParamID
		                                                                                : remote_param_id;
	}
	params::Kind& last_selected_param_kind_for_session() {
		return deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local ? lastSelectedParamKind
		                                                                                : remote_param_kind;
	}
	int32_t remote_param_id = params::kNoParamID;
	params::Kind remote_param_kind = params::Kind::NONE;
	int32_t lastSelectedParamID = params::kNoParamID;
	params::Kind lastSelectedParamKind = params::Kind::NONE;
};
class NoteRow {
public:
	ParamManagerForTimeline paramManager;
};
class InstrumentClip : public Clip {
public:
	bool& affect_entire_for_session() { return affectEntire; }
	bool affectEntire = false;
	NoteRow* row = nullptr;
	ModelStackWithNoteRow* getNoteRowForSelectedDrum(ModelStackWithTimelineCounter* stack) {
		return stack->addNoteRow(7, row);
	}
};
class Drum : public ModControllable {
public:
	DrumType type = DrumType::SOUND;
	ModControllable* toModControllable() { return this; }
	ParamManagerType required_param_manager_type() const override {
		return type == DrumType::SOUND ? ParamManagerType::SOUND : ParamManagerType::NONE;
	}
};
class MelodicInstrument : public Output {
public:
	ParamManagerType required_param_manager_type() const override {
		return type == OutputType::CV ? ParamManagerType::CV : ParamManagerType::SOUND;
	}
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int32_t, params::Kind, bool,
	                                                bool) override;
};
class AudioOutput : public Output {
public:
	ParamManagerType required_param_manager_type() const override { return ParamManagerType::GLOBAL; }
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int32_t, params::Kind, bool,
	                                                bool) override;
};
class MIDIInstrument : public Output {
public:
	ParamManagerType required_param_manager_type() const override { return ParamManagerType::MIDI; }
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int32_t, params::Kind, bool,
	                                                bool) override;
	ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int32_t, ModelStackWithThreeMainThings*);
};
class Kit : public Output {
public:
	ParamManagerType required_param_manager_type() const override { return ParamManagerType::GLOBAL; }
	Drum*& selected_drum_for_session() { return selectedDrum; }
	Drum* selectedDrum = nullptr;
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int32_t, params::Kind, bool,
	                                                bool) override;
	ModelStackWithAutoParam* getModelStackWithParamForKit(ModelStackWithTimelineCounter*, Clip*, int32_t, params::Kind,
	                                                      bool);
	ModelStackWithAutoParam* getModelStackWithParamForKitRow(ModelStackWithTimelineCounter*, Clip*, int32_t,
	                                                         params::Kind, bool);
};
class SoundEditor {
public:
	ModControllable* currentModControllable = nullptr;
	ParamManager* currentParamManager = nullptr;
	bool settings = false;
	bool inSettingsMenu() { return settings; }
	ModelStackWithThreeMainThings* getCurrentModelStack(void* memory) {
		return setupModelStackWithTimelineCounter(memory, nullptr, nullptr)
		    ->addOtherTwoThingsButNoNoteRow(currentModControllable, currentParamManager);
	}
};
extern SoundEditor soundEditor;
inline SoundEditor remote_sound_editor;
inline SoundEditor& sound_editor_for_session() {
	return deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local ? soundEditor : remote_sound_editor;
}
extern void* currentUI;
inline void* getCurrentUI() {
	return currentUI;
}
class AutomationView {
public:
	bool affectEntire = false;
	bool getAffectEntire() { return affectEntire; }
	ModelStackWithAutoParam* getModelStackWithParamForClip(ModelStackWithTimelineCounter*, Clip*, int32_t,
	                                                       params::Kind);
};
class Song {
public:
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithThreeMainThings*, int32_t);
};
namespace deluge::gui::menu_item {
class PatchCableStrength {
public:
	struct Descriptor {
		int32_t data;
	} descriptor{2};
	bool patch_cable_exists_ = false;
	bool horizontal = false;
	int polarity_in_the_ui_ = 1;
	int polarityChanges = 0;
	Descriptor getLearningThing() { return descriptor; }
	bool isInHorizontalMenu() { return horizontal; }
	void setPatchCablePolarity(int) { ++polarityChanges; }
	ModelStackWithAutoParam* getModelStackWithParam(void*);
	ModelStackWithAutoParam* getModelStack(void*, bool = false);
};
class PatchedParam {
public:
	int32_t id = 0;
	int32_t getP() { return id; }
	ModelStackWithAutoParam* getModelStack(void*);
};
class UnpatchedParam {
public:
	int32_t id = 0;
	int32_t getP() { return id; }
	ModelStackWithAutoParam* getModelStack(void*);
};
} // namespace deluge::gui::menu_item
class MidiFollow {
public:
	int errors = 0;
	void displayParamControlError(int32_t, int32_t) { ++errors; }
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int32_t, int32_t, bool);
	ModelStackWithAutoParam* getModelStackWithParamForClip(ModelStackWithTimelineCounter*, Clip*, int32_t, int32_t);
	ModelStackWithAutoParam* getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter*, Clip*, int32_t,
	                                                            int32_t);
	ModelStackWithAutoParam* getModelStackWithParamForKitClip(ModelStackWithTimelineCounter*, Clip*, int32_t, int32_t);
	ModelStackWithAutoParam* getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter*, Clip*, int32_t,
	                                                            int32_t);
};