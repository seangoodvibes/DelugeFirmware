#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>

namespace sound_hibernation_test {
namespace session = deluge::gui::ui_session;
constexpr bool ALPHA_OR_BETA_VERSION = true;
int effects = 0, kills = 0, arp_resets = 0, reassessments = 0, detachments = 0, freezes = 0;
std::function<void()> on_effects, on_kill, on_reassess;
bool detachment_success = true;
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
Song* currentSong = nullptr;
enum class EnvelopeStage { ON, OFF };
struct ModControllableAudio {
	void wontBeRenderedForAWhile() {
		++effects;
		if (on_effects)
			on_effects();
	}
};
struct Sound : ModControllableAudio {
	deluge::lifetime::lifetime_source lifetime;
	bool skippingRendering = false, active_voices = false;
	bool hasActiveVoices() const { return active_voices; }
	struct {
		EnvelopeStage status = EnvelopeStage::ON;
	} sidechain;
	struct Arp {
		void reset() { ++arp_resets; }
	} arp;
	auto watch_sound_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	void killAllVoices() {
		++kills;
		if (on_kill)
			on_kill();
	}
	Arp* getArp() { return &arp; }
	void reassessRenderSkippingStatus(void*, bool) {
		++reassessments;
		skippingRendering = true;
		if (on_reassess)
			on_reassess();
	}
	bool detachSourcesFromAudioFiles(const deluge::lifetime::callback_validation* validation = nullptr) {
		if (validation && !validation->valid())
			return false;
		++detachments;
		return detachment_success;
	}
	void prepareForHibernation();
	void wontBeRenderedForAWhile();
	bool stop_rendering_for_hibernation(const deluge::lifetime::callback_validation* = nullptr);
	bool try_prepare_for_hibernation(const deluge::lifetime::callback_validation* = nullptr);
};
void FREEZE_WITH_ERROR(const char*) {
	++freezes;
}
#include "sound_hibernation.inc"
} // namespace sound_hibernation_test
using namespace sound_hibernation_test;
TEST_GROUP(SoundHibernation) {
	Sound sound;
	Song song;
	void setup() override {
		currentSong = &song;
		session::detail::active = session::Id::Local;
		effects = kills = arp_resets = reassessments = detachments = freezes = 0;
		detachment_success = true;
		on_effects = on_kill = on_reassess = {};
	}
	void teardown() override {
		on_effects = on_kill = on_reassess = {};
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
	}
};
TEST(SoundHibernation, destroyed_sound_during_voice_cleanup_stops_hibernation) {
	auto owner = std::make_unique<Sound>();
	on_kill = [&] { owner.reset(); };
	owner->prepareForHibernation();
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(0, arp_resets);
	LONGS_EQUAL(0, detachments);
}
TEST(SoundHibernation, destroyed_sound_during_effect_cleanup_skips_voice_cleanup) {
	auto owner = std::make_unique<Sound>();
	on_effects = [&] { owner.reset(); };
	owner->prepareForHibernation();
	LONGS_EQUAL(1, effects);
	LONGS_EQUAL(0, kills);
	LONGS_EQUAL(0, detachments);
}
TEST(SoundHibernation, destroyed_sound_during_render_status_callback_skips_post_callback_access) {
	auto owner = std::make_unique<Sound>();
	on_reassess = [&] { owner.reset(); };
	owner->prepareForHibernation();
	LONGS_EQUAL(1, reassessments);
	LONGS_EQUAL(0, freezes);
	LONGS_EQUAL(0, detachments);
}
TEST(SoundHibernation, retired_song_cancels_later_stages) {
	on_kill = [&] { song.lifetime.retire(); };
	sound.prepareForHibernation();
	LONGS_EQUAL(0, arp_resets);
	LONGS_EQUAL(0, detachments);
}
TEST(SoundHibernation, changed_panel_cancels_detachment_and_restores_owner) {
	on_kill = [&] { session::detail::active = session::Id::Remote; };
	sound.prepareForHibernation();
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, detachments);
	LONGS_EQUAL(0, arp_resets);
}
TEST(SoundHibernation, retired_sound_does_not_start_cleanup) {
	sound.lifetime.retire();
	sound.prepareForHibernation();
	LONGS_EQUAL(0, effects);
	LONGS_EQUAL(0, kills);
	LONGS_EQUAL(0, detachments);
}
TEST(SoundHibernation, normal_hibernation_completes_all_stages) {
	sound.prepareForHibernation();
	LONGS_EQUAL(1, effects);
	LONGS_EQUAL(1, kills);
	LONGS_EQUAL(1, arp_resets);
	LONGS_EQUAL(1, reassessments);
	LONGS_EQUAL(1, detachments);
	LONGS_EQUAL(0, freezes);
	CHECK(sound.sidechain.status == EnvelopeStage::OFF);
}
TEST(SoundHibernation, render_idle_without_hibernation_keeps_samples_attached) {
	currentSong = nullptr;
	sound.wontBeRenderedForAWhile();
	LONGS_EQUAL(1, effects);
	LONGS_EQUAL(1, reassessments);
	LONGS_EQUAL(0, detachments);
}

TEST(SoundHibernation, nested_voices_cancel_hibernation_before_arp_reset_and_detachment) {
	on_kill = [&] { sound.active_voices = true; };
	sound.prepareForHibernation();
	CHECK(sound.hasActiveVoices());
	LONGS_EQUAL(0, arp_resets);
	LONGS_EQUAL(0, reassessments);
	LONGS_EQUAL(0, detachments);
}

TEST(SoundHibernation, caller_cancellation_stops_hibernation_before_later_stages) {
	bool valid = true;
	auto check = [&] { return valid; };
	deluge::lifetime::callback_validation validation(check);
	on_kill = [&] { valid = false; };
	CHECK_FALSE(sound.try_prepare_for_hibernation(&validation));
	LONGS_EQUAL(0, arp_resets);
	LONGS_EQUAL(0, detachments);
}
TEST(SoundHibernation, detachment_failure_is_reported_to_outer_owner) {
	detachment_success = false;
	CHECK_FALSE(sound.try_prepare_for_hibernation());
	LONGS_EQUAL(1, detachments);
}
