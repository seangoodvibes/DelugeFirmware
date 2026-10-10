#include "memory/general_memory_allocator.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <set>
#include <unordered_set>

static std::unordered_set<void*> allocations;
static std::function<void()> on_collection_dealloc;
static uint32_t lifecycleSeed = 0;
static int lifecycleStep = -1;
static int lifecycleOperation = -1;
void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::fprintf(stderr, "seed=%u step=%d operation=%d\n", lifecycleSeed, lifecycleStep, lifecycleOperation);
		std::exit(EXIT_FAILURE);
	}
}
GeneralMemoryAllocator& GeneralMemoryAllocator::get() {
	static GeneralMemoryAllocator allocator;
	return allocator;
}
void* GeneralMemoryAllocator::allocMaxSpeed(uint32_t size) {
	void* memory = std::malloc(size);
	check(memory != nullptr, "Host allocation failed");
	allocations.insert(memory);
	return memory;
}
void delugeDealloc(void* memory) {
	auto callback = on_collection_dealloc;
	if (callback)
		callback();
	check(allocations.erase(memory) == 1, "Collection must be freed exactly once");
	std::free(memory);
}
extern "C" void freezeWithError(const char* code) {
	throw code;
}
ParamManager::ParamManager()
    : resonanceBackwardsCompatibilityProcessed(false), expressionParamSetOffset(0), summaries{} {
}
ParamManager::~ParamManager() {
	destructAndForgetParamCollections();
}

void populate(ParamManagerForTimeline& manager, bool expression = true) {
	check(manager.setupUnpatched() == Error::NONE, "Global setup must succeed");
	if (expression) {
		check(manager.ensureExpressionParamSetExists(), "Expression setup must succeed");
		manager.getExpressionParamSetSummary()->whichParamsAreAutomated[0] = 42;
	}
}

