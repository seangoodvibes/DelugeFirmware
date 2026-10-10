#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <vector>

namespace source_range_teardown_test {
int destroyed = 0, yields = 0;
std::function<void()> on_audio;
struct MultiRange {
	virtual ~MultiRange() { ++destroyed; }
};
struct MultiRangeArray {
	int elementSize = 1;
	int storage_stride = 1;
	std::vector<MultiRange*> entries;
	int getNumElements() { return entries.size(); }
	MultiRange* getElement(int index) {
		LONGS_EQUAL(storage_stride, elementSize);
		return entries.at(index);
	}
	void swapStateWith(MultiRangeArray* other) {
		entries.swap(other->entries);
		std::swap(storage_stride, other->storage_stride);
	}
};
struct DxPatch {};
void delugeDealloc(void* memory) {
	::operator delete(memory);
}
struct Source {
	deluge::lifetime::lifetime_source lifetime_source_;
	DxPatch* dxPatch = nullptr;
	MultiRangeArray ranges;
	void destructAllMultiRanges();
	~Source();
};
namespace AudioEngine {
void logAction(const char*) {
}
void routineWithClusterLoading() {
	++yields;
	if (on_audio)
		on_audio();
}
} // namespace AudioEngine
#include "source_destruction.inc"
#include "source_range_teardown.inc"
} // namespace source_range_teardown_test
using namespace source_range_teardown_test;
TEST_GROUP(SourceRangeTeardown) {
	std::vector<void*> allocations;
	MultiRange* create_range() {
		auto* storage = ::operator new(sizeof(MultiRange));
		allocations.push_back(storage);
		return new (storage) MultiRange;
	}
	void setup() override {
		destroyed = yields = 0;
		on_audio = {};
	}
	void teardown() override {
		on_audio = {};
		for (auto* storage : allocations)
			::operator delete(storage);
	}
};
TEST(SourceRangeTeardown, callbacks_see_empty_source_before_any_destruction) {
	Source source;
	source.ranges.entries = {create_range(), create_range()};
	on_audio = [&] { LONGS_EQUAL(0, source.ranges.getNumElements()); };
	source.destructAllMultiRanges();
	LONGS_EQUAL(2, destroyed);
	LONGS_EQUAL(2, yields);
	on_audio = {};
}
TEST(SourceRangeTeardown, source_destruction_during_cleanup_does_not_revisit_retired_storage) {
	auto source = std::make_unique<Source>();
	source->ranges.entries = {create_range(), create_range()};
	on_audio = [&] { source.reset(); };
	source->destructAllMultiRanges();
	LONGS_EQUAL(2, destroyed);
	LONGS_EQUAL(2, yields);
}
TEST(SourceRangeTeardown, nested_ranges_survive_old_cleanup) {
	Source source;
	source.ranges.entries = {create_range(), create_range()};
	auto* replacement = create_range();
	on_audio = [&] {
		if (yields == 1)
			source.ranges.entries = {replacement};
	};
	source.destructAllMultiRanges();
	LONGS_EQUAL(2, destroyed);
	LONGS_EQUAL(1, source.ranges.getNumElements());
	POINTERS_EQUAL(replacement, source.ranges.getElement(0));
	on_audio = {};
	source.destructAllMultiRanges();
	LONGS_EQUAL(3, destroyed);
}
TEST(SourceRangeTeardown, detached_storage_preserves_nondefault_element_stride) {
	Source source;
	source.ranges.elementSize = source.ranges.storage_stride = 8;
	source.ranges.entries = {create_range(), create_range()};
	source.destructAllMultiRanges();
	LONGS_EQUAL(2, destroyed);
	LONGS_EQUAL(8, source.ranges.elementSize);
}
TEST(SourceRangeTeardown, empty_source_does_not_service_audio) {
	Source source;
	source.destructAllMultiRanges();
	LONGS_EQUAL(0, yields);
	LONGS_EQUAL(0, destroyed);
}

TEST(SourceRangeTeardown, destruction_retires_source_before_audio_callbacks) {
	auto source = std::make_unique<Source>();
	source->ranges.entries = {create_range()};
	deluge::lifetime::lifetime_watch watch(source->lifetime_source_);
	bool callback_observed_retirement = false;
	on_audio = [&] { callback_observed_retirement = !watch.alive(); };
	source.reset();
	CHECK(callback_observed_retirement);
	CHECK_FALSE(watch.alive());
	LONGS_EQUAL(1, destroyed);
}
