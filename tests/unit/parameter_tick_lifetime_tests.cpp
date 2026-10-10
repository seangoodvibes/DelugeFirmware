#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace parameter_tick_lifetime_test {
constexpr int PARAM_COLLECTIONS_STORAGE_NUM = 5;
struct ModelStackWithParamCollection {};
struct ParamCollection {
	int calls = 0;
	std::function<void()> callback;
	void tickSamples(int32_t, ModelStackWithParamCollection*) {
		++calls;
		// The callback may delete its collection, so retain the callable separately.
		auto local_callback = callback;
		if (local_callback)
			local_callback();
	}
};
struct ParamCollectionSummary {
	ParamCollection* paramCollection = nullptr;
};
struct ModelStackWithThreeMainThings {
	ModelStackWithParamCollection child;
	ModelStackWithParamCollection* addParamCollection(ParamCollection*, ParamCollectionSummary*) { return &child; }
};
struct ParamManagerForTimeline {
	mutable deluge::lifetime::lifetime_source lifetime;
	ParamCollectionSummary summaries[PARAM_COLLECTIONS_STORAGE_NUM];
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	void tickSamples(int32_t, ModelStackWithThreeMainThings*, const deluge::lifetime::callback_validation* = nullptr);
};
#include "parameter_tick_lifetime.inc"
} // namespace parameter_tick_lifetime_test
using namespace parameter_tick_lifetime_test;
TEST_GROUP(parameter_tick_lifetime) {
	ModelStackWithThreeMainThings stack;
};
TEST(parameter_tick_lifetime, guarded_and_unguarded_live_layouts_tick_each_collection) {
	for (bool guarded : {false, true}) {
		ParamManagerForTimeline manager;
		ParamCollection first, second;
		manager.summaries[0].paramCollection = &first;
		manager.summaries[1].paramCollection = &second;
		auto lifetime = manager.watch_lifetime();
		const auto valid = [&] { return lifetime.alive(); };
		deluge::lifetime::callback_validation validation{valid};
		manager.tickSamples(32, &stack, guarded ? &validation : nullptr);
		LONGS_EQUAL(1, first.calls);
		LONGS_EQUAL(1, second.calls);
	}
}
TEST(parameter_tick_lifetime, callback_can_destroy_manager_without_next_summary_read) {
	auto manager = std::make_unique<ParamManagerForTimeline>();
	ParamCollection first, second;
	manager->summaries[0].paramCollection = &first;
	manager->summaries[1].paramCollection = &second;
	auto lifetime = manager->watch_lifetime();
	const auto valid = [&] { return lifetime.alive(); };
	deluge::lifetime::callback_validation validation{valid};
	first.callback = [&] { manager.reset(); };
	manager->tickSamples(32, &stack, &validation);
	LONGS_EQUAL(1, first.calls);
	LONGS_EQUAL(0, second.calls);
}
TEST(parameter_tick_lifetime, expired_owner_rejects_entry) {
	ParamManagerForTimeline manager;
	ParamCollection first;
	manager.summaries[0].paramCollection = &first;
	const auto valid = [] { return false; };
	deluge::lifetime::callback_validation validation{valid};
	manager.tickSamples(32, &stack, &validation);
	LONGS_EQUAL(0, first.calls);
}
TEST(parameter_tick_lifetime, replacing_any_collection_cancels_remaining_ticks) {
	for (int replaced : {0, 1, 2}) {
		ParamManagerForTimeline manager;
		ParamCollection first, second, replacement;
		manager.summaries[0].paramCollection = &first;
		manager.summaries[1].paramCollection = &second;
		const auto valid = [] { return true; };
		deluge::lifetime::callback_validation validation{valid};
		first.callback = [&] { manager.summaries[replaced].paramCollection = &replacement; };
		manager.tickSamples(32, &stack, &validation);
		LONGS_EQUAL(0, second.calls);
		LONGS_EQUAL(0, replacement.calls);
	}
}
TEST(parameter_tick_lifetime, deletion_of_next_collection_cancels_before_dereference) {
	ParamManagerForTimeline manager;
	ParamCollection first;
	auto next = std::make_unique<ParamCollection>();
	manager.summaries[0].paramCollection = &first;
	manager.summaries[1].paramCollection = next.get();
	const auto valid = [] { return true; };
	deluge::lifetime::callback_validation validation{valid};
	first.callback = [&] {
		manager.summaries[1].paramCollection = nullptr;
		next.reset();
	};
	manager.tickSamples(32, &stack, &validation);
	LONGS_EQUAL(1, first.calls);
}
