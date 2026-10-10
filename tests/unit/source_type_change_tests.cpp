#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>

namespace source_type_change_test {
namespace session = deluge::gui::ui_session;
enum class Error { NONE, INSUFFICIENT_RAM };
enum class OscType { SQUARE, SAMPLE, WAVETABLE, DX7 };
struct MultiRange {
} range, old_range;
struct MultisampleRange {
	int value;
};
struct MultiWaveTableRange {
	int values[2];
};
std::function<void()> on_change, on_cleanup, on_create;
int changes = 0, cleanups = 0, creates = 0, clears = 0, dx_creates = 0;
bool fail_first_change = false;
struct Source {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	OscType oscType = OscType::SQUARE;
	struct {
		int32_t elementSize = 1, count = 1;
		int32_t getNumElements() { return count; }
		void* getElementAddress(int32_t) { return &range; }
		void empty() {
			++clears;
			count = 0;
		}
		Error changeType(int32_t size) {
			++changes;
			const auto error = fail_first_change && changes == 1 ? Error::INSUFFICIENT_RAM : Error::NONE;
			if (error == Error::NONE)
				elementSize = size;
			if (on_change)
				on_change();
			return error;
		}
	} ranges;
	void destructAllMultiRanges() {
		++cleanups;
		ranges.count = 0;
		if (on_cleanup)
			on_cleanup();
	}
	MultiRange* getOrCreateFirstRange() {
		++creates;
		ranges.count = 1;
		if (on_create)
			on_create();
		return &range;
	}
	void ensureDxPatch() { ++dx_creates; }
	void setOscType(OscType);
};
struct Editor {
	Source* currentSource = nullptr;
	int32_t currentMultiRangeIndex = 5;
	MultiRange* currentMultiRange = &old_range;
} editors[2];
Editor& sound_editor_for_session() {
	return editors[session::current() == session::Id::Local ? 0 : 1];
}
#include "source_type_change.inc"
} // namespace source_type_change_test
using namespace source_type_change_test;
TEST_GROUP(SourceTypeChange) {
	Source source, other;
	void setup() override {
		session::detail::active = session::Id::Local;
		editors[0] = {};
		editors[1] = {};
		editors[0].currentSource = &source;
		on_change = on_cleanup = on_create = {};
		changes = cleanups = creates = clears = dx_creates = 0;
		fail_first_change = false;
	}
	void teardown() override {
		on_change = on_cleanup = on_create = {};
		session::detail::active = session::Id::Local;
	}
};
TEST(SourceTypeChange, destroyed_source_during_failed_conversion_cleanup_stops_retry) {
	auto owner = std::make_unique<Source>();
	fail_first_change = true;
	on_cleanup = [&] { owner.reset(); };
	owner->setOscType(OscType::SAMPLE);
	LONGS_EQUAL(1, changes);
	LONGS_EQUAL(0, creates);
	LONGS_EQUAL(0, clears);
	LONGS_EQUAL(5, editors[0].currentMultiRangeIndex);
}
TEST(SourceTypeChange, nested_ranges_are_not_cleared_or_converted) {
	fail_first_change = true;
	on_cleanup = [&] { source.ranges.count = 2; };
	source.setOscType(OscType::SAMPLE);
	LONGS_EQUAL(1, changes);
	LONGS_EQUAL(0, clears);
	LONGS_EQUAL(2, source.ranges.count);
	CHECK(source.oscType == OscType::SQUARE);
}
TEST(SourceTypeChange, normal_failure_cleanup_retries_empty_array_and_updates_selected_editor) {
	fail_first_change = true;
	source.setOscType(OscType::SAMPLE);
	LONGS_EQUAL(2, changes);
	LONGS_EQUAL(1, creates);
	LONGS_EQUAL(1, cleanups);
	LONGS_EQUAL(0, editors[0].currentMultiRangeIndex);
	POINTERS_EQUAL(&range, editors[0].currentMultiRange);
	CHECK(source.oscType == OscType::SAMPLE);
}
TEST(SourceTypeChange, unrelated_editor_is_not_reset_by_failed_conversion) {
	fail_first_change = true;
	editors[0].currentSource = &other;
	source.setOscType(OscType::WAVETABLE);
	CHECK(source.oscType == OscType::WAVETABLE);
	LONGS_EQUAL(5, editors[0].currentMultiRangeIndex);
	POINTERS_EQUAL(&old_range, editors[0].currentMultiRange);
}
TEST(SourceTypeChange, unrelated_editor_is_not_retargeted_after_success) {
	editors[0].currentSource = &other;
	editors[0].currentMultiRangeIndex = 0;
	source.setOscType(OscType::SAMPLE);
	POINTERS_EQUAL(&old_range, editors[0].currentMultiRange);
}
TEST(SourceTypeChange, nested_type_change_is_not_overwritten) {
	on_change = [&] { source.oscType = OscType::DX7; };
	source.setOscType(OscType::SAMPLE);
	CHECK(source.oscType == OscType::DX7);
	LONGS_EQUAL(0, creates);
}
TEST(SourceTypeChange, changed_panel_cancels_cleanup_continuation_and_restores_owner) {
	fail_first_change = true;
	on_cleanup = [&] { session::detail::active = session::Id::Remote; };
	source.setOscType(OscType::SAMPLE);
	LONGS_EQUAL(1, changes);
	LONGS_EQUAL(0, creates);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(5, editors[1].currentMultiRangeIndex);
}
TEST(SourceTypeChange, destroyed_source_during_range_creation_does_not_update_editor) {
	auto owner = std::make_unique<Source>();
	editors[0].currentSource = owner.get();
	editors[0].currentMultiRangeIndex = 0;
	on_create = [&] { owner.reset(); };
	owner->setOscType(OscType::SAMPLE);
	POINTERS_EQUAL(&old_range, editors[0].currentMultiRange);
}
TEST(SourceTypeChange, retired_source_rejects_entry) {
	source.lifetime.retire();
	source.setOscType(OscType::SAMPLE);
	LONGS_EQUAL(0, changes);
	CHECK(source.oscType == OscType::SQUARE);
}
TEST(SourceTypeChange, same_size_and_dx_paths_keep_existing_behavior) {
	source.ranges.elementSize = sizeof(MultisampleRange);
	source.setOscType(OscType::SAMPLE);
	CHECK(source.oscType == OscType::SAMPLE);
	LONGS_EQUAL(0, changes);
	source.setOscType(OscType::DX7);
	CHECK(source.oscType == OscType::DX7);
	LONGS_EQUAL(1, dx_creates);
}
