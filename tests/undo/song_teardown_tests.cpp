#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include <algorithm>
#include <functional>
#include <vector>

namespace song_teardown_test {
struct Clip {};
struct ClipArray {
	std::vector<Clip*> values;
	int32_t getNumElements() { return values.size(); }
	Clip* getClipAtIndex(int32_t index) { return values.at(index); }
	void delete_at_index_preserving_capacity(int32_t index) { values.erase(values.begin() + index); }
};
struct Output {};
using Instrument = Output;
static std::function<void()> on_service;
static std::function<void(Clip*)> on_delete;
static int service_calls;
static int deleted_clips;
static int remaining_cleanup_calls;
namespace AudioEngine {
void logAction(const char*) {
}
void routineWithClusterLoading() {
	++service_calls;
	auto callback = on_service;
	if (callback)
		callback();
}
} // namespace AudioEngine
struct Song {
	ClipArray sessionClips, arrangementOnlyClips;
	Output* firstOutput = nullptr;
	Instrument* firstHibernatingInstrument = nullptr;
	struct {
		Output* value = nullptr;
		Output*& head() { return value; }
	} undo_detached_outputs;
	~Song();
	bool contains_clip_for_undo(const Clip* clip);
	void deleteClipObject(Clip* clip, bool, InstrumentRemoval) {
		auto callback = on_delete;
		if (callback)
			callback(clip);
		delete clip;
		++deleted_clips;
	}
	void deleteAllBackedUpParamManagers(bool) { ++remaining_cleanup_calls; }
	void deleteAllOutputs(Output**) { ++remaining_cleanup_calls; }
	void deleteHibernatingMIDIInstrument() { ++remaining_cleanup_calls; }
};
#include "song_teardown_methods.inc"

TEST_GROUP(SongTeardown){void setup() override{on_service = {};
on_delete = {};
service_calls = deleted_clips = remaining_cleanup_calls = 0;
} // namespace song_teardown_test
void teardown() override {
	on_service = {};
	on_delete = {};
}
}
;

TEST(SongTeardown, clips_leave_membership_before_destruction_and_later_service_callbacks) {
	auto* song = new Song;
	for (int index = 0; index < 65; ++index) {
		song->sessionClips.values.push_back(new Clip);
		song->arrangementOnlyClips.values.push_back(new Clip);
	}
	std::vector<Clip*> retired;
	bool exposed_retired_clip = false;
	on_service = [&] {
		for (auto* clip : retired)
			exposed_retired_clip |= song->contains_clip_for_undo(clip);
	};
	on_delete = [&](Clip* clip) {
		exposed_retired_clip |= song->contains_clip_for_undo(clip);
		retired.push_back(clip);
	};
	delete song;
	CHECK_FALSE(exposed_retired_clip);
	LONGS_EQUAL(130, deleted_clips);
	LONGS_EQUAL(130, retired.size());
	LONGS_EQUAL(7, service_calls);
	LONGS_EQUAL(5, remaining_cleanup_calls);
}

TEST(SongTeardown, empty_song_finishes_remaining_cleanup) {
	delete new Song;
	LONGS_EQUAL(0, deleted_clips);
	LONGS_EQUAL(1, service_calls);
	LONGS_EQUAL(5, remaining_cleanup_calls);
}
TEST(SongTeardown, service_callback_can_empty_an_array_without_duplicate_deletion) {
	auto* song = new Song;
	for (int index = 0; index < 3; ++index) {
		song->sessionClips.values.push_back(new Clip);
		song->arrangementOnlyClips.values.push_back(new Clip);
	}
	bool emptied = false;
	bool exposed_retired_clip = false;
	on_service = [&] {
		if (emptied)
			return;
		emptied = true;
		while (!song->sessionClips.values.empty()) {
			auto* clip = song->sessionClips.values.back();
			song->sessionClips.values.pop_back();
			song->deleteClipObject(clip, true, InstrumentRemoval::NONE);
		}
	};
	on_delete = [&](Clip* clip) { exposed_retired_clip |= song->contains_clip_for_undo(clip); };
	delete song;
	CHECK(emptied);
	CHECK_FALSE(exposed_retired_clip);
	LONGS_EQUAL(6, deleted_clips);
	LONGS_EQUAL(3, service_calls);
	LONGS_EQUAL(5, remaining_cleanup_calls);
}

TEST(SongTeardown, clip_cleanup_callback_can_remove_remaining_clips_without_stale_iteration) {
	auto* song = new Song;
	for (int index = 0; index < 3; ++index)
		song->sessionClips.values.push_back(new Clip);
	bool nested = false;
	bool exposed_retired_clip = false;
	on_delete = [&](Clip* clip) {
		exposed_retired_clip |= song->contains_clip_for_undo(clip);
		if (nested)
			return;
		nested = true;
		while (!song->sessionClips.values.empty()) {
			auto* remaining = song->sessionClips.values.back();
			song->sessionClips.values.pop_back();
			song->deleteClipObject(remaining, true, InstrumentRemoval::NONE);
		}
	};
	delete song;
	CHECK(nested);
	CHECK_FALSE(exposed_retired_clip);
	LONGS_EQUAL(3, deleted_clips);
	LONGS_EQUAL(2, service_calls);
	LONGS_EQUAL(5, remaining_cleanup_calls);
}
} // namespace song_teardown_test
