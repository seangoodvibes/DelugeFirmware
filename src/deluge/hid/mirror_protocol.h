#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::hid::mirror::protocol {

constexpr uint8_t command = 6;
constexpr uint8_t version = 2;
constexpr size_t max_payload = 196;
enum class Op : uint8_t {
	Request,
	Accept,
	Stop,
	Heartbeat,
	Panel,
	OLED,
	Input,
	SyncLED,
	InputAck,
	capability_query,
	capabilities,
	request_rejected
};

// Legacy requests contain only the display type. Extended requests name the UI
// mode explicitly; unsupported modes must never fall back to visible-host input.
enum class session_mode : uint8_t { visible_host = 0, independent = 1 };
// Keep advertised modes and accepted requests aligned. Independent dispatch is not wired.
constexpr uint8_t supported_session_modes = 1;
constexpr bool supports_session_mode(session_mode mode) {
	const auto index = static_cast<uint8_t>(mode);
	return index <= static_cast<uint8_t>(session_mode::independent) && (supported_session_modes & (1u << index)) != 0;
}
struct session_request {
	bool oled;
	session_mode mode;
};
inline std::optional<session_request> decode_session_request(std::span<const uint8_t> bytes) {
	if ((bytes.size() != 1 && bytes.size() != 2) || bytes[0] > 1)
		return std::nullopt;
	const uint8_t mode = bytes.size() == 2 ? bytes[1] : 0;
	if (mode > static_cast<uint8_t>(session_mode::independent))
		return std::nullopt;
	return session_request{bytes[0] != 0, static_cast<session_mode>(mode)};
}

// Keep reconnect attempts distinct even when the clock has not advanced.
// Tokens are bounded by the existing wire format and repeat after 16383 uses.
class SessionTokenGenerator {
public:
	uint16_t next(uint32_t seed) {
		if (!last_)
			last_ = (seed & 0x3fff) ? (seed & 0x3fff) : 1;
		else
			last_ = last_ == 0x3fff ? 1 : last_ + 1;
		return last_;
	}

private:
	uint16_t last_ = 0;
};

// One in-flight input. Packet/session validation happens before acknowledging.
// A mismatched acknowledgement must not release a newer input.
class InputAcknowledgement {
public:
	bool pending() const { return sequence_.has_value(); }
	uint16_t sequence() const { return *sequence_; }
	bool begin(uint16_t sequence) {
		if (pending() || sequence > 0x3fff)
			return false;
		sequence_ = sequence;
		return true;
	}
	bool acknowledge(uint16_t sequence) {
		if (!pending() || *sequence_ != sequence)
			return false;
		sequence_.reset();
		return true;
	}
	void reset() { sequence_.reset(); }

private:
	std::optional<uint16_t> sequence_;
};

// Only panel output commands are replayable. In particular, a peer cannot change
// the UART baud rate or control the local OLED SPI handshake.
constexpr size_t panel_command_size(uint8_t command) {
	if (command >= 1 && command <= 9)
		return 49;
	if (command >= 10 && command <= 17)
		return 1;
	if (command == 19 || command == 23 || command == 243)
		return 2;
	if (command == 20 || command == 21 || command == 224)
		return 5;
	if (command >= 24 && command <= 223)
		return 1;
	if (command >= 228 && command <= 235)
		return 4;
	if (command >= 236 && command <= 240)
		return 1;
	if (command == 241 || command == 242)
		return 55;
	return 0;
}

inline bool valid_panel(std::span<const uint8_t> bytes) {
	if (bytes.empty())
		return false;
	for (size_t i = 0; i < bytes.size();) {
		auto size = panel_command_size(bytes[i]);
		if (!size || size > bytes.size() - i)
			return false;
		i += size;
	}
	return true;
}

// Byte-stuffed 8-to-7 encoding. The first byte of each group carries the high
// bits of the following (up to seven) bytes. No MIDI status byte appears inside.
inline size_t pack(std::span<const uint8_t> source, uint8_t* dest) {
	size_t written = 0;
	for (size_t i = 0; i < source.size();) {
		size_t prefix = written++;
		dest[prefix] = 0;
		for (size_t bit = 0; bit < 7 && i < source.size(); ++bit, ++i) {
			dest[prefix] |= (source[i] >> 7) << bit;
			dest[written++] = source[i] & 127;
		}
	}
	return written;
}

inline int unpack(std::span<const uint8_t> source, std::span<uint8_t> dest) {
	size_t written = 0;
	for (size_t i = 0; i < source.size();) {
		uint8_t prefix = source[i++];
		if (prefix > 127 || i == source.size())
			return -1;
		size_t bit = 0;
		for (; bit < 7 && i < source.size(); ++bit, ++i) {
			if (source[i] > 127 || written == dest.size())
				return -1;
			dest[written++] = source[i] | (((prefix >> bit) & 1) << 7);
		}
		if (prefix >> bit)
			return -1;
	}
	return static_cast<int>(written);
}

struct Packet {
	Op op;
	uint16_t session;
	uint16_t sequence;
	size_t size;
	uint8_t payload[max_payload];
};

// The manufacturer header has already been checked by MIDI dispatch.
inline bool decode(std::span<const uint8_t> data, Packet& packet) {
	if (data.size() < 8 || data.size() > 8 + max_payload + (max_payload + 6) / 7 || data[0] != command
	    || data[1] != version || data.back() != 0xF7 || data[2] > static_cast<uint8_t>(Op::request_rejected))
		return false;
	for (size_t i = 0; i < data.size() - 1; ++i)
		if (data[i] > 127)
			return false;
	int count = unpack(data.subspan(7, data.size() - 8), packet.payload);
	if (count < 0)
		return false;
	packet.op = static_cast<Op>(data[2]);
	packet.session = data[3] | (data[4] << 7);
	packet.sequence = data[5] | (data[6] << 7);
	packet.size = count;
	return true;
}

inline bool valid_input(uint8_t kind, uint8_t key, int32_t value) {
	return (kind == 0 && key < 180 && (value == 0 || value == 1))
	       || (kind == 1 && key < 6 && value >= -127 && value <= 127 && value != 0);
}

} // namespace deluge::hid::mirror::protocol
