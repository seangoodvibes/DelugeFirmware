#pragma once
// Song stores only this enum in the lifecycle target. Avoid pulling the reverb
// menu's inline DSP implementation (and ARM SIMD dependency) into the host build.
// No Song or Reverb object is instantiated by these tests.
namespace deluge::dsp {
class Reverb {
public:
	enum class Model { FREEVERB, MUTABLE, DIGITAL };
};
} // namespace deluge::dsp
