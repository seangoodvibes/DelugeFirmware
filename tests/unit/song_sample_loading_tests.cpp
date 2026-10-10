#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <array>
#include <functional>
#include <memory>
#include <new>
#include <vector>

namespace song_sample_loading_test {
namespace session = deluge::gui::ui_session;
enum class ClipType { AUDIO, INSTRUMENT };
struct Clip {
	deluge::lifetime::lifetime_source lifetime;
	ClipType type = ClipType::AUDIO;
	bool active = true;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	bool isActiveOnOutput() { return active; }
};
int sample_loads = 0, output_loads = 0, audio_yields = 0;
std::function<void()> on_sample, on_output, on_audio;
struct AudioClip : Clip {
	void loadSample(bool) {
		++sample_loads;
		if (on_sample)
			on_sample();
	}
};
struct Output {
	deluge::lifetime::lifetime_source lifetime;
	Output* next = nullptr;
	Clip* active_clip = nullptr;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	Clip* getActiveClip() { return active_clip; }
	void loadAllAudioFiles(bool) {
		++output_loads;
		if (on_output)
			on_output();
	}
	void loadCrucialAudioFilesOnly() { loadAllAudioFiles(true); }
};
struct ClipArray {
	std::vector<Clip*> clips;
	int32_t getNumElements() { return clips.size(); }
	Clip* getClipAtIndex(int32_t index) { return clips.at(index); }
};
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	Output* firstOutput = nullptr;
	ClipArray sessionClips, arrangementOnlyClips;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	bool owns_output_for_undo(Output* output, bool) {
		for (auto* candidate = firstOutput; candidate; candidate = candidate->next)
			if (candidate == output)
				return true;
		return false;
	}
	bool isClipActive(Clip* clip) { return clip->active; }
	void loadAllSamples(bool);
	void loadCrucialSamplesOnly();
};
Song* currentSong = nullptr;
namespace AudioEngine {
void logAction(const char*) {
}
void routineWithClusterLoading() {
	++audio_yields;
	if (on_audio)
		on_audio();
}
} // namespace AudioEngine
#include "song_sample_loading.inc"
} // namespace song_sample_loading_test
using namespace song_sample_loading_test;
TEST_GROUP(SongSampleLoading) {
	Song song;
	Output first, second;
	AudioClip clip, other_clip;
	void setup() override {
		currentSong = &song;
		song.firstOutput = &first;
		first.next = &second;
		first.active_clip = &clip;
		second.active_clip = &other_clip;
		song.sessionClips.clips = {&clip};
		song.arrangementOnlyClips.clips = {&other_clip};
		sample_loads = output_loads = audio_yields = 0;
		on_sample = on_output = on_audio = {};
		session::detail::active = session::Id::Local;
	}
	void teardown() override {
		currentSong = nullptr;
		on_sample = on_output = on_audio = {};
		session::detail::active = session::Id::Local;
	}
	void load(bool crucial) {
		if (crucial)
			song.loadCrucialSamplesOnly();
		else
			song.loadAllSamples(false);
	}
};

TEST(SongSampleLoading, output_callback_retirement_stops_remaining_loads) {
	on_output = [&] { song.lifetime.retire(); };
	song.loadAllSamples(false);
	LONGS_EQUAL(1, output_loads);
	LONGS_EQUAL(0, sample_loads);
}

TEST(SongSampleLoading, crucial_output_removal_stops_before_following_detached_link) {
	on_output = [&] { song.firstOutput = &second; };
	song.loadCrucialSamplesOnly();
	LONGS_EQUAL(1, output_loads);
	LONGS_EQUAL(0, sample_loads);
}

TEST(SongSampleLoading, audio_yield_retiring_clip_stops_before_loading_it) {
	on_audio = [&] { clip.lifetime.retire(); };
	song.loadAllSamples(false);
	LONGS_EQUAL(0, sample_loads);
}

TEST(SongSampleLoading, clip_list_resize_during_sample_load_cancels_remaining_scan) {
	on_sample = [&] { song.arrangementOnlyClips.clips.clear(); };
	song.loadAllSamples(true);
	LONGS_EQUAL(1, sample_loads);
}

TEST(SongSampleLoading, successful_load_preserves_audio_cadence_and_filters) {
	song.loadAllSamples(false);
	LONGS_EQUAL(2, output_loads);
	LONGS_EQUAL(2, sample_loads);
	LONGS_EQUAL(1, audio_yields);
	sample_loads = output_loads = audio_yields = 0;
	clip.active = false;
	song.loadCrucialSamplesOnly();
	LONGS_EQUAL(1, output_loads);
	LONGS_EQUAL(1, sample_loads);
	LONGS_EQUAL(0, audio_yields);
}

