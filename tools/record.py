"""
MQTT Audio Receiver - Debug tool for firmware record & publish testing.

Connects to the MQTT broker, subscribes to the audio topic, accumulates raw
PCM chunks, and saves completed recordings as .wav files.

Protocol:
    Topic "audio": raw 16-bit PCM audio chunks (no header)
    Topic "ack":   ACK (0x01) or NACK (0x00) published back to firmware

ACK is sent immediately once EXPECTED_BYTES (2s of audio) are received.
NACK + save triggered by TIMEOUT_S fallback for short/interrupted recordings.
"""

import time
import threading
import wave
from datetime import datetime
from pathlib import Path

import paho.mqtt.client as mqtt

# MQTT Configuration (matches firmware config.h)
BROKER   = "10.187.150.191"
PORT     = 1883
USERNAME = "test"
PASSWORD = "test"

TOPIC_AUDIO = "audio"
TOPIC_ACK   = "ack"

# Audio Configuration (matches firmware config.h)
SAMPLE_RATE  = 8000
SAMPLE_WIDTH = 2    # 16-bit = 2 bytes
CHANNELS     = 1

# Exact expected byte count: 8000 samples/s × 2 bytes × 2 seconds
EXPECTED_BYTES = SAMPLE_RATE * SAMPLE_WIDTH * 2   # 32 000 bytes

ACK  = bytes([0x01])
NACK = bytes([0x00])

# Fallback: save + NACK if no new chunk arrives within this many seconds
TIMEOUT_S = 1.0

OUTPUT_DIR = Path(__file__).parent / "recordings"


class AudioReceiver:
    def __init__(self):
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

        self._audio_buf: list[bytes] = []
        self._total_bytes            = 0
        self._last_rx: float | None  = None
        self._recording              = False
        self._cooldown_until         = 0.0   # ignore chunks until this monotonic time
        self._lock                   = threading.Lock()

        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self._client.username_pw_set(USERNAME, PASSWORD)
        self._client.on_connect    = self._on_connect
        self._client.on_message    = self._on_message
        self._client.on_disconnect = self._on_disconnect

    # ── MQTT callbacks ─────────────────────────────────────────────────────────

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            print(f"Connected to broker {BROKER}:{PORT}")
            client.subscribe(TOPIC_AUDIO)
            print(f"Subscribed to '{TOPIC_AUDIO}'. Waiting for audio... (Ctrl+C to stop)")
        else:
            print(f"Connection failed: {reason_code}")

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        print(f"Disconnected ({reason_code})")

    def _on_message(self, client, userdata, msg):
        if msg.topic != TOPIC_AUDIO:
            return

        chunk = bytes(msg.payload)
        if not chunk:
            return

        flush_data = None
        with self._lock:
            if time.monotonic() < self._cooldown_until:
                return

            if not self._recording:
                self._audio_buf.clear()
                self._total_bytes = 0
                self._recording   = True
                print("Recording started.")

            self._audio_buf.append(chunk)
            self._total_bytes += len(chunk)
            self._last_rx = time.monotonic()
            print(f"  chunk {len(self._audio_buf):>3}  +{len(chunk)} bytes  total={self._total_bytes}")

            if self._total_bytes >= EXPECTED_BYTES:
                flush_data        = b"".join(self._audio_buf)
                self._audio_buf.clear()
                self._total_bytes = 0
                self._last_rx     = None
                self._recording   = False

        if flush_data is not None:
            self._save_and_ack(flush_data, ack=True)

    # ── Timeout fallback (runs in background thread) ───────────────────────────

    def _timeout_watcher(self):
        while True:
            time.sleep(0.25)
            flush_data = None
            with self._lock:
                if self._recording and self._last_rx is not None:
                    if time.monotonic() - self._last_rx >= TIMEOUT_S:
                        flush_data        = b"".join(self._audio_buf)
                        self._audio_buf.clear()
                        self._total_bytes = 0
                        self._last_rx     = None
                        self._recording   = False

            if flush_data is not None:
                self._save_and_ack(flush_data, ack=False)

    def _save_and_ack(self, audio_data: bytes, ack: bool, cooldown_s: float = 1.0):
        if not audio_data:
            return

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename  = OUTPUT_DIR / f"recording_{timestamp}.wav"

        with wave.open(str(filename), "wb") as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH)
            wav_file.setframerate(SAMPLE_RATE)
            wav_file.writeframes(audio_data)

        duration_ms = (len(audio_data) / SAMPLE_WIDTH) / SAMPLE_RATE * 1000
        label = "ACK" if ack else "NACK"
        print(f"Saved: {filename.name} ({len(audio_data)} bytes, {duration_ms:.0f}ms) → {label}")

        self._client.publish(TOPIC_ACK, ACK if ack else NACK)
        print(f"Published {label} to '{TOPIC_ACK}'")
        with self._lock:
            self._cooldown_until = time.monotonic() + cooldown_s

    # ── Entry point ────────────────────────────────────────────────────────────

    def run(self):
        threading.Thread(target=self._timeout_watcher, daemon=True).start()

        self._client.connect(BROKER, PORT, keepalive=60)
        try:
            self._client.loop_forever()
        except KeyboardInterrupt:
            print("\nShutting down...")
        finally:
            self._client.disconnect()


def main():
    AudioReceiver().run()


if __name__ == "__main__":
    main()
