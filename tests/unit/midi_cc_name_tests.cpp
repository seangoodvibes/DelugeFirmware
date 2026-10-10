#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "util/exceptions.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
namespace midi_cc_name_test {
static bool fail_chars = false, fail_nodes = false;
static int live_allocations = 0;
template <typename T>
struct fault_allocator {
	using value_type = T;
	using is_always_equal = std::true_type;
	fault_allocator() = default;
	template <typename U>
	fault_allocator(const fault_allocator<U>&) {}
	T* allocate(size_t count) {
		if ((std::is_same_v<T, char> && fail_chars) || (!std::is_same_v<T, char> && fail_nodes))
			throw deluge::exception::BAD_ALLOC;
		auto* allocation = std::allocator<T>{}.allocate(count);
		++live_allocations;
		return allocation;
	}
	void deallocate(T* allocation, size_t count) {
		--live_allocations;
		std::allocator<T>{}.deallocate(allocation, count);
	}
	template <typename U>
	bool operator==(const fault_allocator<U>&) const {
		return true;
	}
};
using label_string = std::basic_string<char, std::char_traits<char>, fault_allocator<char>>;
class MIDIInstrument {
public:
	std::map<uint8_t, label_string, std::less<uint8_t>, fault_allocator<std::pair<const uint8_t, label_string>>> labels;
	void setNameForCC(int32_t, std::string_view);
	std::string_view getNameFromCC(int32_t);
};
#include "midi_cc_names.inc"
static constexpr std::string_view long_name =
    "A controller label long enough to require a separately allocated character buffer";
} // namespace midi_cc_name_test
using namespace midi_cc_name_test;
TEST_GROUP(MidiCCNameStorage) {
	MIDIInstrument instrument;
	void setup() override {
		fail_chars = fail_nodes = false;
		live_allocations = 0;
	}
	void teardown() override {
		fail_chars = fail_nodes = false;
		instrument.labels.clear();
		LONGS_EQUAL(0, live_allocations);
	}
};
TEST(MidiCCNameStorage, failed_new_name_allocation_leaves_no_empty_entry) {
	fail_chars = true;
	bool failed = false;
	try {
		instrument.setNameForCC(7, long_name);
	} catch (deluge::exception error) {
		failed = error == deluge::exception::BAD_ALLOC;
	}
	CHECK(failed);
	CHECK(instrument.labels.empty());
	LONGS_EQUAL(0, live_allocations);
}
TEST(MidiCCNameStorage, failed_existing_name_update_preserves_original_and_can_retry) {
	instrument.setNameForCC(7, "original");
	fail_chars = true;
	bool failed = false;
	try {
		instrument.setNameForCC(7, long_name);
	} catch (deluge::exception error) {
		failed = error == deluge::exception::BAD_ALLOC;
	}
	CHECK(failed);
	CHECK(instrument.getNameFromCC(7) == "original");
	LONGS_EQUAL(1, instrument.labels.size());
	fail_chars = false;
	instrument.setNameForCC(7, long_name);
	CHECK(instrument.getNameFromCC(7) == long_name);
}
TEST(MidiCCNameStorage, failed_node_allocation_does_not_change_existing_labels) {
	instrument.setNameForCC(7, "original");
	fail_nodes = true;
	bool failed = false;
	try {
		instrument.setNameForCC(10, long_name);
	} catch (deluge::exception error) {
		failed = error == deluge::exception::BAD_ALLOC;
	}
	CHECK(failed);
	LONGS_EQUAL(1, instrument.labels.size());
	CHECK(instrument.getNameFromCC(7) == "original");
	CHECK(instrument.getNameFromCC(10).empty());
}
TEST(MidiCCNameStorage, invalid_ccs_do_not_allocate_and_boundary_ccs_remain_valid) {
	fail_chars = fail_nodes = true;
	instrument.setNameForCC(-1, long_name);
	instrument.setNameForCC(kNumRealCCNumbers, long_name);
	CHECK(instrument.labels.empty());
	fail_chars = fail_nodes = false;
	instrument.setNameForCC(0, "first");
	instrument.setNameForCC(kNumRealCCNumbers - 1, "last");
	CHECK(instrument.getNameFromCC(0) == "first");
	CHECK(instrument.getNameFromCC(kNumRealCCNumbers - 1) == "last");
}
