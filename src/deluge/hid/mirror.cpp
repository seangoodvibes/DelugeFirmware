#include "hid/mirror.h"
#include "hid/mirror_protocol.h"

#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "drivers/pic/pic.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "hid/encoder_input.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds_state.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include <algorithm>
#include <array>
#include <cstring>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "drivers/oled/oled.h"
}

extern bool developerSysexCodeReceived;

namespace deluge::hid::mirror {
namespace {
using protocol::Op;
enum class State { Idle, Waiting, Client, Host };
State state = State::Idle;
protocol::session_mode active_session_mode = protocol::session_mode::visible_host;
deluge::gui::ui_session::Id input_owner() {
	return active_session_mode == protocol::session_mode::independent ? deluge::gui::ui_session::Id::Remote
	                                                                  : deluge::gui::ui_session::Id::Local;
}
MIDICable* peer = nullptr;
MIDICable* closing_host_peer = nullptr;
std::optional<uint64_t> peer_connection, closing_host_connection;
uint16_t session = 0, tx_sequence = 0, rx_sequence = 0;
protocol::SessionTokenGenerator client_sessions;
protocol::InputAcknowledgement client_input_ack, host_input_ack;
double last_receive = 0, last_heartbeat = 0, back_since = 0, last_o_led = 0;
double last_remote_render = 0;
bool requested = false, failed = false, snapshot_pending = false, busy = false, replaying = false;
Song* requested_song = nullptr;
deluge::gui::ui_session::Id requested_owner = deluge::gui::ui_session::Id::Local;
bool transport_busy = false, sending = false, accepting = false;
bool encoder_input_queued = false;
bool startup_discovery = false, startup_cancelled = false;
MIDICable* discovery_peer = nullptr;
double discovery_started = 0;
std::optional<uint64_t> discovery_connection;
protocol::SessionTokenGenerator discovery_ids;
uint16_t discovery_id = 0;
std::optional<capability_result> discovery_result;

bool connected(MIDICable* cable);
std::optional<uint64_t> connection_identity(const MIDICable* cable);

void expire_discovery() {
	if (discovery_peer
	    && (state != State::Idle || (requested && !startup_discovery)
	        || connection_identity(discovery_peer) != discovery_connection
	        || getSystemTime() - discovery_started >= 3.0)) {
		discovery_peer = nullptr;
		discovery_result.reset();
	}
}

void reset_injected_encoders() {
	deluge::gui::ui_session::Scope owner(input_owner());
	deluge::hid::encoders::clear_session_encoders();
	encoder_input_queued = false;
}
int last_sync_led = -1;
bool local_held[180]{}, remote_held[180]{}, ignore_until_release[180]{};

// Complete PIC commands, each prefixed by its length. Never drop part of a
// command: congestion terminates the session instead of corrupting the panel.
PLACE_SDRAM_BSS std::array<uint8_t, 16384> panel_queue{};
size_t panel_read = 0, panel_write = 0;
uint8_t panel_command[55]{};
size_t panel_position = 0, panel_length = 0;
bool panel_allowed = false;
PLACE_SDRAM_BSS uint8_t cached_commands[256][55]{};
uint8_t cached_lengths[256]{};
struct remote_panel_state {
	uint8_t command[55]{};
	size_t position = 0, length = 0;
	bool allowed = false;
	uint8_t commands[256][55]{};
	uint8_t lengths[256]{};
};
PLACE_SDRAM_BSS remote_panel_state remote_panel;

struct Input {
	uint8_t kind;
	uint8_t key;
	int32_t value;
	uint16_t sequence = 0;
};
PLACE_SDRAM_BSS std::array<Input, 128> input_queue{};
size_t input_read = 0, input_write = 0;

constexpr size_t oled_size = OLED_MAIN_WIDTH_PIXELS * (OLED_MAIN_HEIGHT_PIXELS / 8);
static_assert(oled_size == 768);
PLACE_SDRAM_BSS uint8_t oled_snapshot[oled_size]{}, oled_previous[oled_size]{}, oled_incoming[oled_size]{};
alignas(32) PLACE_SDRAM_BSS uint8_t oled_frames[3][oled_size]{};
size_t oled_block = 7;
bool force_o_led = false, oled_changed = false;
bool oled_commit_pending = false;

std::optional<uint64_t> connection_identity(const MIDICable* cable) {
	if (!cable)
		return std::nullopt;
	uint64_t slot = 0;
	for (auto& device : connectedUSBMIDIDevices[0]) {
		if (device.canHaveMIDISent && device.cable[0] == cable && cable->connectionFlags)
			return (slot << 32) | device.connection_generation;
		++slot;
	}
	return std::nullopt;
}

bool connected(MIDICable* cable) {
	return connection_identity(cable).has_value();
}

bool session_connected() {
	return peer && peer_connection && connection_identity(peer) == peer_connection;
}

bool session_live() {
	return session_connected() && getSystemTime() - last_receive <= 3.0;
}

bool send_discovery_query(MIDICable& cable, bool for_startup) {
	expire_discovery();
	if (state != State::Idle || requested || (busy && !for_startup) || sending || transport_busy || failed
	    || discovery_peer || !connected(&cable))
		return false;
	uint8_t packet[] = {
	    0xF0, 0, 0x21, 0x7B, 1,   protocol::command, protocol::version, static_cast<uint8_t>(Op::capability_query),
	    0,    0, 0,    0,    0xF7};
	if (cable.sendBufferSpace() < sizeof(packet) + 96)
		return false;
	discovery_id = discovery_ids.next(static_cast<uint32_t>(getSystemTime() * 1000000));
	packet[10] = discovery_id & 127;
	packet[11] = discovery_id >> 7;
	discovery_connection = connection_identity(&cable);
	discovery_peer = &cable;
	discovery_started = getSystemTime();
	discovery_result.reset();
	const bool previous_busy = busy;
	busy = sending = true;
	const bool old_developer_code = developerSysexCodeReceived;
	developerSysexCodeReceived = false;
	cable.sendSysex(packet, sizeof(packet));
	midiEngine.flushMIDI();
	developerSysexCodeReceived = old_developer_code;
	busy = previous_busy;
	sending = false;
	return true;
}

bool send(Op op, std::span<const uint8_t> payload = {}) {
	if (sending || (failed && op != Op::Stop) || !session_connected() || payload.size() > protocol::max_payload)
		return false;
	if (op != Op::Stop && !session_live()) {
		failed = true;
		return false;
	}
	uint8_t packet[240] = {0xF0, 0, 0x21, 0x7B, 1, protocol::command, protocol::version, static_cast<uint8_t>(op)};
	packet[8] = session & 127;
	packet[9] = session >> 7;
	packet[10] = tx_sequence & 127;
	packet[11] = tx_sequence >> 7;
	size_t length = 12 + protocol::pack(payload, packet + 12);
	packet[length++] = 0xF7;
	// Leave room for a stop or heartbeat, and for concurrent musical MIDI.
	sending = true;
	if (peer->sendBufferSpace() < length + 96) {
		sending = false;
		return false;
	}
	bool old_developer_code = developerSysexCodeReceived;
	developerSysexCodeReceived = false;
	accepting = op == Op::Accept;
	peer->sendSysex(packet, length);
	accepting = false;
	developerSysexCodeReceived = old_developer_code;
	if (!session_connected()) {
		failed = true;
		sending = false;
		return false;
	}
	tx_sequence = (tx_sequence + 1) & 0x3fff;
	sending = false;
	// The packet has been sent, so retain its sequence even if transmission
	// yielded past the deadline. Callers must not continue work for this session.
	if (op != Op::Stop && !session_live())
		failed = true;
	return true;
}

void queue_panel(const uint8_t* bytes, size_t length) {
	if (panel_write - panel_read + length + 1 > panel_queue.size()) {
		failed = true;
		return;
	}
	panel_queue[panel_write++ % panel_queue.size()] = length;
	for (size_t i = 0; i < length; ++i)
		panel_queue[panel_write++ % panel_queue.size()] = bytes[i];
}

void queue_input(Input event) {
	if (input_write - input_read == input_queue.size()) {
		failed = true;
		return;
	}
	input_queue[input_write++ % input_queue.size()] = event;
}

bool shared_panel_setting(uint8_t command) {
	return command == 19 || command == 23 || command == 243;
}

template <typename Emit>
void emit_indicators(Emit emit) {
	const bool remote = deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Remote;
	auto& lengths = remote ? remote_panel.lengths : cached_lengths;
	auto& commands = remote ? remote_panel.commands : cached_commands;
	for (size_t i = 10; i < 256; ++i) {
		// Physical timing/brightness settings belong to the shared host, even
		// when the client renders an independent Remote UI.
		if (remote && shared_panel_setting(i)) {
			if (cached_lengths[i])
				emit(cached_commands[i], cached_lengths[i]);
		}
		else if (lengths[i] && !(remote && (i == 20 || i == 21 || (i >= 152 && i <= 223))))
			emit(commands[i], lengths[i]);
	}
	if (remote) {
		if (!::display->haveOLED() && !lengths[224]) {
			uint8_t blank[5] = {224};
			emit(blank, sizeof(blank));
		}
		const auto& frame = indicator_leds::frame_for_session();
		for (size_t led = 0; led < frame.leds.size(); ++led) {
			uint8_t command = (frame.leds[led] ? 188 : 152) + led;
			emit(&command, 1);
		}
		for (size_t knob = 0; knob < frame.knobs.size(); ++knob) {
			uint8_t command[5] = {static_cast<uint8_t>(20 + knob)};
			std::copy(frame.knobs[knob].begin(), frame.knobs[knob].end(), command + 1);
			emit(command, sizeof(command));
		}
		return;
	}
	// An indicator never written since boot is off, regardless of the other
	// device's state. This also restores untouched client LEDs on exit.
	for (size_t i = 0; i < 36; ++i) {
		if (!lengths[152 + i] && !lengths[188 + i]) {
			uint8_t off = 152 + i;
			emit(&off, 1);
		}
	}
	for (uint8_t knob : {20, 21}) {
		if (!lengths[knob]) {
			uint8_t off[5] = {knob};
			emit(off, sizeof(off));
		}
	}
}

ActionResult dispatch(uint8_t key, bool on) {
	display::Screensaver::noteActivity();
	if (Pad::isPad(key)) {
		Pad pad(key);
		auto result = matrixDriver.padAction(pad.x, pad.y, on ? USE_DEFAULT_VELOCITY : 0);
		if (on)
			Buttons::ignoreCurrentShiftForSticky();
		return result;
	}
	return Buttons::buttonAction(key, on, false);
}

void stop(const char* reason) {
	// Release injected inputs in the same UI bank that processed them.
	deluge::gui::ui_session::Scope owner(input_owner());
	if (active_session_mode == protocol::session_mode::independent)
		uiTimerManager.unsetTimer(TimerName::GRAPHICS_ROUTINE);
	bool client = is_client();
	closing_host_peer = state == State::Host ? peer : nullptr;
	closing_host_connection = peer_connection;
	send(Op::Stop);
	if (client)
		uiTimerManager.resume_from_mirror();
	state = State::Idle;
	peer = nullptr;
	peer_connection.reset();
	failed = snapshot_pending = false;
	oled_commit_pending = false;
	panel_read = panel_write = input_read = input_write = 0;
	client_input_ack.reset();
	host_input_ack.reset();
	reset_injected_encoders();
	back_since = 0;
	// Only release keys owned by the remote device. A host key held at the same
	// time must stay down. Called outside the SD routine, like physical inputs.
	std::array<bool, 180> held_at_disconnect;
	std::copy(std::begin(remote_held), std::end(remote_held), held_at_disconnect.begin());
	// Any release callback can scan any key, not only the one being released.
	std::fill(std::begin(remote_held), std::end(remote_held), false);
	for (size_t key = 0; key < held_at_disconnect.size(); ++key) {
		if (held_at_disconnect[key]
		    && (active_session_mode == protocol::session_mode::independent
		        || !(local_held[key] && !ignore_until_release[key]))) {
			Buttons::ignoreCurrentShiftForSticky();
			dispatch(key, false);
		}
		if (client)
			ignore_until_release[key] = local_held[key];
	}
	if (client) {
		setOutputState(SYNCED_LED.port, SYNCED_LED.pin, playbackHandler.isExternalClockActive());
		// Restore the client's own indicator/configuration state; received panel
		// bytes never replace this cache. Pad/UI rendering follows below.
		replaying = true;
		emit_indicators([](const uint8_t* bytes, size_t length) { PIC::replay_mirror_bytes({bytes, length}); });
		replaying = false;
		getCurrentUI()->focusRegained();
		renderingNeededRegardlessOfUI();
		::display->popupTextTemporary(reason);
	}
	// Release callbacks can emit panel bytes. Discard any unfinished Remote
	// command only after those callbacks, so it cannot cross session boundaries.
	if (active_session_mode == protocol::session_mode::independent) {
		remote_panel.position = remote_panel.length = 0;
		remote_panel.allowed = false;
		remote_panel.lengths[224] = 0;
		display::OLED::invalidate_remote_frame();
		deluge::gui::ui_session::navigation.active().oled_dirty = true;
	}
	closing_host_peer = nullptr;
	closing_host_connection.reset();
	active_session_mode = protocol::session_mode::visible_host;
	last_remote_render = 0;
}

void begin() {
	struct startup_cleanup {
		~startup_cleanup() {
			// Only an explicitly requeued discovery wait may retain the query.
			if (!requested) {
				startup_discovery = false;
				discovery_peer = nullptr;
				discovery_result.reset();
			}
		}
	} cleanup;
	const bool awaiting_discovery = startup_discovery;
	startup_discovery = false;
	requested = false;
	Song* const initiating_song = requested_song;
	requested_song = nullptr;
	if (currentSong != initiating_song || deluge::gui::ui_session::current() != requested_owner)
		return;
	// Check again: this task runs after the menu action has returned.
	if (!currentSong || state != State::Idle || playbackHandler.playbackState || AudioEngine::firstRecorder
	    || stemExport.processStarted)
		return;
	MIDICable* candidate = nullptr;
	for (auto& device : connectedUSBMIDIDevices[0]) {
		if (!device.canHaveMIDISent || !device.cable[0] || !device.cable[0]->connectionFlags)
			continue;
		if (candidate && candidate != device.cable[0]) {
			::display->popupTextTemporary("Connect only one USB MIDI device");
			return;
		}
		candidate = device.cable[0];
	}
	if (!candidate) {
		::display->popupTextTemporary("No USB MIDI connection");
		return;
	}
	if (!awaiting_discovery) {
		discovery_peer = nullptr;
		discovery_result.reset();
		startup_discovery = true;
		if (!send_discovery_query(*candidate, true)) {
			startup_discovery = false;
			::display->popupTextTemporary("Mirror discovery unavailable");
			return;
		}
	}
	if (startup_cancelled)
		return;
	if (discovery_peer != candidate || connection_identity(candidate) != discovery_connection
	    || getSystemTime() - discovery_started >= 3.0) {
		startup_discovery = false;
		discovery_peer = nullptr;
		discovery_result.reset();
		::display->popupTextTemporary("Mirror discovery timed out");
		return;
	}
	if (!discovery_result) {
		startup_discovery = requested = true;
		requested_song = initiating_song;
		return;
	}
	const auto candidate_connection = discovery_connection;
	const auto capabilities = *discovery_result;
	startup_discovery = false;
	discovery_peer = nullptr;
	discovery_result.reset();
	if (!(capabilities.supported_modes & 1) || capabilities.oled != ::display->haveOLED()) {
		::display->popupTextTemporary("Incompatible mirror device");
		return;
	}
	if (currentSong != initiating_song || deluge::gui::ui_session::current() != requested_owner || !connected(candidate)
	    || state != State::Idle || playbackHandler.playbackState || AudioEngine::firstRecorder
	    || stemExport.processStarted)
		return;
	Song* const song = currentSong;
	const auto owner = deluge::gui::ui_session::current();
	const auto can_continue = [song, candidate, owner, candidate_connection] {
		return currentSong == song && connection_identity(candidate) == candidate_connection
		       && deluge::gui::ui_session::current() == owner && !playbackHandler.playbackState
		       && !AudioEngine::firstRecorder && !stemExport.processStarted;
	};
	// Complete the local UI operation before freezing its tasks.
	sound_editor_for_session().exitCompletely();
	if (!can_continue())
		return;
	Buttons::ignoreCurrentShiftForSticky();
	matrixDriver.noPressesHappening(false);
	if (!can_continue())
		return;
	Buttons::noPressesHappening(false);
	if (!can_continue())
		return;
	currentSong->stopAllAuditioning();
	if (!can_continue())
		return;
	currentSong->stopAllMIDIAndGateNotesPlaying();
	if (!can_continue())
		return;
	AudioEngine::killAllVoices();
	if (!can_continue())
		return;
	midiEngine.flushMIDI();
	if (!can_continue())
		return;
	for (size_t key = 0; key < 180; ++key)
		ignore_until_release[key] = local_held[key];
	for (size_t i = 0; i < 4; ++i)
		deluge::hid::encoders::functionEncoderAt(i).take();
	for (size_t i = 0; i < 2; ++i)
		deluge::hid::encoders::modEncoderAt(i).take();
	::display->popupTextTemporary("Connecting mirror - hold Back to exit");
	if (::display->haveOLED())
		display::OLED::sendMainImage();
	if (!can_continue())
		return;
	// Deltas in a new session must never inherit blocks from an old host.
	memset(oled_incoming, 0, sizeof(oled_incoming));
	oled_commit_pending = false;
	peer = candidate;
	peer_connection = connection_identity(candidate);
	session = client_sessions.next(static_cast<uint32_t>(getSystemTime() * 1000000));
	tx_sequence = rx_sequence = 0;
	panel_read = panel_write = input_read = input_write = 0;
	client_input_ack.reset();
	host_input_ack.reset();
	reset_injected_encoders();
	state = State::Waiting;
	uiTimerManager.pause_for_mirror();
	last_receive = last_heartbeat = getSystemTime();
	uint8_t type = ::display->haveOLED() ? 1 : 0;
	if (!send(Op::Request, {&type, 1}))
		failed = true;
}

void send_panel() {
	uint8_t bytes[protocol::max_payload];
	size_t pos = panel_read, length = 0;
	while (pos != panel_write) {
		size_t next = panel_queue[pos % panel_queue.size()];
		if (length + next > sizeof(bytes))
			break;
		++pos;
		for (size_t i = 0; i < next; ++i)
			bytes[length++] = panel_queue[pos++ % panel_queue.size()];
	}
	if (length && send(Op::Panel, {bytes, length}))
		panel_read = pos;
}

void send_o_led(double now) {
	if (!::display->haveOLED())
		return;
	if (oled_block == 7) {
		if (now - last_o_led < 0.1)
			return;
		if (active_session_mode == protocol::session_mode::independent) {
			if (!display::OLED::copy_remote_frame({oled_snapshot, oled_size}))
				return;
		}
		else {
			if (!display::OLED::local_image())
				return;
			memcpy(oled_snapshot, display::OLED::local_image()[0], oled_size);
		}
		oled_block = 0;
		oled_changed = false;
		last_o_led = now;
	}
	for (; oled_block < 6; ++oled_block) {
		size_t offset = oled_block * 128;
		if (!force_o_led && !memcmp(oled_snapshot + offset, oled_previous + offset, 128))
			continue;
		uint8_t bytes[129];
		bytes[0] = oled_block;
		memcpy(bytes + 1, oled_snapshot + offset, 128);
		if (send(Op::OLED, bytes)) {
			memcpy(oled_previous + offset, oled_snapshot + offset, 128);
			oled_changed = true;
			++oled_block;
		}
		return;
	}
	uint8_t commit = 6;
	if (!oled_changed || send(Op::OLED, {&commit, 1})) {
		oled_block = 7;
		force_o_led = false;
	}
}

// Host snapshots and client committed images share storage: these roles are
// mutually exclusive. Incoming deltas must not modify a committed image while
// the physical OLED queue is congested.
void flush_o_led_commit() {
	if (!oled_commit_pending)
		return;
	for (auto& frame : oled_frames) {
		bool used = spiBusCurrentlySending && DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n == reinterpret_cast<uintptr_t>(frame);
		for (auto* queued : oledFrameQueue)
			used |= queued == frame;
		if (used)
			continue;
		memcpy(frame, oled_snapshot, oled_size);
		enqueueOLEDFrame(frame);
		oled_commit_pending = false;
		return;
	}
}

void receive_o_led(std::span<const uint8_t> bytes) {
	if (!::display->haveOLED() || bytes.empty()) {
		failed = true;
		return;
	}
	if (bytes[0] < 6 && bytes.size() == 129) {
		memcpy(oled_incoming + bytes[0] * 128, bytes.data() + 1, 128);
	}
	else if (bytes[0] == 6 && bytes.size() == 1) {
		// Coalesce only complete frames, never partially received deltas.
		memcpy(oled_snapshot, oled_incoming, oled_size);
		oled_commit_pending = true;
		flush_o_led_commit();
	}
	else
		failed = true;
}

bool remote_ui_ready() {
	deluge::gui::ui_session::Scope owner(deluge::gui::ui_session::Id::Remote);
	return deluge::gui::ui_session::navigation.active().depth > 0 && getCurrentUI();
}

void process_input() {
	deluge::gui::ui_session::Scope owner(input_owner());
	for (size_t handled = 0; handled < 16 && !failed; ++handled) {
		if (!session_live()) {
			failed = true;
			return;
		}
		// A completed input is never dispatched twice when its ACK is blocked.
		if (state == State::Host && host_input_ack.pending()) {
			uint16_t sequence = host_input_ack.sequence();
			uint8_t bytes[] = {static_cast<uint8_t>(sequence), static_cast<uint8_t>(sequence >> 8)};
			if (!send(Op::InputAck, bytes))
				return;
			host_input_ack.acknowledge(sequence);
			// The ACK transmission may have received Stop or a protocol error.
			// Do not dispatch another queued input in this same iteration.
			if (failed)
				return;
		}
		// Retain unprocessed input until Remote navigation exists. Recheck each
		// iteration because dispatch or ACK callbacks can remove the current UI.
		if (state == State::Host && active_session_mode == protocol::session_mode::independent && !remote_ui_ready())
			return;
		if (input_read == input_write)
			return;
		auto event = input_queue[input_read % input_queue.size()];
		if (state == State::Client) {
			if (client_input_ack.pending())
				return;
			uint16_t sequence = tx_sequence;
			uint8_t bytes[6] = {event.kind, event.key};
			uint32_t value = static_cast<uint32_t>(event.value);
			for (size_t i = 0; i < 4; ++i)
				bytes[2 + i] = value >> (8 * i);
			// Publish the in-flight sequence before transmission can service an
			// acknowledgement callback. A blocked send leaves the event queued.
			client_input_ack.begin(sequence);
			if (!send(Op::Input, bytes)) {
				client_input_ack.reset();
				return;
			}
		}
		else if (state == State::Host) {
			if (event.kind == 0) {
				bool on = event.value != 0;
				bool wasOn = remote_held[event.key];
				// Like the physical input handler, establish ownership before an
				// action that might yield to another control scan during SD I/O.
				remote_held[event.key] = on;
				if ((active_session_mode == protocol::session_mode::independent
				     || !(local_held[event.key] && !ignore_until_release[event.key]))
				    && wasOn != on && dispatch(event.key, on) == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
					remote_held[event.key] = wasOn;
					if (!session_live())
						failed = true;
					return;
				}
			}
			else {
				if (!encoder_input_queued) {
					if (!deluge::hid::encoders::queue_session_encoder(event.key, event.value))
						return;
					encoder_input_queued = true;
				}
				deluge::hid::encoders::interpret_session_encoders(false);
				if (!session_live()) {
					failed = true;
					return;
				}
				if (deluge::hid::encoders::session_encoders_pending())
					return;
				encoder_input_queued = false;
			}
			if (!session_live()) {
				failed = true;
				return;
			}
			// This acknowledges dispatch after any encoder retry has been consumed;
			// it is not a promise that all resulting asynchronous work finished.
			host_input_ack.begin(event.sequence);
		}
		++input_read;
	}
}
} // namespace

bool is_host_client_connection(const MIDICable* primary_cable) {
	// Release callbacks can emit MIDI after state has become Idle. Keep the
	// closing client filtered until all teardown callbacks have returned.
	return primary_cable
	       && ((state == State::Host && primary_cable == peer && session_connected())
	           || (primary_cable == closing_host_peer && closing_host_connection
	               && connection_identity(primary_cable) == closing_host_connection));
}

bool is_client() {
	return state == State::Waiting || state == State::Client;
}

std::optional<capability_result> take_capabilities() {
	expire_discovery();
	if (startup_discovery)
		return std::nullopt;
	auto result = discovery_result;
	if (result) {
		discovery_peer = nullptr;
		discovery_result.reset();
	}
	return result;
}

bool request_capabilities(MIDICable& cable) {
	return send_discovery_query(cable, false);
}

bool start() {
	// Becoming a mirror client takes over this device's physical controls and
	// display. A Remote UI editing its host must not initiate that takeover.
	if (deluge::gui::ui_session::current() != deluge::gui::ui_session::Id::Local)
		return false;
	if (busy || transport_busy || sending || requested || !currentSong)
		return false;
	if (state != State::Idle) {
		::display->popupTextTemporary("Already mirroring");
		return false;
	}
	if (playbackHandler.playbackState || AudioEngine::firstRecorder || stemExport.processStarted) {
		::display->popupTextTemporary("Stop playback and recording first");
		return false;
	}
	startup_cancelled = false;
	requested_song = currentSong;
	requested_owner = deluge::gui::ui_session::current();
	requested = true;
	return true;
}

bool local_input(uint8_t key, bool on) {
	if (key >= 180)
		return true;
	local_held[key] = on;
	if (ignore_until_release[key]) {
		if (!on)
			ignore_until_release[key] = false;
		return true;
	}
	if (state == State::Idle && on && key == button::BACK && (requested || startup_discovery)) {
		startup_cancelled = true;
		startup_discovery = requested = false;
		requested_song = nullptr;
		discovery_peer = nullptr;
		discovery_result.reset();
	}
	if (is_client()) {
		if (key == button::BACK)
			back_since = on ? getSystemTime() : 0;
		if (state == State::Client)
			queue_input({0, key, on});
		return true;
	}
	return active_session_mode == protocol::session_mode::visible_host && remote_held[key];
}

bool local_all_released() {
	if (state == State::Idle) {
		std::fill(std::begin(local_held), std::end(local_held), false);
		std::fill(std::begin(ignore_until_release), std::end(ignore_until_release), false);
		return false;
	}
	for (size_t key = 0; key < 180; ++key) {
		if (local_held[key] && !local_input(key, false))
			dispatch(key, false);
	}
	return true;
}

bool encoders() {
	if (!is_client())
		return false;
	for (size_t i = 0; i < 6; ++i) {
		int32_t delta = i < 4 ? deluge::hid::encoders::functionEncoderAt(i).take()
		                      : deluge::hid::encoders::modEncoderAt(i - 4).take();
		while (delta && state == State::Client && !failed) {
			int32_t step = std::clamp<int32_t>(delta, -127, 127);
			queue_input({1, static_cast<uint8_t>(i), step});
			delta -= step;
		}
	}
	return true;
}

bool panel_byte(uint8_t byte) {
	const auto owner = deluge::gui::ui_session::current();
	const bool remote = owner == deluge::gui::ui_session::Id::Remote;
	auto& position = remote ? remote_panel.position : panel_position;
	auto& length = remote ? remote_panel.length : panel_length;
	auto& allowed = remote ? remote_panel.allowed : panel_allowed;
	auto& command_bytes = remote ? remote_panel.command : panel_command;
	auto& lengths = remote ? remote_panel.lengths : cached_lengths;
	auto& commands = remote ? remote_panel.commands : cached_commands;
	if (replaying)
		return !remote;
	if (!position) {
		length = protocol::panel_command_size(byte);
		allowed = length != 0;
		if (!length)
			length = (byte == 18 || byte == 225 || byte == 244) ? 2 : 1;
	}
	command_bytes[position++] = byte;
	bool allow = !is_client() || (!allowed && command_bytes[0] >= 248 && command_bytes[0] <= 251);
	if (position == length) {
		if (allowed && !is_client()) {
			uint8_t command = command_bytes[0];
			// Persistent panel state for joining clients; animations themselves are
			// streamed in order. A fresh pad render supplies the initial pad state.
			if (command <= 9 || command == 19 || command == 20 || command == 21 || command == 23 || command == 224
			    || command == 243 || (command >= 152 && command <= 223)) {
				if (command >= 152 && command <= 223)
					lengths[command >= 188 ? command - 36 : command + 36] = 0;
				memcpy(commands[command], command_bytes, length);
				lengths[command] = length;
			}
			if (state == State::Host && (owner == input_owner() || (!remote && shared_panel_setting(command))))
				queue_panel(command_bytes, length);
		}
		position = 0;
	}
	return !remote && allow;
}

void received(MIDICable& cable, uint8_t* data, int32_t length) {
	// Failure is terminal until routine() tears down this session. More USB
	// packets may arrive before then, including while storage blocks routine().
	if (failed)
		return;
	// Payload starts at command byte and includes F7. No legacy manufacturer
	// header is accepted for this protocol (dispatch enforces this).
	protocol::Packet packet;
	if (length < 0 || !protocol::decode({data, static_cast<size_t>(length)}, packet) || !connected(&cable))
		return;
	uint16_t token = packet.session;
	uint16_t sequence = packet.sequence;
	auto op = packet.op;
	size_t count = packet.size;
	std::span<const uint8_t> bytes(packet.payload, count);
	// Discovery is outside session sequencing and never reserves a peer.
	if (op == Op::capability_query) {
		if (token || count || state != State::Idle || requested || busy || sending || transport_busy)
			return;
		uint8_t response[] = {0xF0,
		                      0,
		                      0x21,
		                      0x7B,
		                      1,
		                      protocol::command,
		                      protocol::version,
		                      static_cast<uint8_t>(Op::capabilities),
		                      0,
		                      0,
		                      0,
		                      0,
		                      0,
		                      protocol::supported_session_modes,
		                      static_cast<uint8_t>(::display->haveOLED()),
		                      0xF7};
		response[10] = sequence & 127;
		response[11] = sequence >> 7;
		// Payload: mode bitmask (visible host only), followed by display type.
		if (cable.sendBufferSpace() < sizeof(response) + 96)
			return;
		busy = sending = true;
		const bool old_developer_code = developerSysexCodeReceived;
		developerSysexCodeReceived = false;
		cable.sendSysex(response, sizeof(response));
		developerSysexCodeReceived = old_developer_code;
		busy = sending = false;
		return;
	}
	if (op == Op::capabilities) {
		expire_discovery();
		if (discovery_peer == &cable && !discovery_result && !token && sequence == discovery_id && count == 2
		    && bytes[1] <= 1)
			discovery_result = capability_result{bytes[0], bytes[1] != 0};
		return; // Discovery responses cannot participate in a live session.
	}
	if (op == Op::Request) {
		const auto session_request = protocol::decode_session_request(bytes);
		if (session_request && !protocol::supports_session_mode(session_request->mode) && state == State::Idle
		    && !requested && !busy && !sending && !transport_busy && token && !sequence) {
			uint8_t response[] = {0xF0,
			                      0,
			                      0x21,
			                      0x7B,
			                      1,
			                      protocol::command,
			                      protocol::version,
			                      static_cast<uint8_t>(Op::request_rejected),
			                      static_cast<uint8_t>(token & 127),
			                      static_cast<uint8_t>(token >> 7),
			                      0,
			                      0,
			                      0,
			                      1,
			                      0xF7};
			// Reason 1: requested UI mode is unsupported. Do not reserve the peer.
			if (cable.sendBufferSpace() < sizeof(response) + 96)
				return;
			busy = sending = true;
			const bool old_developer_code = developerSysexCodeReceived;
			developerSysexCodeReceived = false;
			cable.sendSysex(response, sizeof(response));
			developerSysexCodeReceived = old_developer_code;
			busy = sending = false;
			return;
		}
		if (!session_request || !protocol::supports_session_mode(session_request->mode) || !currentSong
		    || state != State::Idle || requested || busy || session_request->oled != ::display->haveOLED()
		    || sequence != 0 || !token)
			return;
		peer = &cable;
		peer_connection = connection_identity(&cable);
		session = token;
		tx_sequence = 0;
		rx_sequence = 1;
		active_session_mode = session_request->mode;
		state = State::Host;
		// Remove pre-negotiation musical traffic now, even if USB cannot drain
		// until after this session ends. Never touch the in-flight buffer.
		for (auto& device : connectedUSBMIDIDevices[0])
			if (device.cable[0] == peer)
				device.discard_queued_non_sys_ex();
		panel_read = panel_write = input_read = input_write = 0;
		client_input_ack.reset();
		host_input_ack.reset();
		reset_injected_encoders();
		snapshot_pending = true;
		last_receive = last_heartbeat = getSystemTime();
		return;
	}
	if (peer != &cable || token != session || state == State::Idle)
		return;
	// Storage can delay routine() past the liveness deadline. A late packet
	// must not refresh last_receive and revive that expired session.
	if (!session_live()) {
		failed = true;
		return;
	}
	if (op == Op::request_rejected) {
		if (state == State::Waiting && sequence == 0 && count == 1 && bytes[0] == 1)
			failed = true;
		return;
	}
	if (sequence != rx_sequence) {
		failed = true;
		return;
	}
	rx_sequence = (rx_sequence + 1) & 0x3fff;
	last_receive = getSystemTime();
	if (op == Op::Stop && count == 0) {
		failed = true;
		return;
	}
	if (op == Op::Heartbeat && count == 0)
		return;
	if (op == Op::Accept && count == 0 && state == State::Waiting) {
		state = State::Client;
		return;
	}
	if (op == Op::InputAck && state == State::Client && count == 2) {
		uint16_t acknowledged = bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8);
		if (!client_input_ack.acknowledge(acknowledged))
			failed = true;
		return;
	}
	if (op == Op::Panel && state == State::Client && protocol::valid_panel(bytes)) {
		// Replay only whole validated commands, and never overwrite the PIC ring.
		if (uartGetTxBufferSpace(UART_ITEM_PIC) < static_cast<uint32_t>(count + 64)) {
			failed = true;
			return;
		}
		replaying = true;
		PIC::replay_mirror_bytes(bytes);
		replaying = false;
		return;
	}
	if (op == Op::OLED && state == State::Client) {
		receive_o_led(bytes);
		return;
	}
	if (op == Op::SyncLED && state == State::Client && count == 1 && bytes[0] <= 1) {
		setOutputState(SYNCED_LED.port, SYNCED_LED.pin, bytes[0]);
		return;
	}
	if (op == Op::Input && state == State::Host && count == 6) {
		// Entering Host reserves the peer, but input is valid only after the
		// acceptance packet was queued successfully.
		if (snapshot_pending && !accepting) {
			failed = true;
			return;
		}
		uint32_t value = 0;
		for (size_t i = 0; i < 4; ++i)
			value |= static_cast<uint32_t>(bytes[2 + i]) << (8 * i);
		int32_t signed_value = static_cast<int32_t>(value);
		if (protocol::valid_input(bytes[0], bytes[1], signed_value)) {
			queue_input({bytes[0], bytes[1], signed_value, sequence});
			return;
		}
	}
	failed = true;
}

