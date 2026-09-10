#include "CppUTest/TestHarness.h"
#include "hid/led/indicator_leds_state.h"
#include "hid/mirror_midi_filter.h"
#include "mirror_environment.h"
// Include the actual runtime once. Private state is accessed only to reset the
// firmware singleton between cases; stimuli use public firmware entry points.
#include "hid/mirror.cpp"

namespace m = deluge::hid::mirror;
namespace p = deluge::hid::mirror::protocol;
namespace session = deluge::gui::ui_session;
namespace enc = deluge::hid::encoders;

TEST_GROUP(MirrorRuntime) {
	MIDICable cable, stranger;
	void setup() override {
		session::detail::active = session::Id::Local;
		m::state = m::State::Idle;
		m::last_remote_render = 0;
		session::navigation.for_owner(session::Id::Remote) = {};
		on_remote_render = on_ui_timers = {};
		defer_ui_render = false;
		m::active_session_mode = p::session_mode::visible_host;
		deluge::hid::display::OLED::remote_frame_available = false;
		deluge::hid::display::OLED::remote_pixels.fill(0);
		m::peer = nullptr;
		m::peer_connection.reset();
		m::closing_host_connection.reset();
		m::startup_discovery = m::startup_cancelled = false;
		m::discovery_peer = nullptr;
		m::discovery_result.reset();
		m::discovery_ids = {};
		m::discovery_id = 0;
		fixture::on_discovery = [&] {
			if (m::startup_discovery) {
				incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
				cable.sent.pop_back(); // Existing session tests inspect session traffic only.
			}
			else if (fixture::on_send)
				fixture::on_send();
		};
		m::closing_host_peer = nullptr;
		m::requested = m::failed = m::busy = m::replaying = m::transport_busy = m::snapshot_pending = false;
		m::session = m::tx_sequence = m::rx_sequence = 0;
		m::client_sessions = {};
		m::sending = m::accepting = false;
		m::requested_song = nullptr;
		m::requested_owner = session::Id::Local;
		m::panel_read = m::panel_write = m::input_read = m::input_write = 0;
		m::panel_position = m::panel_length = 0;
		m::remote_panel = {};
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			session::Scope scope(owner);
			const_cast<indicator_leds::IndicatorFrame&>(indicator_leds::frame_for_session()) = {};
		}
		m::client_input_ack.reset();
		m::host_input_ack.reset();
		m::reset_injected_encoders();
		m::oled_commit_pending = false;
		m::back_since = m::last_heartbeat = m::last_receive = m::last_o_led = 0;
		std::fill(std::begin(m::local_held), std::end(m::local_held), false);
		std::fill(std::begin(m::remote_held), std::end(m::remote_held), false);
		std::fill(std::begin(m::ignore_until_release), std::end(m::ignore_until_release), false);
		std::fill(std::begin(m::cached_lengths), std::end(m::cached_lengths), 0);
		fixture::now = 1;
		fixture::uart_space = 65536;
		fixture::panel_frames.clear();
		fixture::oled_frames.clear();
		fixture::events.clear();
		fixture::main_pad_owners.clear();
		fixture::sidebar_pad_owners.clear();
		fixture::input_result = ActionResult::DEALT_WITH;
		fixture::defer_encoder = false;
		fixture::on_encoder = {};
		fixture::on_input = {};
		fixture::on_exit_editor = fixture::on_stop_audition = fixture::on_send = {};
		fixture::note_stops = 0;
		currentSong = &song;
		fixture::encoder_queues = fixture::encoder_dispatches = fixture::focused = 0;
		physical_display = {};
		uiTimerManager = {};
		playbackHandler = {};
		AudioEngine::firstRecorder = nullptr;
		AudioEngine::audioRoutineLocked = sdRoutineLock = false;
		stemExport.processStarted = false;
		spiBusCurrentlySending = false;
		mock_dma.N0SA_n = 0;
		for (auto& q : oledFrameQueue)
			q = nullptr;
		for (auto& d : connectedUSBMIDIDevices[0])
			d = {};
		connectedUSBMIDIDevices[0][0] = {true, {&cable}};
		for (auto& e : enc::functions)
			e.value = 0;
		for (auto& e : enc::mods)
			e.value = 0;
	}
	void teardown() override {
		on_remote_render = on_ui_timers = {};
		defer_ui_render = false;
		fixture::on_input = {};
		fixture::on_exit_editor = fixture::on_stop_audition = fixture::on_send = {};
		fixture::note_stops = 0;
		currentSong = &song;
		sdRoutineLock = false;
		AudioEngine::audioRoutineLocked = false;
		fixture::input_result = ActionResult::DEALT_WITH;
		cable.connectionFlags = 0;
		m::routine();
	}
	void incoming(p::Op op, uint16_t sequence, std::vector<uint8_t> payload = {}, uint16_t token = 42,
	              MIDICable* from = nullptr) {
		std::vector<uint8_t> data(240);
		data[0] = p::command;
		data[1] = p::version;
		data[2] = static_cast<uint8_t>(op);
		data[3] = token & 127;
		data[4] = token >> 7;
		data[5] = sequence & 127;
		data[6] = sequence >> 7;
		size_t n = 7 + p::pack(payload, data.data() + 7);
		data[n++] = 0xF7;
		m::received(from ? *from : cable, data.data(), n);
	}
	void host() {
		incoming(p::Op::Request, 0, {1});
		m::routine();
		CHECK_TRUE(m::is_host_client_connection(&cable));
		cable.sent.clear();
	}
	void client() {
		CHECK_TRUE(m::start());
		m::routine();
		CHECK_TRUE(m::is_client());
		incoming(p::Op::Accept, 0, {}, m::session);
		cable.sent.clear();
	}
	void input(uint16_t seq, uint8_t kind, uint8_t key, int32_t value) {
		uint32_t v = value;
		incoming(p::Op::Input, seq,
		         {kind, key, static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
		          static_cast<uint8_t>(v >> 24)});
	}
	std::vector<p::Packet> sent(p::Op op) {
		std::vector<p::Packet> result;
		for (auto& raw : cable.sent) {
			p::Packet packet;
			CHECK_TRUE(p::decode({raw.data() + 5, raw.size() - 5}, packet));
			if (packet.op == op)
				result.push_back(packet);
		}
		return result;
	}
};

TEST(MirrorRuntime, startup_waits_for_menu_return_and_pauses_client_timers) {
	CHECK_TRUE(m::start());
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	m::routine();
	CHECK_TRUE(m::is_client());
	CHECK_TRUE(uiTimerManager.paused);
	auto packets = sent(p::Op::Request);
	LONGS_EQUAL(1, packets.size());
	LONGS_EQUAL(1, packets[0].payload[0]);
	CHECK_TRUE(packets[0].session != 0);
}
TEST(MirrorRuntime, startup_rejects_playback_recording_and_export) {
	playbackHandler.playbackState = 1;
	CHECK_FALSE(m::start());
	playbackHandler.playbackState = 0;
	AudioEngine::firstRecorder = &song;
	CHECK_FALSE(m::start());
	AudioEngine::firstRecorder = nullptr;
	stemExport.processStarted = true;
	CHECK_FALSE(m::start());
	CHECK_TRUE(cable.sent.empty());
}
TEST(MirrorRuntime, startup_rechecks_busy_state_after_menu_returns) {
	CHECK_TRUE(m::start());
	playbackHandler.playbackState = 1;
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_TRUE(cable.sent.empty());
}
TEST(MirrorRuntime, startup_requires_exactly_one_connected_usb_peer) {
	connectedUSBMIDIDevices[0][0] = {};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	connectedUSBMIDIDevices[0][0] = {true, {&cable}};
	connectedUSBMIDIDevices[0][1] = {true, {&stranger}};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_TRUE(cable.sent.empty());
}
TEST(MirrorRuntime, host_negotiation_rejects_wrong_display_token_and_sequence) {
	incoming(p::Op::Request, 0, {0});
	CHECK_FALSE(m::is_host_client_connection(&cable));
	incoming(p::Op::Request, 0, {1}, 0);
	CHECK_FALSE(m::is_host_client_connection(&cable));
	incoming(p::Op::Request, 1, {1});
	CHECK_FALSE(m::is_host_client_connection(&cable));
	host();
	LONGS_EQUAL(0, fixture::events.size());
	CHECK_FALSE(uiTimerManager.paused);
}
TEST(MirrorRuntime, non_o_led_devices_negotiate_only_with_matching_display) {
	physical_display.oled = false;
	incoming(p::Op::Request, 0, {1});
	CHECK_FALSE(m::is_host_client_connection(&cable));
	incoming(p::Op::Request, 0, {0});
	m::routine();
	CHECK_TRUE(m::is_host_client_connection(&cable));
	CHECK_TRUE(sent(p::Op::OLED).empty());
}
TEST(MirrorRuntime, wrong_peer_and_session_cannot_inject_input) {
	host();
	connectedUSBMIDIDevices[0][1] = {true, {&stranger}};
	incoming(p::Op::Input, 1, {0, 2, 1, 0, 0, 0}, 42, &stranger);
	incoming(p::Op::Input, 1, {0, 2, 1, 0, 0, 0}, 43);
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(m::is_host_client_connection(&cable));
}
TEST(MirrorRuntime, sequence_gap_terminates_session_without_dispatch) {
	host();
	input(2, 0, 2, 1);
	m::routine();
	CHECK_FALSE(m::is_host_client_connection(&cable));
	CHECK_TRUE(fixture::events.empty());
}
TEST(MirrorRuntime, invalid_input_terminates_before_touching_panel_state) {
	host();
	input(1, 0, 180, 1);
	m::routine();
	CHECK_FALSE(m::is_host_client_connection(&cable));
	CHECK_TRUE(fixture::events.empty());
}
TEST(MirrorRuntime, deferred_pad_retries_before_acknowledgement_and_does_not_lose_ownership) {
	host();
	fixture::input_result = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	input(1, 0, 2, 1);
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	CHECK_FALSE(m::remote_held[2]);
	fixture::input_result = ActionResult::DEALT_WITH;
	m::routine();
	LONGS_EQUAL(2, fixture::events.size());
	CHECK_TRUE(m::remote_held[2]);
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
	m::routine();
	LONGS_EQUAL(2, fixture::events.size());
}
TEST(MirrorRuntime, congested_ack_never_redispatches_completed_input) {
	host();
	cable.space = 0;
	input(1, 0, 2, 1);
	m::routine();
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	cable.space = 65536;
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
}
TEST(MirrorRuntime, deferred_encoder_is_queued_once_and_blocks_later_buttons) {
	host();
	fixture::defer_encoder = true;
	input(1, 1, 0, 5);
	input(2, 0, 2, 1);
	m::routine();
	m::routine();
	LONGS_EQUAL(1, fixture::encoder_queues);
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	fixture::defer_encoder = false;
	m::routine();
	LONGS_EQUAL(1, fixture::encoder_queues);
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(2, sent(p::Op::InputAck).size());
}
TEST(MirrorRuntime, remote_callback_cannot_reenter_input_and_restores_its_owner) {
	host();
	fixture::on_input = [] { m::routine(); };
	input(1, 0, 2, 1);
	{
		session::Scope remote(session::Id::Remote);
		m::routine();
		CHECK_TRUE(session::current() == session::Id::Remote);
	}
	LONGS_EQUAL(1, fixture::events.size());
	CHECK_TRUE(fixture::events[0].owner == session::Id::Local);
}
TEST(MirrorRuntime, teardown_during_remote_processing_releases_visible_host_controls) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	fixture::events.clear();
	incoming(p::Op::Stop, 2);
	{
		session::Scope remote(session::Id::Remote);
		m::routine();
		CHECK_TRUE(session::current() == session::Id::Remote);
	}
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[0].key);
	LONGS_EQUAL(0, fixture::events[0].value);
	CHECK_TRUE(fixture::events[0].owner == session::Id::Local);
	CHECK_TRUE(m::state == m::State::Idle);
}

TEST(MirrorRuntime, initial_snapshot_uses_visible_host_pad_bank_when_serviced_from_remote) {
	incoming(p::Op::Request, 0, {1});
	{
		session::Scope remote(session::Id::Remote);
		m::routine();
		CHECK_TRUE(session::current() == session::Id::Remote);
	}
	LONGS_EQUAL(1, fixture::main_pad_owners.size());
	LONGS_EQUAL(1, fixture::sidebar_pad_owners.size());
	CHECK_TRUE(fixture::main_pad_owners[0] == session::Id::Local);
	CHECK_TRUE(fixture::sidebar_pad_owners[0] == session::Id::Local);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}

