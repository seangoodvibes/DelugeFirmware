#include "platform.h"
#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/consequence/consequence_param_change.h"
#include "model/model_stack.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace {
std::unordered_map<void*, uint32_t> allocations;
[[noreturn]] void unsupported() {
	throw std::logic_error("Unexpected platform operation in parameter lifecycle test");
}
} // namespace
namespace parameter_test {
bool allow_midi_params = false;
std::vector<midi_notification> midi_notifications;
bool allow_patch_cables = false;
size_t notifications = 0;
int allocations_before_failure = -1;
size_t allocation_failures = 0;
uint32_t last_failed_allocation_size = 0;
bool allow_no_action = false;
int32_t loop_length = 32;
int32_t play_pos = 0;
bool reversed = false;
bool allow_recording_controls = false;
int indicator_calls = 0;
uint8_t indicator_knob = 0;
uint8_t indicator_level = 0;
bool indicator_bipolar = false;
size_t outstanding_allocations() {
	return allocations.size();
}
void reset() {
	allow_midi_params = false;
	midi_notifications.clear();
	allow_patch_cables = false;
	notifications = 0;
	allow_recording_controls = false;
	indicator_calls = 0;
	allocations_before_failure = -1;
	allocation_failures = 0;
	last_failed_allocation_size = 0;
	allow_no_action = false;
	loop_length = 32;
	play_pos = 0;
	reversed = false;
	playbackHandler.playbackState = 0;
	playbackHandler.recording = RecordingMode::OFF;
	playbackHandler.ticksLeftInCountIn = 0;
}
} // namespace parameter_test
MemoryRegion::MemoryRegion() = default;
GeneralMemoryAllocator::GeneralMemoryAllocator() = default;
void* GeneralMemoryAllocator::alloc(uint32_t size, bool, bool, void*) {
	if (parameter_test::allocations_before_failure == 0) {
		++parameter_test::allocation_failures;
		parameter_test::last_failed_allocation_size = size;
		return nullptr;
	}
	if (parameter_test::allocations_before_failure > 0) {
		--parameter_test::allocations_before_failure;
	}
	void* result = std::malloc(size);
	if (!result) {
		throw std::bad_alloc();
	}
	allocations.emplace(result, size);
	return result;
}
void GeneralMemoryAllocator::dealloc(void* address) {
	if (!address) {
		return;
	}
	if (allocations.erase(address) != 1) {
		std::abort();
	}
	std::free(address);
}
uint32_t GeneralMemoryAllocator::getAllocatedSize(void* address) {
	return allocations.at(address);
}
uint32_t GeneralMemoryAllocator::shortenLeft(void*, uint32_t, uint32_t) {
	return 0;
}
uint32_t GeneralMemoryAllocator::shortenRight(void*, uint32_t) {
	return 0;
}
void GeneralMemoryAllocator::extend(void*, uint32_t, uint32_t, uint32_t* left, uint32_t* right, void*) {
	*left = *right = 0;
}
extern "C" void delugeDealloc(void* address) {
	GeneralMemoryAllocator::get().dealloc(address);
}

View::View() = default;
View view;
void View::notifyParamAutomationOccurred(ParamManager*, bool) {
	++parameter_test::notifications;
}
ActionLogger::ActionLogger() = default;
ActionLogger actionLogger;
Action* ActionLogger::getNewAction(ActionType, ActionAddition) {
	if (parameter_test::allow_no_action)
		return nullptr;
	unsupported();
}
bool Action::containsConsequenceParamChange(ParamCollection*, int32_t) {
	unsupported();
}
void Action::recordParamChangeDefinitely(ModelStackWithAutoParam const*, bool) {
	unsupported();
}
void Action::recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam const*, bool) {
	unsupported();
}
PlaybackHandler::PlaybackHandler() = default;
PlaybackHandler playbackHandler;
bool PlaybackHandler::isCurrentlyRecording() {
	if (recording != RecordingMode::OFF) {
		unsupported();
	}
	return false;
}
uint32_t PlaybackHandler::getTimePerInternalTick() {
	return 100;
}
uint32_t PlaybackHandler::getTimePerInternalTickInverse(bool) {
	return UINT32_MAX / 100;
}
namespace AudioEngine {
uint32_t audioSampleTimer = 100000;
bool bypassCulling = false;
} // namespace AudioEngine
Song* currentSong = nullptr;
bool isUIModeActive(uint32_t) {
	unsupported();
}
// Simulated timeline: no Song, Clip, NoteRow, UI, or audio interrupt is required.
int32_t ModelStackWithNoteRow::getLoopLength() const {
	return parameter_test::loop_length;
}
int32_t ModelStackWithNoteRow::getLastProcessedPos() const {
	return parameter_test::play_pos;
}
int32_t ModelStackWithNoteRow::getLivePos() const {
	return parameter_test::play_pos;
}
bool ModelStackWithNoteRow::isCurrentlyPlayingReversed() const {
	return parameter_test::reversed;
}
int32_t ModelStackWithNoteRow::getPosAtWhichPlaybackWillCut() const {
	return INT32_MAX;
}

