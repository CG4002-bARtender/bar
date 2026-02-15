"""
MQTT Audio Receiver - Debug tool for firmware record & publish testing.

Subscribes to glove/audio topic, collects audio fragments by messageId,
and saves completed recordings as .wav files.

Protocol:
    Bytes 0-1: messageId (uint16 LE)
    Bytes 2-3: fragmentId (uint16 LE) - 0xFFFF = end sentinel
    Bytes 4+:  Audio data (16-bit PCM, 16kHz mono)
"""

import ssl
import struct
import wave
from datetime import datetime
from pathlib import Path

import paho.mqtt.client as mqtt

# MQTT Configuration (matches firmware config.h)
MQTT_BROKER = "192.168.1.4"
MQTT_PORT = 8883
MQTT_USERNAME = "test"
MQTT_PASSWORD = "test"
MQTT_CLIENT_ID = "python_audio_receiver"
MQTT_AUDIO_TOPIC = "audio"

# Audio Configuration
SAMPLE_RATE = 16000
SAMPLE_WIDTH = 2  # 16-bit = 2 bytes
CHANNELS = 1

# Sentinel value indicating end of recording
FRAGMENT_SENTINEL = 0xFFFF

# Output directory for recordings
OUTPUT_DIR = Path(__file__).parent / "recordings"


class AudioReceiver:
    def __init__(self):
        # Dict to store fragments: {messageId: {fragmentId: audio_data}}
        self.recordings: dict[int, dict[int, bytes]] = {}
        self.client = mqtt.Client(
            client_id=MQTT_CLIENT_ID,
            protocol=mqtt.MQTTv311,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        )

        # Setup callbacks
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

        # Setup TLS (insecure for dev - matches firmware)
        self.client.tls_set(cert_reqs=ssl.CERT_NONE)
        self.client.tls_insecure_set(True)

        # Auth
        self.client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

        # Ensure output directory exists
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            print(f"Connected to MQTT broker: {MQTT_BROKER}:{MQTT_PORT}")
            client.subscribe(MQTT_AUDIO_TOPIC)
            print(f"Subscribed to: {MQTT_AUDIO_TOPIC}")
            print("Waiting for audio fragments...")
        else:
            print(f"Connection failed with code: {reason_code}")

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        print(f"Disconnected from broker (code: {reason_code})")

    def _on_message(self, client, userdata, msg):
        payload = msg.payload

        if len(payload) < 4:
            print(f"Invalid fragment: payload too short ({len(payload)} bytes)")
            return

        # Parse header (little-endian uint16 for messageId and fragmentId)
        message_id, fragment_id = struct.unpack("<HH", payload[:4])
        audio_data = payload[4:]

        # Check for sentinel (end of recording)
        if fragment_id == FRAGMENT_SENTINEL:
            print(f"[MSG {message_id}] Received end sentinel")
            self._save_recording(message_id)
            return

        # Store fragment
        if message_id not in self.recordings:
            self.recordings[message_id] = {}
            print(f"[MSG {message_id}] New recording started")

        self.recordings[message_id][fragment_id] = audio_data

        # Debug: print checksum and first 8 audio bytes (as signed int8)
        checksum = sum(audio_data)
        first8 = [int.from_bytes(bytes([b]), 'little', signed=True) for b in audio_data[:8]]
        print(
            f"RX frag={fragment_id} len={len(audio_data)} sum={checksum} first8={first8}"
        )

    def _save_recording(self, message_id: int):
        if message_id not in self.recordings:
            print(f"[MSG {message_id}] No fragments to save")
            return

        fragments = self.recordings.pop(message_id)

        if not fragments:
            print(f"[MSG {message_id}] Empty recording, skipping")
            return

        # Sort fragments by fragmentId and concatenate audio data
        sorted_fragment_ids = sorted(fragments.keys())
        audio_data = b"".join(fragments[fid] for fid in sorted_fragment_ids)

        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = OUTPUT_DIR / f"recording_{message_id}_{timestamp}.wav"

        # Write WAV file
        with wave.open(str(filename), "wb") as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH)
            wav_file.setframerate(SAMPLE_RATE)
            wav_file.writeframes(audio_data)

        duration_ms = (len(audio_data) / SAMPLE_WIDTH) / SAMPLE_RATE * 1000
        print(
            f"[MSG {message_id}] Saved: {filename.name} "
            f"({len(sorted_fragment_ids)} fragments, {len(audio_data)} bytes, {duration_ms:.0f}ms)"
        )

    def run(self):
        print(f"Connecting to {MQTT_BROKER}:{MQTT_PORT}...")
        self.client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)

        try:
            self.client.loop_forever()
        except KeyboardInterrupt:
            print("\nShutting down...")
            self.client.disconnect()


def main():
    receiver = AudioReceiver()
    receiver.run()


if __name__ == "__main__":
    main()