TEST(SongSampleLoading, output_destruction_during_load_does_not_read_freed_next_pointer) {
	for (bool crucial : {false, true}) {
		auto output = std::make_unique<Output>();
		output->next = &second;
		output->active_clip = &clip;
		song.firstOutput = output.get();
		on_output = [&] {
			song.firstOutput = &second;
			output.reset();
		};
		const int previous_loads = output_loads;
		load(crucial);
		LONGS_EQUAL(previous_loads + 1, output_loads);
		LONGS_EQUAL(0, sample_loads);
	}
}

TEST(SongSampleLoading, clip_destruction_during_audio_yield_does_not_read_freed_type) {
	auto owned_clip = std::make_unique<AudioClip>();
	song.sessionClips.clips = {owned_clip.get()};
	on_audio = [&] {
		song.sessionClips.clips.clear();
		owned_clip.reset();
	};
	song.loadAllSamples(false);
	LONGS_EQUAL(0, sample_loads);
}

TEST(SongSampleLoading, sample_callback_destruction_cancels_both_scan_modes) {
	for (bool crucial : {false, true}) {
		auto owned_clip = std::make_unique<AudioClip>();
		song.sessionClips.clips = {owned_clip.get()};
		on_sample = [&] {
			song.sessionClips.clips.clear();
			owned_clip.reset();
		};
		const int previous_loads = sample_loads;
		load(crucial);
		LONGS_EQUAL(previous_loads + 1, sample_loads);
	}
}

TEST(SongSampleLoading, output_callback_same_address_song_reuse_stops_both_scan_modes) {
	for (bool crucial : {false, true}) {
		song.firstOutput = &first;
		on_output = [&] {
			song.~Song();
			new (&song) Song;
		};
		const int previous_loads = output_loads;
		load(crucial);
		LONGS_EQUAL(previous_loads + 1, output_loads);
		LONGS_EQUAL(0, sample_loads);
	}
}

TEST(SongSampleLoading, detached_song_loading_cancels_if_current_song_is_reused) {
	Song active_song;
	currentSong = &active_song;
	on_output = [&] {
		active_song.~Song();
		new (&active_song) Song;
	};
	song.loadAllSamples(true);
	LONGS_EQUAL(1, output_loads);
	LONGS_EQUAL(0, sample_loads);
}

TEST(SongSampleLoading, output_callback_owner_change_cancels_and_restores_source_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (bool crucial : {false, true}) {
			on_output = [=] {
				session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
			};
			const int previous_loads = output_loads;
			load(crucial);
			CHECK(session::current() == owner);
			LONGS_EQUAL(previous_loads + 1, output_loads);
			LONGS_EQUAL(0, sample_loads);
		}
	}
}

TEST(SongSampleLoading, retired_song_does_not_begin_loading) {
	song.lifetime.retire();
	song.loadAllSamples(true);
	song.loadCrucialSamplesOnly();
	LONGS_EQUAL(0, output_loads);
	LONGS_EQUAL(0, sample_loads);
}

TEST(SongSampleLoading, no_current_song_can_load_detached_song) {
	currentSong = nullptr;
	song.loadAllSamples(true);
	LONGS_EQUAL(2, output_loads);
	LONGS_EQUAL(2, sample_loads);
}

TEST(SongSampleLoading, same_size_clip_replacement_cancels_both_scan_modes) {
	for (bool crucial : {false, true}) {
		song.sessionClips.clips = {&clip};
		on_sample = [&] { song.sessionClips.clips[0] = &other_clip; };
		const int previous_loads = sample_loads;
		load(crucial);
		LONGS_EQUAL(previous_loads + 1, sample_loads);
	}
}

TEST(SongSampleLoading, instrument_only_scan_still_services_audio_every_eight_clips) {
	std::array<Clip, 17> clips;
	song.sessionClips.clips.clear();
	song.arrangementOnlyClips.clips.clear();
	for (auto& item : clips) {
		item.type = ClipType::INSTRUMENT;
		song.sessionClips.clips.push_back(&item);
	}
	song.loadAllSamples(false);
	LONGS_EQUAL(3, audio_yields);
	LONGS_EQUAL(0, sample_loads);
}
