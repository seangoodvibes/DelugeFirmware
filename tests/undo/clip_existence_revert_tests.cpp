#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_navigation_state.h"
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace clip_existence_revert_test {
namespace util {
template <typename T>
constexpr auto to_underlying(T value) {
	return static_cast<std::underlying_type_t<T>>(value);
}
} // namespace util
struct Clip;
struct ModelStack;
struct ModelStackWithTimelineCounter;
static int target_accesses;
static std::function<void(int)> on_activation;
static int activation_calls;
static void activate(int stage) {
	++activation_calls;
	auto callback = on_activation;
	if (callback)
		callback(stage);
}
static int preparation_calls;
static Error preparation_error;
struct Output {
	Clip* getActiveClip() {
		++target_accesses;
		return nullptr;
	}
	void setActiveClip(ModelStackWithTimelineCounter*) {
		++target_accesses;
		activate(1);
	}
	void pickAnActiveClipIfPossible(ModelStack*) { ++target_accesses; }
};
struct Clip {
	Output* output = nullptr;
	bool activeIfNoSolo = false;
	ArmState armState = ArmState::OFF;
	bool isActiveOnOutput() {
		++target_accesses;
		return false;
	}
	void expectNoFurtherTicks(void*) { ++target_accesses; }
	void detachFromOutput(ModelStackWithTimelineCounter*, bool, bool, bool) { ++target_accesses; }
};
struct ClipArray {
	void deleteAtIndex(int) { ++target_accesses; }
};
struct Song {
	ClipArray sessionClips, arrangementOnlyClips;
	Clip* registered_clip = nullptr;
	Output* registered_output = nullptr;
	bool owns_output_for_undo(Output* output) { return registered_output == output; }
	int get_clip_index_for_undo(ClipArray*, Clip* target) {
		++target_accesses;
		return target == registered_clip ? 0 : -1;
	}
	void notify_peer_clip_inserted(int) {
		++target_accesses;
		deluge::gui::ui_session::request_peer_structural_refresh();
	}
	void notify_peer_clip_removed(int) { ++target_accesses; }
	void removeSessionClipLowLevel(Clip*, int) { ++target_accesses; }
};
static Song* currentSong;
struct ModelStackWithTimelineCounter {
	Song* song;
	Clip* target;
	Clip* getTimelineCounterAllowNull() { return target; }
};
struct ModelStack {
	Song* song;
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip* target) {
		++target_accesses;
		timeline.song = song;
		timeline.target = target;
		return &timeline;
	}
};
static struct {
	bool playbackState = false;
	bool isEitherClockActive() { return false; }
} playbackHandler;
static int arrangement;
static int* currentPlaybackMode = &arrangement;
static struct {
	void toggleClipStatus(Clip*, int*, bool, int) {
		++target_accesses;
		activate(0);
	}
} session;
#define D_PRINTLN(...) ((void)0)
struct ConsequenceClipExistence {
	Clip* clip = nullptr;
	ClipArray* clipArray = nullptr;
	int clipIndex = 0;
	ExistenceChangeType type = ExistenceChangeType::DELETE;
	bool shouldBeActiveWhileExistent = false;
	bool owns_detached_clip = false;
	Error prepare_for_deletion(ModelStackWithTimelineCounter*) {
		++preparation_calls;
		return preparation_error;
	}
	Error reserve_for_recreation(Song*) {
		++preparation_calls;
		return preparation_error;
	}
	Error reattach_for_recreation(ModelStackWithTimelineCounter*) {
		++preparation_calls;
		return preparation_error;
	}
	Error commit_recreation(Song*) {
		++preparation_calls;
		return preparation_error;
	}
	Error revert(TimeType time, ModelStack* modelStack);
};
#undef ALPHA_OR_BETA_VERSION
#define ALPHA_OR_BETA_VERSION 0
#include "clip_existence_revert.inc"
#undef D_PRINTLN

TEST_GROUP(ClipExistenceRevert) {
	Song song;
	Clip clip;
	Output output;
	ModelStack stack;
	ConsequenceClipExistence consequence;
	void setup() override {
		currentSong = &song;
		song.registered_clip = &clip;
		song.registered_output = &output;
		clip.output = &output;
		on_activation = {};
		activation_calls = 0;
		stack.song = &song;
		consequence.clip = &clip;
		consequence.clipArray = &song.sessionClips;
		target_accesses = preparation_calls = 0;
		preparation_error = Error::INSUFFICIENT_RAM;
	}
	void teardown() override {
		currentSong = nullptr;
	}
};

TEST(ClipExistenceRevert, stale_song_is_rejected_before_array_or_timeline_access_in_both_directions) {
	Song replacement;
	currentSong = &replacement;
	for (auto time : {BEFORE, AFTER}) {
		CHECK(consequence.revert(time, &stack) == Error::BUG);
		LONGS_EQUAL(0, target_accesses);
		LONGS_EQUAL(0, preparation_calls);
	}
}

