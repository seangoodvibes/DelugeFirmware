#include "CppUTest/TestHarness.h"
#include "storage/multi_range/multi_range_array.h"
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>

static_assert(!std::is_convertible_v<MultiRangeArray*, ResizeableArray*>);
static_assert(!std::is_copy_constructible_v<MultiRangeArray>);
template <typename T>
concept exposes_untracked_empty = requires(T& array) { array.empty(); };
static_assert(!exposes_untracked_empty<MultiRangeArray>);

namespace multi_range_layout_test {
enum class Error { NONE, INSUFFICIENT_RAM, ABORTED_BY_USER };
bool fail_allocation = false;
std::function<void()> on_allocate;
struct MultiRange {
	int16_t topNote = 32767;
	virtual ~MultiRange() = default;
};
struct MultisampleRange : MultiRange {
	int data = 0;
};
struct MultiWaveTableRange : MultiRange {
	int data[8]{};
};
// Storage double preserves ring-array ownership rules: callers destroy ranges,
// mutations can move their bytes, and swapping storage does not swap element size.
struct OrderedResizeableArray {
	int32_t elementSize, numElements = 0;
	std::unique_ptr<unsigned char[]> memory;
	explicit OrderedResizeableArray(int32_t size) : elementSize(size) {}
	void* getElementAddress(int32_t index) { return memory.get() + index * elementSize; }
	Error insertAtIndex(int32_t index, int32_t count, void*) {
		if (on_allocate)
			on_allocate();
		if (fail_allocation)
			return Error::INSUFFICIENT_RAM;
		if (!memory)
			memory = std::make_unique<unsigned char[]>(8 * sizeof(MultiWaveTableRange));
		std::memmove(static_cast<unsigned char*>(getElementAddress(index + count)), getElementAddress(index),
		             (numElements - index) * elementSize);
		numElements += count;
		return Error::NONE;
	}
	void deleteAtIndex(int32_t index, int32_t count, bool) {
		std::memmove(getElementAddress(index), getElementAddress(index + count),
		             (numElements - index - count) * elementSize);
		numElements -= count;
	}
	bool ensureEnoughSpaceAllocated(int32_t) { return !fail_allocation; }
	void swapStateWith(OrderedResizeableArray* other) {
		memory.swap(other->memory);
		std::swap(numElements, other->numElements);
	}
	void empty() {
		memory.reset();
		numElements = 0;
	}
};
struct MultiRangeArray : OrderedResizeableArray {
	uint32_t layout_revision_ = 0;
	explicit MultiRangeArray(int32_t size = sizeof(MultisampleRange)) : OrderedResizeableArray(size) {}
	MultiRange* getElement(int32_t);
	MultiRange* insertMultiRange(int32_t);
	Error changeType(int32_t, const deluge::lifetime::callback_validation* = nullptr);
	Error insertAtIndex(int32_t, int32_t = 1, void* = nullptr);
	void deleteAtIndex(int32_t, int32_t = 1, bool = true);
	bool ensureEnoughSpaceAllocated(int32_t);
	void swapStateWith(MultiRangeArray*);
	void destroy_ranges() {
		for (int32_t index = 0; index < numElements; ++index)
			getElement(index)->~MultiRange();
		empty();
	}
};
#include "multi_range_layout.inc"
} // namespace multi_range_layout_test
namespace layout_test = multi_range_layout_test;
TEST_GROUP(MultiRangeLayout) {
	layout_test::MultiRangeArray first, second;
	void setup() override {
		layout_test::fail_allocation = false;
		layout_test::on_allocate = {};
	}
	void teardown() override {
		layout_test::on_allocate = {};
		first.destroy_ranges();
		second.destroy_ranges();
	}
};
TEST(MultiRangeLayout, delete_and_reinsert_at_same_address_invalidates_identity) {
	auto* original = first.insertMultiRange(0);
	const auto revision = first.layout_revision_;
	original->~MultiRange();
	first.deleteAtIndex(0);
	auto* replacement = first.insertMultiRange(0);
	POINTERS_EQUAL(original, replacement);
	LONGS_EQUAL(1, first.numElements);
	CHECK(revision != first.layout_revision_);
}
TEST(MultiRangeLayout, swapping_round_trip_invalidates_both_arrays) {
	auto* original = first.insertMultiRange(0);
	const auto first_revision = first.layout_revision_, second_revision = second.layout_revision_;
	first.swapStateWith(&second);
	first.swapStateWith(&second);
	POINTERS_EQUAL(original, first.getElement(0));
	CHECK(first.layout_revision_ != first_revision);
	CHECK(second.layout_revision_ != second_revision);
}
TEST(MultiRangeLayout, failed_reservation_invalidates_watch_without_losing_ranges) {
	auto* original = first.insertMultiRange(0);
	const auto revision = first.layout_revision_;
	layout_test::fail_allocation = true;
	CHECK_FALSE(first.ensureEnoughSpaceAllocated(3));
	CHECK(first.layout_revision_ != revision);
	POINTERS_EQUAL(original, first.getElement(0));
	LONGS_EQUAL(1, first.numElements);
}
TEST(MultiRangeLayout, failed_insertion_preserves_original_and_invalidates_watch) {
	auto* original = first.insertMultiRange(0);
	const auto revision = first.layout_revision_;
	layout_test::fail_allocation = true;
	POINTERS_EQUAL(nullptr, first.insertMultiRange(1));
	CHECK(first.layout_revision_ != revision);
	POINTERS_EQUAL(original, first.getElement(0));
}
TEST(MultiRangeLayout, type_conversion_preserves_keys_and_changes_revision) {
	first.insertMultiRange(0)->topNote = 60;
	first.insertMultiRange(1)->topNote = 72;
	const auto revision = first.layout_revision_;
	CHECK(first.changeType(sizeof(layout_test::MultiWaveTableRange)) == layout_test::Error::NONE);
	CHECK(first.layout_revision_ != revision);
	LONGS_EQUAL(sizeof(layout_test::MultiWaveTableRange), first.elementSize);
	LONGS_EQUAL(60, first.getElement(0)->topNote);
	LONGS_EQUAL(72, first.getElement(1)->topNote);
}
TEST(MultiRangeLayout, failed_conversion_preserves_original_and_invalidates_watch) {
	auto* original = first.insertMultiRange(0);
	original->topNote = 60;
	const auto revision = first.layout_revision_;
	layout_test::fail_allocation = true;
	CHECK(first.changeType(sizeof(layout_test::MultiWaveTableRange)) == layout_test::Error::INSUFFICIENT_RAM);
	CHECK(first.layout_revision_ != revision);
	POINTERS_EQUAL(original, first.getElement(0));
	LONGS_EQUAL(60, first.getElement(0)->topNote);
	LONGS_EQUAL(sizeof(layout_test::MultisampleRange), first.elementSize);
}
TEST(MultiRangeLayout, empty_type_change_invalidates_revision) {
	const auto revision = first.layout_revision_;
	CHECK(first.changeType(sizeof(layout_test::MultiWaveTableRange)) == layout_test::Error::NONE);
	CHECK(first.layout_revision_ != revision);
	LONGS_EQUAL(0, first.numElements);
}

