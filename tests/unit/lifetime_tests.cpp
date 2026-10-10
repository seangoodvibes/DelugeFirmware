#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
using deluge::lifetime::lifetime_source;
using deluge::lifetime::lifetime_watch;
static_assert(!std::is_copy_constructible_v<lifetime_source>);
static_assert(!std::is_move_constructible_v<lifetime_source>);
static_assert(!std::is_copy_constructible_v<lifetime_watch>);
static_assert(!std::is_move_constructible_v<lifetime_watch>);
TEST_GROUP(LifetimeWatch){};
TEST(LifetimeWatch, retirement_invalidates_existing_and_future_watches) {
	lifetime_source source;
	lifetime_watch first(source), second(source);
	CHECK(first.alive());
	CHECK(second.alive());
	source.retire();
	CHECK_FALSE(first.alive());
	CHECK_FALSE(second.alive());
	lifetime_watch late(source);
	CHECK_FALSE(late.alive());
	source.retire();
}
TEST(LifetimeWatch, watch_can_outlive_destroyed_source) {
	std::optional<lifetime_source> source(std::in_place);
	lifetime_watch watch(*source);
	source.reset();
	CHECK_FALSE(watch.alive());
}
TEST(LifetimeWatch, unlinking_middle_and_head_preserves_other_watches) {
	lifetime_source source;
	lifetime_watch first(source);
	std::optional<lifetime_watch> middle(std::in_place, source);
	std::optional<lifetime_watch> head(std::in_place, source);
	middle.reset();
	head.reset();
	CHECK(first.alive());
	lifetime_watch newest(source);
	source.retire();
	CHECK_FALSE(first.alive());
	CHECK_FALSE(newest.alive());
}
TEST(LifetimeWatch, address_reuse_does_not_revive_old_watch) {
	alignas(lifetime_source) std::byte storage[sizeof(lifetime_source)];
	auto* source = std::construct_at(reinterpret_cast<lifetime_source*>(storage));
	std::optional<lifetime_watch> old_watch(std::in_place, *source);
	std::destroy_at(source);
	source = std::construct_at(reinterpret_cast<lifetime_source*>(storage));
	lifetime_watch new_watch(*source);
	CHECK_FALSE(old_watch->alive());
	CHECK(new_watch.alive());
	old_watch.reset();
	CHECK(new_watch.alive());
	std::destroy_at(source);
	CHECK_FALSE(new_watch.alive());
}

TEST(LifetimeWatch, retirement_reports_only_the_first_transition) {
	lifetime_source source;
	CHECK(source.retire());
	CHECK_FALSE(source.retire());
}

TEST(LifetimeWatch, empty_watch_is_not_alive) {
	lifetime_watch watch;
	CHECK_FALSE(watch.alive());
}