int main() {
	for (bool include_expression : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backedUpParamManagers.failInsertion = true;
		bool observed_cleanup = false;
		on_collection_dealloc = [&] {
			if (observed_cleanup)
				return;
			observed_cleanup = true;
			for (const auto& summary : source.summaries)
				check(!summary.paramCollection, "Failed backup must detach every collection before retirement");
			check(source.expressionParamSetOffset == 0, "Failed backup must reset source expression offset");
			populate(source);
		};
		song.backUpParamManager(&owner, &clip, &source, include_expression);
		on_collection_dealloc = {};
		check(observed_cleanup, "Insertion failure must retire original parameters");
		check(source.matches_type(ParamManagerType::GLOBAL) && source.getExpressionParamSet(),
		      "Failed backup cleanup must preserve callback-created source parameters");
		check(allocations.size() == 2, "Only callback-created parameters must survive failed backup");
		check(song.backedUpParamManagers.getNumElements() == 0, "Failed insertion must not publish a backup");
	}
	check(allocations.empty(), "Failed backup replacement regression must not leak");
	for (bool include_expression : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clip;
		auto* source = new ParamManagerForTimeline();
		populate(*source);
		song.backedUpParamManagers.failInsertion = true;
		on_collection_dealloc = [&] {
			if (!source)
				return;
			auto* retiring_source = source;
			source = nullptr;
			delete retiring_source;
		};
		song.backUpParamManager(&owner, &clip, source, include_expression);
		on_collection_dealloc = {};
		check(!source && allocations.empty(), "Failed backup must tolerate caller destruction during retirement");
		check(song.backedUpParamManagers.getNumElements() == 0, "Caller destruction must leave no backup entry");
	}

	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline original, replacement;
		populate(original);
		populate(replacement);
		song.backUpParamManager(&owner, &clip, &original, true);
		song.backedUpParamManagers.failInsertion = true;
		bool nested = false;
		on_collection_dealloc = [&] {
			if (nested)
				return;
			nested = true;
			song.deleteBackedUpParamManagersForModControllable(&owner);
		};
		song.backUpParamManager(&owner, &clip, &replacement, true);
		on_collection_dealloc = {};
		check(nested && allocations.empty() && song.backedUpParamManagers.getNumElements() == 0,
		      "Backup replacement must not access an entry deleted by retirement callbacks");
	}

	for (bool move_expression : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline original, replacement, latest;
		populate(original);
		populate(replacement);
		populate(latest);
		song.backUpParamManager(&owner, &clip, &original, true);
		auto* replacement_main = replacement.summaries[0].paramCollection;
		auto* replacement_expression = replacement.getExpressionParamSet();
		auto* latest_main = latest.summaries[0].paramCollection;
		auto* latest_expression = latest.getExpressionParamSet();
		bool nested = false;
		on_collection_dealloc = [&] {
			if (nested)
				return;
			nested = true;
			auto* published = song.getBackedUpParamManagerForExactClip(&owner, &clip);
			check(published && published->summaries[0].paramCollection == replacement_main,
			      "Replacement backup must be published before retiring old parameters");
			check(published->getExpressionParamSet() == (move_expression ? replacement_expression : nullptr),
			      "Replacement must publish the requested expression ownership before callbacks");
			song.backUpParamManager(&owner, &clip, &latest, true);
		};
		song.backUpParamManager(&owner, &clip, &replacement, move_expression);
		on_collection_dealloc = {};
		auto* result = song.getBackedUpParamManagerForExactClip(&owner, &clip);
		check(nested && result && result->summaries[0].paramCollection == latest_main
		          && result->getExpressionParamSet() == latest_expression,
		      "Outer backup replacement must not overwrite the nested replacement");
		check(replacement.getExpressionParamSet() == (move_expression ? nullptr : replacement_expression),
		      "Main-only backup must retain source expression ownership");
	}
	check(allocations.empty(), "Reentrant replacement tests must release every collection");

	for (bool existing_generic : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clip;
		if (existing_generic) {
			ParamManagerForTimeline generic;
			populate(generic);
			song.backUpParamManager(&owner, nullptr, &generic, true);
		}
		ParamManagerForTimeline source;
		populate(source);
		song.backUpParamManager(&owner, &clip, &source, true);
		bool nested = false;
		on_collection_dealloc = [&] {
			if (nested)
				return;
			nested = true;
			auto* generic = song.getBackedUpParamManagerForExactClip(&owner, nullptr);
			check(generic != nullptr, "Generic must be published before nested output cleanup");
			song.deleteBackedUpParamManagersForModControllable(&owner);
		};
		song.deleteBackedUpParamManagersForClip(&clip);
		on_collection_dealloc = {};
		check(nested && allocations.empty() && song.backedUpParamManagers.getNumElements() == 0,
		      "Conversion must not access or resurrect a generic entry removed during retirement");
	}

	for (int layout = 0; layout < 3; ++layout) {
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		auto* sibling_clip = &clips[0];
		auto* retiring_clip = &clips[1];
		if (BackupTable::key(sibling_clip) > BackupTable::key(retiring_clip))
			std::swap(sibling_clip, retiring_clip);
		if (layout > 0) {
			ParamManagerForTimeline prior;
			populate(prior);
			song.backUpParamManager(&owner, layout == 1 ? sibling_clip : nullptr, &prior, true);
		}
		ParamManagerForTimeline source;
		populate(source);
		auto* source_main = source.summaries[0].paramCollection;
		song.backUpParamManager(&owner, retiring_clip, &source, true);
		song.backedUpParamManagers.failInsertion = true;
		int frees = 0;
		on_collection_dealloc = [&] {
			++frees;
			for (auto* entry : song.backedUpParamManagers.entries)
				check(static_cast<BackedUpParamManager*>(entry)->clip != retiring_clip,
				      "Clip conversion must remove its old key before collection destruction");
			auto* generic = song.getBackedUpParamManagerForExactClip(&owner, nullptr);
			check(generic && generic->summaries[0].paramCollection == source_main
			          && generic->getExpressionParamSet() == nullptr,
			      "Clip conversion must publish generic main parameters before destructive callbacks");
		};
		song.deleteBackedUpParamManagersForClip(retiring_clip);
		on_collection_dealloc = {};
		check(frees == (layout == 2 ? 3 : 1),
		      "Conversion must release only expression and superseded generic ownership");
		check(song.backedUpParamManagers.getNumElements() == (layout == 1 ? 2 : 1),
		      "Clip conversion must preserve sibling backups without allocating");
	}
	check(allocations.empty(), "Conversion publication tests must free all retained collections");

	for (bool exact_match : {false, true}) {
		for (bool alias_selected : {false, true}) {
			Song song;
			ModControllableAudio owners[2];
			Clip requested_clip;
			ParamManagerForTimeline source, other;
			populate(source);
			populate(other);
			song.backUpParamManager(&owners[0], exact_match ? &requested_clip : nullptr, &source, true);
			song.backUpParamManager(&owners[1], nullptr, &other, true);
			auto* selected = song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &requested_clip);
			auto* unrelated = song.getBackedUpParamManagerForExactClip(&owners[1], nullptr);
			auto owned = allocations;
			check(!song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &requested_clip,
			                                                      alias_selected ? selected : unrelated),
			      "Preferred restore must reject destinations inside any backup registry entry");
			check(allocations == owned && song.backedUpParamManagers.getNumElements() == 2,
			      "Rejected preferred restore must preserve all backup ownership");
			check(song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &requested_clip) == selected,
			      "Rejected restore must leave the selected backup available");
		}
	}
	for (bool preferred : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip requested_clip;
		Clip* selected_clip = preferred ? nullptr : &requested_clip;
		ParamManagerForTimeline source, destination;
		populate(source);
		populate(destination);
		auto* original_main = source.summaries[0].paramCollection;
		auto* destination_expression = destination.getExpressionParamSet();
		song.backUpParamManager(&owner, selected_clip, &source, true);
		bool replaced = false;
		void* replacement_main = nullptr;
		on_collection_dealloc = [&] {
			if (replaced)
				return;
			replaced = true;
			check(song.backedUpParamManagers.getNumElements() == 0,
			      "Restore source must be detached before destination collection cleanup");
			ParamManagerForTimeline replacement;
			populate(replacement);
			replacement_main = replacement.summaries[0].paramCollection;
			song.backUpParamManager(&owner, selected_clip, &replacement, true);
		};
		auto* restored = preferred
		                     ? song.getBackedUpParamManagerPreferablyWithClip(&owner, &requested_clip, &destination)
		                     : song.getBackedUpParamManagerForExactClip(&owner, &requested_clip, &destination);
		on_collection_dealloc = {};
		check(replaced && restored == &destination && destination.summaries[0].paramCollection == original_main,
		      "Restore must retain its original source across callback replacement of the registry key");
		check(destination.getExpressionParamSet() == destination_expression,
		      "Restore must preserve destination expression ownership");
		auto* remaining = song.getBackedUpParamManagerForExactClip(&owner, selected_clip);
		check(remaining && remaining->summaries[0].paramCollection == replacement_main,
		      "Restore must not consume a replacement backup published by its cleanup callback");
	}
	check(allocations.empty(), "Callback-safe restore tests must release all collections");

	for (bool during_service : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clips[3];
		ParamManagerForTimeline generic;
		populate(generic);
		auto* generic_main = generic.summaries[0].paramCollection;
		song.backUpParamManager(&owner, nullptr, &generic, true);
		for (auto& clip : clips) {
			ParamManagerForTimeline source;
			populate(source);
			song.backUpParamManager(&owner, &clip, &source, true);
		}
		bool nested = false;
		int frees = 0;
		auto cleanup_remaining = [&] {
			if (nested)
				return;
			nested = true;
			song.deleteAllBackedUpParamManagersWithClips();
		};
		if (during_service)
			AudioEngine::on_routine = cleanup_remaining;
		on_collection_dealloc = [&] {
			++frees;
			if (!during_service)
				cleanup_remaining();
		};
		song.deleteAllBackedUpParamManagersWithClips();
		on_collection_dealloc = {};
		AudioEngine::on_routine = {};
		check(nested && frees == 6 && allocations.size() == 2,
		      "Nested clip-only cleanup must delete each clip backup once and preserve generic parameters");
		auto* remaining = song.getBackedUpParamManagerForExactClip(&owner, nullptr);
		check(song.backedUpParamManagers.getNumElements() == 1 && remaining
		          && remaining->summaries[0].paramCollection == generic_main,
		      "Nested cleanup must retain the original generic backup");
	}
	{
		Song song;
		ModControllableAudio owners[2];
		auto* preceding_owner = &owners[0];
		auto* target_owner = &owners[1];
		if (BackupTable::key(preceding_owner) > BackupTable::key(target_owner))
			std::swap(preceding_owner, target_owner);
		ParamManagerForTimeline preceding_parameters;
		populate(preceding_parameters);
		song.backUpParamManager(preceding_owner, nullptr, &preceding_parameters, true);
		Clip clips[3];
		for (auto& clip : clips) {
			ParamManagerForTimeline source;
			populate(source);
			song.backUpParamManager(target_owner, &clip, &source, true);
		}
		bool nested = false;
		int frees = 0;
		on_collection_dealloc = [&] {
			++frees;
			if (nested)
				return;
			nested = true;
			song.deleteBackedUpParamManagersForModControllable(preceding_owner);
		};
		song.deleteBackedUpParamManagersForModControllable(target_owner);
		on_collection_dealloc = {};
		check(nested && frees == 8 && allocations.empty(),
		      "Output cleanup must re-resolve its index after a callback removes preceding entries");
		check(song.backedUpParamManagers.getNumElements() == 0, "Output cleanup must not skip shifted entries");
	}

	for (bool clip_only : {false, true}) {
		Song song;
		ModControllableAudio retiring_owner, retained_owner;
		Clip clip;
		ParamManagerForTimeline retiring, retained;
		populate(retiring);
		populate(retained);
		auto* retained_main = retained.summaries[0].paramCollection;
		auto* retained_expression = retained.getExpressionParamSet();
		song.backUpParamManager(&retiring_owner, &clip, &retiring, true);
		song.backUpParamManager(&retained_owner, nullptr, &retained, true);
		int frees = 0;
		on_collection_dealloc = [&] {
			++frees;
			check(song.backedUpParamManagers.getNumElements() == 1,
			      "Selective cleanup must unlink retiring entry before collection destruction");
		};
		if (clip_only)
			song.deleteAllBackedUpParamManagersWithClips();
		else
			song.deleteBackedUpParamManagersForModControllable(&retiring_owner);
		on_collection_dealloc = {};
		check(frees == 2 && allocations.size() == 2, "Selective cleanup must free only the retiring backup");
		auto* backup = song.getBackedUpParamManagerForExactClip(&retained_owner, nullptr);
		check(backup && backup->summaries[0].paramCollection == retained_main
		          && backup->getExpressionParamSet() == retained_expression,
		      "Selective cleanup must preserve unrelated generic collection identities");
	}
	check(allocations.empty(), "Selective cleanup tests must release retained backups at scope exit");

	{
		Song song;
		ModControllableAudio owner;
		Clip original_clip, added_clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backUpParamManager(&owner, &original_clip, &source, true);
		bool inserted = false;
		int frees = 0;
		on_collection_dealloc = [&] {
			++frees;
			if (inserted)
				return;
			inserted = true;
			check(song.backedUpParamManagers.getNumElements() == 0,
			      "Callback insertion must not encounter a retiring registry entry");
			ParamManagerForTimeline added_parameters;
			populate(added_parameters);
			song.backUpParamManager(&owner, &added_clip, &added_parameters, true);
		};
		song.deleteAllBackedUpParamManagers(false);
		on_collection_dealloc = {};
		check(inserted && frees == 4 && allocations.empty(),
		      "Bulk cleanup must consume callback-added entries without losing collection ownership");
		check(song.backedUpParamManagers.getNumElements() == 0, "Callback insertion must not leave an orphan backup");
	}

	// A malformed backup must remain reclaimable without using layout-dependent transfer.
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backUpParamManager(&owner, &clip, &source, true);
		auto* backup = static_cast<BackedUpParamManager*>(song.backedUpParamManagers.getElementAddress(0));
		backup->paramManager.summaries[3] = backup->paramManager.summaries[0];
		backup->paramManager.summaries[4] = backup->paramManager.summaries[1];
		backup->paramManager.summaries[0] = {0};
		// Keep an expression alias to exercise exactly-once cleanup as well.
		backup->paramManager.expressionParamSetOffset = 255;
		on_collection_dealloc = [&] {
			check(song.backedUpParamManagers.getNumElements() == 0, "Malformed backup must be unlinked before cleanup");
		};
		song.deleteAllBackedUpParamManagers(false);
		on_collection_dealloc = {};
		check(allocations.empty(), "Malformed backup cleanup must free every unique collection");
	}
	for (bool during_service : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clips[3];
		for (auto& clip : clips) {
			ParamManagerForTimeline source;
			populate(source);
			song.backUpParamManager(&owner, &clip, &source, true);
		}
		bool nested = false;
		int frees = 0;
		auto cleanup_remaining = [&] {
			if (nested)
				return;
			nested = true;
			song.deleteAllBackedUpParamManagers(false);
		};
		if (during_service)
			AudioEngine::on_routine = cleanup_remaining;
		on_collection_dealloc = [&] {
			++frees;
			if (!during_service)
				cleanup_remaining();
		};
		song.deleteAllBackedUpParamManagers(true);
		on_collection_dealloc = {};
		AudioEngine::on_routine = {};
		check(nested && frees == 6 && allocations.empty(), "Nested bulk cleanup must free each collection once");
		check(song.backedUpParamManagers.getNumElements() == 0, "Nested cleanup must leave no registry entries");
	}

	for (bool release_storage : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backUpParamManager(&owner, &clip, &source, true);
		int frees = 0;
		on_collection_dealloc = [&] {
			++frees;
			check(song.backedUpParamManagers.getNumElements() == 0,
			      "Retiring backup must leave registry before any collection is destroyed");
		};
		song.deleteAllBackedUpParamManagers(release_storage);
		on_collection_dealloc = {};
		check(frees == 2 && allocations.empty(), "Bulk cleanup must release main and expression exactly once");
		check(song.backedUpParamManagers.getNumElements() == 0, "Bulk cleanup must empty membership");
		check(song.backedUpParamManagers.empty_calls == (release_storage ? 1 : 0),
		      "Bulk cleanup must honor caller's storage-release choice");
	}

	for (bool include_expression : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline parameters, restored;
		populate(parameters, include_expression);
		auto* main = parameters.summaries[0].paramCollection;
		song.backUpParamManager(&owner, &clip, &parameters, true);
		song.backedUpParamManagers.failInsertion = true;
		song.deleteBackedUpParamManagersForClip(&clip);
		auto* generic = song.getBackedUpParamManagerForExactClip(&owner, nullptr);
		check(generic && generic->summaries[0].paramCollection == main,
		      "First-entry generic conversion must preserve main collection identity");
		check(generic->getExpressionParamSet() == nullptr && allocations.size() == 1,
		      "First-entry generic conversion must discard only deleted clip expression");
		check(song.getBackedUpParamManagerForExactClip(&owner, nullptr, &restored) == &restored,
		      "Converted generic backup must remain restorable");
		check(restored.summaries[0].paramCollection == main && restored.getExpressionParamSet() == nullptr,
		      "Generic restoration must not inherit deleted clip expression");
	}
	check(allocations.empty(), "First-entry expression cleanup must not leak");

	{
		Song song;
		ModControllableAudio owner;
		ParamManagerForTimeline parameters;
		populate(parameters);
		song.backUpParamManager(&owner, nullptr, &parameters, true);
		song.deleteBackedUpParamManagersForClip(nullptr);
		check(song.getBackedUpParamManagerForExactClip(&owner, nullptr) != nullptr,
		      "Null clip cleanup must preserve generic backups and terminate");
	}

	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline parameters;
		populate(parameters);
		song.backUpParamManager(&owner, &clip, &parameters, true);
		int callbacks = 0;
		AudioEngine::on_routine = [&] {
			++callbacks;
			check(!song.getBackedUpParamManagerForExactClip(&owner, &clip),
			      "Entry must be processed before callback dispatch");
			song.deleteBackedUpParamManagersForModControllable(&owner);
		};
		song.deleteBackedUpParamManagersForClip(&clip);
		AudioEngine::on_routine = {};
		check(callbacks == (ALPHA_OR_BETA_VERSION ? 2 : 1) && song.backedUpParamManagers.getNumElements() == 0,
		      "Callback removal must not leave a stale entry access");
	}

	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline parameters;
		populate(parameters);
		song.backUpParamManager(&owner, &clip, &parameters, true);
		int callbacks = 0;
		AudioEngine::on_routine = [&] {
			if (++callbacks == 1) {
				ParamManagerForTimeline replacement;
				populate(replacement);
				song.backUpParamManager(&owner, &clip, &replacement, true);
			}
		};
		song.deleteBackedUpParamManagersForClip(&clip);
		AudioEngine::on_routine = {};
		check(callbacks == (ALPHA_OR_BETA_VERSION ? 3 : 2) && !song.getBackedUpParamManagerForExactClip(&owner, &clip),
		      "Restarted scan must process a callback-inserted backup");
	}
	for (uint32_t seed : {1u, 42u, 0xDE1u, 0xC0FFEEu}) {
		lifecycleSeed = seed;
		{
			Song song;
			ModControllableAudio owners[3];
			Clip clips[5];
			std::array<ParamManagerForTimeline, 4> live;
			std::mt19937 random(seed);
			for (lifecycleStep = 0; lifecycleStep < 2000; ++lifecycleStep) {
				auto* owner = &owners[random() % 3];
				auto clipIndex = random() % 6;
				auto* clip = clipIndex == 5 ? nullptr : &clips[clipIndex];
				auto& manager = live[random() % live.size()];
				lifecycleOperation = random() % 7;
				song.backedUpParamManagers.failInsertion = random() % 4 == 0;
				switch (lifecycleOperation) {
				case 0:
				case 1:
					populate(manager, random() % 2);
					song.backUpParamManager(owner, clip, &manager, random() % 2);
					break;
				case 2:
				case 3: {
					BackedUpParamManager* expected = nullptr;
					for (void* memory : song.backedUpParamManagers.entries) {
						auto* entry = static_cast<BackedUpParamManager*>(memory);
						if (entry->modControllable != owner) {
							continue;
						}
						if (entry->clip == clip) {
							expected = entry;
							break;
						}
						if (lifecycleOperation == 3 && (!expected || !entry->clip)) {
							expected = entry;
						}
					}
					auto* expectedMain = expected ? expected->paramManager.summaries[0].paramCollection : nullptr;
					auto* previousMain = manager.summaries[0].paramCollection;
					auto* expression = manager.getExpressionParamSet();
					auto ownedBefore = allocations;
					int entriesBefore = song.backedUpParamManagers.getNumElements();
					auto* restored = lifecycleOperation == 2
					                     ? song.getBackedUpParamManagerForExactClip(owner, clip, &manager)
					                     : song.getBackedUpParamManagerPreferablyWithClip(owner, clip, &manager);
					check(bool(restored) == bool(expected), "Restore must agree with independent selection");
					if (restored) {
						check(restored == &manager && manager.summaries[0].paramCollection == expectedMain,
						      "Restore must transfer the selected backup's main collection");
						check(song.backedUpParamManagers.getNumElements() == entriesBefore - 1,
						      "Restore must consume exactly one entry");
						if (expression) {
							check(manager.getExpressionParamSet() == expression,
							      "Restore must preserve live expression");
						}
					}
					else {
						check(allocations == ownedBefore && manager.summaries[0].paramCollection == previousMain
						          && manager.getExpressionParamSet() == expression,
						      "Missing backup must leave live ownership unchanged");
					}
					break;
				}
				case 4:
					if (clip) {
						song.deleteBackedUpParamManagersForClip(clip);
						for (void* memory : song.backedUpParamManagers.entries) {
							check(static_cast<BackedUpParamManager*>(memory)->clip != clip,
							      "Clip deletion must remove every reference to that clip");
						}
					}
					break;
				case 5:
					song.deleteBackedUpParamManagersForModControllable(owner);
					for (void* memory : song.backedUpParamManagers.entries) {
						check(static_cast<BackedUpParamManager*>(memory)->modControllable != owner,
						      "Output cleanup must remove every backup for that output");
					}
					break;
				case 6:
					manager.destructAndForgetParamCollections();
					break;
				}
				song.backedUpParamManagers.failInsertion = false;
				std::unordered_set<void*> reachable;
				auto audit = [&](ParamManager& checked) {
					for (auto& summary : checked.summaries) {
						if (summary.paramCollection) {
							check(allocations.contains(summary.paramCollection),
							      "Every collection pointer must be live");
							check(reachable.insert(summary.paramCollection).second,
							      "Every collection must have one owner");
						}
					}
					check(checked.has_valid_layout(), "Every manager must retain a valid layout");
				};
				for (auto& checked : live) {
					audit(checked);
				}
				std::set<std::pair<uint32_t, uint32_t>> keys;
				std::pair<uint32_t, uint32_t> previous{};
				for (void* memory : song.backedUpParamManagers.entries) {
					auto* entry = static_cast<BackedUpParamManager*>(memory);
					audit(entry->paramManager);
					auto key = std::make_pair(BackupTable::key(entry->modControllable), BackupTable::key(entry->clip));
					check(keys.insert(key).second && key >= previous, "Backup keys must remain unique and sorted");
					previous = key;
				}
				check(reachable == allocations, "Every allocated collection must remain reachable");
			}
		}
		check(allocations.empty(), "Lifecycle teardown must release all live and backed-up collections");
	}
	lifecycleStep = -1;
	lifecycleOperation = -1;
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline valid, incompatible;
		populate(valid);
		song.backUpParamManager(&owner, &clip, &valid, true);
		auto* backup = song.getBackedUpParamManagerForExactClip(&owner, &clip);
		auto* savedMain = backup->summaries[0].paramCollection;
		check(incompatible.setupMIDI() == Error::NONE, "Create incompatible incoming backup");
		auto* incoming = incompatible.summaries[0].paramCollection;
		auto owned = allocations;