TEST(MirrorRuntime, congested_acceptance_retries_snapshot_in_visible_host_bank) {
	incoming(p::Op::Request, 0, {1});
	cable.space = 0;
	{
		session::Scope remote(session::Id::Remote);
		m::routine();
		CHECK_TRUE(fixture::main_pad_owners.empty());
		CHECK_TRUE(fixture::sidebar_pad_owners.empty());
		CHECK_TRUE(sent(p::Op::Accept).empty());
		cable.space = 65536;
		m::routine();
		m::routine();
		CHECK_TRUE(session::current() == session::Id::Remote);
	}
	LONGS_EQUAL(1, fixture::main_pad_owners.size());
	LONGS_EQUAL(1, fixture::sidebar_pad_owners.size());
	CHECK_TRUE(fixture::main_pad_owners[0] == session::Id::Local);
	CHECK_TRUE(fixture::sidebar_pad_owners[0] == session::Id::Local);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}

TEST(MirrorRuntime, host_held_key_survives_remote_release_and_disconnect) {
	host();
	CHECK_FALSE(m::local_input(2, true));
	input(1, 0, 2, 1);
	input(2, 0, 2, 0);
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	cable.connectionFlags = 0;
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_FALSE(m::local_input(2, false));
}
TEST(MirrorRuntime, disconnect_releases_only_remote_held_keys_and_clears_injected_movement) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	fixture::defer_encoder = true;
	input(2, 1, 0, 4);
	m::routine();
	cable.connectionFlags = 0;
	m::routine();
	CHECK_FALSE(enc::queued);
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(0, fixture::events.back().value);
}
TEST(MirrorRuntime, client_waits_for_matching_ack_before_next_input) {
	client();
	CHECK_TRUE(m::local_input(2, true));
	CHECK_TRUE(m::local_input(2, false));
	m::routine();
	auto packets = sent(p::Op::Input);
	LONGS_EQUAL(1, packets.size());
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Input).size());
	auto seq = packets[0].sequence;
	incoming(p::Op::InputAck, 1, {static_cast<uint8_t>(seq), static_cast<uint8_t>(seq >> 8)}, m::session);
	m::routine();
	LONGS_EQUAL(2, sent(p::Op::Input).size());
	CHECK_TRUE(fixture::events.empty());
}
TEST(MirrorRuntime, client_unsolicited_ack_ends_session_and_restores_timers) {
	client();
	incoming(p::Op::InputAck, 1, {0, 0}, m::session);
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	LONGS_EQUAL(1, fixture::focused);
}
TEST(MirrorRuntime, client_physical_encoder_movement_is_split_without_overflow) {
	client();
	enc::functions[0].value = 300;
	CHECK_TRUE(m::encoders());
	LONGS_EQUAL(0, enc::functions[0].value);
	m::routine();
	LONGS_EQUAL(127, sent(p::Op::Input)[0].payload[2]);
	LONGS_EQUAL(3, m::input_write);
}
TEST(MirrorRuntime, queue_overflow_ends_session_rather_than_dropping_commands) {
	client();
	for (int i = 0; i < 129; ++i)
		m::local_input(2, i % 2);
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_TRUE(sent(p::Op::Input).empty());
}
TEST(MirrorRuntime, client_suppresses_local_display_but_keeps_pic_handshake_bytes) {
	client();
	CHECK_FALSE(m::panel_byte(152));
	CHECK_TRUE(m::panel_byte(248));
	CHECK_TRUE(m::local_input(2, true));
	CHECK_TRUE(fixture::events.empty());
}
TEST(MirrorRuntime, client_validates_panel_before_replay_and_rejects_congestion) {
	client();
	incoming(p::Op::Panel, 1, {152}, m::session);
	LONGS_EQUAL(1, fixture::panel_frames.size());
	fixture::uart_space = 0;
	incoming(p::Op::Panel, 2, {153}, m::session);
	LONGS_EQUAL(1, fixture::panel_frames.size());
	m::routine();
	CHECK_FALSE(m::is_client());
}
TEST(MirrorRuntime, o_led_commit_uses_only_buffers_not_queued_or_in_dma) {
	client();
	std::vector<uint8_t> block(129, 77);
	block[0] = 0;
	incoming(p::Op::OLED, 1, block, m::session);
	CHECK_TRUE(fixture::oled_frames.empty());
	oledFrameQueue[0] = m::oled_frames[0];
	spiBusCurrentlySending = true;
	mock_dma.N0SA_n = reinterpret_cast<uintptr_t>(m::oled_frames[1]);
	incoming(p::Op::OLED, 2, {6}, m::session);
	LONGS_EQUAL(1, fixture::oled_frames.size());
	LONGS_EQUAL(77, fixture::oled_frames[0][0]);
	LONGS_EQUAL(77, m::oled_frames[2][0]);
}
TEST(MirrorRuntime, timeout_and_back_hold_restore_client_without_replaying_held_startup_keys) {
	m::local_input(2, true);
	client();
	CHECK_TRUE(m::local_input(2, false));
	m::routine();
	CHECK_TRUE(sent(p::Op::Input).empty());
	m::local_input(deluge::hid::button::BACK, true);
	fixture::now += 2.1;
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
}
TEST(MirrorRuntime, timeout_works_while_transport_heartbeat_continues) {
	host();
	fixture::now += 3.1;
	m::transport_routine();
	CHECK_TRUE(m::is_host_client_connection(&cable));
	m::routine();
	CHECK_FALSE(m::is_host_client_connection(&cable));
}
TEST(MirrorRuntime, sd_routine_defers_ui_input_but_transport_still_runs) {
	host();
	input(1, 0, 2, 1);
	sdRoutineLock = true;
	fixture::now += 0.3;
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	m::transport_routine();
	CHECK_FALSE(sent(p::Op::Heartbeat).empty());
	CHECK_TRUE(fixture::events.empty());
	sdRoutineLock = false;
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
}

TEST(MirrorRuntime, congested_o_led_commit_retries_without_publishing_uncommitted_deltas) {
	client();
	for (size_t i = 0; i < 3; ++i)
		oledFrameQueue[i] = m::oled_frames[i];
	std::vector<uint8_t> block(129, 77);
	block[0] = 0;
	incoming(p::Op::OLED, 1, block, m::session);
	incoming(p::Op::OLED, 2, {6}, m::session);
	CHECK_TRUE(fixture::oled_frames.empty());
	block[1] = 88;
	incoming(p::Op::OLED, 3, block, m::session);
	oledFrameQueue[0] = nullptr;
	m::transport_routine();
	LONGS_EQUAL(1, fixture::oled_frames.size());
	LONGS_EQUAL(77, fixture::oled_frames[0][0]);
	m::transport_routine();
	LONGS_EQUAL(1, fixture::oled_frames.size());
}

TEST(MirrorRuntime, congested_o_led_keeps_latest_complete_frame) {
	client();
	for (size_t i = 0; i < 3; ++i)
		oledFrameQueue[i] = m::oled_frames[i];
	std::vector<uint8_t> block(129, 77);
	block[0] = 0;
	incoming(p::Op::OLED, 1, block, m::session);
	incoming(p::Op::OLED, 2, {6}, m::session);
	block[1] = 88;
	incoming(p::Op::OLED, 3, block, m::session);
	incoming(p::Op::OLED, 4, {6}, m::session);
	oledFrameQueue[0] = nullptr;
	m::transport_routine();
	LONGS_EQUAL(1, fixture::oled_frames.size());
	LONGS_EQUAL(88, fixture::oled_frames[0][0]);
}

TEST(MirrorRuntime, disconnect_discards_congested_o_led_commit) {
	client();
	for (size_t i = 0; i < 3; ++i)
		oledFrameQueue[i] = m::oled_frames[i];
	incoming(p::Op::OLED, 1, {6}, m::session);
	cable.connectionFlags = 0;
	m::routine();
	for (auto& queued : oledFrameQueue)
		queued = nullptr;
	cable.connectionFlags = 1;
	client();
	m::transport_routine();
	CHECK_TRUE(fixture::oled_frames.empty());
}

TEST(MirrorRuntime, failed_client_ignores_panel_packets_before_deferred_disconnect) {
	client();
	sdRoutineLock = true;
	incoming(p::Op::Heartbeat, 99, {}, m::session);
	const auto before = fixture::panel_frames.size();
	incoming(p::Op::Panel, 1, {152}, m::session);
	LONGS_EQUAL(before, fixture::panel_frames.size());
	m::routine();
	CHECK_TRUE(m::is_client());
	sdRoutineLock = false;
	m::routine();
	CHECK_FALSE(m::is_client());
}

TEST(MirrorRuntime, stopped_client_cannot_acknowledge_more_input_before_disconnect) {
	client();
	CHECK_TRUE(m::local_input(2, true));
	m::routine();
	auto packets = sent(p::Op::Input);
	LONGS_EQUAL(1, packets.size());
	const auto sequence = packets[0].sequence;
	incoming(p::Op::Stop, 1, {}, m::session);
	incoming(p::Op::InputAck, 2, {static_cast<uint8_t>(sequence), static_cast<uint8_t>(sequence >> 8)}, m::session);
	CHECK_TRUE(m::client_input_ack.pending());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(m::client_input_ack.pending());
}

TEST(MirrorRuntime, teardown_releases_ownership_before_local_input_callback) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	bool suppressed = true;
	fixture::on_input = [&] { suppressed = m::local_input(2, true); };
	incoming(p::Op::Stop, 2);
	m::routine();
	CHECK_FALSE(suppressed);
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(0, fixture::events.back().value);
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, restart_is_rejected_during_release_and_allowed_after_teardown) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	bool started_during_cleanup = true;
	fixture::on_input = [&] { started_during_cleanup = m::start(); };
	incoming(p::Op::Stop, 2);
	m::routine();
	CHECK_FALSE(started_during_cleanup);
	fixture::on_input = {};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_TRUE(m::is_client());
}

TEST(MirrorRuntime, teardown_preserves_local_press_for_another_remotely_held_key) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	input(2, 0, 3, 1);
	m::routine();
	bool suppressed = true;
	int releases = 0;
	fixture::on_input = [&] {
		++releases;
		suppressed = m::local_input(3, true);
	};
	incoming(p::Op::Stop, 3);
	m::routine();
	CHECK_FALSE(suppressed);
	LONGS_EQUAL(1, releases);
	LONGS_EQUAL(3, fixture::events.size());
	LONGS_EQUAL(2, fixture::events.back().key);
	LONGS_EQUAL(0, fixture::events.back().value);
	fixture::on_input = {};
	CHECK_FALSE(m::local_input(3, false));
}

TEST(MirrorRuntime, teardown_releases_every_remote_key_exactly_once) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	input(2, 0, 3, 1);
	m::routine();
	incoming(p::Op::Stop, 3);
	m::routine();
	LONGS_EQUAL(4, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[2].key);
	LONGS_EQUAL(0, fixture::events[2].value);
	LONGS_EQUAL(3, fixture::events[3].key);
	LONGS_EQUAL(0, fixture::events[3].value);
	m::routine();
	LONGS_EQUAL(4, fixture::events.size());
}

TEST(MirrorRuntime, reconnect_rejects_old_accept_when_clock_has_not_advanced) {
	client();
	const auto previous = m::session;
	incoming(p::Op::Stop, 1, {}, previous);
	m::routine();
	CHECK_TRUE(m::start());
	m::routine();
	const auto next = m::session;
	CHECK_TRUE(next != previous);
	incoming(p::Op::Accept, 0, {}, previous);
	CHECK_TRUE(m::local_input(2, true));
	m::routine();
	CHECK_TRUE(sent(p::Op::Input).empty());
	incoming(p::Op::Accept, 0, {}, next);
	CHECK_TRUE(m::local_input(2, false));
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Input).size());
}

TEST(MirrorRuntime, session_tokens_traverse_nonzero_wire_range_before_reuse) {
	p::SessionTokenGenerator tokens;
	LONGS_EQUAL(0x3fff, tokens.next(0x3fff));
	for (uint16_t expected = 1; expected < 0x3fff; ++expected)
		LONGS_EQUAL(expected, tokens.next(0x3fff));
	LONGS_EQUAL(0x3fff, tokens.next(0));
	p::SessionTokenGenerator zero_seed;
	LONGS_EQUAL(1, zero_seed.next(0));
	LONGS_EQUAL(2, zero_seed.next(0));
}

TEST(MirrorRuntime, reconnect_o_led_delta_cannot_inherit_previous_session_blocks) {
	client();
	std::vector<uint8_t> old_block(129, 0x55);
	old_block[0] = 1;
	incoming(p::Op::OLED, 1, old_block, m::session);
	incoming(p::Op::OLED, 2, {6}, m::session);
	incoming(p::Op::Stop, 3, {}, m::session);
	m::routine();
	client();
	fixture::oled_frames.clear();
	std::vector<uint8_t> new_block(129, 0x33);
	new_block[0] = 0;
	incoming(p::Op::OLED, 1, new_block, m::session);
	incoming(p::Op::OLED, 2, {6}, m::session);
	LONGS_EQUAL(1, fixture::oled_frames.size());
	for (size_t i = 0; i < 768; ++i)
		LONGS_EQUAL(i < 128 ? 0x33 : 0, fixture::oled_frames.back()[i]);
}

