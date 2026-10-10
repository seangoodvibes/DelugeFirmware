#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include <new>

namespace {
// Destination is an empty local or registry owner. This move cannot allocate or
// run callbacks, and must preserve malformed tails so cleanup can reclaim them.
void take_parameter_ownership(ParamManager& source, ParamManager& destination) {
	for (int32_t slot = 0; slot < PARAM_COLLECTIONS_STORAGE_NUM; ++slot) {
		destination.summaries[slot] = source.summaries[slot];
		source.summaries[slot] = {0};
	}
	destination.expressionParamSetOffset = source.expressionParamSetOffset;
	destination.resonanceBackwardsCompatibilityProcessed = source.resonanceBackwardsCompatibilityProcessed;
	source.expressionParamSetOffset = 0;
	source.resonanceBackwardsCompatibilityProcessed = false;
}
} // namespace

// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerForExactClip(ModControllableAudio* modControllable, Clip* clip,
                                                        ParamManager* stealInto) {

	if (!modControllable)
		return nullptr;
	// Lookup exposes backup storage for preflight, but transfers must target a
	// live manager outside that array: removal can move or destroy its entries.
	if (stealInto) {
		for (int32_t i = 0; i < backedUpParamManagers.getNumElements(); ++i) {
			auto* backup = static_cast<BackedUpParamManager*>(backedUpParamManagers.getElementAddress(i));
			if (stealInto == &backup->paramManager)
				return nullptr;
		}
	}

	uint32_t keyWords[2];
	keyWords[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable));
	keyWords[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip));

	int32_t iCorrectClip = backedUpParamManagers.searchMultiWordExact(keyWords);

	if (iCorrectClip == -1) {
		return nullptr;
	}

	BackedUpParamManager* elementCorrectClip =
	    (BackedUpParamManager*)backedUpParamManagers.getElementAddress(iCorrectClip);
	if (!elementCorrectClip->paramManager.matches_type(modControllable->required_param_manager_type())) {
		return nullptr;
	}

	if (stealInto) {
		// Destination cleanup may replace or delete registry entries. Claim the
		// selected source first and never access registry storage after yielding.
		ParamManager retained_parameters;
		detach_backup_at_index(iCorrectClip, retained_parameters);
		stealInto->destructMainParamCollections();
		stealInto->stealParamCollectionsFrom(&retained_parameters, true);
		return stealInto;
	}
	else {
		return &elementCorrectClip->paramManager;
	}
}

// If none for the correct Clip, return one for a different Clip - prioritizing NULL Clip.
// Returns NULL if couldn't find one.
// Supply stealInto to have it delete the "backed up" element, putting the contents into stealInto.
ParamManager* Song::getBackedUpParamManagerPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
                                                              ParamManager* stealInto) {

	if (!modControllable)
		return nullptr;
	int32_t i_any_clip =
	    backedUpParamManagers.search(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable)),
	                                 GREATER_OR_EQUAL); // Search just by first word
	if (i_any_clip >= backedUpParamManagers.getNumElements()) {
		return nullptr;
	}
	BackedUpParamManager* element_any_clip = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i_any_clip);
	if (element_any_clip->modControllable != modControllable) {
		return nullptr; // If nothing with even the correct modControllable at all, get out
	}

	BackedUpParamManager* element_correct_clip = nullptr;
	const auto required_type = modControllable->required_param_manager_type();
	for (int32_t i = i_any_clip; i < backedUpParamManagers.getNumElements(); ++i) {
		auto* element = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);
		if (element->modControllable != modControllable) {
			break;
		}
		if (!element->paramManager.matches_type(required_type)) {
			continue;
		}
		// Entries are sorted by clip pointer, so a null-clip fallback comes first.
		if (!element_correct_clip || element->clip == clip) {
			element_correct_clip = element;
		}
		if (element->clip == clip) {
			break;
		}
	}
	if (!element_correct_clip) {
		return nullptr;
	}

	if (stealInto) {
		// Share exact lookup's destination-alias rejection and retained-source
		// transfer after choosing the compatible exact or fallback key.
		return getBackedUpParamManagerForExactClip(modControllable, element_correct_clip->clip, stealInto);
	}
	return &element_correct_clip->paramManager;
}

// Steals stuff.
// shouldStealExpressionParamsToo should only be true to save expression params from being destructed (e.g. if the
// Clip is being destructed).
void Song::backUpParamManager(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
                              bool shouldStealExpressionParamsToo) {

	// An empty or expression-only manager can legitimately have been backed up already.
	if (paramManager->matches_type(ParamManagerType::NONE)) {
		return;
	}
#if ALPHA_OR_BETA_VERSION
	if (!paramManager->matches_type(modControllable->required_param_manager_type())) {
		FREEZE_WITH_ERROR("PM10");
	}
#endif
	// Never replace a usable backup with an incomplete manager in release builds either.
	if (!paramManager->matches_type(modControllable->required_param_manager_type())) {
		return;
	}

	uint32_t keyWords[2];
	keyWords[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable));
	keyWords[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip));

	int32_t indexToInsertAt;

	int32_t i = backedUpParamManagers.searchMultiWordExact(keyWords, &indexToInsertAt);

	if (i != -1) {
		auto* element = static_cast<BackedUpParamManager*>(backedUpParamManagers.getElementAddress(i));
		ParamManager retiring_parameters;
		take_parameter_ownership(element->paramManager, retiring_parameters);
		// Publish before retirement can yield. A nested replacement or deletion
		// must remain authoritative; never access the entry after cleanup starts.
		element->paramManager.stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
		return;
	}

	Error error = backedUpParamManagers.insertAtIndex(indexToInsertAt);
	if (error != Error::NONE) {
		// Retire locally: cleanup callbacks may replace or destroy the caller.
		// Preserve failure semantics, including discarding expression parameters.
		ParamManager retiring_parameters;
		take_parameter_ownership(*paramManager, retiring_parameters);
		return;
	}
	auto* element = new (backedUpParamManagers.getElementAddress(indexToInsertAt)) BackedUpParamManager();
	element->modControllable = modControllable;
	element->clip = clip;
	element->paramManager.stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
}

