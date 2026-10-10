#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace source_first_range_test {
std::function<void()> on_allocate;
bool fail_allocation = false;
int allocations = 0, destructions = 0;
enum class OscType { SAMPLE, WAVETABLE };
struct MultiRange {
	int topNote = 0;
	virtual ~MultiRange() { ++destructions; }
};
struct MultiRangeArray {
	explicit MultiRangeArray(int size = 32) : stride(size) {}
	int stride;
	unsigned revision = 0;
	std::vector<MultiRange*> entries;
	~MultiRangeArray() {
		for (auto* entry : entries)
			::operator delete(entry);
	}
	int element_size() const { return stride; }
	unsigned layout_revision() const { return revision; }
	int getNumElements() const { return entries.size(); }
	MultiRange* getElement(int index) { return entries.at(index); }
	MultiRange* insertMultiRange(int) {
		++allocations;
		++revision;
		if (on_allocate)
			on_allocate();
		if (fail_allocation)
			return nullptr;
		auto* range = new (::operator new(sizeof(MultiRange))) MultiRange;
		entries.push_back(range);
		return range;
	}
	void swapStateWith(MultiRangeArray* other) {
		++revision;
		++other->revision;
		entries.swap(other->entries);
		std::swap(stride, other->stride);
	}
};
struct Source {
	deluge::lifetime::lifetime_source lifetime;
	MultiRangeArray ranges;
	OscType oscType = OscType::SAMPLE;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	~Source() {
		lifetime.retire();
		for (auto* range : ranges.entries)
			range->~MultiRange();
	}
	MultiRange* getOrCreateFirstRange();
};
#include "source_first_range.inc"
} // namespace source_first_range_test
using namespace source_first_range_test;
TEST_GROUP(SourceFirstRange){void setup() override{on_allocate = {};
fail_allocation = false;
allocations = destructions = 0;
deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
}
void teardown() override {
	on_allocate = {};
	deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
}
}
;
TEST(SourceFirstRange, successful_creation_preserves_stride_and_sets_full_range) {
	Source source;
	source.ranges.stride = 64;
	auto* range = source.getOrCreateFirstRange();
	CHECK(range != nullptr);
	POINTERS_EQUAL(range, source.ranges.getElement(0));
	LONGS_EQUAL(32767, range->topNote);
	LONGS_EQUAL(64, source.ranges.element_size());
	LONGS_EQUAL(0, destructions);
}
TEST(SourceFirstRange, existing_range_is_returned_without_allocation) {
	Source source;
	auto* range = source.ranges.insertMultiRange(0);
	range->topNote = 72;
	POINTERS_EQUAL(range, source.getOrCreateFirstRange());
	LONGS_EQUAL(1, allocations);
	LONGS_EQUAL(72, range->topNote);
}
TEST(SourceFirstRange, failed_allocation_preserves_empty_source) {
	Source source;
	fail_allocation = true;
	POINTERS_EQUAL(nullptr, source.getOrCreateFirstRange());
	LONGS_EQUAL(0, source.ranges.getNumElements());
	LONGS_EQUAL(0, source.ranges.layout_revision());
}
TEST(SourceFirstRange, destroyed_source_during_allocation_does_not_receive_range) {
	auto source = std::make_unique<Source>();
	on_allocate = [&] { source.reset(); };
	POINTERS_EQUAL(nullptr, source->getOrCreateFirstRange());
	LONGS_EQUAL(1, destructions);
}
TEST(SourceFirstRange, nested_creation_preserves_the_nested_range) {
	Source source;
	MultiRange* nested_range = nullptr;
	on_allocate = [&] {
		auto callback = std::exchange(on_allocate, {});
		nested_range = source.getOrCreateFirstRange();
		nested_range->topNote = 88;
	};
	POINTERS_EQUAL(nullptr, source.getOrCreateFirstRange());
	POINTERS_EQUAL(nested_range, source.ranges.getElement(0));
	LONGS_EQUAL(88, nested_range->topNote);
	LONGS_EQUAL(1, destructions);
}
TEST(SourceFirstRange, oscillator_change_cancels_range_publication) {
	Source source;
	on_allocate = [&] { source.oscType = OscType::WAVETABLE; };
	POINTERS_EQUAL(nullptr, source.getOrCreateFirstRange());
	LONGS_EQUAL(0, source.ranges.getNumElements());
	LONGS_EQUAL(1, destructions);
}
TEST(SourceFirstRange, layout_change_cancels_even_when_source_is_still_empty) {
	Source source;
	on_allocate = [&] { ++source.ranges.revision; };
	POINTERS_EQUAL(nullptr, source.getOrCreateFirstRange());
	LONGS_EQUAL(0, source.ranges.getNumElements());
	LONGS_EQUAL(1, destructions);
}