namespace FlashStorage {
Polarity defaultPatchCablePolarity = Polarity::BIPOLAR;
uint8_t defaultBendRange[2] = {2, 48};
} // namespace FlashStorage
namespace AudioEngine {
bool mustUpdateReverbParamsBeforeNextRender = false;
}
void copyModelStack(void* destination, void const* source, int32_t size) {
	std::memcpy(destination, source, size);
}
bool Clip::isActiveOnOutput() {
	unsupported();
}
int32_t rangeFinalValues[kMaxNumPatchCables];
PatchCableAcceptance patch_cable_acceptance(ModelStackWithThreeMainThings const*, PatchSource, int32_t) {
	if (!parameter_test::allow_patch_cables)
		unsupported();
	return PatchCableAcceptance::ALLOWED;
}
void notify_patch_cable_value_change(ModelStackWithAutoParam const*, int32_t) {
	if (!parameter_test::allow_patch_cables)
		unsupported();
}
void Sound::notifyValueChangeViaLPF(int32_t, bool, ModelStackWithThreeMainThings const*, int32_t, int32_t, bool) {
	unsupported();
}

namespace {
std::unique_ptr<ConsequenceParamChange> snapshot;
}
std::unique_ptr<ConsequenceParamChange> parameter_test::take_snapshot() {
	return std::move(snapshot);
}
void ActionLogger::recordUnautomatedParamChange(ModelStackWithAutoParam const* context, ActionType) {
	if (snapshot) {
		unsupported();
	}
	snapshot = std::make_unique<ConsequenceParamChange>(context, false);
}
namespace Buttons {
bool isShiftButtonPressed() {
	if (parameter_test::allow_recording_controls)
		return false;
	unsupported();
}
} // namespace Buttons
namespace deluge::l10n {
char const* get(String) {
	unsupported();
}
} // namespace deluge::l10n
deluge::hid::Display* display = nullptr;

ModelStackWithAutoParam* ModControllable::getParamFromModEncoder(int32_t, ModelStackWithThreeMainThings*, bool) {
	unsupported();
}
ModelStackWithAutoParam* ModControllable::getParamFromMIDIKnob(MIDIKnob&, ModelStackWithThreeMainThings*) {
	unsupported();
}
uint8_t* ModControllable::getModKnobMode() {
	unsupported();
}
int32_t ModControllable::getKnobPosForNonExistentParam(int32_t, ModelStackWithAutoParam*) {
	unsupported();
}

// Native lifecycle tests run outside the automation editor.
void get_automation_interpolation(bool& before, bool& after) {
	before = false;
	after = false;
}

namespace indicator_leds {
void setKnobIndicatorLevel(uint8_t knob, uint8_t level, bool bipolar) {
	++parameter_test::indicator_calls;
	parameter_test::indicator_knob = knob;
	parameter_test::indicator_level = level;
	parameter_test::indicator_bipolar = bipolar;
}
} // namespace indicator_leds

void notify_midi_param_value_change(ModelStackWithAutoParam const* stack, int32_t old_value, int32_t new_value) {
	if (!parameter_test::allow_midi_params)
		unsupported();
	parameter_test::midi_notifications.push_back({stack->paramId, old_value, new_value});
}
