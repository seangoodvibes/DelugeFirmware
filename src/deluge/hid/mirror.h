#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

class MIDICable;

namespace deluge::hid::mirror {
bool is_client();
// Match the physical USB connection by its primary cable, covering all virtual ports.
bool is_host_client_connection(const MIDICable* primary_cable);
// Physical-device takeover: may only be requested by the Local UI.
bool start();
struct capability_result {
	uint8_t supported_modes;
	bool oled;
};
// Idle-only advisory discovery. Results expire after three seconds and are consumed once.
// Startup owns its own pending result; take_capabilities never consumes that result.
bool request_capabilities(MIDICable& cable);
std::optional<capability_result> take_capabilities();
void routine();
// Transport-only service, also safe during a yielding host SD operation.
void transport_routine();
void received(MIDICable& cable, uint8_t* data, int32_t length);
// Called at the physical input boundary, before any local UI state changes.
bool local_input(uint8_t key, bool on);
bool local_all_released();
bool encoders();
// Called for every byte written to the panel controller. Returns false when
// local panel output must be suppressed in client mode.
bool panel_byte(uint8_t byte);
} // namespace deluge::hid::mirror