void service_remote_ui() {
	if (state != State::Host || active_session_mode != protocol::session_mode::independent || snapshot_pending || failed
	    || sdRoutineLock || AudioEngine::audioRoutineLocked)
		return;
	deluge::gui::ui_session::Scope owner(deluge::gui::ui_session::Id::Remote);
	auto& navigation = deluge::gui::ui_session::navigation.active();
	if (!remote_ui_ready())
		return;
	auto* const source_song = currentSong;
	if (!uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE))
		uiTimerManager.setTimer(TimerName::GRAPHICS_ROUTINE, 15);
	uiTimerManager.routine();
	if (failed || state != State::Host || !session_live() || currentSong != source_song
	    || deluge::gui::ui_session::current() != deluge::gui::ui_session::Id::Remote) {
		failed = true;
		return;
	}
	const double now = getSystemTime();
	if (sdRoutineLock || AudioEngine::audioRoutineLocked || !navigation.depth || !getCurrentUI()
	    || now - last_remote_render < 0.01)
		return;
	last_remote_render = now;
	doAnyPendingUIRendering();
	if (!session_live() || currentSong != source_song
	    || deluge::gui::ui_session::current() != deluge::gui::ui_session::Id::Remote || !remote_ui_ready())
		failed = true;
}

bool prepare_remote_snapshot() {
	if (active_session_mode != protocol::session_mode::independent)
		return true;
	if (!remote_ui_ready())
		return false;
	deluge::gui::ui_session::Scope owner(deluge::gui::ui_session::Id::Remote);
	auto* const source_song = currentSong;
	auto& navigation = deluge::gui::ui_session::navigation.active();
	if (navigation.rendering)
		return false;
	// Pending transitions may need timer callbacks before grid rendering can
	// consume its dirty rows. The normal service waits until after acceptance.
	uiTimerManager.routine();
	if (failed || state != State::Host || !session_live() || currentSong != source_song || !remote_ui_ready()
	    || deluge::gui::ui_session::current() != deluge::gui::ui_session::Id::Remote) {
		failed = true;
		return false;
	}
	if (sdRoutineLock || AudioEngine::audioRoutineLocked || navigation.rendering)
		return false;
	navigation.main_rows_dirty = 0xFFFFFFFF;
	navigation.side_rows_dirty = 0xFFFFFFFF;
	navigation.oled_dirty = true;
	doAnyPendingUIRendering();
	if (failed || !session_live() || currentSong != source_song || !remote_ui_ready()
	    || deluge::gui::ui_session::current() != deluge::gui::ui_session::Id::Remote) {
		failed = true;
		return false;
	}
	return !sdRoutineLock && !AudioEngine::audioRoutineLocked && !navigation.rendering && !navigation.main_rows_dirty
	       && !navigation.side_rows_dirty && !navigation.oled_dirty;
}

