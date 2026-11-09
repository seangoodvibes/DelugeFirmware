import math
import unittest

from transition_spectrum import SIZE, RATE, fft, compare_window


class SpectrumTests(unittest.TestCase):
    def test_fft_known_bin(self):
        tone = [math.sin(2 * math.pi * 32 * i / SIZE) for i in range(SIZE)]
        spectrum = fft(tone)
        self.assertAlmostEqual(abs(spectrum[32]), SIZE / 2, places=7)
        self.assertLess(max(abs(spectrum[k]) for k in range(SIZE // 2) if k != 32), 1e-8)

    def test_shape_is_gain_invariant_but_detects_changed_tone(self):
        def tone(hz, gain):
            return [gain * math.sin(2 * math.pi * hz * i / RATE)
                    for i in range(SIZE) for _ in range(2)]
        pcm = tone(300, 10000) + tone(300, 20000) + tone(3000, 10000)
        gain = compare_window(pcm, SIZE, 0)
        self.assertAlmostEqual(gain['gain_db'], 20 * math.log10(2), places=7)
        self.assertLess(gain['mean_octave_shape_difference_db'], 1e-7)
        changed = compare_window(pcm, 2 * SIZE, 0)
        self.assertLess(abs(changed['gain_db']), .01)
        self.assertGreater(changed['mean_octave_shape_difference_db'], 30)
