#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace sample_holder_clone_test {
static std::function<void()> on_assignment;
static int assignment_calls = 0;
static bool last_reversed = false;
struct SampleHolder {
	struct path_fixture {
		std::shared_ptr<std::string> value = std::make_shared<std::string>("sample.wav");
		void set(const path_fixture* source) { value = source->value; }
	} filePath;
	void* audioFile = nullptr;
	uint64_t startPos = 7, endPos = 99;
	int32_t waveformViewScroll = 11, waveformViewZoom = 22;
	virtual ~SampleHolder() = default;
	void setAudioFile(void* sample, bool reversed) {
		++assignment_calls;
		last_reversed = reversed;
		audioFile = sample;
		startPos = endPos = 0;
		waveformViewScroll = waveformViewZoom = 0;
		if (on_assignment)
			on_assignment();
	}
	void beenClonedFrom(const SampleHolder*, bool);
};
struct SampleHolderForClip : SampleHolder {
	int16_t transpose = 3;
	int8_t cents = 4;
	void beenClonedFrom(const SampleHolderForClip*, bool);
};
#include "clip_sample_holder_clone.inc"
#include "sample_holder_clone.inc"
} // namespace sample_holder_clone_test
using namespace sample_holder_clone_test;
TEST_GROUP(SampleHolderClone) {
	SampleHolderForClip destination;
	int sample;
	void setup() override {
		on_assignment = {};
		assignment_calls = 0;
		last_reversed = false;
	}
	void teardown() override {
		on_assignment = {};
	}
	void check_settings() {
		LONGS_EQUAL(7, destination.startPos);
		LONGS_EQUAL(99, destination.endPos);
		LONGS_EQUAL(11, destination.waveformViewScroll);
		LONGS_EQUAL(22, destination.waveformViewZoom);
	}
};
TEST(SampleHolderClone, normal_clone_preserves_exact_settings_after_assignment) {
	SampleHolderForClip source;
	source.audioFile = &sample;
	destination.beenClonedFrom(&source, true);
	check_settings();
	POINTERS_EQUAL(&sample, destination.audioFile);
	CHECK(last_reversed);
	LONGS_EQUAL(3, destination.transpose);
	LONGS_EQUAL(4, destination.cents);
	POINTERS_EQUAL(source.filePath.value.get(), destination.filePath.value.get());
}
TEST(SampleHolderClone, source_destruction_during_sample_assignment_is_safe) {
	auto* source = new SampleHolderForClip;
	source->audioFile = &sample;
	on_assignment = [&] { delete source; };
	destination.beenClonedFrom(source, false);
	check_settings();
	POINTERS_EQUAL(&sample, destination.audioFile);
	STRCMP_EQUAL("sample.wav", destination.filePath.value->c_str());
	LONGS_EQUAL(3, destination.transpose);
	LONGS_EQUAL(4, destination.cents);
}
TEST(SampleHolderClone, reused_source_address_does_not_supply_replacement_settings) {
	auto* source = new SampleHolderForClip;
	source->audioFile = &sample;
	on_assignment = [&] {
		std::destroy_at(source);
		source = std::construct_at(source);
		source->startPos = source->endPos = 1000;
		source->waveformViewScroll = source->waveformViewZoom = 1000;
	};
	destination.beenClonedFrom(source, false);
	check_settings();
	delete source;
}
TEST(SampleHolderClone, empty_sample_still_copies_settings_without_assignment) {
	SampleHolderForClip source;
	destination.beenClonedFrom(&source, false);
	check_settings();
	LONGS_EQUAL(0, assignment_calls);
	POINTERS_EQUAL(nullptr, destination.audioFile);
}
TEST(SampleHolderClone, source_edit_during_assignment_does_not_mix_metadata_versions) {
	SampleHolderForClip source;
	source.audioFile = &sample;
	on_assignment = [&] {
		source.startPos = source.endPos = 1000;
		source.waveformViewScroll = source.waveformViewZoom = 1000;
	};
	destination.beenClonedFrom(&source, false);
	check_settings();
	LONGS_EQUAL(1000, source.startPos);
}
