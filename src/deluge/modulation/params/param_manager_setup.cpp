#include "memory/general_memory_allocator.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "util/lifetime.h"
#include <new>

Error ParamManager::setupMIDI() {
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIParamCollection));
	if (!memory) {
		return Error::INSUFFICIENT_RAM;
	}

	destructMainParamCollections();
	summaries[1] = summaries[0]; // Preserve expression while installing the MIDI collection.
	summaries[0].paramCollection = new (memory) MIDIParamCollection(&summaries[0]);
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
	expressionParamSetOffset = 1;
	return Error::NONE;
}

Error ParamManager::setupUnpatched() {
	void* memoryUnpatched = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(UnpatchedParamSet));
	if (!memoryUnpatched) {
		return Error::INSUFFICIENT_RAM;
	}

	destructMainParamCollections();
	// Expression belongs to the clip and survives instrument-type changes.
	summaries[1] = summaries[0];
	auto* unpatched = new (memoryUnpatched) UnpatchedParamSet(&summaries[0]);
	summaries[0].paramCollection = unpatched;
	unpatched->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
	expressionParamSetOffset = 1;
	return Error::NONE;
}

Error ParamManager::setupWithPatching() {
	void* memoryUnpatched = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(UnpatchedParamSet));
	if (!memoryUnpatched) {
		return Error::INSUFFICIENT_RAM;
	}

	void* memoryPatched = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(PatchedParamSet));
	if (!memoryPatched) {
ramError2:
		delugeDealloc(memoryUnpatched);
		return Error::INSUFFICIENT_RAM;
	}

	void* memoryPatchCables = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(PatchCableSet));
	if (!memoryPatchCables) {
		delugeDealloc(memoryPatched);
		goto ramError2;
	}

	destructMainParamCollections();
	summaries[3] = summaries[0];
	auto* unpatched = new (memoryUnpatched) UnpatchedParamSet(&summaries[0]);
	summaries[0].paramCollection = unpatched;
	unpatched->kind = deluge::modulation::params::Kind::UNPATCHED_SOUND;
	summaries[1].paramCollection = new (memoryPatched) PatchedParamSet(&summaries[1]);
	summaries[2].paramCollection = new (memoryPatchCables) PatchCableSet(&summaries[2]);
	summaries[4] = {0};
	expressionParamSetOffset = 3;
	return Error::NONE;
}

// Returns whether there is one / one could be created.
bool ParamManager::ensureExpressionParamSetExists(bool forDrum,
                                                  const deluge::lifetime::callback_validation* owner_validation) {
	if (owner_validation && !owner_validation->valid())
		return false;
	int32_t offset = getExpressionParamSetOffset();
	getExpressionParamSetSummary(); // Validate the offset and optional collection before indexing or allocating.
	if (!summaries[offset].paramCollection) {

		ParamCollection* original_collections[PARAM_COLLECTIONS_STORAGE_NUM];
		for (int32_t i = 0; i < PARAM_COLLECTIONS_STORAGE_NUM; ++i)
			original_collections[i] = summaries[i].paramCollection;
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(ExpressionParamSet));
		if (!memory) {
			return false;
		}

		// Validate before touching this manager: its row or clip may have been removed.
		if (owner_validation && !owner_validation->valid()) {
			delugeDealloc(memory);
			return false;
		}
		bool layout_matches = getExpressionParamSetOffset() == offset;
		for (int32_t i = 0; layout_matches && i < PARAM_COLLECTIONS_STORAGE_NUM; ++i)
			layout_matches = summaries[i].paramCollection == original_collections[i];
		if (!layout_matches) {
			delugeDealloc(memory);
			return false;
		}
		summaries[offset].paramCollection = new (memory) ExpressionParamSet(&summaries[offset], forDrum);
		summaries[offset + 1] = {0};
	}
	return true;
}

ExpressionParamSet*
ParamManager::getOrCreateExpressionParamSet(bool forDrum,
                                            const deluge::lifetime::callback_validation* owner_validation) {
	if (!ensureExpressionParamSetExists(forDrum, owner_validation)) {
		return nullptr;
	}

	return getExpressionParamSet();
}
