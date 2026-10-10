#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

#define ALPHA_OR_BETA_VERSION 1
namespace multisample_import_cleanup_test {
std::vector<int> events;
int frees = 0;
struct Sample {
	int32_t numReasonsToBeLoaded = 1;
	void removeReason(const char* code) {
		STRCMP_EQUAL("E393", code);
		--numReasonsToBeLoaded;
		events.push_back(1);
	}
};
void delugeDealloc(void* memory) {
	++frees;
	events.push_back(2);
	std::free(memory);
}
void FREEZE_WITH_ERROR(const char*) {
	FAIL("unexpected invalid sample reason count");
}
#include "multisample_import_cleanup.inc"
} // namespace multisample_import_cleanup_test
using namespace multisample_import_cleanup_test;
TEST_GROUP(MultisampleImportCleanup){void setup() override{events.clear();
frees = 0;
}
}
;
TEST(MultisampleImportCleanup, allocation_failure_releases_samples_before_freeing_pointer_table) {
	Sample first, second;
	second.numReasonsToBeLoaded = 3;
	auto** table = static_cast<Sample**>(std::malloc(2 * sizeof(Sample*)));
	CHECK(table != nullptr);
	table[0] = &first;
	table[1] = &second;
	release_multisample_import_samples(table, 2);
	LONGS_EQUAL(0, first.numReasonsToBeLoaded);
	LONGS_EQUAL(2, second.numReasonsToBeLoaded);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(3, events.size());
	LONGS_EQUAL(1, events[0]);
	LONGS_EQUAL(1, events[1]);
	LONGS_EQUAL(2, events[2]);
}
TEST(MultisampleImportCleanup, duplicate_entries_release_each_owned_reference_once) {
	Sample sample;
	sample.numReasonsToBeLoaded = 3;
	auto** table = static_cast<Sample**>(std::malloc(2 * sizeof(Sample*)));
	CHECK(table != nullptr);
	table[0] = table[1] = &sample;
	release_multisample_import_samples(table, 2);
	LONGS_EQUAL(1, sample.numReasonsToBeLoaded);
	LONGS_EQUAL(1, frees);
}

TEST(MultisampleImportCleanup, completed_entries_are_not_released_twice_when_skipped_entries_are_cleaned_up) {
	Sample imported, skipped;
	imported.numReasonsToBeLoaded = 2; // Temporary import ownership and the destination holder.
	auto** table = static_cast<Sample**>(std::malloc(2 * sizeof(Sample*)));
	CHECK(table != nullptr);
	table[0] = &imported;
	table[1] = &skipped;
	release_multisample_import_sample(table[0]);
	CHECK(table[0] == nullptr);
	LONGS_EQUAL(1, imported.numReasonsToBeLoaded);
	release_multisample_import_samples(table, 2);
	LONGS_EQUAL(1, imported.numReasonsToBeLoaded);
	LONGS_EQUAL(0, skipped.numReasonsToBeLoaded);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(3, events.size());
}
TEST(MultisampleImportCleanup, per_entry_release_is_idempotent) {
	Sample imported;
	Sample* slot = &imported;
	release_multisample_import_sample(slot);
	release_multisample_import_sample(slot);
	CHECK(slot == nullptr);
	LONGS_EQUAL(0, imported.numReasonsToBeLoaded);
	LONGS_EQUAL(1, events.size());
}
TEST(MultisampleImportCleanup, empty_result_still_frees_the_sorting_table) {
	auto** table = static_cast<Sample**>(std::malloc(sizeof(Sample*)));
	CHECK(table != nullptr);
	release_multisample_import_samples(table, 0);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(1, events.size());
}
