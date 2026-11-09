"""The comparison must tolerate capture offsets without hiding missing notes."""

import array
import math
import unittest

from audio_equivalence import compare


class AudioEquivalenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.period = array.array("i")
        for i in range(88200):
            t = i / 44100
            active = (
                t < 0.35 or 0.5 <= t < 0.85
            )  # Second clip cycle is deliberately silent.
            sample = (
                round(10_000_000 * math.sin(2 * math.pi * 440 * t)) if active else 0
            )
            cls.period.extend((sample, sample // 2))

    def test_fractional_bin_offset_keeps_silence_and_waveforms(self):
        shifted = self.period[442:] + self.period[:442]  # Half a 10ms measurement bin.
        result = compare(self.period * 2, shifted * 2, 2)
        self.assertLess(result["envelope_relative_rms_error"], 0.02)
        self.assertEqual(result["note_activity_mismatch_fraction"], 0)
        self.assertTrue(result["waveform_relative_rms_errors"])
        self.assertLess(max(result["waveform_relative_rms_errors"]), 0.03)

    def test_missing_note_is_not_hidden_by_alignment(self):
        missing = self.period[:]
        for i in range(22050 * 2, 44100 * 2):
            missing[i] = 0
        result = compare(self.period * 2, missing * 2, 2)
        self.assertGreater(result["envelope_relative_rms_error"], 0.12)
        self.assertGreater(result["note_activity_mismatch_fraction"], 0.04)


if __name__ == "__main__":
    unittest.main()