#if ALPHA_OR_BETA_VERSION
		bool froze = false;
		try {
			song.backUpParamManager(&owner, &clip, &incompatible, true);
		} catch (const char* code) {
			froze = std::strcmp(code, "PM10") == 0;
		}
		check(froze, "Diagnostics must flag an incompatible incoming backup");
#else
		song.backUpParamManager(&owner, &clip, &incompatible, true);
#endif
		check(allocations == owned && incompatible.summaries[0].paramCollection == incoming,
		      "Rejected backup must preserve incoming ownership in both diagnostics modes");
		check(song.getBackedUpParamManagerForExactClip(&owner, &clip) == backup
		          && backup->summaries[0].paramCollection == savedMain,
		      "Rejected backup must not replace the existing compatible backup");
	}
	check(allocations.empty(), "Rejected backup teardown must release all allocations");
	{
		Song song;
		ModControllableAudio owners[3];
		Clip clips[2];
		ParamManagerForTimeline fallback, exact, unrelated, destination;
		populate(fallback);
		populate(exact);
		populate(unrelated);
		populate(destination);
		song.backUpParamManager(&owners[0], nullptr, &fallback, true);
		song.backUpParamManager(&owners[0], &clips[0], &exact, true);
		song.backUpParamManager(&owners[1], &clips[0], &unrelated, true);
		auto* preferred = song.getBackedUpParamManagerForExactClip(&owners[0], &clips[0]);
		auto* nullBackup = song.getBackedUpParamManagerForExactClip(&owners[0], nullptr);
		check(song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &clips[0]) == preferred,
		      "Compatible exact match must beat null fallback");
		check(preferred->setupMIDI() == Error::NONE, "Create incompatible backup fixture");
		auto owned = allocations;
		auto* destinationMain = destination.summaries[0].paramCollection;
		check(!song.getBackedUpParamManagerForExactClip(&owners[0], &clips[0], &destination),
		      "Exact lookup must reject incompatible backup");
		check(allocations == owned && destination.summaries[0].paramCollection == destinationMain,
		      "Rejected restore must leave destination untouched");
		check(song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &clips[0]) == nullBackup,
		      "Incompatible exact match must not mask compatible fallback");
		check(nullBackup->setupMIDI() == Error::NONE, "Make all owner backups incompatible");
		check(!song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &clips[0]),
		      "Lookup must not cross into another output's compatible backups");
		check(!song.getBackedUpParamManagerPreferablyWithClip(&owners[2], &clips[0]),
		      "Missing output must not borrow another output's backup");
		song.deleteBackedUpParamManagersForModControllable(&owners[0]);
		check(song.getBackedUpParamManagerForExactClip(&owners[1], &clips[0]) != nullptr,
		      "Deleting one output's backups must preserve another output");
	}
	check(allocations.empty(), "Selection cases must release all collections");
	{
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		ParamManagerForTimeline first, removed;
		populate(first);
		populate(removed);
		song.backUpParamManager(&owner, &clips[0], &first, true);
		song.backUpParamManager(&owner, &clips[1], &removed, true);
		auto* survivor = song.getBackedUpParamManagerForExactClip(&owner, &clips[0]);
		song.backedUpParamManagers.failInsertion = true;
		song.deleteBackedUpParamManagersForClip(&clips[1]);
		check(song.backedUpParamManagers.getNumElements() == 2 && allocations.size() == 3,
		      "Generic conversion must preserve main parameters without allocating a table entry");
		check(song.getBackedUpParamManagerForExactClip(&owner, &clips[0]) == survivor,
		      "In-place conversion must preserve the other clip's backup");
		check(song.getBackedUpParamManagerForExactClip(&owner, nullptr) != nullptr,
		      "Generic conversion must succeed even when insertion is disabled");
	}
	check(allocations.empty(), "In-place conversion must not leak");
	{
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		ParamManagerForTimeline first, second;
		populate(first);
		populate(second);
		song.backUpParamManager(&owner, &clips[0], &first, true);
		song.backUpParamManager(&owner, &clips[1], &second, true);
		song.deleteBackedUpParamManagersForClip(&clips[1]);
		song.deleteBackedUpParamManagersForModControllable(&owner);
		check(allocations.empty(), "Deleting a non-first clip backup must release its expression collection");
	}
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source, destination;
		populate(source);
		populate(destination);
		auto* main = source.summaries[0].paramCollection;
		auto* expression = destination.getExpressionParamSet();
		song.backUpParamManager(&owner, &clip, &source, true);
		check(!source.matches_type(ParamManagerType::ANY), "Backup must relinquish source ownership");
		auto* backup = song.getBackedUpParamManagerForExactClip(&owner, &clip);
		check(backup && backup->summaries[0].paramCollection == main, "Exact lookup must retain collection identity");
		check(song.getBackedUpParamManagerForExactClip(&owner, &clip, &destination) == &destination,
		      "Restore must return destination");
		check(destination.summaries[0].paramCollection == main && destination.getExpressionParamSet() == expression,
		      "Restore must replace main collections and preserve destination expression");
		check(song.backedUpParamManagers.getNumElements() == 0 && allocations.size() == 2,
		      "Restore must remove backup and free superseded collections");
	}
	check(allocations.empty(), "Restored manager destruction must not leak");
	for (bool existingFallback : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		ParamManagerForTimeline source, deleted;
		populate(source);
		populate(deleted);
		song.backUpParamManager(&owner, existingFallback ? nullptr : &clips[0], &source, true);
		auto* deletedMain = deleted.summaries[0].paramCollection;
		song.backUpParamManager(&owner, &clips[1], &deleted, true);
		song.deleteBackedUpParamManagersForClip(&clips[1]);
		check(!song.getBackedUpParamManagerForExactClip(&owner, &clips[1]), "Deleted clip key must be removed");
		auto* fallback = song.getBackedUpParamManagerForExactClip(&owner, nullptr);
		check(fallback && fallback->summaries[0].paramCollection == deletedMain,
		      "Deleted clip main collections must become the null-clip fallback");
		check(song.getBackedUpParamManagerPreferablyWithClip(&owner, &clips[1]) == fallback,
		      "Missing exact clip must select null-clip fallback");
		song.deleteBackedUpParamManagersForModControllable(&owner);
		check(allocations.empty(), "Fallback replacement must release all superseded expression and main collections");
	}
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backUpParamManager(&owner, &clip, &source, false);
		check(source.getExpressionParamSet() != nullptr && !source.matches_type(ParamManagerType::ANY_MAIN),
		      "Main-only backup must retain clip expression");
		populate(source, false);
		auto* replacement = source.summaries[0].paramCollection;
		song.backUpParamManager(&owner, &clip, &source, true);
		check(song.backedUpParamManagers.getNumElements() == 1
		          && song.getBackedUpParamManagerForExactClip(&owner, &clip)->summaries[0].paramCollection
		                 == replacement,
		      "Backing up the same key must replace rather than duplicate");
		song.deleteBackedUpParamManagersForClip(&clip);
		check(!song.getBackedUpParamManagerForExactClip(&owner, &clip)
		          && song.getBackedUpParamManagerForExactClip(&owner, nullptr),
		      "Deleting the first backup must clear its clip reference");
	}
	check(allocations.empty(), "Song destruction must release remaining backups");
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backedUpParamManagers.failInsertion = true;
		song.backUpParamManager(&owner, &clip, &source, true);
		check(allocations.empty() && source.has_valid_layout() && !source.matches_type(ParamManagerType::ANY),
		      "Failed backup insertion must clean source collections exactly once");
		check(song.backedUpParamManagers.getNumElements() == 0, "Failed insertion must not create a backup");
	}
	std::puts("Song backup regressions passed");
}