TEST(MirrorRuntime, reconnect_early_o_led_commit_starts_from_blank_frame) {
	client();
	std::vector<uint8_t> block(129, 0x77);
	block[0] = 0;
	incoming(p::Op::OLED, 1, block, m::session);
	incoming(p::Op::Stop, 2, {}, m::session);
	m::routine();
	client();
	fixture::oled_frames.clear();
	incoming(p::Op::OLED, 1, {6}, m::session);
	LONGS_EQUAL(1, fixture::oled_frames.size());
	for (auto pixel : fixture::oled_frames.back())
		LONGS_EQUAL(0, pixel);
}

TEST(MirrorRuntime, startup_aborts_when_menu_exit_removes_song) {
	fixture::on_exit_editor = [] { currentSong = nullptr; };
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
	LONGS_EQUAL(0, fixture::note_stops);
}

TEST(MirrorRuntime, startup_aborts_when_audition_cleanup_changes_song) {
	Song replacement;
	fixture::on_stop_audition = [&] { currentSong = &replacement; };
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	LONGS_EQUAL(0, fixture::note_stops);
}

TEST(MirrorRuntime, startup_aborts_when_menu_exit_disconnects_peer_or_starts_playback) {
	fixture::on_exit_editor = [&] { cable.connectionFlags = 0; };
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	cable.connectionFlags = 1;
	fixture::on_exit_editor = [] { playbackHandler.playbackState = 1; };
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
}

TEST(MirrorRuntime, pending_startup_does_not_follow_changed_song) {
	CHECK_TRUE(m::start());
	Song replacement;
	currentSong = &replacement;
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_TRUE(m::is_client());
}

TEST(MirrorRuntime, pending_startup_does_not_follow_changed_panel_owner) {
	CHECK_TRUE(m::start());
	{
		session::Scope remote(session::Id::Remote);
		m::routine();
		CHECK_FALSE(m::is_client());
		CHECK_FALSE(uiTimerManager.paused);
		CHECK_TRUE(sent(p::Op::Request).empty());
	}
	m::routine();
	CHECK_FALSE(m::is_client());
}

TEST(MirrorRuntime, remote_ui_cannot_start_physical_mirror_takeover) {
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::start());
		m::routine();
		CHECK_TRUE(session::current() == session::Id::Remote);
	}
	CHECK_TRUE(cable.sent.empty());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(m::requested);
	LONGS_EQUAL(0, fixture::note_stops);
	// Rejection must not prevent a subsequent physical-device request.
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_TRUE(m::is_client());
	LONGS_EQUAL(1, sent(p::Op::Request).size());
}

TEST(MirrorRuntime, remote_start_rejection_preserves_local_discovery) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::start());
	m::routine();
	const auto query_id = m::discovery_id;
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::start());
	}
	incoming(p::Op::capabilities, query_id, {1, 1}, 0);
	m::routine();
	CHECK_TRUE(m::is_client());
	LONGS_EQUAL(1, sent(p::Op::capability_query).size());
	LONGS_EQUAL(1, sent(p::Op::Request).size());
}

TEST(MirrorRuntime, duplicate_pending_startup_is_rejected) {
	CHECK_TRUE(m::start());
	CHECK_FALSE(m::start());
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Request).size());
}

TEST(MirrorRuntime, host_rejects_input_before_sending_acceptance) {
	incoming(p::Op::Request, 0, {1});
	input(1, 0, 2, 1);
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, congested_acceptance_does_not_authorize_remote_input) {
	incoming(p::Op::Request, 0, {1});
	cable.space = 0;
	m::routine();
	CHECK_TRUE(sent(p::Op::Accept).empty());
	input(1, 0, 2, 1);
	cable.space = 65536;
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, host_requires_song_but_allows_playback_during_handshake) {
	currentSong = nullptr;
	incoming(p::Op::Request, 0, {1});
	CHECK_FALSE(m::is_host_client_connection(&cable));
	currentSong = &song;
	playbackHandler.playbackState = 1;
	host();
	input(1, 0, 2, 1);
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
}

TEST(MirrorRuntime, transport_failure_during_send_suppresses_later_frames_but_allows_stop) {
	host();
	m::panel_byte(152);
	playbackHandler.external = true;
	fixture::on_send = [&] {
		fixture::on_send = {};
		incoming(p::Op::Heartbeat, 99);
	};
	m::transport_routine();
	LONGS_EQUAL(1, sent(p::Op::SyncLED).size());
	CHECK_TRUE(sent(p::Op::Panel).empty());
	CHECK_TRUE(sent(p::Op::OLED).empty());
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Stop).size());
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, congested_handshake_retries_acceptance_before_frames) {
	incoming(p::Op::Request, 0, {1});
	cable.space = 0;
	m::routine();
	m::transport_routine();
	CHECK_TRUE(cable.sent.empty());
	cable.space = 65536;
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_FALSE(cable.sent.empty());
	LONGS_EQUAL(static_cast<uint8_t>(p::Op::Accept), cable.sent.front()[7]);
	CHECK_TRUE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, client_accepts_acknowledgement_during_input_transmission) {
	client();
	CHECK_TRUE(m::local_input(2, true));
	fixture::on_send = [&] {
		auto packets = sent(p::Op::Input);
		if (packets.empty())
			return;
		fixture::on_send = {};
		auto sequence = packets.back().sequence;
		incoming(p::Op::InputAck, 1, {static_cast<uint8_t>(sequence), static_cast<uint8_t>(sequence >> 8)}, m::session);
	};
	m::routine();
	CHECK_TRUE(m::is_client());
	CHECK_FALSE(m::client_input_ack.pending());
	LONGS_EQUAL(1, sent(p::Op::Input).size());
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Input).size());
	CHECK_TRUE(m::local_input(2, false));
	m::routine();
	LONGS_EQUAL(2, sent(p::Op::Input).size());
}

TEST(MirrorRuntime, blocked_input_send_clears_pending_acknowledgement_and_retries_event) {
	client();
	CHECK_TRUE(m::local_input(2, true));
	cable.space = 0;
	m::routine();
	CHECK_FALSE(m::client_input_ack.pending());
	CHECK_TRUE(sent(p::Op::Input).empty());
	cable.space = 65536;
	m::routine();
	CHECK_TRUE(m::client_input_ack.pending());
	auto packets = sent(p::Op::Input);
	LONGS_EQUAL(1, packets.size());
	LONGS_EQUAL(2, packets[0].payload[1]);
	LONGS_EQUAL(1, packets[0].payload[2]);
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Input).size());
}

TEST(MirrorRuntime, nested_transport_cannot_reuse_input_packet_sequence) {
	client();
	fixture::now += 0.5;
	CHECK_TRUE(m::local_input(2, true));
	fixture::on_send = [&] {
		fixture::on_send = {};
		m::transport_routine();
	};
	m::routine();
	auto inputs = sent(p::Op::Input);
	auto heartbeats = sent(p::Op::Heartbeat);
	LONGS_EQUAL(1, inputs.size());
	LONGS_EQUAL(1, heartbeats.size());
	LONGS_EQUAL((inputs[0].sequence + 1) & 0x3fff, heartbeats[0].sequence);
	CHECK_TRUE(m::is_client());
}

TEST(MirrorRuntime, nested_routine_defers_teardown_until_current_transmission_finishes) {
	client();
	fixture::now += 0.5;
	fixture::on_send = [&] {
		fixture::on_send = {};
		incoming(p::Op::Stop, 1, {}, m::session);
		m::routine();
		CHECK_TRUE(m::is_client());
	};
	m::transport_routine();
	CHECK_TRUE(m::is_client());
	m::routine();
	CHECK_FALSE(m::is_client());
	auto heartbeat = sent(p::Op::Heartbeat);
	auto stop = sent(p::Op::Stop);
	LONGS_EQUAL(1, heartbeat.size());
	LONGS_EQUAL(1, stop.size());
	LONGS_EQUAL((heartbeat[0].sequence + 1) & 0x3fff, stop[0].sequence);
}

TEST(MirrorRuntime, input_arriving_during_acceptance_waits_for_normal_dispatch) {
	incoming(p::Op::Request, 0, {1});
	fixture::on_send = [&] {
		fixture::on_send = {};
		input(1, 0, 2, 1);
		m::routine();
		CHECK_TRUE(fixture::events.empty());
	};
	m::routine();
	CHECK_TRUE(m::is_host_client_connection(&cable));
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(1, fixture::events[0].value);
	auto accepted = sent(p::Op::Accept);
	auto acknowledgements = sent(p::Op::InputAck);
	LONGS_EQUAL(1, accepted.size());
	LONGS_EQUAL(1, acknowledgements.size());
	LONGS_EQUAL((accepted[0].sequence + 1) & 0x3fff, acknowledgements[0].sequence);
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
}

TEST(MirrorRuntime, stop_during_acceptance_discards_following_input) {
	incoming(p::Op::Request, 0, {1});
	fixture::on_send = [&] {
		fixture::on_send = {};
		incoming(p::Op::Stop, 1);
		input(2, 0, 2, 1);
	};
	m::routine();
	CHECK_TRUE(fixture::main_pad_owners.empty());
	CHECK_TRUE(fixture::sidebar_pad_owners.empty());
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, malformed_input_during_acceptance_does_not_render_snapshot) {
	incoming(p::Op::Request, 0, {1});
	fixture::on_send = [&] {
		fixture::on_send = {};
		incoming(p::Op::Input, 1, {0});
	};
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(fixture::main_pad_owners.empty());
	CHECK_TRUE(fixture::sidebar_pad_owners.empty());
	CHECK_TRUE(sent(p::Op::Panel).empty());
	CHECK_TRUE(sent(p::Op::OLED).empty());
	CHECK_TRUE(fixture::events.empty());
	const auto accepted = sent(p::Op::Accept);
	LONGS_EQUAL(1, accepted.size());
	m::routine();
	const auto stopped = sent(p::Op::Stop);
	LONGS_EQUAL(1, stopped.size());
	LONGS_EQUAL((accepted[0].sequence + 1) & 0x3fff, stopped[0].sequence);
	CHECK_TRUE(m::state == m::State::Idle);
}

TEST(MirrorRuntime, stop_during_host_acknowledgement_prevents_next_queued_dispatch) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	fixture::on_send = [&] {
		fixture::on_send = {};
		incoming(p::Op::Stop, 3);
	};
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[0].key);
	m::routine();
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[1].key);
	LONGS_EQUAL(0, fixture::events[1].value);
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, input_arriving_during_host_acknowledgement_dispatches_exactly_once) {
	host();
	input(1, 0, 2, 1);
	fixture::on_send = [&] {
		fixture::on_send = {};
		input(2, 0, 3, 1);
	};
	m::routine();
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[0].key);
	LONGS_EQUAL(3, fixture::events[1].key);
	LONGS_EQUAL(2, sent(p::Op::InputAck).size());
	m::routine();
	LONGS_EQUAL(2, fixture::events.size());
}

TEST(MirrorRuntime, host_cleanup_keeps_closing_peer_sysex_only_through_release_callbacks) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	bool checked = false;
	fixture::on_input = [&] {
		checked = true;
		CHECK_TRUE(m::is_host_client_connection(&cable));
		CHECK_FALSE(m::is_host_client_connection(&stranger));
		CHECK_FALSE(m::allow_usb_packet(m::is_host_client_connection(&cable), 0x00643c09));
		CHECK_FALSE(m::allow_usb_packet(m::is_host_client_connection(&cable), 0x0000f80f));
		CHECK_TRUE(m::allow_usb_packet(m::is_host_client_connection(&cable), 0x0000f705));
	};
	incoming(p::Op::Stop, 2);
	m::routine();
	CHECK_TRUE(checked);
	CHECK_FALSE(m::is_host_client_connection(&cable));
	CHECK_TRUE(m::allow_usb_packet(m::is_host_client_connection(&cable), 0x00643c09));
}

TEST(MirrorRuntime, reconnect_during_stop_send_releases_keys_without_filtering_replacement) {
	host();
	input(1, 0, 2, 1);
	m::routine();
	cable.sent.clear();
	const auto outgoing_sequence = m::tx_sequence;
	fixture::on_send = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	int release_count = 0;
	fixture::on_input = [&] {
		++release_count;
		CHECK_FALSE(m::is_host_client_connection(&cable));
		CHECK_TRUE(m::allow_usb_packet(m::is_host_client_connection(&cable), 0x00643c09));
		incoming(p::Op::Request, 0, {1}, 43);
		m::routine();
		CHECK_TRUE(m::state == m::State::Idle);
	};
	incoming(p::Op::Stop, 2);
	m::routine();
	LONGS_EQUAL(1, release_count);
	LONGS_EQUAL(1, sent(p::Op::Stop).size());
	LONGS_EQUAL(outgoing_sequence, m::tx_sequence);
	CHECK_FALSE(m::failed);
	fixture::on_send = {};
	fixture::on_input = {};
	incoming(p::Op::Request, 0, {1}, 43);
	m::routine();
	CHECK_TRUE(m::is_host_client_connection(&cable));
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}

