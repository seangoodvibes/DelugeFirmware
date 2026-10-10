#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace song_owner_lifetime_test {
struct Clip {};
struct Output {};
using Instrument = Output;
enum class InstrumentRemoval { NONE };
std::function<void()> on_cleanup;
int deleted_clips, output_lists, backup_cleanups, midi_cleanups, services;
void cleanup_callback() {
	if (on_cleanup)
		on_cleanup();
}
namespace AudioEngine {
void routineWithClusterLoading() {
	++services;
	cleanup_callback();
}
void logAction(const char*) {
	cleanup_callback();
}
} // namespace AudioEngine
struct ClipArray {
	std::vector<Clip*> clips;
	int getNumElements() { return clips.size(); }
	Clip* getClipAtIndex(int index) { return clips.at(index); }
	void delete_at_index_preserving_capacity(int index) { clips.erase(clips.begin() + index); }
};
struct Song {
	mutable deluge::lifetime::lifetime_source lifetime_;
	ClipArray sessionClips, arrangementOnlyClips;
	Output* firstOutput = nullptr;
	Instrument* firstHibernatingInstrument = nullptr;
	struct {
		Output* value = nullptr;
		Output*& head() { return value; }
	} undo_detached_outputs;
	~Song();
	deluge::lifetime::lifetime_watch watch_lifetime() const;
	void deleteClipObject(Clip* clip, bool deleting_song, InstrumentRemoval) {
		CHECK(deleting_song);
		++deleted_clips;
		delete clip;
		cleanup_callback();
	}
	void deleteAllBackedUpParamManagers(bool empty) {
		CHECK_FALSE(empty);
		++backup_cleanups;
		cleanup_callback();
	}
	void deleteAllOutputs(Output**) {
		++output_lists;
		cleanup_callback();
	}
	void deleteHibernatingMIDIInstrument() {
		++midi_cleanups;
		cleanup_callback();
	}
};
#include "song_owner_lifetime.inc"
} // namespace song_owner_lifetime_test
using namespace song_owner_lifetime_test;
TEST_GROUP(song_owner_lifetime){
    void setup() override{deleted_clips = output_lists = backup_cleanups = midi_cleanups = services = 0;
on_cleanup = {};
}
void teardown() override {
	on_cleanup = {};
}
}
;
TEST(song_owner_lifetime, retires_before_every_teardown_callback) {
	auto song = std::make_unique<Song>();
	song->sessionClips.clips.push_back(new Clip);
	song->arrangementOnlyClips.clips.push_back(new Clip);
	auto* raw_song = song.get();
	auto watch = song->watch_lifetime();
	CHECK(watch.alive());
	on_cleanup = [&] {
		CHECK_FALSE(watch.alive());
		auto new_watch = raw_song->watch_lifetime();
		CHECK_FALSE(new_watch.alive());
	};
	song.reset();
	CHECK_FALSE(watch.alive());
	LONGS_EQUAL(2, deleted_clips);
	LONGS_EQUAL(3, services);
	LONGS_EQUAL(3, output_lists);
	LONGS_EQUAL(1, backup_cleanups);
	LONGS_EQUAL(1, midi_cleanups);
}
TEST(song_owner_lifetime, same_address_reconstruction_does_not_revive_old_watch) {
	alignas(Song) unsigned char storage[sizeof(Song)];
	auto* song = new (storage) Song;
	auto old_watch = song->watch_lifetime();
	song->~Song();
	CHECK_FALSE(old_watch.alive());
	song = new (storage) Song;
	auto new_watch = song->watch_lifetime();
	CHECK(new_watch.alive());
	CHECK_FALSE(old_watch.alive());
	song->~Song();
	CHECK_FALSE(new_watch.alive());
}
TEST(song_owner_lifetime, watchers_can_leave_before_song_destruction) {
	Song song;
	{
		auto first = song.watch_lifetime();
		auto second = song.watch_lifetime();
		CHECK(first.alive());
		CHECK(second.alive());
	}
	auto remaining = song.watch_lifetime();
	CHECK(remaining.alive());
}