TEST(ClipExistenceRevert, missing_song_is_rejected_before_array_or_timeline_access_in_both_directions) {
	currentSong = nullptr;
	for (auto time : {BEFORE, AFTER}) {
		CHECK(consequence.revert(time, &stack) == Error::BUG);
		LONGS_EQUAL(0, target_accesses);
		LONGS_EQUAL(0, preparation_calls);
	}
}

TEST(ClipExistenceRevert, preparation_failure_does_not_detach_or_claim_clip) {
	CHECK(consequence.revert(AFTER, &stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(2, target_accesses); // Membership preflight and timeline construction only.
	LONGS_EQUAL(1, preparation_calls);
	CHECK_FALSE(consequence.owns_detached_clip);
}

TEST(ClipExistenceRevert, reservation_failure_does_not_reattach_or_publish_clip) {
	CHECK(consequence.revert(BEFORE, &stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, target_accesses); // Timeline construction only.
	LONGS_EQUAL(1, preparation_calls);
}
TEST(ClipExistenceRevert, recreated_clip_activation_completes_in_live_context) {
	preparation_error = Error::NONE;
	consequence.shouldBeActiveWhileExistent = true;
	CHECK(consequence.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(2, activation_calls);
}

TEST(ClipExistenceRevert, destroyed_clip_at_each_activation_boundary_stops_further_access) {
	preparation_error = Error::NONE;
	consequence.shouldBeActiveWhileExistent = true;
	for (int boundary = 0; boundary < 2; ++boundary) {
		auto target = std::make_unique<Clip>();
		target->output = &output;
		song.registered_clip = target.get();
		consequence.clip = target.get();
		activation_calls = 0;
		on_activation = [&](int stage) {
			if (stage == boundary) {
				song.registered_clip = nullptr;
				target.reset();
			}
		};
		CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
		LONGS_EQUAL(boundary + 1, activation_calls);
		CHECK_FALSE(target);
	}
}

TEST(ClipExistenceRevert, changed_activation_context_is_rejected_at_each_boundary) {
	using namespace deluge::gui::ui_session;
	preparation_error = Error::NONE;
	consequence.shouldBeActiveWhileExistent = true;
	Song replacement;
	Clip other_clip;
	Output other_output;
	for (int boundary = 0; boundary < 2; ++boundary) {
		for (int change = 0; change < 10; ++change) {
			currentSong = &song;
			stack.song = &song;
			consequence.clip = &clip;
			consequence.clipArray = &song.sessionClips;
			clip.output = &output;
			song.registered_output = &output;
			activation_calls = 0;
			on_activation = [&](int stage) {
				if (stage != boundary)
					return;
				switch (change) {
				case 0:
					currentSong = &replacement;
					break;
				case 1:
					stack.timeline.song = &replacement;
					break;
				case 2:
					stack.timeline.target = &other_clip;
					break;
				case 3:
					song.registered_output = nullptr;
					break;
				case 4:
					clip.output = &other_output;
					break;
				case 5:
					navigation.for_owner(Id::Local).structural_refresh.request();
					break;
				case 6:
					navigation.for_owner(Id::Remote).structural_refresh.request();
					break;
				case 7:
					stack.song = &replacement;
					break;
				case 8:
					consequence.clip = &other_clip;
					break;
				case 9:
					consequence.clipArray = &song.arrangementOnlyClips;
					break;
				}
			};
			CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
			LONGS_EQUAL(boundary + 1, activation_calls);
		}
	}
}
TEST(ClipExistenceRevert, activation_owner_change_is_rejected_but_returned_scope_is_valid) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	preparation_error = Error::NONE;
	consequence.shouldBeActiveWhileExistent = true;
	for (int boundary = 0; boundary < 2; ++boundary) {
		std::optional<Scope> remote;
		activation_calls = 0;
		on_activation = [&](int stage) {
			if (stage == boundary)
				remote.emplace(Id::Remote);
		};
		CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
		LONGS_EQUAL(boundary + 1, activation_calls);
		on_activation = {};
	}
	on_activation = [](int) { Scope nested(Id::Remote); };
	activation_calls = 0;
	CHECK(consequence.revert(BEFORE, &stack) == Error::NONE);
	LONGS_EQUAL(2, activation_calls);
}

TEST(ClipExistenceRevert, destroyed_output_at_each_activation_boundary_stops_further_access) {
	preparation_error = Error::NONE;
	consequence.shouldBeActiveWhileExistent = true;
	for (int boundary = 0; boundary < 2; ++boundary) {
		auto target_output = std::make_unique<Output>();
		clip.output = target_output.get();
		song.registered_output = target_output.get();
		activation_calls = 0;
		on_activation = [&](int stage) {
			if (stage == boundary) {
				song.registered_output = nullptr;
				target_output.reset();
			}
		};
		CHECK(consequence.revert(BEFORE, &stack) == Error::BUG);
		LONGS_EQUAL(boundary + 1, activation_calls);
		CHECK_FALSE(target_output);
	}
}
} // namespace clip_existence_revert_test
