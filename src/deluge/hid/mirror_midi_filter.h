#pragma once
#include <cstdint>

namespace deluge::hid::mirror {
// USB-MIDI CIN 5 also carries single-byte system common messages. Only F7
// is a SysEx terminator; clock and other realtime events use CIN F.
inline bool allow_usb_packet(bool mirror_client_connection, uint32_t packet) {
	if (!mirror_client_connection)
		return true;
	const auto cin = packet & 0x0f;
	return cin == 0x04 || cin == 0x06 || cin == 0x07 || (cin == 0x05 && ((packet >> 8) & 0xff) == 0xf7);
}
} // namespace deluge::hid::mirror
