#!/usr/bin/env python3
"""Render a public-domain score to a CC0 sample track, from scratch.

The samples this player ships must be redistributable without conditions, and
that rules out the obvious shortcut. A recording carries the performer's rights
even when the composition is public domain, and rendering a MIDI through a
sampled soundfont — FluidR3, TimGM, any of them — embeds that soundfont's
recorded waveforms in the output, so the result inherits whatever licence the
soundfont carries. Neither can honestly be called CC0.

So the tone here is computed, not sampled: a struck-string model built from a
harmonic series with per-partial decay, a hammer-noise transient, and a touch
of detuning between two voices per note. Nothing in the output originates in
anyone else's file. The only input is the note list in a public-domain score.

Usage:
    python3 tools/render_sample.py score.mid out.ogg ["Title for the notes"]

The MIDI reader is deliberately small: it handles what a piano score uses —
tempo changes, note on/off across several tracks — and nothing else.
"""
from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

RATE = 44100


# ── the smallest MIDI reader that can read a piano score ────────────────────

def _varint(data: bytes, index: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[index]
        index += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, index


def read_midi(path: str) -> tuple[list[tuple[float, float, int, int]], float]:
    """Return (start, duration, midi note, velocity) in seconds, and the end."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise SystemExit(f"{path}: not a MIDI file")
    _fmt, track_count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise SystemExit("SMPTE time division is not supported")

    # Collect every event with its absolute tick, across all tracks, then walk
    # them in tick order so one tempo map applies to all of them.
    events: list[tuple[int, int, bytes]] = []
    index = 14
    for order in range(track_count):
        if data[index:index + 4] != b"MTrk":
            break
        length = struct.unpack(">I", data[index + 4:index + 8])[0]
        end = index + 8 + length
        cursor = index + 8
        tick = 0
        status = 0
        while cursor < end:
            delta, cursor = _varint(data, cursor)
            tick += delta
            byte = data[cursor]
            if byte & 0x80:
                status = byte
                cursor += 1
            if status == 0xFF:                     # meta
                kind = data[cursor]
                cursor += 1
                size, cursor = _varint(data, cursor)
                events.append((tick, order, bytes([0xFF, kind]) +
                               data[cursor:cursor + size]))
                cursor += size
            elif status in (0xF0, 0xF7):           # sysex, skipped
                size, cursor = _varint(data, cursor)
                cursor += size
            else:
                size = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
                events.append((tick, order, bytes([status]) +
                               data[cursor:cursor + size]))
                cursor += size
        index = end

    events.sort(key=lambda item: (item[0], item[1]))
    notes: list[tuple[float, float, int, int]] = []
    open_notes: dict[tuple[int, int], tuple[float, int]] = {}
    seconds_per_tick = 0.5 / division              # 120bpm until told otherwise
    now = 0.0
    tick_at = 0
    for tick, _order, payload in events:
        now += (tick - tick_at) * seconds_per_tick
        tick_at = tick
        if payload[0] == 0xFF:
            if payload[1] == 0x51 and len(payload) >= 5:      # set tempo
                micros = int.from_bytes(payload[2:5], "big")
                seconds_per_tick = micros / 1_000_000.0 / division
            continue
        kind, channel = payload[0] & 0xF0, payload[0] & 0x0F
        if kind == 0x90 and payload[2]:                       # note on
            open_notes[(channel, payload[1])] = (now, payload[2])
        elif kind in (0x80, 0x90):                            # note off
            started = open_notes.pop((channel, payload[1]), None)
            if started:
                start, velocity = started
                notes.append((start, max(0.05, now - start), payload[1],
                              velocity))
    end = max((start + length for start, length, _n, _v in notes), default=0.0)
    return notes, end


# ── a struck string, computed rather than sampled ───────────────────────────

def voice(note: int, seconds: float, velocity: int) -> np.ndarray:
    """One piano-ish note: harmonic series, per-partial decay, hammer noise."""
    freq = 440.0 * 2.0 ** ((note - 69) / 12.0)
    # Let every note ring past its written length; a piano does, and cutting it
    # at note-off is what makes a synthesised score sound like a organ.
    length = seconds + 1.6
    count = int(length * RATE)
    t = np.arange(count) / RATE
    amplitude = (velocity / 127.0) ** 1.4

    out = np.zeros(count)
    partial = 1
    while partial <= 12 and freq * partial < RATE / 2:
        # Higher partials start quieter and die sooner — that ratio is most of
        # what separates a struck string from a sawtooth.
        gain = 1.0 / partial ** 1.7
        decay = np.exp(-t * (1.6 + 0.55 * partial + freq / 2200.0))
        # Slight inharmonicity, as in a real string under tension.
        stretched = freq * partial * (1.0 + 0.00045 * partial * partial)
        out += gain * decay * np.sin(2 * np.pi * stretched * t)
        partial += 1

    # Two voices a few cents apart keep a single note from sounding sterile.
    detune = np.sin(2 * np.pi * freq * 1.0008 * t) * np.exp(-t * 2.2) * 0.18
    out += detune

    hammer = np.random.default_rng(note).standard_normal(count)
    hammer *= np.exp(-t * 120.0) * 0.06
    out += hammer

    attack = np.minimum(1.0, t / 0.004)            # 4ms, no click
    release = np.minimum(1.0, np.maximum(0.0, (length - t) / 0.25))
    return out * attack * release * amplitude * 0.28


def render(notes, end: float) -> np.ndarray:
    total = int((end + 2.5) * RATE)
    left = np.zeros(total)
    right = np.zeros(total)
    for start, seconds, note, velocity in notes:
        wave_data = voice(note, seconds, velocity)
        at = int(start * RATE)
        stop = min(total, at + len(wave_data))
        chunk = wave_data[:stop - at]
        # Low notes sit slightly left, high notes slightly right, the way a
        # piano is heard from the player's seat.
        pan = 0.5 + (note - 60) / 190.0
        pan = min(0.78, max(0.22, pan))
        left[at:stop] += chunk * (1.0 - pan)
        right[at:stop] += chunk * pan
    peak = max(np.max(np.abs(left)), np.max(np.abs(right)), 1e-9)
    scale = 0.89 / peak
    stereo = np.empty(total * 2)
    stereo[0::2] = left * scale
    stereo[1::2] = right * scale
    return stereo


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    source, destination = argv[0], argv[1]
    notes, end = read_midi(source)
    print(f"{len(notes)} notes, {end:.1f}s")
    samples = render(notes, end)
    pcm = (np.clip(samples, -1.0, 1.0) * 32767).astype("<i2")
    with tempfile.NamedTemporaryFile(suffix=".wav") as raw:
        with wave.open(raw.name, "wb") as handle:
            handle.setnchannels(2)
            handle.setsampwidth(2)
            handle.setframerate(RATE)
            handle.writeframes(pcm.tobytes())
        command = ["ffmpeg", "-loglevel", "error", "-y", "-i", raw.name,
                   "-c:a", "libvorbis", "-qscale:a", "3"]
        if len(argv) > 2:
            command += ["-metadata", f"title={argv[2]}"]
        command.append(destination)
        subprocess.run(command, check=True)
    print(f"wrote {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
