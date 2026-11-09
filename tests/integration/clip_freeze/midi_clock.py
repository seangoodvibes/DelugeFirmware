"""External MIDI source for a DelugeSession, paced by captured guest audio time.

Front-panel control stays in DelugeSession. Its launch options attach a USB
MIDI chardev; this source sends actual MIDI Start/Clock/Stop bytes rather
than changing the firmware's clock flags or calling firmware functions.
"""
import atexit
import socket
import threading
import time


class MidiClock:
    def __init__(self, session, audio_path, socket_path):
        self.audio_path = audio_path
        self.interval = 44100 / 192  # The test song has tick magnitude zero.
        self.stopping = threading.Event()
        self.error = None
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.connection.connect(str(socket_path))
        self.connection.setblocking(False)
        self.thread = None
        atexit.register(self.close)

    def start(self, start_transport=True):
        if start_transport:
            self.connection.sendall(bytes([0xFA]))
        self.thread = threading.Thread(target=self._send_ticks, daemon=True)
        self.thread.start()

    def _send_ticks(self):
        target = self.audio_path.stat().st_size / 8
        try:
            while not self.stopping.is_set():
                # Drain outgoing MIDI too. Otherwise a full duplex socket can
                # backpressure the emulated USB controller after many clocks.
                try:
                    while self.connection.recv(4096):
                        pass
                except BlockingIOError:
                    pass
                now = self.audio_path.stat().st_size / 8
                if now >= target:
                    try:
                        self.connection.send(bytes([0xF8]))
                        target += self.interval
                    except BlockingIOError:
                        self.stopping.wait(.0005)
                else:
                    self.stopping.wait(.0005)
        except (OSError, ValueError) as exc:
            if not self.stopping.is_set():
                self.error = repr(exc)

    def close(self):
        if self.stopping.is_set():
            return
        self.stopping.set()
        if self.thread:
            self.thread.join(timeout=1)
        try:
            self.connection.sendall(bytes([0xFC]))
        except OSError:
            pass
        self.connection.close()
