#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kit_unique_name_test {
enum class Error { NONE, INSUFFICIENT_RAM, ABORTED_BY_USER };
std::function<void()> on_append;
Error append_error = Error::NONE;
int appends = 0, lookups = 0;
struct String {
	std::string value = "REC";
	int getLength() const { return value.size(); }
	const char* get() const { return value.c_str(); }
	Error concatenateAtPos(const char* suffix, int position) {
		++appends;
		if (auto callback = std::exchange(on_append, {}))
			callback();
		if (append_error != Error::NONE)
			return append_error;
		value.resize(position);
		value += suffix;
		return Error::NONE;
	}
};
void intToString(int value, char* buffer) {
	std::snprintf(buffer, 12, "%d", value);
}
struct Kit {
	deluge::lifetime::lifetime_source lifetime;
	std::vector<std::string> names;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	bool getDrumFromName(const char* name) {
		++lookups;
		for (const auto& entry : names)
			if (entry == name)
				return true;
		return false;
	}
	Error makeDrumNameUnique(String*, int32_t);
};
#define D_PRINTLN(...) ((void)0)
#include "kit_unique_name.inc"
#undef D_PRINTLN
} // namespace kit_unique_name_test
using namespace kit_unique_name_test;
TEST_GROUP(KitUniqueName) {
	String name;
	void setup() override {
		on_append = {};
		append_error = Error::NONE;
		appends = lookups = 0;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void teardown() override {
		on_append = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
};
TEST(KitUniqueName, skips_existing_suffixes) {
	Kit kit;
	kit.names = {"REC1", "REC2"};
	CHECK(kit.makeDrumNameUnique(&name, 1) == Error::NONE);
	STRCMP_EQUAL("REC3", name.get());
	LONGS_EQUAL(3, lookups);
}
TEST(KitUniqueName, append_failure_returns_without_scanning_kit) {
	Kit kit;
	append_error = Error::INSUFFICIENT_RAM;
	CHECK(kit.makeDrumNameUnique(&name, 1) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, lookups);
}
TEST(KitUniqueName, actual_kit_destruction_during_append_cancels_lookup) {
	auto kit = std::make_unique<Kit>();
	on_append = [&] { kit.reset(); };
	CHECK(kit->makeDrumNameUnique(&name, 1) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, lookups);
}
TEST(KitUniqueName, same_address_reconstruction_cannot_revive_naming) {
	std::optional<Kit> kit(std::in_place);
	auto* original = &*kit;
	on_append = [&] {
		kit.reset();
		kit.emplace();
		POINTERS_EQUAL(original, &*kit);
	};
	CHECK(original->makeDrumNameUnique(&name, 1) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, lookups);
}
TEST(KitUniqueName, changed_panel_cancels_lookup) {
	Kit kit;
	on_append = [&] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	CHECK(kit.makeDrumNameUnique(&name, 1) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, lookups);
}
TEST(KitUniqueName, retired_kit_does_not_allocate_name) {
	Kit kit;
	kit.lifetime.retire();
	CHECK(kit.makeDrumNameUnique(&name, 1) == Error::ABORTED_BY_USER);
	LONGS_EQUAL(0, appends);
}