void routine() {
	expire_discovery();
	if (busy || sending || sdRoutineLock || AudioEngine::audioRoutineLocked)
		return;
	busy = true;
	if (requested)
		begin();
	if (state != State::Idle) {
		double now = getSystemTime();
		if (failed || !session_live() || (is_client() && back_since && now - back_since > 2.0)) {
			stop("Mirror ended");
		}
		else {
			auto* const snapshot_song = currentSong;
			// Accept transmission can service an incoming Stop or protocol error.
			// The packet was sent, but a failed session must not start rendering.
			if (state == State::Host && snapshot_pending && prepare_remote_snapshot() && send(Op::Accept) && !failed) {
				// Acceptance can yield. Never snapshot an invalidated Remote UI or
				// retry Accept within a session that the client already accepted.
				if (currentSong != snapshot_song
				    || (active_session_mode == protocol::session_mode::independent && !remote_ui_ready())) {
					failed = true;
				}
				else {
					// Snapshot indicators and pads from the same owner as input dispatch.
					deluge::gui::ui_session::Scope owner(input_owner());
					panel_read = panel_write = 0;
					emit_indicators(queue_panel);
					PadLEDs::sendOutMainPadColours();
					PadLEDs::sendOutSidebarColours();
					force_o_led = true;
					oled_block = 7;
					last_o_led = 0;
					last_sync_led = -1;
					snapshot_pending = false;
				}
			}
			process_input();
			service_remote_ui();
			transport_routine();
		}
		midiEngine.flushMIDI();
	}
	busy = false;
}

void transport_routine() {
	if (transport_busy || sending || state == State::Idle || failed)
		return;
	if (!session_live()) {
		failed = true;
		return;
	}
	transport_busy = true;
	if (state == State::Client)
		flush_o_led_commit();
	double now = getSystemTime();
	if (now - last_heartbeat > 0.25 && send(Op::Heartbeat))
		last_heartbeat = now;
	if (state == State::Host && !snapshot_pending) {
		uint8_t sync = playbackHandler.isExternalClockActive();
		if (last_sync_led != sync && send(Op::SyncLED, {&sync, 1}))
			last_sync_led = sync;
		send_panel();
		send_o_led(now);
	}
	midiEngine.flushMIDI();
	transport_busy = false;
}
} // namespace deluge::hid::mirror