TEST(MirrorRuntime, reconnect_during_release_drops_old_filter_and_finishes_all_releases) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	m::routine();
	fixture::events.clear();
	cable.sent.clear();
	int release_count = 0;
	fixture::on_input = [&] {
		if (!release_count++) {
			CHECK_TRUE(m::is_host_client_connection(&cable));
			++connectedUSBMIDIDevices[0][0].connection_generation;
		}
		CHECK_FALSE(m::is_host_client_connection(&cable));
		incoming(p::Op::Request, 0, {1}, 43);
		CHECK_FALSE(m::start());
		m::routine();
	};
	incoming(p::Op::Stop, 3);
	m::routine();
	LONGS_EQUAL(2, release_count);
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[0].key);
	LONGS_EQUAL(3, fixture::events[1].key);
	LONGS_EQUAL(0, fixture::events[0].value);
	LONGS_EQUAL(0, fixture::events[1].value);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_FALSE(m::closing_host_connection.has_value());
	CHECK_FALSE(m::is_host_client_connection(&cable));
	fixture::on_input = {};
	incoming(p::Op::Request, 0, {1}, 43);
	m::routine();
	CHECK_TRUE(m::is_host_client_connection(&cable));
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	LONGS_EQUAL(2, fixture::events.size());
}

TEST(MirrorRuntime, host_acceptance_purges_only_negotiated_peers_queued_musical_traffic) {
	connectedUSBMIDIDevices[0][1] = {true, {&stranger}};
	host();
	LONGS_EQUAL(1, connectedUSBMIDIDevices[0][0].discarded_queues);
	LONGS_EQUAL(0, connectedUSBMIDIDevices[0][1].discarded_queues);
}

TEST(MirrorRuntime, explicit_visible_host_mode_negotiates_existing_mirroring) {
	incoming(p::Op::Request, 0, {1, 0});
	m::routine();
	CHECK_TRUE(m::is_host_client_connection(&cable));
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}
TEST(MirrorRuntime, independent_or_unknown_mode_never_falls_back_to_visible_host) {
	for (uint8_t mode : {2, 127, 255}) {
		incoming(p::Op::Request, 0, {1, mode});
		m::routine();
		CHECK_FALSE(m::is_host_client_connection(&cable));
		CHECK_TRUE(m::state == m::State::Idle);
		CHECK_TRUE(cable.sent.empty());
	}
}
TEST(MirrorRuntime, extended_request_rejects_malformed_or_incompatible_display) {
	for (auto payload : {std::vector<uint8_t>{}, std::vector<uint8_t>{2, 0}, std::vector<uint8_t>{0, 0},
	                     std::vector<uint8_t>{1, 0, 0}}) {
		incoming(p::Op::Request, 0, payload);
		m::routine();
		CHECK_TRUE(m::state == m::State::Idle);
		CHECK_TRUE(cable.sent.empty());
	}
}
TEST(MirrorRuntime, unsupported_mode_does_not_replace_active_session) {
	host();
	incoming(p::Op::Request, 0, {1, 1}, 99, &stranger);
	CHECK_TRUE(m::is_host_client_connection(&cable));
	LONGS_EQUAL(42, m::session);
	CHECK_FALSE(m::failed);
}

TEST(MirrorRuntime, capability_query_reports_only_supported_mode_without_starting_session) {
	incoming(p::Op::capability_query, 0, {}, 0);
	auto replies = sent(p::Op::capabilities);
	LONGS_EQUAL(1, replies.size());
	LONGS_EQUAL(0, replies[0].session);
	LONGS_EQUAL(0, replies[0].sequence);
	LONGS_EQUAL(2, replies[0].size);
	LONGS_EQUAL(1, replies[0].payload[0]);
	LONGS_EQUAL(1, replies[0].payload[1]);
	CHECK_TRUE(m::state == m::State::Idle);
	CHECK_TRUE(m::peer == nullptr);
	CHECK_FALSE(m::requested);
	LONGS_EQUAL(0, connectedUSBMIDIDevices[0][0].discarded_queues);
}
TEST(MirrorRuntime, capability_query_rejects_session_fields_payload_and_congestion) {
	incoming(p::Op::capability_query, 0, {}, 42);
	incoming(p::Op::capability_query, 0, {0}, 0);
	cable.space = 0;
	incoming(p::Op::capability_query, 0, {}, 0);
	CHECK_TRUE(cable.sent.empty());
	CHECK_TRUE(m::state == m::State::Idle);
}
TEST(MirrorRuntime, discovery_does_not_change_active_session_sequence) {
	host();
	const auto receive_sequence = m::rx_sequence;
	incoming(p::Op::capability_query, 0, {}, 0);
	incoming(p::Op::capabilities, receive_sequence, {1, 1});
	LONGS_EQUAL(receive_sequence, m::rx_sequence);
	CHECK_FALSE(m::failed);
	CHECK_TRUE(cable.sent.empty());
	CHECK_TRUE(m::is_host_client_connection(&cable));
}
TEST(MirrorRuntime, capability_response_blocks_reentrant_discovery_and_session_start) {
	fixture::on_send = [&] {
		incoming(p::Op::capability_query, 0, {}, 0);
		incoming(p::Op::Request, 0, {1});
		CHECK_FALSE(m::start());
	};
	incoming(p::Op::capability_query, 0, {}, 0);
	LONGS_EQUAL(1, cable.sent.size());
	CHECK_TRUE(m::state == m::State::Idle);
	CHECK_FALSE(m::busy);
	CHECK_FALSE(m::sending);
}

TEST(MirrorRuntime, unsupported_mode_is_rejected_without_reserving_host) {
	incoming(p::Op::Request, 0, {1, 1});
	auto replies = sent(p::Op::request_rejected);
	LONGS_EQUAL(1, replies.size());
	LONGS_EQUAL(42, replies[0].session);
	LONGS_EQUAL(0, replies[0].sequence);
	LONGS_EQUAL(1, replies[0].size);
	LONGS_EQUAL(1, replies[0].payload[0]);
	CHECK_TRUE(m::state == m::State::Idle);
	CHECK_TRUE(m::peer == nullptr);
	LONGS_EQUAL(0, connectedUSBMIDIDevices[0][0].discarded_queues);
}
TEST(MirrorRuntime, rejection_terminates_only_matching_waiting_handshake) {
	CHECK_TRUE(m::start());
	m::routine();
	incoming(p::Op::request_rejected, 0, {1}, m::session + 1);
	CHECK_FALSE(m::failed);
	incoming(p::Op::request_rejected, 1, {1}, m::session);
	CHECK_FALSE(m::failed);
	incoming(p::Op::request_rejected, 0, {2}, m::session);
	CHECK_FALSE(m::failed);
	incoming(p::Op::request_rejected, 0, {1}, m::session);
	CHECK_TRUE(m::failed);
}
TEST(MirrorRuntime, rejection_cannot_interrupt_accepted_client_session) {
	client();
	const auto receive_sequence = m::rx_sequence;
	incoming(p::Op::request_rejected, 0, {1}, m::session);
	CHECK_FALSE(m::failed);
	LONGS_EQUAL(receive_sequence, m::rx_sequence);
	CHECK_TRUE(m::state == m::State::Client);
}

TEST(MirrorRuntime, client_discovery_returns_validated_result_once_without_pausing_ui) {
	CHECK_TRUE(m::request_capabilities(cable));
	LONGS_EQUAL(1, sent(p::Op::capability_query).size());
	CHECK_FALSE(m::request_capabilities(cable));
	CHECK_FALSE(m::take_capabilities().has_value());
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	auto result = m::take_capabilities();
	CHECK_TRUE(result.has_value());
	LONGS_EQUAL(1, result->supported_modes);
	CHECK_TRUE(result->oled);
	CHECK_FALSE(m::take_capabilities().has_value());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(m::state == m::State::Idle);
}
TEST(MirrorRuntime, client_discovery_rejects_malformed_and_expired_responses) {
	CHECK_TRUE(m::request_capabilities(cable));
	incoming(p::Op::capabilities, 1, {1, 1}, 0);
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 42);
	incoming(p::Op::capabilities, m::discovery_id, {1, 2}, 0);
	incoming(p::Op::capabilities, m::discovery_id, {1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
	fixture::now += 3.0;
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
	CHECK_TRUE(m::request_capabilities(cable));
}
TEST(MirrorRuntime, client_discovery_discards_result_on_disconnect_or_session_start) {
	CHECK_TRUE(m::request_capabilities(cable));
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	cable.connectionFlags = 0;
	CHECK_FALSE(m::take_capabilities().has_value());
	cable.connectionFlags = 1;
	CHECK_TRUE(m::request_capabilities(cable));
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	CHECK_TRUE(m::start());
	CHECK_FALSE(m::take_capabilities().has_value());
}

TEST(MirrorRuntime, capability_host_echoes_query_id_without_session_sequence_change) {
	incoming(p::Op::capability_query, 0x3fff, {}, 0);
	auto replies = sent(p::Op::capabilities);
	LONGS_EQUAL(1, replies.size());
	LONGS_EQUAL(0x3fff, replies[0].sequence);
	LONGS_EQUAL(0, m::tx_sequence);
	LONGS_EQUAL(0, m::rx_sequence);
}
TEST(MirrorRuntime, client_discovery_rejects_late_reply_after_retry) {
	CHECK_TRUE(m::request_capabilities(cable));
	const auto old_id = m::discovery_id;
	fixture::now += 3.0;
	CHECK_TRUE(m::request_capabilities(cable));
	CHECK_TRUE(old_id != m::discovery_id);
	incoming(p::Op::capabilities, old_id, {1, 1}, 0);
	incoming(p::Op::capabilities, 0, {1, 1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	CHECK_TRUE(m::take_capabilities().has_value());
}
TEST(MirrorRuntime, client_discovery_allocates_fresh_id_without_clock_advance) {
	CHECK_TRUE(m::request_capabilities(cable));
	const auto old_id = m::discovery_id;
	incoming(p::Op::capabilities, old_id, {1, 1}, 0);
	CHECK_TRUE(m::take_capabilities().has_value());
	CHECK_TRUE(m::request_capabilities(cable));
	CHECK_TRUE(old_id != m::discovery_id);
	incoming(p::Op::capabilities, old_id, {1, 1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
}

TEST(MirrorRuntime, discovery_disconnect_allows_new_peer_without_waiting_for_timeout) {
	CHECK_TRUE(m::request_capabilities(cable));
	cable.connectionFlags = 0;
	connectedUSBMIDIDevices[0][1] = {true, {&stranger}};
	CHECK_TRUE(m::request_capabilities(stranger));
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0, &stranger);
	CHECK_TRUE(m::take_capabilities().has_value());
}
TEST(MirrorRuntime, discovery_service_discards_result_before_peer_reconnects) {
	CHECK_TRUE(m::request_capabilities(cable));
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	cable.connectionFlags = 0;
	m::routine();
	cable.connectionFlags = 1;
	CHECK_FALSE(m::take_capabilities().has_value());
	CHECK_TRUE(m::request_capabilities(cable));
}
TEST(MirrorRuntime, discovery_ignores_matching_id_from_other_connected_cable) {
	connectedUSBMIDIDevices[0][1] = {true, {&stranger}};
	CHECK_TRUE(m::request_capabilities(cable));
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0, &stranger);
	CHECK_FALSE(m::take_capabilities().has_value());
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	CHECK_TRUE(m::take_capabilities().has_value());
}
TEST(MirrorRuntime, discovery_accepts_synchronous_reply_and_blocks_reentrant_query) {
	fixture::on_send = [&] {
		CHECK_FALSE(m::request_capabilities(cable));
		incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	};
	CHECK_TRUE(m::request_capabilities(cable));
	CHECK_TRUE(m::take_capabilities().has_value());
	LONGS_EQUAL(1, cable.sent.size());
	CHECK_FALSE(m::busy);
	CHECK_FALSE(m::sending);
}

TEST(MirrorRuntime, startup_waits_for_discovery_without_freezing_client) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
	LONGS_EQUAL(1, sent(p::Op::capability_query).size());
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	m::routine();
	CHECK_TRUE(m::is_client());
	CHECK_TRUE(uiTimerManager.paused);
	LONGS_EQUAL(1, sent(p::Op::Request).size());
}
TEST(MirrorRuntime, startup_rejects_incompatible_or_timed_out_discovery_before_freezing) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::start());
	m::routine();
	incoming(p::Op::capabilities, m::discovery_id, {2, 1}, 0);
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_FALSE(m::requested);
	CHECK_TRUE(m::start());
	m::routine();
	fixture::now += 3.0;
	m::routine();
	CHECK_FALSE(m::requested);
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
}

