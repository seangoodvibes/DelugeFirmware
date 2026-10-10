#include "CppUTest/TestHarness.h"
#include <functional>
#include <new>

namespace output_teardown_test {
static std::function<void()> on_service;
static std::function<void()> on_reference_cleanup;
static std::function<void()> on_destruction;
static int destructions, deallocations, reference_cleanups;
struct Output {
	Output* next = nullptr;
	virtual ~Output() {
		++destructions;
		auto callback = on_destruction;
		if (callback)
			callback();
	}
};
namespace AudioEngine {
void logAction(const char*) {
}
void routineWithClusterLoading() {
	auto callback = on_service;
	if (callback)
		callback();
}
} // namespace AudioEngine
void delugeDealloc(void* memory) {
	++deallocations;
	::operator delete(memory);
}
struct Song {
	Output* firstOutput = nullptr;
	bool exposed_retiring_output = false;
	void clearRecordingFromReferencesTo(Output* retiring) {
		++reference_cleanups;
		for (auto* output = firstOutput; output; output = output->next)
			exposed_retiring_output |= output == retiring;
		auto callback = on_reference_cleanup;
		if (callback)
			callback();
	}
	void deleteAllOutputs(Output** prevPointer);
};
#include "output_teardown_method.inc"

TEST_GROUP(OutputTeardown) {
	Song song;
	void setup() override {
		on_service = on_reference_cleanup = on_destruction = {};
		destructions = deallocations = reference_cleanups = 0;
	}
	void add(int count) {
		for (int index = 0; index < count; ++index) {
			auto* output = new Output;
			output->next = song.firstOutput;
			song.firstOutput = output;
		}
	}
	void teardown() override {
		on_service = on_reference_cleanup = on_destruction = {};
		song.deleteAllOutputs(&song.firstOutput);
	}
};

TEST(OutputTeardown, nested_audio_cleanup_can_empty_list_before_outer_iteration_resumes) {
	add(3);
	bool nested = false;
	on_service = [&] {
		if (nested)
			return;
		nested = true;
		song.deleteAllOutputs(&song.firstOutput);
	};
	song.deleteAllOutputs(&song.firstOutput);
	CHECK(nested);
	POINTERS_EQUAL(nullptr, song.firstOutput);
	CHECK_FALSE(song.exposed_retiring_output);
	LONGS_EQUAL(3, destructions);
	LONGS_EQUAL(3, deallocations);
	LONGS_EQUAL(3, reference_cleanups);
}

TEST(OutputTeardown, reference_and_destructor_callbacks_can_remove_the_remaining_list) {
	for (bool during_destruction : {false, true}) {
		add(3);
		bool nested = false;
		auto cleanup = [&] {
			if (nested)
				return;
			nested = true;
			song.deleteAllOutputs(&song.firstOutput);
		};
		if (during_destruction)
			on_destruction = cleanup;
		else
			on_reference_cleanup = cleanup;
		song.deleteAllOutputs(&song.firstOutput);
		on_reference_cleanup = on_destruction = {};
		CHECK(nested);
		POINTERS_EQUAL(nullptr, song.firstOutput);
		CHECK_FALSE(song.exposed_retiring_output);
		LONGS_EQUAL(during_destruction ? 6 : 3, destructions);
		LONGS_EQUAL(destructions, deallocations);
		LONGS_EQUAL(destructions, reference_cleanups);
	}
}

TEST(OutputTeardown, audio_callback_replacement_is_read_from_the_live_list) {
	add(2);
	bool replaced = false;
	on_service = [&] {
		if (replaced)
			return;
		replaced = true;
		song.deleteAllOutputs(&song.firstOutput);
		add(1);
	};
	song.deleteAllOutputs(&song.firstOutput);
	CHECK(replaced);
	POINTERS_EQUAL(nullptr, song.firstOutput);
	CHECK_FALSE(song.exposed_retiring_output);
	LONGS_EQUAL(3, destructions);
	LONGS_EQUAL(3, deallocations);
}
} // namespace output_teardown_test
