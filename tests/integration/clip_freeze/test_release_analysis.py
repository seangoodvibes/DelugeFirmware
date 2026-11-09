import math
import unittest

from analyze_release import ac_rms, initial_tail_end


class ReleaseAnalysisTest(unittest.TestCase):
    def test_dc_is_not_a_release_tail(self):
        self.assertEqual(ac_rms([550000, -280000] * 4410, 0, 4410), 0)

    def test_late_isolated_event_does_not_extend_initial_release(self):
        self.assertEqual(initial_tail_end([1, 0.5, 0.1, 0, 0, 0, 0, 0.02, 0]), 0.3)

    def test_audible_tone_survives_dc_removal(self):
        pcm = []
        for i in range(4410):
            tone = 10000 * math.sin(2 * math.pi * 1000 * i / 44100)
            pcm.extend((tone + 550000, tone - 280000))
        self.assertAlmostEqual(ac_rms(pcm, 0, 4410), 10000 / math.sqrt(2), places=6)


if __name__ == "__main__":
    unittest.main()
