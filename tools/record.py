"""
BLE Audio Receiver - Debug tool for firmware record & publish testing.

Connects to the ESP32 "bar" device over BLE, subscribes to audio notifications
on the NUS TX characteristic, collects audio fragments by messageId, and saves
completed recordings as .wav files.

Protocol:
    Bytes 0-1: messageId (uint16 LE)
    Bytes 2-3: fragmentId (uint16 LE) - 0xFFFF = end sentinel
    Bytes 4+:  Audio data (16-bit PCM, 16kHz mono)
"""

import asyncio
import struct
import time
import wave
from datetime import datetime
from pathlib import Path

from bleak import BleakClient, BleakScanner

# BLE Configuration (matches firmware config.h)
DEVICE_NAME     = "bar"
SERVICE_UUID    = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
CHAR_UUID_TX    = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

# Audio Configuration (matches firmware config.h)
SAMPLE_RATE     = 8000
SAMPLE_WIDTH    = 2       # 16-bit = 2 bytes
CHANNELS        = 1
FRAGMENT_SENTINEL = 0xFFFF

# Output directory for recordings
OUTPUT_DIR = Path(__file__).parent / "recordings"


TIMEOUT_S = 2.0  # save recording if no new fragments arrive within this many seconds


class AudioReceiver:
    def __init__(self):
        # {messageId: {fragmentId: audio_data}}
        self.recordings: dict[int, dict[int, bytes]] = {}
        # {messageId: timestamp of last received fragment}
        self.last_rx: dict[int, float] = {}
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    def _on_notification(self, _handle: int, payload: bytearray):
        if len(payload) < 4:
            print(f"Invalid fragment: payload too short ({len(payload)} bytes)")
            return

        message_id, fragment_id = struct.unpack("<HH", payload[:4])
        audio_data = bytes(payload[4:])

        if fragment_id == FRAGMENT_SENTINEL:
            print(f"[MSG {message_id}] Received end sentinel")
            self._save_recording(message_id)
            return

        if message_id not in self.recordings:
            self.recordings[message_id] = {}
            print(f"[MSG {message_id}] New recording started")

        self.recordings[message_id][fragment_id] = audio_data
        self.last_rx[message_id] = time.monotonic()

        checksum = sum(audio_data)
        first8 = [int.from_bytes(bytes([b]), "little", signed=True) for b in audio_data[:8]]
        print(f"RX frag={fragment_id} len={len(audio_data)} sum={checksum} first8={first8}")

    def _save_recording(self, message_id: int):
        if message_id not in self.recordings:
            print(f"[MSG {message_id}] No fragments to save")
            return

        self.last_rx.pop(message_id, None)
        fragments = self.recordings.pop(message_id)
        if not fragments:
            print(f"[MSG {message_id}] Empty recording, skipping")
            return

        sorted_ids = sorted(fragments.keys())
        audio_data = b"".join(fragments[fid] for fid in sorted_ids)

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = OUTPUT_DIR / f"recording_{message_id}_{timestamp}.wav"

        with wave.open(str(filename), "wb") as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH)
            wav_file.setframerate(SAMPLE_RATE)
            wav_file.writeframes(audio_data)

        duration_ms = (len(audio_data) / SAMPLE_WIDTH) / SAMPLE_RATE * 1000
        print(
            f"[MSG {message_id}] Saved: {filename.name} "
            f"({len(sorted_ids)} fragments, {len(audio_data)} bytes, {duration_ms:.0f}ms)"
        )

    async def _timeout_watcher(self):
        while True:
            await asyncio.sleep(0.5)
            now = time.monotonic()
            timed_out = [mid for mid, ts in self.last_rx.items() if now - ts >= TIMEOUT_S]
            for mid in timed_out:
                print(f"[MSG {mid}] Timeout — saving without sentinel")
                self._save_recording(mid)

    async def run(self):
        print(f"Scanning for '{DEVICE_NAME}'...")
        device = await BleakScanner.find_device_by_name(DEVICE_NAME)
        if device is None:
            print(f"Device '{DEVICE_NAME}' not found. Is it advertising?")
            return

        print(f"Found {device.name} [{device.address}]. Connecting...")
        async with BleakClient(device) as client:
            print("Connected. Subscribing to audio notifications...")
            await client.start_notify(CHAR_UUID_TX, self._on_notification)
            print("Waiting for audio fragments... (Ctrl+C to stop)")
            watcher = asyncio.create_task(self._timeout_watcher())
            try:
                await asyncio.get_event_loop().create_future()  # run until cancelled
            except asyncio.CancelledError:
                pass
            finally:
                watcher.cancel()
                await client.stop_notify(CHAR_UUID_TX)
                print("Disconnected.")


def main():
    receiver = AudioReceiver()
    try:
        asyncio.run(receiver.run())
    except KeyboardInterrupt:
        print("\nShutting down...")


if __name__ == "__main__":
    main()
