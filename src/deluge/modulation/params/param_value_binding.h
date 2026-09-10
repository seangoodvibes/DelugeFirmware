#pragma once

#include <cstdint>

namespace deluge::modulation::params {

// ParamSet-owned automation shares the owner's scalar instead of mirroring it.
// Direct automation writes and undo therefore cannot leave getValue() stale.
class param_value_binding {
public:
	void bind(int32_t& value) { external_value_ = &value; }
	int32_t& value() { return external_value_ ? *external_value_ : standalone_value_; }

private:
	// Keep standalone values inline without a self-pointer, so raw relocation of
	// MIDI, patch-cable, and temporary AutoParams does not leave a dangling binding.
	int32_t standalone_value_ = 0;
	int32_t* external_value_ = nullptr;
};

} // namespace deluge::modulation::params