TEST(MultiRangeLayout, swapping_different_range_types_moves_stride_with_storage) {
	first.insertMultiRange(0);
	auto* sample_second = first.insertMultiRange(1);
	CHECK(second.changeType(sizeof(layout_test::MultiWaveTableRange)) == layout_test::Error::NONE);
	second.insertMultiRange(0);
	auto* wave_second = second.insertMultiRange(1);
	first.swapStateWith(&second);
	const auto first_size = first.elementSize;
	const auto second_size = second.elementSize;
	const bool first_matches = first.getElement(1) == wave_second;
	const bool second_matches = second.getElement(1) == sample_second;
	first.swapStateWith(&second);
	LONGS_EQUAL(sizeof(layout_test::MultiWaveTableRange), first_size);
	LONGS_EQUAL(sizeof(layout_test::MultisampleRange), second_size);
	CHECK_TRUE(first_matches);
	CHECK_TRUE(second_matches);
}

TEST(MultiRangeLayout, conversion_rejects_cancelled_entry_without_touching_ranges) {
	auto* original = first.insertMultiRange(0);
	const auto revision = first.layout_revision_;
	const auto context_valid = [] { return false; };
	deluge::lifetime::callback_validation validation(context_valid);
	CHECK(first.changeType(sizeof(layout_test::MultiWaveTableRange), &validation)
	      == layout_test::Error::ABORTED_BY_USER);
	POINTERS_EQUAL(original, first.getElement(0));
	LONGS_EQUAL(revision, first.layout_revision_);
}
TEST(MultiRangeLayout, conversion_allocation_cancellation_preserves_original) {
	auto* original = first.insertMultiRange(0);
	bool valid = true;
	const auto context_valid = [&] { return valid; };
	deluge::lifetime::callback_validation validation(context_valid);
	layout_test::on_allocate = [&] { valid = false; };
	CHECK(first.changeType(sizeof(layout_test::MultiWaveTableRange), &validation)
	      == layout_test::Error::ABORTED_BY_USER);
	POINTERS_EQUAL(original, first.getElement(0));
	LONGS_EQUAL(sizeof(layout_test::MultisampleRange), first.elementSize);
}
TEST(MultiRangeLayout, conversion_nested_layout_change_aborts_even_without_caller_validation) {
	auto* original = first.insertMultiRange(0);
	layout_test::on_allocate = [&] { ++first.layout_revision_; };
	CHECK(first.changeType(sizeof(layout_test::MultiWaveTableRange)) == layout_test::Error::ABORTED_BY_USER);
	POINTERS_EQUAL(original, first.getElement(0));
}
TEST(MultiRangeLayout, conversion_owner_destruction_during_allocation_stops_old_storage_access) {
	struct Owner {
		deluge::lifetime::lifetime_source lifetime;
		layout_test::MultiRangeArray ranges;
		~Owner() {
			lifetime.retire();
			ranges.destroy_ranges();
		}
	};
	auto owner = std::make_unique<Owner>();
	owner->ranges.insertMultiRange(0);
	deluge::lifetime::lifetime_watch owner_watch(owner->lifetime);
	const auto context_valid = [&] { return owner_watch.alive(); };
	deluge::lifetime::callback_validation validation(context_valid);
	layout_test::on_allocate = [&] { owner.reset(); };
	CHECK(owner->ranges.changeType(sizeof(layout_test::MultiWaveTableRange), &validation)
	      == layout_test::Error::ABORTED_BY_USER);
	CHECK_FALSE(owner_watch.alive());
}