TEST(MirrorRuntime, startup_rechecks_playback_after_discovery_response) {
	fixture::on_discovery = [&] {
		incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
		playbackHandler.playbackState = 1;
	};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
}

TEST(MirrorRuntime, back_cancels_startup_before_discovery_is_sent) {
	CHECK_TRUE(m::start());
	CHECK_FALSE(m::local_input(deluge::hid::button::BACK, true));
	m::routine();
	CHECK_FALSE(m::requested);
	CHECK_FALSE(m::is_client());
	CHECK_TRUE(cable.sent.empty());
	CHECK_FALSE(uiTimerManager.paused);
}
TEST(MirrorRuntime, back_cancels_pending_discovery_and_late_reply_cannot_restart_it) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::start());
	m::routine();
	const auto cancelled_id = m::discovery_id;
	CHECK_FALSE(m::local_input(deluge::hid::button::BACK, true));
	incoming(p::Op::capabilities, cancelled_id, {1, 1}, 0);
	m::routine();
	CHECK_FALSE(m::requested);
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
	CHECK_TRUE(sent(p::Op::Stop).empty());
	CHECK_FALSE(m::local_input(deluge::hid::button::BACK, false));
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_TRUE(m::discovery_id != cancelled_id);
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	m::routine();
	CHECK_TRUE(m::is_client());
}
TEST(MirrorRuntime, back_during_discovery_transmission_prevents_client_takeover) {
	fixture::on_discovery = [&] {
		CHECK_FALSE(m::local_input(deluge::hid::button::BACK, true));
		incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::requested);
	CHECK_FALSE(m::startup_discovery);
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
}

TEST(MirrorRuntime, abandoned_discovery_clears_query_and_ignores_late_reply) {
	fixture::on_discovery = {};
	Song replacement_song;
	for (int change = 0; change < 4; ++change) {
		CHECK_TRUE(m::start());
		m::routine();
		const auto abandoned_id = m::discovery_id;
		switch (change) {
		case 0:
			currentSong = &replacement_song;
			break;
		case 1:
			session::detail::active = session::Id::Remote;
			break;
		case 2:
			playbackHandler.playbackState = 1;
			break;
		case 3:
			connectedUSBMIDIDevices[0][1] = {true, {&stranger}};
			break;
		}
		m::routine();
		CHECK_FALSE(m::requested);
		CHECK_FALSE(m::startup_discovery);
		CHECK_TRUE(m::discovery_peer == nullptr);
		incoming(p::Op::capabilities, abandoned_id, {1, 1}, 0);
		CHECK_FALSE(m::take_capabilities().has_value());
		CHECK_FALSE(m::is_client());
		CHECK_FALSE(uiTimerManager.paused);
		currentSong = &song;
		session::detail::active = session::Id::Local;
		playbackHandler.playbackState = 0;
		connectedUSBMIDIDevices[0][1] = {};
	}
	CHECK_TRUE(m::request_capabilities(cable));
	CHECK_TRUE(sent(p::Op::Request).empty());
}
TEST(MirrorRuntime, abandoning_startup_discards_already_received_capabilities) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::start());
	m::routine();
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	playbackHandler.playbackState = 1;
	m::routine();
	CHECK_FALSE(m::take_capabilities().has_value());
	CHECK_TRUE(m::discovery_peer == nullptr);
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
}

TEST(MirrorRuntime, host_oled_baseline_and_delta_reconstruct_complete_client_frames) {
	std::array<uint8_t, 768> baseline;
	for (size_t i = 0; i < baseline.size(); ++i)
		baseline[i] = static_cast<uint8_t>(i * 37 + i / 128);
	memcpy(deluge::hid::display::OLED::local_image()[0], baseline.data(), baseline.size());
	incoming(p::Op::Request, 0, {1});
	m::routine();
	for (int step = 0; step < 7; ++step)
		m::transport_routine();
	auto baseline_packets = sent(p::Op::OLED);
	LONGS_EQUAL(7, baseline_packets.size());
	for (size_t block = 0; block < 6; ++block) {
		LONGS_EQUAL(129, baseline_packets[block].size);
		LONGS_EQUAL(block, baseline_packets[block].payload[0]);
	}
	LONGS_EQUAL(1, baseline_packets.back().size);
	LONGS_EQUAL(6, baseline_packets.back().payload[0]);
	cable.sent.clear();
	auto updated = baseline;
	for (size_t i = 256; i < 384; ++i)
		updated[i] ^= 0xa5;
	memcpy(deluge::hid::display::OLED::local_image()[0], updated.data(), updated.size());
	fixture::now += 0.2;
	for (int step = 0; step < 3; ++step)
		m::transport_routine();
	auto delta_packets = sent(p::Op::OLED);
	LONGS_EQUAL(2, delta_packets.size());
	LONGS_EQUAL(2, delta_packets[0].payload[0]);
	LONGS_EQUAL(6, delta_packets[1].payload[0]);
	m::stop("test transition");
	client();
	fixture::oled_frames.clear();
	auto deliver = [&](const auto& packets) {
		for (const auto& packet : packets)
			incoming(p::Op::OLED, m::rx_sequence, std::vector<uint8_t>(packet.payload, packet.payload + packet.size),
			         m::session);
	};
	deliver(baseline_packets);
	LONGS_EQUAL(1, fixture::oled_frames.size());
	MEMCMP_EQUAL(baseline.data(), fixture::oled_frames.back().data(), baseline.size());
	deliver(delta_packets);
	LONGS_EQUAL(2, fixture::oled_frames.size());
	MEMCMP_EQUAL(updated.data(), fixture::oled_frames.back().data(), updated.size());
	CHECK_FALSE(m::failed);
}

TEST(MirrorRuntime, advisory_getter_cannot_consume_pending_startup_capabilities) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::start());
	m::routine();
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
	CHECK_FALSE(m::request_capabilities(cable));
	m::routine();
	CHECK_TRUE(m::is_client());
	LONGS_EQUAL(1, sent(p::Op::Request).size());
}
TEST(MirrorRuntime, advisory_getter_cannot_consume_startup_reply_during_transmission) {
	fixture::on_discovery = [&] {
		incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
		CHECK_FALSE(m::take_capabilities().has_value());
	};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_TRUE(m::is_client());
	LONGS_EQUAL(1, sent(p::Op::Request).size());
}
TEST(MirrorRuntime, startup_does_not_reuse_cached_advisory_capabilities) {
	fixture::on_discovery = {};
	CHECK_TRUE(m::request_capabilities(cable));
	const auto advisory_id = m::discovery_id;
	incoming(p::Op::capabilities, advisory_id, {1, 1}, 0);
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_TRUE(m::discovery_id != advisory_id);
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	incoming(p::Op::capabilities, advisory_id, {1, 1}, 0);
	m::routine();
	CHECK_FALSE(m::is_client());
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	m::routine();
	CHECK_TRUE(m::is_client());
}

TEST(MirrorRuntime, discovery_rejects_reconnected_cable_with_same_pointer) {
	CHECK_TRUE(m::request_capabilities(cable));
	const auto old_id = m::discovery_id;
	++connectedUSBMIDIDevices[0][0].connection_generation;
	incoming(p::Op::capabilities, old_id, {1, 1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
	CHECK_TRUE(m::request_capabilities(cable));
	CHECK_TRUE(m::discovery_id != old_id);
}
TEST(MirrorRuntime, discovery_rejects_cached_result_after_unobserved_reconnect) {
	CHECK_TRUE(m::request_capabilities(cable));
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	++connectedUSBMIDIDevices[0][0].connection_generation;
	CHECK_FALSE(m::take_capabilities().has_value());
}
TEST(MirrorRuntime, discovery_rejects_cable_moved_to_another_slot) {
	CHECK_TRUE(m::request_capabilities(cable));
	connectedUSBMIDIDevices[0][1] = connectedUSBMIDIDevices[0][0];
	connectedUSBMIDIDevices[0][0] = {};
	incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
	CHECK_FALSE(m::take_capabilities().has_value());
}

TEST(MirrorRuntime, expired_host_session_cannot_be_revived_by_input_during_storage) {
	host();
	fixture::now += 3.1;
	sdRoutineLock = true;
	input(1, 0, 2, 1);
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(fixture::events.empty());
	sdRoutineLock = false;
	m::routine();
	CHECK_TRUE(m::state == m::State::Idle);
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
}

TEST(MirrorRuntime, expired_client_session_cannot_be_revived_by_heartbeat) {
	client();
	fixture::now += 3.1;
	incoming(p::Op::Heartbeat, m::rx_sequence, {}, m::session);
	CHECK_TRUE(m::failed);
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
}

TEST(MirrorRuntime, expired_host_transport_stops_during_storage_without_releasing_filter_early) {
	host();
	fixture::now += 3.1;
	sdRoutineLock = true;
	m::transport_routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(cable.sent.empty());
	CHECK_TRUE(m::is_host_client_connection(&cable));
	sdRoutineLock = false;
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Stop).size());
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, expired_client_transport_defers_cleanup_without_sending_heartbeat) {
	client();
	fixture::now += 3.1;
	sdRoutineLock = true;
	m::transport_routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(cable.sent.empty());
	CHECK_TRUE(uiTimerManager.paused);
	sdRoutineLock = false;
	m::routine();
	CHECK_FALSE(uiTimerManager.paused);
	LONGS_EQUAL(1, sent(p::Op::Stop).size());
}

TEST(MirrorRuntime, expiry_during_input_callback_prevents_ack_and_next_dispatch) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	fixture::on_input = [] { fixture::now += 3.1; };
	m::routine();
	CHECK_TRUE(m::failed);
	LONGS_EQUAL(1, fixture::events.size());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	fixture::on_input = {};
	m::routine();
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[1].key);
	LONGS_EQUAL(0, fixture::events[1].value);
}

TEST(MirrorRuntime, expiry_during_ack_send_prevents_next_input_and_preserves_stop_sequence) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	fixture::on_send = [] { fixture::now += 3.1; };
	m::routine();
	CHECK_TRUE(m::failed);
	LONGS_EQUAL(1, fixture::events.size());
	const auto acknowledgements = sent(p::Op::InputAck);
	LONGS_EQUAL(1, acknowledgements.size());
	fixture::on_send = {};
	m::routine();
	const auto stopped = sent(p::Op::Stop);
	LONGS_EQUAL(1, stopped.size());
	LONGS_EQUAL((acknowledgements[0].sequence + 1) & 0x3fff, stopped[0].sequence);
}

TEST(MirrorRuntime, heartbeats_keep_long_input_callback_alive_without_reentrant_dispatch) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	bool first_callback = true;
	fixture::on_input = [&] {
		if (!first_callback)
			return;
		first_callback = false;
		for (uint16_t sequence = 3; sequence < 7; ++sequence) {
			fixture::now += 2.0;
			incoming(p::Op::Heartbeat, sequence);
			m::routine();
			LONGS_EQUAL(1, fixture::events.size());
		}
	};
	m::routine();
	fixture::on_input = {};
	CHECK_FALSE(m::failed);
	CHECK_TRUE(m::is_host_client_connection(&cable));
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(2, sent(p::Op::InputAck).size());
}

TEST(MirrorRuntime, expired_deferred_button_is_not_retried_after_teardown) {
	host();
	input(1, 0, 2, 1);
	fixture::input_result = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	fixture::on_input = [] { fixture::now += 3.1; };
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	fixture::on_input = {};
	fixture::input_result = ActionResult::DEALT_WITH;
	m::routine();
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	CHECK_TRUE(m::state == m::State::Idle);
}

TEST(MirrorRuntime, expired_deferred_encoder_is_cleared_before_new_session) {
	host();
	input(1, 1, 0, 1);
	fixture::defer_encoder = true;
	fixture::on_encoder = [] { fixture::now += 3.1; };
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	fixture::on_encoder = {};
	m::routine();
	CHECK_FALSE(enc::session_encoders_pending());
	const auto dispatched = fixture::encoder_dispatches;
	fixture::defer_encoder = false;
	host();
	m::routine();
	LONGS_EQUAL(dispatched, fixture::encoder_dispatches);
	CHECK_TRUE(sent(p::Op::InputAck).empty());
}

