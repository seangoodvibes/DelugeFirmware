import math
import unittest
from tempo_analysis import compare_tempo, RATE


def passage(period, frequency):
    count = round(period * RATE)
    return [round(1000000 * math.sin(2 * math.pi * frequency * i / RATE))
            if (i % count) / count < .35 else 0
            for i in range(count * 3) for _ in range(2)]


class TempoAnalysisTests(unittest.TestCase):
    def test_pitch_preserving_stretch(self):
        result = compare_tempo(passage(1, 440), passage(1.25, 440), RATE, RATE * 1.25)
        self.assertLess(result['median_pitch_error_fraction'], .001)
        self.assertLess(result['activity_mismatch_fraction'], .02)
        self.assertLess(result['envelope_error'], .08)

    def test_rejects_repitch_and_wrong_beat_period(self):
        original = passage(1, 440)
        repitched = compare_tempo(original, passage(1.25, 440 / 1.25), RATE, RATE * 1.25)
        self.assertGreater(repitched['median_pitch_error_fraction'], .15)
        wrong_time = compare_tempo(original, original, RATE, RATE * 1.25)
        self.assertGreater(wrong_time['activity_mismatch_fraction'], .1)