void Song::deleteBackedUpParamManagersForClip(Clip* clip) {
	if (!clip)
		return;

	AudioEngine::logAction("Song::deleteBackedUpParamManagersForClip");

	// Ok, this is the one sticky one where we actually do have to go through every element
	int32_t i = 0;

	while (i < backedUpParamManagers.getNumElements()) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);
		if (backedUp->clip == clip) {

			{
				ModControllableAudio* const owner = backedUp->modControllable;
				const int32_t first_index = backedUpParamManagers.search(
				    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)), GREATER_OR_EQUAL, 0, i + 1);
				auto* first = static_cast<BackedUpParamManager*>(backedUpParamManagers.getElementAddress(first_index));
				ParamManager retiring_clip_parameters;
				take_parameter_ownership(backedUp->paramManager, retiring_clip_parameters);

				if (first != backedUp && !first->clip) {
					ParamManager retiring_generic_parameters;
					take_parameter_ownership(first->paramManager, retiring_generic_parameters);
					// Publish replacement main parameters and remove the old clip
					// key before either retired manager destroys any collections.
					first->paramManager.stealParamCollectionsFrom(&retiring_clip_parameters);
					backedUp->~BackedUpParamManager();
					backedUpParamManagers.delete_at_index_preserving_capacity(i);
				}
				else {
					// Reuse the source slot, avoiding allocation even under memory pressure.
					backedUp->clip = nullptr;
					backedUp->paramManager.stealParamCollectionsFrom(&retiring_clip_parameters);
					backedUpParamManagers.repositionElement(i, first_index);
				}
				// Expression and superseded generic collections retire only after
				// the registry is consistent. Callbacks may now edit it safely.
			}
			// Do not retain an entry pointer or scan index across callbacks. The
			// processed entry no longer references this clip; restart to find any
			// remaining entries after callback-driven removal or insertion.
			AudioEngine::routineWithClusterLoading();
			i = 0;
		}
		else {
			i++;
		}
	}

	// Test that everything's still in order

#if ALPHA_OR_BETA_VERSION
	AudioEngine::routineWithClusterLoading();

	Clip* lastClip;
	ModControllableAudio* lastModControllable;

	for (int32_t i = 0; i < backedUpParamManagers.getNumElements(); i++) {

		BackedUpParamManager* backedUp = (BackedUpParamManager*)backedUpParamManagers.getElementAddress(i);

		if (i >= 1) {

			if (backedUp->modControllable < lastModControllable) {
				FREEZE_WITH_ERROR("E053");
			}

			else if (backedUp->modControllable == lastModControllable) {
				if (backedUp->clip < lastClip) {
					FREEZE_WITH_ERROR("E054");
				}
				else if (backedUp->clip == lastClip) {
					FREEZE_WITH_ERROR("E055");
				}
			}
		}

		lastClip = backedUp->clip;
		lastModControllable = backedUp->modControllable;
	}

#endif
}

void Song::deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable) {
	while (true) {
		// Destruction may edit or compact the registry. Resolve the owner again
		// each time instead of carrying its old index across collection callbacks.
		const int32_t index = backedUpParamManagers.search(
		    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(modControllable)), GREATER_OR_EQUAL);
		if (index >= backedUpParamManagers.getNumElements())
			return;
		auto* backup = static_cast<BackedUpParamManager*>(backedUpParamManagers.getElementAddress(index));
		if (backup->modControllable != modControllable)
			return;
		delete_backup_at_index(index);
	}
}

// The destination is an empty local owner. Moving summary ownership and removing
// the slot must finish without callbacks or allocation.
void Song::detach_backup_at_index(int32_t index, ParamManager& detached_parameters) {
	auto* backup = static_cast<BackedUpParamManager*>(backedUpParamManagers.getElementAddress(index));
	take_parameter_ownership(backup->paramManager, detached_parameters);
	backup->~BackedUpParamManager();
	backedUpParamManagers.delete_at_index_preserving_capacity(index);
}

void Song::delete_backup_at_index(int32_t index) {
	ParamManager retiring_parameters;
	detach_backup_at_index(index, retiring_parameters);
	// Destruction may service callbacks, but the registry slot is already gone.
}

void Song::deleteAllBackedUpParamManagers(bool shouldAlsoEmptyVector) {
	while (backedUpParamManagers.getNumElements() > 0) {
		AudioEngine::routineWithClusterLoading();
		const int32_t index = backedUpParamManagers.getNumElements() - 1;
		if (index < 0)
			break;
		delete_backup_at_index(index);
	}
	if (shouldAlsoEmptyVector) {
		backedUpParamManagers.empty();
	}
}

void Song::deleteAllBackedUpParamManagersWithClips() {
	while (true) {
		AudioEngine::routineWithClusterLoading();
		// Neither a pointer nor a run boundary may survive audio servicing or
		// collection destruction. Re-scan the live table, keeping generic backups.
		int32_t index = backedUpParamManagers.getNumElements() - 1;
		while (index >= 0) {
			auto* backup = static_cast<BackedUpParamManager*>(backedUpParamManagers.getElementAddress(index));
			if (backup->clip)
				break;
			--index;
		}
		if (index < 0)
			return;
		delete_backup_at_index(index);
	}
}
