#pragma once
#include <cstdint>

namespace deluge::io::midi {
// The flush pass prepares an inclusive circular device range. Filtering may
// leave its original first device empty, so choose again before starting USB.
template <typename Ready>
int32_t first_ready_usb_device(int32_t first, int32_t last, int32_t count, Ready ready) {
	if (count <= 0 || first < 0 || first >= count || last < 0 || last >= count)
		return -1;
	for (int32_t device = first;; device = (device + 1) % count) {
		if (ready(device))
			return device;
		if (device == last)
			return -1;
	}
}
} // namespace deluge::io::midi