TEST(MirrorRuntime, heartbeat_at_timeout_boundary_preserves_session) {
	host();
	fixture::now += 3.0;
	incoming(p::Op::Heartbeat, 1);
	m::routine();
	CHECK_FALSE(m::failed);
	CHECK_TRUE(m::is_host_client_connection(&cable));
	fixture::now += 0.1;
	input(2, 0, 2, 1);
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
}

TEST(MirrorRuntime, host_generation_change_rejects_old_session_input_and_releases_midi_filter) {
	host();
	++connectedUSBMIDIDevices[0][0].connection_generation;
	CHECK_FALSE(m::is_host_client_connection(&cable));
	input(1, 0, 2, 1);
	CHECK_TRUE(m::failed);
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(cable.sent.empty());
	CHECK_TRUE(m::state == m::State::Idle);
}
TEST(MirrorRuntime, host_generation_change_discards_queued_input_before_dispatch) {
	host();
	input(1, 0, 2, 1);
	++connectedUSBMIDIDevices[0][0].connection_generation;
	m::routine();
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(cable.sent.empty());
	CHECK_TRUE(m::state == m::State::Idle);
}
TEST(MirrorRuntime, client_generation_change_stops_transport_without_sending_to_new_connection) {
	client();
	++connectedUSBMIDIDevices[0][0].connection_generation;
	fixture::now += 0.5;
	m::transport_routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(cable.sent.empty());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(cable.sent.empty());
}

TEST(MirrorRuntime, reconnect_during_ack_prevents_next_queued_input_dispatch) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	const auto outgoing_sequence = m::tx_sequence;
	fixture::on_send = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	m::routine();
	CHECK_TRUE(m::failed);
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[0].key);
	LONGS_EQUAL(outgoing_sequence, m::tx_sequence);
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
	m::routine();
	CHECK_TRUE(sent(p::Op::Stop).empty());
	CHECK_TRUE(m::state == m::State::Idle);
}
TEST(MirrorRuntime, reconnect_during_client_input_send_does_not_advance_sequence) {
	client();
	CHECK_TRUE(m::local_input(2, true));
	const auto outgoing_sequence = m::tx_sequence;
	fixture::on_send = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	m::routine();
	CHECK_TRUE(m::failed);
	LONGS_EQUAL(outgoing_sequence, m::tx_sequence);
	CHECK_FALSE(m::client_input_ack.pending());
	CHECK_FALSE(m::sending);
	m::routine();
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Stop).empty());
}
TEST(MirrorRuntime, reconnect_during_accept_does_not_publish_host_frames) {
	incoming(p::Op::Request, 0, {1});
	fixture::on_send = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(m::snapshot_pending);
	LONGS_EQUAL(0, m::tx_sequence);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_TRUE(sent(p::Op::Panel).empty());
	CHECK_TRUE(sent(p::Op::OLED).empty());
	CHECK_FALSE(m::accepting);
	CHECK_FALSE(m::sending);
}

TEST(MirrorRuntime, startup_rejects_reconnect_after_discovery_reply_during_send) {
	fixture::on_discovery = [&] {
		incoming(p::Op::capabilities, m::discovery_id, {1, 1}, 0);
		++connectedUSBMIDIDevices[0][0].connection_generation;
	};
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_FALSE(m::requested);
	CHECK_TRUE(sent(p::Op::Request).empty());
}
TEST(MirrorRuntime, startup_rejects_reconnect_during_local_ui_cleanup) {
	fixture::on_exit_editor = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	CHECK_TRUE(m::start());
	m::routine();
	CHECK_FALSE(m::is_client());
	CHECK_FALSE(uiTimerManager.paused);
	CHECK_TRUE(sent(p::Op::Request).empty());
	CHECK_TRUE(m::discovery_peer == nullptr);
}
TEST(MirrorRuntime, host_reconnect_during_input_does_not_create_ack_or_dispatch_later_input) {
	host();
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	fixture::on_input = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_FALSE(m::host_input_ack.pending());
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(2, fixture::events[0].key);
	CHECK_TRUE(cable.sent.empty());
}

TEST(MirrorRuntime, reconnect_during_deferred_button_stops_retry_immediately) {
	host();
	input(1, 0, 2, 1);
	fixture::input_result = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	fixture::on_input = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
	m::process_input();
	CHECK_TRUE(m::failed);
	CHECK_FALSE(m::remote_held[2]);
	CHECK_FALSE(m::host_input_ack.pending());
	LONGS_EQUAL(1, fixture::events.size());
	m::routine();
	LONGS_EQUAL(1, fixture::events.size());
	CHECK_TRUE(cable.sent.empty());
}
TEST(MirrorRuntime, reconnect_during_encoder_dispatch_clears_deferred_state_without_replay) {
	for (bool deferred : {false, true}) {
		host();
		input(1, 1, 0, 1);
		fixture::defer_encoder = deferred;
		fixture::on_encoder = [&] { ++connectedUSBMIDIDevices[0][0].connection_generation; };
		const int previous_dispatches = fixture::encoder_dispatches;
		m::process_input();
		CHECK_TRUE(m::failed);
		CHECK_FALSE(m::host_input_ack.pending());
		LONGS_EQUAL(previous_dispatches + 1, fixture::encoder_dispatches);
		m::routine();
		CHECK_FALSE(enc::session_encoders_pending());
		CHECK_FALSE(m::encoder_input_queued);
		LONGS_EQUAL(previous_dispatches + 1, fixture::encoder_dispatches);
		CHECK_TRUE(cable.sent.empty());
		fixture::on_encoder = {};
	}
}

// Exercise the routing internally while independent negotiation remains disabled.
TEST(MirrorRuntime, independent_input_uses_remote_owner_and_does_not_merge_local_keys) {
	host();
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::active_session_mode = p::session_mode::independent;
	CHECK_FALSE(m::local_input(2, true));
	std::vector<session::Id> owners;
	fixture::on_input = [&] { owners.push_back(session::current()); };
	input(1, 0, 2, 1);
	m::routine();
	LONGS_EQUAL(1, owners.size());
	CHECK_TRUE(owners[0] == session::Id::Remote);
	CHECK_TRUE(session::current() == session::Id::Local);
	CHECK_FALSE(m::local_input(2, false));
	CHECK_TRUE(m::remote_held[2]);
	input(2, 0, 2, 0);
	m::routine();
	LONGS_EQUAL(2, owners.size());
	CHECK_TRUE(owners[1] == session::Id::Remote);
	LONGS_EQUAL(2, sent(p::Op::InputAck).size());
}
TEST(MirrorRuntime, independent_disconnect_releases_remote_keys_even_when_host_holds_same_key) {
	host();
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::active_session_mode = p::session_mode::independent;
	input(1, 0, 2, 1);
	m::routine();
	CHECK_FALSE(m::local_input(2, true));
	std::vector<session::Id> owners;
	fixture::on_input = [&] { owners.push_back(session::current()); };
	m::stop("test");
	LONGS_EQUAL(1, owners.size());
	CHECK_TRUE(owners[0] == session::Id::Remote);
	CHECK_TRUE(m::local_held[2]);
	CHECK_FALSE(m::remote_held[2]);
	CHECK_TRUE(m::active_session_mode == p::session_mode::visible_host);
	CHECK_TRUE(session::current() == session::Id::Local);
}
TEST(MirrorRuntime, independent_deferred_input_retries_under_remote_owner) {
	host();
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::active_session_mode = p::session_mode::independent;
	std::vector<session::Id> owners;
	fixture::on_input = [&] { owners.push_back(session::current()); };
	fixture::input_result = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	input(1, 0, 2, 1);
	m::routine();
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	fixture::input_result = ActionResult::DEALT_WITH;
	m::routine();
	LONGS_EQUAL(2, owners.size());
	for (auto owner : owners)
		CHECK_TRUE(owner == session::Id::Remote);
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
}

TEST(MirrorRuntime, independent_encoder_retries_under_remote_owner_before_ack) {
	host();
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::active_session_mode = p::session_mode::independent;
	std::vector<session::Id> owners;
	fixture::on_encoder = [&] { owners.push_back(session::current()); };
	fixture::defer_encoder = true;
	input(1, 1, 0, 1);
	m::routine();
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	fixture::defer_encoder = false;
	m::routine();
	LONGS_EQUAL(2, owners.size());
	for (auto owner : owners)
		CHECK_TRUE(owner == session::Id::Remote);
	CHECK_TRUE(session::current() == session::Id::Local);
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
	fixture::on_encoder = {};
}

TEST(MirrorRuntime, independent_oled_streams_remote_snapshot_without_local_frame_bytes) {
	host();
	m::active_session_mode = p::session_mode::independent;
	m::oled_block = 7;
	m::force_o_led = true;
	m::last_o_led = 0;
	memset(deluge::hid::display::OLED::pixels, 0x11, 768);
	deluge::hid::display::OLED::remote_frame_available = true;
	deluge::hid::display::OLED::remote_pixels.fill(0x23);
	m::transport_routine();
	// Later blocks must retain the snapshot even if Remote publishes again.
	deluge::hid::display::OLED::remote_pixels.fill(0x45);
	for (int step = 0; step < 6; ++step)
		m::transport_routine();
	auto packets = sent(p::Op::OLED);
	LONGS_EQUAL(7, packets.size());
	for (size_t block = 0; block < 6; ++block) {
		LONGS_EQUAL(block, packets[block].payload[0]);
		for (size_t byte = 1; byte < 129; ++byte)
			LONGS_EQUAL(0x23, packets[block].payload[byte]);
	}
	LONGS_EQUAL(6, packets.back().payload[0]);
	cable.sent.clear();
	fixture::now += 0.2;
	for (int step = 0; step < 7; ++step)
		m::transport_routine();
	packets = sent(p::Op::OLED);
	LONGS_EQUAL(7, packets.size());
	LONGS_EQUAL(0x45, packets[0].payload[1]);
	LONGS_EQUAL(0x11, deluge::hid::display::OLED::pixels[0][0]);
}
TEST(MirrorRuntime, independent_oled_waits_for_remote_frame_without_local_fallback) {
	host();
	m::active_session_mode = p::session_mode::independent;
	m::oled_block = 7;
	m::force_o_led = true;
	m::last_o_led = 0;
	for (int step = 0; step < 7; ++step)
		m::transport_routine();
	CHECK_TRUE(sent(p::Op::OLED).empty());
	CHECK_TRUE(m::is_host_client_connection(&cable));
	deluge::hid::display::OLED::remote_frame_available = true;
	for (int step = 0; step < 7; ++step)
		m::transport_routine();
	LONGS_EQUAL(7, sent(p::Op::OLED).size());
}

TEST(MirrorRuntime, independent_panel_routes_only_remote_commands_and_suppresses_physical_output) {
	host();
	m::active_session_mode = p::session_mode::independent;
	CHECK_TRUE(m::panel_byte(188));
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::panel_byte(189));
	}
	m::transport_routine();
	auto packets = sent(p::Op::Panel);
	LONGS_EQUAL(1, packets.size());
	LONGS_EQUAL(1, packets[0].size);
	LONGS_EQUAL(189, packets[0].payload[0]);
	LONGS_EQUAL(1, m::cached_lengths[188]);
	LONGS_EQUAL(0, m::cached_lengths[189]);
	LONGS_EQUAL(1, m::remote_panel.lengths[189]);
}
TEST(MirrorRuntime, panel_parsers_remain_separate_when_owner_changes_mid_command) {
	host();
	m::active_session_mode = p::session_mode::independent;
	CHECK_TRUE(m::panel_byte(19));
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::panel_byte(20));
		CHECK_FALSE(m::panel_byte(1));
	}
	CHECK_TRUE(m::panel_byte(7));
	{
		session::Scope remote(session::Id::Remote);
		for (uint8_t byte : {2, 3, 4})
			CHECK_FALSE(m::panel_byte(byte));
	}
	m::transport_routine();
	auto packets = sent(p::Op::Panel);
	LONGS_EQUAL(1, packets.size());
	LONGS_EQUAL(7, packets[0].size);
	LONGS_EQUAL(19, packets[0].payload[0]);
	LONGS_EQUAL(7, packets[0].payload[1]);
	LONGS_EQUAL(20, packets[0].payload[2]);
	for (size_t index = 1; index < 5; ++index)
		LONGS_EQUAL(index, packets[0].payload[index + 2]);
	LONGS_EQUAL(7, m::cached_commands[19][1]);
}
TEST(MirrorRuntime, visible_panel_stream_excludes_remote_output) {
	host();
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::panel_byte(188));
	}
	m::transport_routine();
	CHECK_TRUE(sent(p::Op::Panel).empty());
}
TEST(MirrorRuntime, independent_initial_snapshot_uses_remote_pad_owner_and_indicator_cache) {
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	CHECK_TRUE(m::panel_byte(188));
	{
		session::Scope remote(session::Id::Remote);
		indicator_leds::setLedState(static_cast<IndicatorLED>(1), true);
	}
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	m::routine();
	CHECK_TRUE(fixture::main_pad_owners.back() == session::Id::Remote);
	CHECK_TRUE(fixture::sidebar_pad_owners.back() == session::Id::Remote);
	bool remote_on = false, local_on = false;
	for (auto packet : sent(p::Op::Panel)) {
		for (size_t offset = 0; offset < packet.size;) {
			uint8_t command = packet.payload[offset];
			remote_on |= command == 189;
			local_on |= command == 188;
			offset += p::panel_command_size(command);
		}
	}
	CHECK_TRUE(remote_on);
	CHECK_FALSE(local_on);
}

