#include "CppUTest/TestHarness.h"
#include <cstdint>
#include <vector>
namespace note_row_lookup_test {
enum class OutputType { KIT, SYNTH, MIDI_OUT, CV };
struct Output {
	OutputType type = OutputType::SYNTH;
};
struct NoteRow {
	int y;
};
struct InstrumentClip {
	Output* output = nullptr;
	struct {
		std::vector<NoteRow*> rows;
		int getNumElements() { return rows.size(); }
		NoteRow* getElement(int index) { return rows.at(index); }
	} noteRows;
	int pitch_lookups = 0;
	NoteRow* getNoteRowForYNote(int id) {
		++pitch_lookups;
		for (auto* row : noteRows.rows)
			if (row->y == id)
				return row;
		return nullptr;
	}
	NoteRow* find_note_row_from_id(int32_t);
};
#include "note_row_lookup.inc"
} // namespace note_row_lookup_test
using namespace note_row_lookup_test;
TEST_GROUP(note_row_lookup){};
TEST(note_row_lookup, kit_indices_are_checked_without_pitch_lookup) {
	InstrumentClip clip;
	Output output{OutputType::KIT};
	NoteRow row{60};
	clip.output = &output;
	clip.noteRows.rows = {&row};
	POINTERS_EQUAL(&row, clip.find_note_row_from_id(0));
	for (int id : {-1, 1, 60, INT32_MAX})
		POINTERS_EQUAL(nullptr, clip.find_note_row_from_id(id));
	LONGS_EQUAL(0, clip.pitch_lookups);
	LONGS_EQUAL(1, clip.noteRows.rows.size());
}
TEST(note_row_lookup, melodic_lookup_preserves_missing_rows) {
	for (auto type : {OutputType::SYNTH, OutputType::MIDI_OUT, OutputType::CV}) {
		InstrumentClip clip;
		Output output{type};
		NoteRow row{60};
		clip.output = &output;
		clip.noteRows.rows = {&row};
		POINTERS_EQUAL(&row, clip.find_note_row_from_id(60));
		POINTERS_EQUAL(nullptr, clip.find_note_row_from_id(61));
		LONGS_EQUAL(1, clip.noteRows.rows.size());
		LONGS_EQUAL(2, clip.pitch_lookups);
	}
}
TEST(note_row_lookup, absent_output_returns_missing_without_lookup) {
	InstrumentClip clip;
	POINTERS_EQUAL(nullptr, clip.find_note_row_from_id(0));
	LONGS_EQUAL(0, clip.pitch_lookups);
}
