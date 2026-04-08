"""
test_record.py — Key-triggered laptop-side receiver for test_record firmware.

Press any key to trigger one recording. The firmware records, streams PCM
audio back over USB-serial, and this script saves it as a .wav file in
tools/output/. Recordings are numbered sequentially: recording_1.wav, etc.

Usage:
    uv run python tools/test_record.py --port COM3            (Windows)
    uv run python tools/test_record.py --port /dev/ttyUSB0   (Linux/macOS)

Protocol (host → device):
    1 byte: 0x01 = START

Protocol (device → host):
    [0xAB][0xCD][TYPE:1][LEN:2LE][PAYLOAD:LEN]
    TYPE 0x01 = AUDIO_CHUNK   raw int16 PCM, little-endian
    TYPE 0x02 = REC_END       signals end of one recording

Protocol (host → device after REC_END):
    1 byte: 0x01 = ACK, 0x00 = NACK
"""

import argparse
import math
import struct
import sys
import wave
from pathlib import Path

import serial

# ── Hardware / protocol configuration (must match firmware config.h) ──────────
BAUD_RATE    = 921600
SAMPLE_RATE  = 8000
SAMPLE_WIDTH = 2        # int16 = 2 bytes
CHANNELS     = 1
GAIN         = 8        # amplification applied before saving

CMD_START    = bytes([0x01])
MAGIC        = bytes([0xAB, 0xCD])
TYPE_CHUNK   = 0x01
TYPE_REC_END = 0x02

ACK  = bytes([0x01])
NACK = bytes([0x00])

OUTPUT_DIR = Path(__file__).parent / "output"


# ── Cross-platform single keypress ────────────────────────────────────────────

def _wait_for_keypress() -> str:
    """Block until a single key is pressed; return the key character."""
    if sys.platform == "win32":
        import msvcrt
        return msvcrt.getch().decode(errors="replace")
    else:
        import termios, tty
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            return sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)


# ── Serial helpers ─────────────────────────────────────────────────────────────

def read_exact(port: serial.Serial, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = port.read(n - len(buf))
        if not chunk:
            raise EOFError("Serial port disconnected")
        buf += chunk
    return buf


def read_packet(port: serial.Serial) -> tuple[int, bytes]:
    """Block until one complete framed packet is received.
    Returns (packet_type, payload_bytes).
    """
    window = bytearray(2)
    while True:
        window[0] = window[1]
        window[1] = read_exact(port, 1)[0]
        if bytes(window) == MAGIC:
            break

    header   = read_exact(port, 3)   # type(1) + len_lo(1) + len_hi(1)
    pkt_type = header[0]
    pkt_len  = struct.unpack_from("<H", header, 1)[0]
    payload  = read_exact(port, pkt_len) if pkt_len > 0 else b""
    return pkt_type, payload


# ── Recording helpers ──────────────────────────────────────────────────────────

def receive_recording(port: serial.Serial) -> bytes:
    """Accumulate AUDIO_CHUNK packets until REC_END. Returns raw PCM bytes."""
    pcm = bytearray()
    while True:
        pkt_type, payload = read_packet(port)
        if pkt_type == TYPE_CHUNK:
            pcm.extend(payload)
        elif pkt_type == TYPE_REC_END:
            return bytes(pcm)


def apply_gain(pcm: bytes) -> bytes:
    n       = len(pcm) // SAMPLE_WIDTH
    samples = struct.unpack(f"<{n}h", pcm)
    clipped = (max(-32768, min(32767, s * GAIN)) for s in samples)
    return struct.pack(f"<{n}h", *clipped)


def save_wav(pcm: bytes, path: Path) -> None:
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(pcm)


def next_recording_path(out_dir: Path) -> Path:
    """Return output/recording_N.wav where N is the next unused number."""
    n = 1
    while (out_dir / f"recording_{n}.wav").exists():
        n += 1
    return out_dir / f"recording_{n}.wav"


def print_stats(pcm: bytes) -> None:
    n       = len(pcm) // SAMPLE_WIDTH
    samples = struct.unpack(f"<{n}h", pcm)
    rms     = math.sqrt(sum(s * s for s in samples) / n) if n else 0
    dur     = len(pcm) / (SAMPLE_RATE * SAMPLE_WIDTH)
    print(f"  {dur:.2f}s  rms={rms:.1f}  min={min(samples)}  max={max(samples)}")


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Key-triggered ESP32 recorder")
    parser.add_argument("--port", required=True, help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--out",  default=str(OUTPUT_DIR), help="Output directory")
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Connecting to {args.port} @ {BAUD_RATE} baud...")
    with serial.Serial(args.port, BAUD_RATE, timeout=5) as port:
        print(f"Connected. Saving to {out_dir.resolve()}")
        print("Press any key to record. Ctrl+C to quit.\n")

        while True:
            path = next_recording_path(out_dir)
            print(f"[{path.name}]  Press any key to start...", end="", flush=True)

            try:
                _wait_for_keypress()
            except KeyboardInterrupt:
                print("\nStopped.")
                return

            print(" recording...", end="", flush=True)

            try:
                port.write(CMD_START)
                port.flush()

                pcm = receive_recording(port)
                print_stats(pcm)

                try:
                    save_wav(apply_gain(pcm), path)
                    port.write(ACK)
                    port.flush()
                    print(f"  → {path.name}  [ACK]")
                except Exception as exc:
                    port.write(NACK)
                    port.flush()
                    print(f"  [NACK] {exc}")

            except EOFError:
                print("\nDevice disconnected.")
                return
            except KeyboardInterrupt:
                print("\nStopped.")
                return


if __name__ == "__main__":
    main()