TEST(MirrorRuntime, independent_ui_services_timers_and_rendering_under_remote_owner) {
	host();
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	int timers = 0, renders = 0;
	on_ui_timers = [&] {
		CHECK_TRUE(session::current() == session::Id::Remote);
		++timers;
	};
	on_remote_render = [&] {
		CHECK_TRUE(session::current() == session::Id::Remote);
		++renders;
	};
	m::routine();
	LONGS_EQUAL(1, timers);
	LONGS_EQUAL(1, renders);
	m::routine();
	LONGS_EQUAL(2, timers);
	LONGS_EQUAL(1, renders);
	fixture::now += 0.02;
	m::routine();
	LONGS_EQUAL(2, renders);
	CHECK_TRUE(session::current() == session::Id::Local);
}
TEST(MirrorRuntime, remote_ui_service_waits_for_initialization_and_storage) {
	host();
	int calls = 0;
	on_ui_timers = on_remote_render = [&] { ++calls; };
	m::routine();
	m::active_session_mode = p::session_mode::independent;
	m::routine();
	LONGS_EQUAL(0, calls);
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	sdRoutineLock = true;
	m::routine();
	LONGS_EQUAL(0, calls);
	sdRoutineLock = false;
	m::routine();
	LONGS_EQUAL(2, calls);
}
TEST(MirrorRuntime, remote_timer_disconnect_prevents_render_and_remote_frame_send) {
	host();
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	int renders = 0;
	on_remote_render = [&] { ++renders; };
	on_ui_timers = [&] { incoming(p::Op::Stop, 1); };
	m::routine();
	LONGS_EQUAL(0, renders);
	CHECK_TRUE(sent(p::Op::OLED).empty());
	CHECK_TRUE(m::failed);
	m::routine();
	CHECK_FALSE(m::is_host_client_connection(&cable));
}

TEST(MirrorRuntime, independent_graphics_timer_starts_remotely_and_preserves_local_deadline) {
	host();
	uiTimerManager.setTimer(TimerName::GRAPHICS_ROUTINE, 50);
	const auto local_deadline = uiTimerManager.state.get(TimerName::GRAPHICS_ROUTINE).triggerTime;
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::routine();
	{
		session::Scope remote(session::Id::Remote);
		CHECK_TRUE(uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
		LONGS_EQUAL(15, uiTimerManager.state.get(TimerName::GRAPHICS_ROUTINE).triggerTime);
		uiTimerManager.setTimer(TimerName::GRAPHICS_ROUTINE, 7);
	}
	m::routine();
	{
		session::Scope remote(session::Id::Remote);
		LONGS_EQUAL(7, uiTimerManager.state.get(TimerName::GRAPHICS_ROUTINE).triggerTime);
	}
	CHECK_TRUE(uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	LONGS_EQUAL(local_deadline, uiTimerManager.state.get(TimerName::GRAPHICS_ROUTINE).triggerTime);
	m::stop("test");
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	}
	CHECK_TRUE(uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	LONGS_EQUAL(local_deadline, uiTimerManager.state.get(TimerName::GRAPHICS_ROUTINE).triggerTime);
}
TEST(MirrorRuntime, independent_graphics_timer_waits_for_remote_ui) {
	host();
	m::active_session_mode = p::session_mode::independent;
	m::routine();
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	}
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::routine();
	{
		session::Scope remote(session::Id::Remote);
		CHECK_TRUE(uiTimerManager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	}
}

TEST(MirrorRuntime, independent_disconnect_discards_partial_remote_panel_command_only) {
	host();
	m::active_session_mode = p::session_mode::independent;
	CHECK_TRUE(m::panel_byte(19));
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::panel_byte(189));
		CHECK_FALSE(m::panel_byte(20));
		CHECK_FALSE(m::panel_byte(1));
	}
	m::stop("test");
	LONGS_EQUAL(0, m::remote_panel.position);
	LONGS_EQUAL(0, m::remote_panel.length);
	CHECK_FALSE(m::remote_panel.allowed);
	LONGS_EQUAL(1, m::remote_panel.lengths[189]);
	LONGS_EQUAL(1, m::panel_position);
	CHECK_TRUE(m::panel_byte(7));
	LONGS_EQUAL(7, m::cached_commands[19][1]);
	host();
	m::active_session_mode = p::session_mode::independent;
	{
		session::Scope remote(session::Id::Remote);
		CHECK_FALSE(m::panel_byte(190));
	}
	m::transport_routine();
	auto packets = sent(p::Op::Panel);
	LONGS_EQUAL(1, packets.size());
	LONGS_EQUAL(1, packets[0].size);
	LONGS_EQUAL(190, packets[0].payload[0]);
}

TEST(MirrorRuntime, independent_disconnect_discards_partial_panel_from_release_callback) {
	host();
	m::active_session_mode = p::session_mode::independent;
	m::remote_held[2] = true;
	int releases = 0;
	fixture::on_input = [&] {
		++releases;
		deluge::hid::display::OLED::remote_frame_available = true;
		CHECK_TRUE(session::current() == session::Id::Remote);
		CHECK_FALSE(m::panel_byte(20));
	};
	m::stop("test");
	LONGS_EQUAL(1, releases);
	CHECK_FALSE(deluge::hid::display::OLED::remote_frame_available);
	LONGS_EQUAL(0, m::remote_panel.position);
	LONGS_EQUAL(0, m::remote_panel.length);
}

TEST(MirrorRuntime, independent_input_waits_for_remote_navigation_before_dispatch_and_ack) {
	host();
	m::active_session_mode = p::session_mode::independent;
	input(1, 0, 2, 1);
	m::process_input();
	CHECK_TRUE(fixture::events.empty());
	CHECK_TRUE(sent(p::Op::InputAck).empty());
	LONGS_EQUAL(0, m::input_read);
	CHECK_FALSE(m::remote_held[2]);
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::process_input();
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
	CHECK_TRUE(m::remote_held[2]);
}
TEST(MirrorRuntime, independent_input_rechecks_navigation_between_queued_commands) {
	host();
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	input(1, 0, 2, 1);
	input(2, 0, 3, 1);
	fixture::on_input = [] { session::navigation.active().depth = 0; };
	m::process_input();
	LONGS_EQUAL(1, fixture::events.size());
	LONGS_EQUAL(1, sent(p::Op::InputAck).size());
	CHECK_FALSE(m::remote_held[3]);
	fixture::on_input = {};
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::process_input();
	LONGS_EQUAL(2, fixture::events.size());
	LONGS_EQUAL(2, sent(p::Op::InputAck).size());
	CHECK_TRUE(m::remote_held[3]);
}

TEST(MirrorRuntime, independent_initial_snapshot_waits_for_remote_navigation) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	m::routine();
	CHECK_TRUE(m::snapshot_pending);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_TRUE(sent(p::Op::Panel).empty());
	CHECK_TRUE(fixture::main_pad_owners.empty());
	CHECK_TRUE(fixture::sidebar_pad_owners.empty());
	CHECK_TRUE(session::current() == session::Id::Local);
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	m::routine();
	CHECK_FALSE(m::snapshot_pending);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	LONGS_EQUAL(1, fixture::main_pad_owners.size());
	CHECK_TRUE(fixture::main_pad_owners.back() == session::Id::Remote);
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}
TEST(MirrorRuntime, independent_accept_callback_invalidating_navigation_aborts_snapshot) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	fixture::on_send = [] { session::navigation.for_owner(session::Id::Remote).depth = 0; };
	m::routine();
	CHECK_TRUE(m::failed);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_TRUE(fixture::main_pad_owners.empty());
	CHECK_TRUE(fixture::sidebar_pad_owners.empty());
	CHECK_TRUE(sent(p::Op::Panel).empty());
	fixture::on_send = {};
	m::routine();
	CHECK_TRUE(m::state == m::State::Idle);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}

TEST(MirrorRuntime, independent_disconnect_invalidates_remote_frame_before_reconnect) {
	host();
	m::active_session_mode = p::session_mode::independent;
	deluge::hid::display::OLED::remote_frame_available = true;
	session::navigation.for_owner(session::Id::Local).oled_dirty = false;
	m::stop("test");
	CHECK_FALSE(deluge::hid::display::OLED::remote_frame_available);
	CHECK_TRUE(session::navigation.for_owner(session::Id::Remote).oled_dirty);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Local).oled_dirty);
	host();
	m::active_session_mode = p::session_mode::independent;
	m::oled_block = 7;
	m::last_o_led = 0;
	m::transport_routine();
	CHECK_TRUE(sent(p::Op::OLED).empty());
	deluge::hid::display::OLED::remote_frame_available = true;
	deluge::hid::display::OLED::remote_pixels.fill(0x23);
	m::transport_routine();
	CHECK_FALSE(sent(p::Op::OLED).empty());
}

TEST(MirrorRuntime, remote_render_rechecks_locks_after_timer_callbacks_and_resumes) {
	for (bool audio_lock : {false, true}) {
		host();
		m::active_session_mode = p::session_mode::independent;
		auto& remote_navigation = session::navigation.for_owner(session::Id::Remote);
		remote_navigation.depth = 1;
		remote_navigation.oled_dirty = true;
		remote_navigation.main_rows_dirty = 3;
		remote_navigation.side_rows_dirty = 5;
		int renders = 0;
		on_remote_render = [&] { ++renders; };
		on_ui_timers = [&] {
			if (audio_lock)
				AudioEngine::audioRoutineLocked = true;
			else
				sdRoutineLock = true;
		};
		const double previous_render = m::last_remote_render;
		m::service_remote_ui();
		LONGS_EQUAL(0, renders);
		CHECK_TRUE(remote_navigation.oled_dirty);
		LONGS_EQUAL(3, remote_navigation.main_rows_dirty);
		LONGS_EQUAL(5, remote_navigation.side_rows_dirty);
		DOUBLES_EQUAL(previous_render, m::last_remote_render, 0);
		CHECK_FALSE(m::failed);
		CHECK_TRUE(session::current() == session::Id::Local);
		on_ui_timers = {};
		AudioEngine::audioRoutineLocked = sdRoutineLock = false;
		m::service_remote_ui();
		LONGS_EQUAL(1, renders);
		m::stop("test");
		on_remote_render = {};
	}
}

TEST(MirrorRuntime, remote_indicator_producers_feed_transport_and_cache_without_physical_output) {
	host();
	m::active_session_mode = p::session_mode::independent;
	const auto led_writes = fixture::leds.size();
	const auto knob_writes = fixture::knobs.size();
	std::array<uint8_t, 4> expected;
	{
		session::Scope remote(session::Id::Remote);
		indicator_leds::setLedState(IndicatorLED::PLAY, true);
		// Force a level change even if an earlier test left this owner at 64.
		indicator_leds::actuallySetKnobIndicatorLevel(0, 0);
		indicator_leds::actuallySetKnobIndicatorLevel(0, 64);
		expected = indicator_leds::frame_for_session().knobs[0];
	}
	LONGS_EQUAL(led_writes, fixture::leds.size());
	LONGS_EQUAL(knob_writes, fixture::knobs.size());
	const auto led_command = 188 + static_cast<uint8_t>(IndicatorLED::PLAY);
	LONGS_EQUAL(1, m::remote_panel.lengths[led_command]);
	LONGS_EQUAL(5, m::remote_panel.lengths[20]);
	for (size_t i = 0; i < expected.size(); ++i)
		LONGS_EQUAL(expected[i], m::remote_panel.commands[20][i + 1]);
	m::transport_routine();
	bool found_led = false, found_knob = false;
	for (auto packet : sent(p::Op::Panel)) {
		for (size_t offset = 0; offset < packet.size;) {
			auto command = packet.payload[offset];
			found_led |= command == led_command;
			if (command == 20) {
				bool matches = true;
				for (size_t i = 0; i < expected.size(); ++i)
					matches &= packet.payload[offset + i + 1] == expected[i];
				found_knob |= matches;
			}
			offset += p::panel_command_size(command);
		}
	}
	CHECK_TRUE(found_led);
	CHECK_TRUE(found_knob);
}

