#include "CppUTest/TestHarness.h"
#include "util/lifetime.h"
#include <functional>
#include <optional>
namespace drum_destruction_lifetime_test {
std::function<void()> on_cleanup;
struct member_fixture {
	~member_fixture() {
		if (on_cleanup)
			on_cleanup();
	}
};
struct Drum {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	member_fixture member;
	virtual ~Drum();
	void retire_lifetime() { lifetime_source.retire(); }
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
};
struct SoundDrum : Drum {
	member_fixture member;
	~SoundDrum() override;
};
struct MIDIDrum : Drum {
	member_fixture member;
	~MIDIDrum() override;
};
struct GateDrum : Drum {
	member_fixture member;
	~GateDrum() override;
};
#include "drum_destruction_lifetime.inc"
#include "gate_drum_destruction_lifetime.inc"
#include "midi_drum_destruction_lifetime.inc"
#include "sound_drum_destruction_lifetime.inc"
template <class T>
void check_retirement() {
	std::optional<T> drum(std::in_place);
	auto watch = drum->watch_lifetime();
	int callbacks = 0;
	on_cleanup = [&] {
		++callbacks;
		CHECK_FALSE(watch.alive());
	};
	drum.reset();
	CHECK_FALSE(watch.alive());
	CHECK(callbacks > 0);
	on_cleanup = {};
}
} // namespace drum_destruction_lifetime_test
using namespace drum_destruction_lifetime_test;
TEST_GROUP(drum_destruction_lifetime){void teardown() override{on_cleanup = {};
}
}
;
TEST(drum_destruction_lifetime, base_retires_before_member_cleanup) {
	check_retirement<Drum>();
}
TEST(drum_destruction_lifetime, sound_retires_before_derived_member_cleanup) {
	check_retirement<SoundDrum>();
}
TEST(drum_destruction_lifetime, midi_retires_before_derived_member_cleanup) {
	check_retirement<MIDIDrum>();
}
TEST(drum_destruction_lifetime, gate_retires_before_derived_member_cleanup) {
	check_retirement<GateDrum>();
}
