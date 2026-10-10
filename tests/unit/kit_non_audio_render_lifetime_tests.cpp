#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
namespace kit_non_audio_render_lifetime_test {
constexpr int ARP_MAX_INSTRUCTION_NOTES = 3, ARP_NOTE_NONE = -1;
namespace deluge = ::deluge;
} // namespace kit_non_audio_render_lifetime_test
namespace deluge::modulation::params {
constexpr int GLOBAL_ARP_RATE = 0;
}
namespace kit_non_audio_render_lifetime_test {
int song;
int* currentSong = &song;
int paramNeutralValues[1]{};
int cableToExpParamShortcut(int value) {
	return value;
}
int getFinalParameterValueExp(int, int value) {
	return value;
}
struct StereoSample {};
enum class ArpMode { OFF, ON };
enum class DrumType { SOUND, MIDI, GATE };
enum class ArpNoteStatus { OFF, PENDING, PLAYING };
struct ArpeggiatorSettings {
	ArpMode mode = ArpMode::ON;
	int gate = 0, rate = 0;
	int getPhaseIncrement(int value) { return value; }
};
struct ArpNote {
	int noteCodeOnPostArp[3]{60, 61, ARP_NOTE_NONE};
	ArpNoteStatus noteStatus[3]{ArpNoteStatus::PENDING, ArpNoteStatus::PENDING, ArpNoteStatus::OFF};
};
struct ArpReturnInstruction {
	ArpNote* arpNoteOn = nullptr;
	int glideNoteCodeOffPostArp[3]{60, ARP_NOTE_NONE, ARP_NOTE_NONE};
	int noteCodeOffPostArp[3]{61, ARP_NOTE_NONE, ARP_NOTE_NONE};
};
std::function<void()> on_generation, on_dispatch;
int dispatched = 0, generated = 0;
struct NonAudioDrum {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	DrumType type = DrumType::MIDI;
	ArpeggiatorSettings arpSettings;
	struct {
		ArpNote note;
		uint64_t revision = 0;
		uint64_t instruction_revision() const { return revision; }
		void render(ArpeggiatorSettings*, ArpReturnInstruction* instruction, size_t, uint32_t, uint32_t) {
			++generated;
			instruction->arpNoteOn = &note;
			if (on_generation)
				on_generation();
		}
	} arpeggiator;
	void noteOffPostArp(int) {
		++dispatched;
		if (on_dispatch)
			on_dispatch();
	}
	void noteOnPostArp(int, ArpNote* note, int index) {
		CHECK(note->noteStatus[index] == ArpNoteStatus::PLAYING);
		++dispatched;
		if (on_dispatch)
			on_dispatch();
	}
};
struct NoteRow {
	NonAudioDrum* drum;
	uint64_t undo_identity = 1;
};
struct Kit;
struct InstrumentClip {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	Kit* output = nullptr;
	struct {
		std::vector<NoteRow*> rows;
		int getNumElements() { return rows.size(); }
		NoteRow* getElement(int index) { return rows.at(index); }
	} noteRows;
	NoteRow* find_note_row_from_id(int index) {
		return index >= 0 && index < noteRows.getNumElements() ? noteRows.getElement(index) : nullptr;
	}
};
struct Kit {
	mutable deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime}; }
	InstrumentClip* activeClip = nullptr;
	std::vector<NonAudioDrum*> drums;
	int getDrumIndex(NonAudioDrum* drum) {
		for (size_t i = 0; i < drums.size(); ++i)
			if (drums[i] == drum)
				return i;
		return -1;
	}
	void renderNonAudioArpPostOutput(std::span<StereoSample>);
};
#include "kit_non_audio_render_lifetime.inc"
} // namespace kit_non_audio_render_lifetime_test
using namespace kit_non_audio_render_lifetime_test;
TEST_GROUP(kit_non_audio_render_lifetime) {
	std::unique_ptr<Kit> kit;
	std::unique_ptr<InstrumentClip> clip;
	std::unique_ptr<NonAudioDrum> drum;
	std::unique_ptr<NoteRow> row;
	void reset() {
		on_generation = {};
		on_dispatch = {};
		dispatched = generated = 0;
		kit = std::make_unique<Kit>();
		clip = std::make_unique<InstrumentClip>();
		drum = std::make_unique<NonAudioDrum>();
		row = std::make_unique<NoteRow>(NoteRow{drum.get()});
		kit->activeClip = clip.get();
		clip->output = kit.get();
		kit->drums = {drum.get()};
		clip->noteRows.rows = {row.get()};
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_generation = {};
		on_dispatch = {};
	}
	void render() {
		StereoSample output[1];
		kit->renderNonAudioArpPostOutput(output);
	}
};
TEST(kit_non_audio_render_lifetime, live_midi_and_gate_rows_dispatch_all_instructions) {
	NonAudioDrum second;
	second.type = DrumType::GATE;
	NoteRow second_row{&second};
	kit->drums.push_back(&second);
	clip->noteRows.rows.push_back(&second_row);
	render();
	LONGS_EQUAL(2, generated);
	LONGS_EQUAL(8, dispatched);
}
TEST(kit_non_audio_render_lifetime, generation_can_delete_owners) {
	on_generation = [&] {
		drum.reset();
		row.reset();
		clip.reset();
		kit.reset();
	};
	render();
	LONGS_EQUAL(1, generated);
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_non_audio_render_lifetime, each_dispatch_can_delete_owners) {
	for (int boundary = 1; boundary <= 4; ++boundary) {
		reset();
		on_dispatch = [&] {
			if (dispatched == boundary) {
				drum.reset();
				row.reset();
				clip.reset();
				kit.reset();
			}
		};
		render();
		LONGS_EQUAL(boundary, dispatched);
	}
}
TEST(kit_non_audio_render_lifetime, each_dispatch_can_remove_row) {
	for (int boundary = 1; boundary <= 4; ++boundary) {
		reset();
		on_dispatch = [&] {
			if (dispatched == boundary) {
				clip->noteRows.rows.clear();
				row.reset();
			}
		};
		render();
		LONGS_EQUAL(boundary, dispatched);
	}
}
TEST(kit_non_audio_render_lifetime, dispatch_rejects_live_drum_detachment) {
	on_dispatch = [&] { kit->drums.clear(); };
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_non_audio_render_lifetime, dispatch_preserves_clip_retarget) {
	InstrumentClip replacement;
	on_dispatch = [&] { kit->activeClip = &replacement; };
	render();
	LONGS_EQUAL(1, dispatched);
	POINTERS_EQUAL(&replacement, kit->activeClip);
}
TEST(kit_non_audio_render_lifetime, generation_rejects_reused_row_identity) {
	on_generation = [&] { ++row->undo_identity; };
	render();
	LONGS_EQUAL(0, dispatched);
}
TEST(kit_non_audio_render_lifetime, note_on_preserves_nested_reset_status) {
	on_dispatch = [&] {
		if (dispatched == 3) {
			drum->arpeggiator.note.noteStatus[0] = ArpNoteStatus::OFF;
			drum->arpeggiator.note.noteCodeOnPostArp[1] = ARP_NOTE_NONE;
		}
	};
	render();
	LONGS_EQUAL(3, dispatched);
	CHECK(drum->arpeggiator.note.noteStatus[0] == ArpNoteStatus::OFF);
}
TEST(kit_non_audio_render_lifetime, retired_and_detached_targets_do_not_generate) {
	for (int target = 0; target < 4; ++target) {
		reset();
		if (target == 0)
			kit->lifetime.retire();
		if (target == 1)
			clip->lifetime.retire();
		if (target == 2)
			drum->lifetime.retire();
		if (target == 3)
			kit->drums.clear();
		render();
		LONGS_EQUAL(0, generated);
		LONGS_EQUAL(0, dispatched);
	}
}
TEST(kit_non_audio_render_lifetime, sound_and_disabled_arps_are_skipped) {
	drum->type = DrumType::SOUND;
	render();
	LONGS_EQUAL(0, generated);
	drum->type = DrumType::MIDI;
	drum->arpSettings.mode = ArpMode::OFF;
	render();
	LONGS_EQUAL(0, generated);
	row->drum = nullptr;
	render();
	LONGS_EQUAL(0, generated);
}
TEST(kit_non_audio_render_lifetime, dispatch_rejects_replacement_row_at_same_index) {
	auto replacement = std::make_unique<NoteRow>(NoteRow{drum.get()});
	on_dispatch = [&] {
		clip->noteRows.rows[0] = replacement.get();
		row.reset();
	};
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_non_audio_render_lifetime, dispatch_rejects_row_drum_reassignment) {
	NonAudioDrum replacement;
	kit->drums.push_back(&replacement);
	on_dispatch = [&] { row->drum = &replacement; };
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_non_audio_render_lifetime, dispatch_rejects_song_change) {
	int replacement_song;
	on_dispatch = [&] { currentSong = &replacement_song; };
	render();
	currentSong = &song;
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_non_audio_render_lifetime, dispatch_rejects_panel_change) {
	std::unique_ptr<deluge::gui::ui_session::Scope> changed_owner;
	const auto other_owner = deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local
	                             ? deluge::gui::ui_session::Id::Remote
	                             : deluge::gui::ui_session::Id::Local;
	on_dispatch = [&] { changed_owner = std::make_unique<deluge::gui::ui_session::Scope>(other_owner); };
	render();
	LONGS_EQUAL(1, dispatched);
}
TEST(kit_non_audio_render_lifetime, missing_or_mismatched_clip_does_not_generate) {
	kit->activeClip = nullptr;
	render();
	kit->activeClip = clip.get();
	clip->output = nullptr;
	render();
	LONGS_EQUAL(0, generated);
	LONGS_EQUAL(0, dispatched);
}

TEST(kit_non_audio_render_lifetime, surviving_drum_with_new_instruction_cancels_remaining_events) {
	for (int stop_after : {1, 2, 3}) {
		reset();
		on_dispatch = [&] {
			if (dispatched == stop_after) {
				++drum->arpeggiator.revision;
				drum->arpeggiator.note.noteCodeOnPostArp[1] = 90;
			}
		};
		render();
		LONGS_EQUAL(stop_after, dispatched);
	}
}
