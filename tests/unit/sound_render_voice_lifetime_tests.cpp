#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
namespace sound_render_voice_lifetime_test {
using q31_t = int32_t;
struct ModelStackWithSoundFlags {};
std::function<void()> on_render, on_release, on_destroy;
int rendered = 0, released = 0, destroyed = 0;
struct Voice {
	mutable deluge::lifetime::lifetime_source lifetime_;
	~Voice();
	deluge::lifetime::lifetime_watch watch_lifetime() const;
	void setAsUnassigned(ModelStackWithSoundFlags*) {
		deleted = true;
		auto watch = watch_lifetime();
		if (watch.alive()) {
			++released;
			if (on_release)
				on_release();
		}
		else {
			++destroyed;
			if (on_destroy)
				on_destroy();
		}
	}
	bool still_going = true, deleted = false;
	bool render(ModelStackWithSoundFlags*, q31_t*, size_t, bool, bool, int, bool, bool, int) {
		const bool result = still_going;
		++rendered;
		if (on_render)
			on_render();
		return result;
	}
	bool shouldBeDeleted() const { return deleted; }
};
#include "voice_owner_lifetime.inc"
struct Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	using ActiveVoice = std::unique_ptr<Voice>;
	std::vector<ActiveVoice> voices_;
	int sourcesChanged = 0;
	bool invertReversed = true;
	bool clear_voices(const deluge::lifetime::callback_validation&);
	void checkVoiceExists(const ActiveVoice& voice, const char*) { CHECK(voice != nullptr); }
	void freeActiveVoice(const ActiveVoice& voice, ModelStackWithSoundFlags*, bool erase) {
		CHECK_FALSE(erase);
		voice->deleted = true;
		++released;
		if (on_release)
			on_release();
	}
	bool process_render_voices(ModelStackWithSoundFlags*, std::span<q31_t>, bool, bool, bool, bool, int32_t,
	                           const deluge::lifetime::callback_validation*);
};
#include "sound_render_voice_lifetime.inc"
} // namespace sound_render_voice_lifetime_test
using namespace sound_render_voice_lifetime_test;
TEST_GROUP(sound_render_voice_lifetime) {
	std::unique_ptr<Sound> sound;
	ModelStackWithSoundFlags stack;
	bool context_valid = true;
	void reset() {
		sound = std::make_unique<Sound>();
		sound->voices_.push_back(std::make_unique<Voice>());
		sound->voices_.push_back(std::make_unique<Voice>());
		on_render = on_release = on_destroy = {};
		rendered = released = destroyed = 0;
		context_valid = true;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_render = on_release = on_destroy = {};
	}
	bool clear() {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive() && context_valid; };
		const deluge::lifetime::callback_validation validation{valid};
		return sound->clear_voices(validation);
	}
	bool render() {
		deluge::lifetime::lifetime_watch watch{sound->lifetime};
		const auto valid = [&] { return watch.alive() && context_valid; };
		const deluge::lifetime::callback_validation validation{valid};
		return sound->process_render_voices(&stack, {}, false, false, false, false, 0, &validation);
	}
};
TEST(sound_render_voice_lifetime, live_voices_render_and_finished_voices_are_removed) {
	sound->voices_[0]->still_going = false;
	CHECK(render());
	LONGS_EQUAL(2, rendered);
	LONGS_EQUAL(1, released);
	LONGS_EQUAL(1, sound->voices_.size());
}
TEST(sound_render_voice_lifetime, empty_batch_is_valid_and_retired_entry_does_no_work) {
	sound->voices_.clear();
	CHECK(render());
	sound->lifetime.retire();
	CHECK_FALSE(render());
	LONGS_EQUAL(0, rendered);
}
TEST(sound_render_voice_lifetime, destruction_during_render_or_release_stops_traversal) {
	for (bool during_release : {false, true}) {
		reset();
		sound->voices_[0]->still_going = false;
		auto destroy = [&] { sound.reset(); };
		if (during_release)
			on_release = destroy;
		else
			on_render = destroy;
		CHECK_FALSE(render());
		LONGS_EQUAL(1, rendered);
		LONGS_EQUAL(during_release ? 1 : 0, released);
	}
}
TEST(sound_render_voice_lifetime, vector_mutation_during_callbacks_cancels_before_reusing_cursor) {
	for (bool during_release : {false, true}) {
		for (int mutation = 0; mutation < 4; ++mutation) {
			reset();
			sound->voices_[0]->still_going = false;
			auto mutate = [&] {
				switch (mutation) {
				case 0:
					sound->voices_.clear();
					break;
				case 1:
					sound->voices_.erase(sound->voices_.begin());
					break;
				case 2:
					sound->voices_[0] = std::make_unique<Voice>();
					break;
				case 3:
					sound->voices_.reserve(sound->voices_.capacity() + 1);
					break;
				}
			};
			if (during_release)
				on_release = mutate;
			else
				on_render = mutate;
			CHECK_FALSE(render());
			LONGS_EQUAL(1, rendered);
		}
	}
}
TEST(sound_render_voice_lifetime, context_change_leaves_remaining_voices_untouched) {
	on_render = [&] { context_valid = false; };
	CHECK_FALSE(render());
	LONGS_EQUAL(1, rendered);
	LONGS_EQUAL(0, released);
	LONGS_EQUAL(2, sound->voices_.size());
}
TEST(sound_render_voice_lifetime, missing_voice_cancels_without_dereferencing_it) {
	sound->voices_[0].reset();
	CHECK_FALSE(render());
	LONGS_EQUAL(0, rendered);
}

