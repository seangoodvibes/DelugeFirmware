#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace sound_drum_choke_lifetime_test {
constexpr int kNumExpressionDimensions = 3, kNoteForDrum = 60;
enum class PolyphonyMode { CHOKE, POLY };
int song;
int* currentSong = &song;
struct ParamManager {};
struct SoundDrum;
struct NoteRow {
	SoundDrum* drum = nullptr;
	uint64_t undo_identity = 1;
	ParamManager paramManager;
};
std::function<void()> on_choke, on_start;
int chokes = 0, starts = 0;
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	SoundDrum* member = nullptr;
	int getDrumIndex(SoundDrum* drum) { return member == drum ? 0 : -1; }
	void choke() {
		++chokes;
		if (on_choke)
			on_choke();
	}
};
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* output = nullptr;
	NoteRow* row = nullptr;
	NoteRow* find_note_row_from_id(int index) { return index == 0 ? row : nullptr; }
};
struct ModelStackWithThreeMainThings {
	int* song = currentSong;
	InstrumentClip* clip = nullptr;
	NoteRow* row = nullptr;
	ParamManager* paramManager = nullptr;
	int noteRowId = 0;
	InstrumentClip* getTimelineCounterAllowNull() { return clip; }
	NoteRow* getNoteRowAllowNull() { return row; }
};
struct Arpeggiator {
	uint64_t revision = 0;
	uint64_t instruction_revision() const { return revision; }
};
bool expect_mpe = true;
struct Sound {
	void noteOn(ModelStackWithThreeMainThings*, Arpeggiator*, int note, const int16_t* mpe, uint32_t sync, int32_t late,
	            uint32_t samples_late, int32_t velocity, int32_t channel,
	            const deluge::lifetime::callback_validation* validation) {
		CHECK(validation);
		CHECK(validation->valid());
		++starts;
		LONGS_EQUAL(60, note);
		LONGS_EQUAL(16, sync);
		LONGS_EQUAL(1, late);
		LONGS_EQUAL(2, samples_late);
		LONGS_EQUAL(99, velocity);
		LONGS_EQUAL(3, channel);
		if (on_start)
			on_start();
		if (expect_mpe) {
			CHECK(mpe);
			LONGS_EQUAL(11, mpe[0]);
			LONGS_EQUAL(22, mpe[1]);
			LONGS_EQUAL(33, mpe[2]);
		}
		else
			CHECK(mpe == nullptr);
	}
};
struct SoundDrum : Sound {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* kit = nullptr;
	PolyphonyMode polyphonic = PolyphonyMode::CHOKE;
	Arpeggiator arpeggiator;
	void noteOn(ModelStackWithThreeMainThings*, uint8_t, const int16_t*, int32_t, uint32_t, int32_t, uint32_t);
};
#include "sound_drum_choke_lifetime.inc"
} // namespace sound_drum_choke_lifetime_test
using namespace sound_drum_choke_lifetime_test;
TEST_GROUP(sound_drum_choke_lifetime) {
	std::unique_ptr<SoundDrum> drum;
	std::unique_ptr<Kit> kit;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<NoteRow> row;
	std::unique_ptr<int16_t[]> mpe;
	ModelStackWithThreeMainThings stack;
	void reset() {
		drum = std::make_unique<SoundDrum>();
		kit = std::make_unique<Kit>();
		clip = std::make_unique<InstrumentClip>();
		row = std::make_unique<NoteRow>();
		mpe = std::make_unique<int16_t[]>(3);
		mpe[0] = 11;
		mpe[1] = 22;
		mpe[2] = 33;
		drum->kit = kit.get();
		kit->member = drum.get();
		clip->output = kit.get();
		clip->row = row.get();
		row->drum = drum.get();
		currentSong = &song;
		stack = {currentSong, clip.get(), row.get(), &row->paramManager, 0};
		on_choke = on_start = {};
		chokes = starts = 0;
		expect_mpe = true;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_choke = on_start = {};
		currentSong = &song;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void send() {
		drum->noteOn(&stack, 99, mpe.get(), 3, 16, 1, 2);
	}
};
TEST(sound_drum_choke_lifetime, normal_choke_starts_note_with_stable_mpe_values) {
	on_choke = [&] { mpe.reset(); };
	send();
	LONGS_EQUAL(1, chokes);
	LONGS_EQUAL(1, starts);
}
TEST(sound_drum_choke_lifetime, any_owner_deletion_during_choke_cancels_note_start) {
	for (int owner = 0; owner < 3; ++owner) {
		reset();
		on_choke = [&] {
			if (owner == 0)
				drum.reset();
			if (owner == 1)
				kit.reset();
			if (owner == 2)
				clip.reset();
		};
		send();
		LONGS_EQUAL(0, starts);
	}
}
TEST(sound_drum_choke_lifetime, routing_or_row_changes_cancel_note_start) {
	for (int mutation = 0; mutation < 10; ++mutation) {
		reset();
		on_choke = [&] {
			switch (mutation) {
			case 0:
				kit->member = nullptr;
				break;
			case 1:
				drum->kit = nullptr;
				break;
			case 2:
				currentSong = nullptr;
				break;
			case 3:
				stack.paramManager = nullptr;
				break;
			case 4:
				stack.clip = nullptr;
				break;
			case 5:
				clip->row = nullptr;
				row.reset();
				break;
			case 6:
				++row->undo_identity;
				break;
			case 7:
				++drum->arpeggiator.revision;
				break;
			case 8:
				drum->polyphonic = PolyphonyMode::POLY;
				break;
			case 9:
				deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote;
				break;
			}
		};
		send();
		LONGS_EQUAL(0, starts);
	}
}
TEST(sound_drum_choke_lifetime, polyphonic_and_standalone_clipless_notes_skip_choke) {
	for (bool standalone : {false, true}) {
		reset();
		if (standalone) {
			drum->kit = nullptr;
			stack.clip = nullptr;
			stack.row = nullptr;
		}
		else
			drum->polyphonic = PolyphonyMode::POLY;
		send();
		LONGS_EQUAL(0, chokes);
		LONGS_EQUAL(1, starts);
	}
}
TEST(sound_drum_choke_lifetime, null_mpe_and_final_note_callback_deletion_are_supported) {
	mpe.reset();
	expect_mpe = false;
	on_start = [&] {
		drum.reset();
		kit.reset();
		clip.reset();
		row.reset();
	};
	send();
	LONGS_EQUAL(1, starts);
}
TEST(sound_drum_choke_lifetime, invalid_or_retired_context_does_not_choke) {
	stack.paramManager = nullptr;
	send();
	stack.paramManager = &row->paramManager;
	clip->row = nullptr;
	send();
	clip->row = row.get();
	clip->lifetime.retire();
	send();
	stack.clip = nullptr;
	stack.row = nullptr;
	kit->lifetime.retire();
	send();
	drum->lifetime.retire();
	send();
	LONGS_EQUAL(0, chokes);
	LONGS_EQUAL(0, starts);
}