TEST(MirrorRuntime, independent_shared_panel_settings_route_live_without_local_indicators) {
	host();
	m::active_session_mode = p::session_mode::independent;
	for (uint8_t command : {19, 23, 243}) {
		CHECK_TRUE(m::panel_byte(command));
		CHECK_TRUE(m::panel_byte(7));
	}
	CHECK_TRUE(m::panel_byte(188));
	m::transport_routine();
	auto packets = sent(p::Op::Panel);
	LONGS_EQUAL(1, packets.size());
	LONGS_EQUAL(6, packets[0].size);
	const uint8_t expected[] = {19, 7, 23, 7, 243, 7};
	for (size_t i = 0; i < 6; ++i)
		LONGS_EQUAL(expected[i], packets[0].payload[i]);
}
TEST(MirrorRuntime, remote_snapshot_uses_shared_host_settings_and_remote_indicators) {
	for (uint8_t command : {19, 23, 243}) {
		m::panel_byte(command);
		m::panel_byte(7);
	}
	session::Scope remote(session::Id::Remote);
	// Stale Remote cache entries must not override shared host configuration.
	for (uint8_t command : {19, 23, 243}) {
		m::panel_byte(command);
		m::panel_byte(3);
	}
	indicator_leds::setLedState(static_cast<IndicatorLED>(1), true);
	int settings = 0;
	bool remote_led = false;
	m::emit_indicators([&](const uint8_t* bytes, size_t length) {
		if (m::shared_panel_setting(bytes[0])) {
			++settings;
			LONGS_EQUAL(2, length);
			LONGS_EQUAL(7, bytes[1]);
		}
		remote_led |= bytes[0] == 189;
	});
	LONGS_EQUAL(3, settings);
	CHECK_TRUE(remote_led);
}

TEST(MirrorRuntime, remote_snapshot_uses_indicator_frame_despite_stale_or_missing_command_cache) {
	session::Scope remote(session::Id::Remote);
	auto& frame = const_cast<indicator_leds::IndicatorFrame&>(indicator_leds::frame_for_session());
	frame.leds[0] = true;
	frame.knobs[0] = {1, 2, 3, 4};
	m::panel_byte(189); // stale cache says LED 1 is on; its frame says off.
	m::panel_byte(20);
	for (uint8_t value : {9, 9, 9, 9})
		m::panel_byte(value);
	int led_count = 0, knob_count = 0;
	m::emit_indicators([&](const uint8_t* bytes, size_t length) {
		const auto command = bytes[0];
		if (command >= 152 && command <= 223) {
			++led_count;
			CHECK_TRUE(command == 188 || (command >= 153 && command <= 187));
		}
		if (command == 20 || command == 21) {
			++knob_count;
			LONGS_EQUAL(5, length);
			for (size_t i = 1; i < length; ++i)
				LONGS_EQUAL(command == 20 ? i : 0, bytes[i]);
		}
	});
	LONGS_EQUAL(36, led_count);
	LONGS_EQUAL(2, knob_count);
}

TEST(MirrorRuntime, independent_disconnect_invalidates_numeric_cache_and_snapshot_blanks_until_fresh_render) {
	physical_display.oled = false;
	incoming(p::Op::Request, 0, {0});
	m::routine();
	m::active_session_mode = p::session_mode::independent;
	// Physical host digits remain valid across Remote teardown.
	m::panel_byte(224);
	for (uint8_t value : {9, 8, 7, 6})
		m::panel_byte(value);
	m::remote_held[2] = true;
	fixture::on_input = [] {
		m::panel_byte(224);
		for (uint8_t value : {1, 2, 3, 4})
			m::panel_byte(value);
	};
	m::stop("test");
	fixture::on_input = {};
	LONGS_EQUAL(0, m::remote_panel.lengths[224]);
	LONGS_EQUAL(5, m::cached_lengths[224]);
	LONGS_EQUAL(9, m::cached_commands[224][1]);
	session::Scope remote(session::Id::Remote);
	int displays = 0;
	m::emit_indicators([&](const uint8_t* bytes, size_t length) {
		if (bytes[0] != 224)
			return;
		++displays;
		LONGS_EQUAL(5, length);
		for (size_t i = 1; i < length; ++i)
			LONGS_EQUAL(0, bytes[i]);
	});
	LONGS_EQUAL(1, displays);
	m::panel_byte(224);
	for (uint8_t value : {4, 3, 2, 1})
		m::panel_byte(value);
	displays = 0;
	m::emit_indicators([&](const uint8_t* bytes, size_t length) {
		if (bytes[0] != 224)
			return;
		++displays;
		LONGS_EQUAL(5, length);
		for (size_t i = 1; i < length; ++i)
			LONGS_EQUAL(5 - i, bytes[i]);
	});
	LONGS_EQUAL(1, displays);
}
TEST(MirrorRuntime, remote_oled_snapshot_does_not_emit_numeric_blank) {
	session::Scope remote(session::Id::Remote);
	m::emit_indicators([](const uint8_t* bytes, size_t) { CHECK_TRUE(bytes[0] != 224); });
}

TEST(MirrorRuntime, song_replacement_during_acceptance_prevents_snapshot_and_queued_input) {
	for (auto mode : {p::session_mode::visible_host, p::session_mode::independent}) {
		currentSong = &song;
		incoming(p::Op::Request, 0, {1});
		m::active_session_mode = mode;
		session::navigation.for_owner(session::Id::Remote).depth = 1;
		Song replacement;
		fixture::main_pad_owners.clear();
		fixture::sidebar_pad_owners.clear();
		fixture::events.clear();
		cable.sent.clear();
		fixture::on_send = [&] {
			currentSong = &replacement;
			input(1, 0, 2, 1);
		};
		m::routine();
		CHECK_TRUE(m::failed);
		LONGS_EQUAL(1, sent(p::Op::Accept).size());
		CHECK_TRUE(fixture::main_pad_owners.empty());
		CHECK_TRUE(fixture::sidebar_pad_owners.empty());
		CHECK_TRUE(fixture::events.empty());
		CHECK_TRUE(sent(p::Op::InputAck).empty());
		CHECK_TRUE(sent(p::Op::Panel).empty());
		CHECK_TRUE(sent(p::Op::OLED).empty());
		fixture::on_send = {};
		m::routine();
		CHECK_TRUE(m::state == m::State::Idle);
		currentSong = &song;
	}
}

TEST(MirrorRuntime, remote_render_losing_navigation_blocks_frame_transmission_and_disconnects) {
	host();
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	on_remote_render = [] {
		deluge::hid::display::OLED::remote_frame_available = true;
		deluge::hid::display::OLED::remote_pixels.fill(0x23);
		m::panel_byte(189);
		session::navigation.active().depth = 0;
	};
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(sent(p::Op::Panel).empty());
	CHECK_TRUE(sent(p::Op::OLED).empty());
	CHECK_TRUE(session::current() == session::Id::Local);
	on_remote_render = {};
	m::routine();
	CHECK_TRUE(m::state == m::State::Idle);
	CHECK_FALSE(deluge::hid::display::OLED::remote_frame_available);
}
TEST(MirrorRuntime, remote_render_valid_navigation_change_keeps_session_live) {
	host();
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	on_remote_render = [] {
		session::navigation.active().depth = 2;
		m::panel_byte(189);
	};
	m::routine();
	CHECK_FALSE(m::failed);
	CHECK_TRUE(m::state == m::State::Host);
	LONGS_EQUAL(1, sent(p::Op::Panel).size());
	CHECK_TRUE(session::current() == session::Id::Local);
}

TEST(MirrorRuntime, independent_snapshot_renders_remote_before_acceptance) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	int preparations = 0;
	on_remote_render = [&] {
		CHECK_TRUE(session::current() == session::Id::Remote);
		if (m::snapshot_pending) {
			++preparations;
			CHECK_TRUE(sent(p::Op::Accept).empty());
			CHECK_TRUE(session::navigation.active().oled_dirty);
			CHECK_TRUE(session::navigation.active().main_rows_dirty != 0);
			CHECK_TRUE(session::navigation.active().side_rows_dirty != 0);
		}
	};
	m::routine();
	LONGS_EQUAL(1, preparations);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_TRUE(session::current() == session::Id::Local);
}
TEST(MirrorRuntime, invalidated_remote_snapshot_preparation_never_accepts_client) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	on_remote_render = [] { session::navigation.active().depth = 0; };
	m::routine();
	CHECK_TRUE(m::failed);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_TRUE(sent(p::Op::Panel).empty());
	CHECK_TRUE(sent(p::Op::OLED).empty());
	CHECK_TRUE(session::current() == session::Id::Local);
}

TEST(MirrorRuntime, snapshot_preparation_waits_for_existing_remote_render) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	auto& navigation = session::navigation.for_owner(session::Id::Remote);
	navigation.depth = 1;
	navigation.rendering = true;
	navigation.main_rows_dirty = 3;
	navigation.side_rows_dirty = 5;
	int renders = 0;
	on_remote_render = [&] { ++renders; };
	m::routine();
	LONGS_EQUAL(0, renders);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_FALSE(m::failed);
	LONGS_EQUAL(3, navigation.main_rows_dirty);
	LONGS_EQUAL(5, navigation.side_rows_dirty);
	CHECK_TRUE(session::current() == session::Id::Local);
	navigation.rendering = false;
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_FALSE(m::snapshot_pending);
}
TEST(MirrorRuntime, snapshot_preparation_rechecks_render_in_progress_after_callback) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	auto& navigation = session::navigation.for_owner(session::Id::Remote);
	navigation.depth = 1;
	on_remote_render = [] { session::navigation.active().rendering = true; };
	m::routine();
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_TRUE(m::snapshot_pending);
	CHECK_FALSE(m::failed);
	on_remote_render = {};
	navigation.rendering = false;
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_FALSE(m::snapshot_pending);
}

TEST(MirrorRuntime, snapshot_acceptance_waits_for_each_pending_render_output) {
	for (int pending_output = 0; pending_output < 3; ++pending_output) {
		incoming(p::Op::Request, 0, {1});
		m::active_session_mode = p::session_mode::independent;
		auto& navigation = session::navigation.for_owner(session::Id::Remote);
		navigation.depth = 1;
		cable.sent.clear();
		defer_ui_render = true;
		on_remote_render = [&] {
			navigation.main_rows_dirty = pending_output == 0 ? 1 : 0;
			navigation.side_rows_dirty = pending_output == 1 ? 1 : 0;
			navigation.oled_dirty = pending_output == 2;
		};
		m::routine();
		CHECK_TRUE(m::snapshot_pending);
		CHECK_FALSE(m::failed);
		CHECK_TRUE(sent(p::Op::Accept).empty());
		CHECK_TRUE(sent(p::Op::Panel).empty());
		CHECK_TRUE(session::current() == session::Id::Local);
		on_remote_render = {};
		defer_ui_render = false;
		m::routine();
		CHECK_FALSE(m::snapshot_pending);
		LONGS_EQUAL(1, sent(p::Op::Accept).size());
		m::stop("test");
	}
}

TEST(MirrorRuntime, pending_snapshot_services_remote_timers_to_finish_deferred_render) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	int preparation_ticks = 0;
	defer_ui_render = true;
	on_ui_timers = [&] {
		CHECK_TRUE(session::current() == session::Id::Remote);
		if (m::snapshot_pending && ++preparation_ticks == 2)
			defer_ui_render = false;
	};
	m::routine();
	CHECK_TRUE(m::snapshot_pending);
	LONGS_EQUAL(1, preparation_ticks);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	m::routine();
	CHECK_FALSE(m::snapshot_pending);
	LONGS_EQUAL(2, preparation_ticks);
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
	CHECK_TRUE(session::current() == session::Id::Local);
}
TEST(MirrorRuntime, preparation_timer_stop_prevents_render_and_acceptance) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	int renders = 0;
	on_remote_render = [&] { ++renders; };
	on_ui_timers = [&] { incoming(p::Op::Stop, 1); };
	m::routine();
	CHECK_TRUE(m::failed);
	LONGS_EQUAL(0, renders);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_TRUE(sent(p::Op::Panel).empty());
}
TEST(MirrorRuntime, preparation_timer_audio_lock_defers_render_until_unlock) {
	incoming(p::Op::Request, 0, {1});
	m::active_session_mode = p::session_mode::independent;
	session::navigation.for_owner(session::Id::Remote).depth = 1;
	int renders = 0;
	on_remote_render = [&] { ++renders; };
	on_ui_timers = [] { AudioEngine::audioRoutineLocked = true; };
	m::routine();
	LONGS_EQUAL(0, renders);
	CHECK_TRUE(sent(p::Op::Accept).empty());
	CHECK_FALSE(m::failed);
	on_ui_timers = {};
	AudioEngine::audioRoutineLocked = false;
	m::routine();
	LONGS_EQUAL(1, sent(p::Op::Accept).size());
}