TEST(sound_render_voice_lifetime, finished_voice_is_detached_before_destructor_callback) {
	sound->voices_[0]->still_going = false;
	on_destroy = [&] { LONGS_EQUAL(1, sound->voices_.size()); };
	CHECK(render());
	LONGS_EQUAL(1, destroyed);
	LONGS_EQUAL(1, sound->voices_.size());
}
TEST(sound_render_voice_lifetime, voice_destructor_can_destroy_sound_without_invalid_vector_access) {
	sound->voices_[0]->still_going = false;
	on_destroy = [&] {
		if (destroyed == 1)
			sound.reset();
	};
	CHECK_FALSE(render());
	CHECK(!sound);
	LONGS_EQUAL(2, destroyed);
}
TEST(sound_render_voice_lifetime, voice_destructor_mutation_preserves_replacement_batch) {
	sound->voices_[0]->still_going = false;
	on_destroy = [&] {
		if (destroyed == 1)
			sound->voices_.push_back(std::make_unique<Voice>());
	};
	CHECK_FALSE(render());
	LONGS_EQUAL(2, sound->voices_.size());
	LONGS_EQUAL(1, destroyed);
}
TEST(sound_render_voice_lifetime, cancelled_cleanup_preserves_remaining_finished_voice) {
	for (auto& voice : sound->voices_)
		voice->still_going = false;
	on_destroy = [&] { context_valid = false; };
	CHECK_FALSE(render());
	LONGS_EQUAL(1, destroyed);
	LONGS_EQUAL(1, sound->voices_.size());
	CHECK(sound->voices_[0]->deleted);
}

TEST(sound_render_voice_lifetime, destructor_removing_remaining_voice_cancels_cleanup) {
	sound->voices_[0]->still_going = false;
	on_destroy = [&] {
		if (destroyed == 1)
			sound->voices_[0].reset();
	};
	CHECK_FALSE(render());
	LONGS_EQUAL(2, destroyed);
	CHECK(!sound->voices_[0]);
}

TEST(sound_render_voice_lifetime, voice_is_retired_before_destructor_callback) {
	auto watch = sound->voices_[0]->watch_lifetime();
	on_destroy = [&] { CHECK_FALSE(watch.alive()); };
	sound->voices_[0].reset();
	CHECK_FALSE(watch.alive());
}
TEST(sound_render_voice_lifetime, same_address_reconstruction_cancels_render_or_release_batch) {
	for (bool during_release : {false, true}) {
		reset();
		auto* original = sound->voices_[0].get();
		original->still_going = false;
		auto replace = [&] {
			original->~Voice();
			new (original) Voice();
		};
		if (during_release)
			on_release = replace;
		else
			on_render = replace;
		CHECK_FALSE(render());
		POINTERS_EQUAL(original, sound->voices_[0].get());
		LONGS_EQUAL(1, rendered);
		LONGS_EQUAL(1, destroyed);
		CHECK_FALSE(sound->voices_[0]->deleted);
	}
}
TEST(sound_render_voice_lifetime, replacement_voice_has_a_new_live_watch) {
	auto* original = sound->voices_[0].get();
	auto old_watch = original->watch_lifetime();
	original->~Voice();
	new (original) Voice();
	auto new_watch = original->watch_lifetime();
	CHECK_FALSE(old_watch.alive());
	CHECK(new_watch.alive());
}
TEST(sound_render_voice_lifetime, retired_voice_is_not_dispatched) {
	sound->voices_[0]->lifetime_.retire();
	CHECK_FALSE(render());
	LONGS_EQUAL(0, rendered);
}

TEST(sound_render_voice_lifetime, clear_releases_then_detaches_every_voice) {
	on_destroy = [&] {
		LONGS_EQUAL(2, released);
		LONGS_EQUAL(2 - destroyed, sound->voices_.size());
	};
	CHECK(clear());
	LONGS_EQUAL(2, released);
	LONGS_EQUAL(2, destroyed);
	CHECK(sound->voices_.empty());
	CHECK_FALSE(sound->invertReversed);
}
TEST(sound_render_voice_lifetime, clear_owner_deletion_during_release_or_destruction_is_safe) {
	for (bool during_destruction : {false, true}) {
		reset();
		if (during_destruction)
			on_destroy = [&] {
				if (destroyed == 1)
					sound.reset();
			};
		else
			on_release = [&] { sound.reset(); };
		CHECK_FALSE(clear());
		CHECK(!sound);
	}
}
TEST(sound_render_voice_lifetime, clear_cancels_on_same_address_reconstruction) {
	auto* original = sound->voices_[0].get();
	on_release = [&] {
		original->~Voice();
		new (original) Voice();
	};
	CHECK_FALSE(clear());
	LONGS_EQUAL(1, released);
	LONGS_EQUAL(2, sound->voices_.size());
	CHECK_FALSE(sound->voices_[0]->deleted);
}
TEST(sound_render_voice_lifetime, clear_preserves_new_voices_from_destructor_callbacks) {
	on_destroy = [&] {
		if (destroyed == 1)
			sound->voices_.push_back(std::make_unique<Voice>());
	};
	CHECK_FALSE(clear());
	LONGS_EQUAL(2, sound->voices_.size());
	LONGS_EQUAL(1, destroyed);
	CHECK_FALSE(sound->voices_.back()->deleted);
}
TEST(sound_render_voice_lifetime, clear_rejects_entry_without_modifying_state) {
	context_valid = false;
	CHECK_FALSE(clear());
	CHECK(sound->invertReversed);
	LONGS_EQUAL(0, released);
}
TEST(sound_render_voice_lifetime, clear_cancels_when_release_reallocates_voice_vector) {
	on_release = [&] { sound->voices_.reserve(sound->voices_.capacity() + 1); };
	CHECK_FALSE(clear());
	LONGS_EQUAL(1, released);
	LONGS_EQUAL(0, destroyed);
}
