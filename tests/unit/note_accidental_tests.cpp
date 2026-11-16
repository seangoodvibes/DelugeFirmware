#include "CppUTest/TestHarness.h"
#include "model/note/accidental.h"
#include <array>
#include <cstdlib>
#include <string>
using namespace deluge::notes;
TEST_GROUP(NoteAccidental){};
static bool cMajor(int32_t note) {
	constexpr std::array<bool, 12> scale{true, false, true, false, true, true, false, true, false, true, false, true};
	return scale[note % 12];
}
TEST(NoteAccidental, UnsignedStorageRoundTripsSignedOffsets) {
	CHECK_EQUAL(0, encodeAccidental(0));
	CHECK_EQUAL(1, encodeAccidental(1));
	CHECK_EQUAL(255, encodeAccidental(-1));
	for (int32_t offset = -128; offset < 128; ++offset)
		CHECK_EQUAL(offset, decodeAccidental(encodeAccidental(offset)));
}
TEST(NoteAccidental, SnapsNearestAndPrefersLowerOnTies) {
	CHECK_EQUAL(60, nearestScaleNote(61, cMajor));
	CHECK_EQUAL(62, nearestScaleNote(63, cMajor));
	CHECK_EQUAL(65, nearestScaleNote(66, cMajor));
	// A sparse scale can snap upwards, requiring a negative accidental.
	auto cAndG = [](int32_t n) { return n % 12 == 0 || n % 12 == 7; };
	CHECK_EQUAL(67, nearestScaleNote(66, cAndG));
	CHECK_EQUAL(-1, 66 - nearestScaleNote(66, cAndG));
}
TEST(NoteAccidental, EveryMidiPitchSoundsAsPlayedWithoutChangingScale) {
	for (int32_t played = 0; played < 128; ++played) {
		int32_t snapped = nearestScaleNote(played, cMajor);
		CHECK(snapped >= 0 && snapped < 128);
		CHECK(cMajor(snapped));
		CHECK_EQUAL(played, snapped + decodeAccidental(encodeAccidental(played - snapped)));
		if (cMajor(played))
			CHECK_EQUAL(played, snapped);
		for (int32_t candidate = 0; candidate < 128; ++candidate)
			if (cMajor(candidate))
				CHECK(std::abs(played - snapped) <= std::abs(played - candidate));
	}
}
TEST(NoteAccidental, ChromaticRecordingNeedsNoOffset) {
	for (int32_t played = 0; played < 128; ++played) {
		int32_t snapped = nearestScaleNote(played, [](int32_t) { return true; });
		CHECK_EQUAL(played, snapped);
		CHECK_EQUAL(0, encodeAccidental(played - snapped));
	}
}
TEST(NoteAccidental, HeldPitchCollisionCanUseAnotherScaleRow) {
	int32_t snapped = nearestScaleNote(61, [](int32_t n) { return cMajor(n) && n != 60; });
	CHECK_EQUAL(62, snapped);
	CHECK_EQUAL(61, snapped + decodeAccidental(encodeAccidental(61 - snapped)));
	CHECK_EQUAL(-1, nearestScaleNote(61, [](int32_t) { return false; }));
}
TEST(NoteAccidental, MidiBoundariesNeverWrap) {
	CHECK_EQUAL(1, nearestScaleNote(0, [](int32_t n) { return n % 12 == 1; }));
	CHECK_EQUAL(126, nearestScaleNote(127, [](int32_t n) { return n % 12 == 6; }));
}
TEST(NoteAccidental, OldNoteRecordsDefaultToNatural) {
	for (size_t length : {20, 22, 26, 28})
		CHECK_EQUAL(0, accidentalFromNoteData(std::string(length, 'F')));
}
TEST(NoteAccidental, ExtendedNoteRecordsPreservePositiveAndNegativeOffsets) {
	std::string record(28, '0');
	CHECK_EQUAL(0, accidentalFromNoteData(record + "00"));
	CHECK_EQUAL(2, decodeAccidental(accidentalFromNoteData(record + "02")));
	CHECK_EQUAL(-2, decodeAccidental(accidentalFromNoteData(record + "FE")));
	CHECK_EQUAL(-1, decodeAccidental(accidentalFromNoteData(record + "ff")));
	CHECK_EQUAL(0, accidentalFromNoteData(record + "ZZ"));
}

TEST(NoteAccidental, FeatureOffLeavesScaleExpansionToExistingRecordingPath) {
	bool queriedScale = false;
	int32_t row = noteForRecording(61, true, false, [&](int32_t) {
		queriedScale = true;
		return false;
	});
	CHECK_EQUAL(61, row);
	CHECK_EQUAL(0, encodeAccidental(61 - row));
	CHECK_FALSE(queriedScale);
}
TEST(NoteAccidental, FeatureOnOnlySnapsScaleModeClips) {
	CHECK_EQUAL(60, noteForRecording(61, true, true, cMajor));
	CHECK_EQUAL(61, noteForRecording(61, false, true, cMajor));
	CHECK_EQUAL(61, noteForRecording(61, false, false, cMajor));
}
