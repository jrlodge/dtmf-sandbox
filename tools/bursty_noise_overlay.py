#!/usr/bin/env python3
"""Create a bursty noise mix on top of a base WAV file.

The script reads a base 16-bit mono PCM WAV, constructs a mostly silent
noise track with a few random bursts, scales the noise to the requested
SNR relative to the base, and writes the mixed output.
"""
from __future__ import annotations

import argparse
import math
import random
import sys
import wave
from array import array
from pathlib import Path
from typing import Tuple


def read_pcm16_mono(path: Path) -> Tuple[int, array]:
    with wave.open(str(path), "rb") as wf:
        if wf.getnchannels() != 1:
            raise ValueError(f"{path} is not mono")
        if wf.getsampwidth() != 2:
            raise ValueError(f"{path} is not 16-bit PCM")
        framerate = wf.getframerate()
        data = array("h")
        data.frombytes(wf.readframes(wf.getnframes()))
    return framerate, data


def write_pcm16_mono(path: Path, framerate: int, samples: array) -> None:
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(framerate)
        wf.writeframes(samples.tobytes())


def rms(samples: array) -> float:
    if not samples:
        return 0.0
    acc = 0.0
    for s in samples:
        acc += float(s) * float(s)
    return math.sqrt(acc / len(samples))


def clamp_int16(value: float) -> int:
    return max(-32768, min(32767, int(round(value))))


def build_noise_track(
    length: int, framerate: int, mode: str, noise_source: array | None
) -> array:
    bursts = random.randint(2, 4)
    output = array("h", [0] * length)

    def fill_white(start: int, end: int) -> None:
        for i in range(start, end):
            output[i] = clamp_int16(random.gauss(0, 0.35 * 32767))

    def fill_from_source(start: int, end: int) -> None:
        if not noise_source:
            return
        src_len = len(noise_source)
        if src_len == 0:
            return
        for idx, pos in enumerate(range(start, end)):
            output[pos] = noise_source[idx % src_len]

    for _ in range(bursts):
        dur_ms = random.randint(200, 400)
        dur_samples = int(framerate * dur_ms / 1000)
        if dur_samples <= 0 or dur_samples >= length:
            continue
        start = random.randint(0, length - dur_samples)
        end = start + dur_samples
        if mode == "white":
            fill_white(start, end)
        else:
            fill_from_source(start, end)

    return output


def mix_bursty_noise(
    base_path: Path, out_path: Path, snr_db: float, noise_mode: str, noise_path: Path | None
) -> None:
    framerate, base_samples = read_pcm16_mono(base_path)
    noise_source: array | None = None
    if noise_mode != "white":
        if noise_path is None:
            raise ValueError("noise_path is required for non-white noise")
        noise_rate, noise_source = read_pcm16_mono(noise_path)
        if noise_rate != framerate:
            raise ValueError(
                f"Sample rate mismatch: base={framerate}Hz noise={noise_rate}Hz"
            )

    noise_track = build_noise_track(len(base_samples), framerate, noise_mode, noise_source)

    base_rms = rms(base_samples)
    noise_rms = rms(noise_track)
    if base_rms == 0:
        base_rms = 1e-6
        print("Warning: base is silent; using epsilon to continue", file=sys.stderr)
    if noise_rms == 0:
        noise_rms = 1e-6
        print("Warning: noise track is silent; using epsilon to continue", file=sys.stderr)

    target_noise_rms = base_rms / (10 ** (snr_db / 20))
    scale = target_noise_rms / noise_rms

    mixed = array("h")
    for b, n in zip(base_samples, noise_track):
        mixed.append(clamp_int16(float(b) + float(n) * scale))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_pcm16_mono(out_path, framerate, mixed)


def main() -> None:
    parser = argparse.ArgumentParser(description="Apply bursty noise to a WAV file")
    parser.add_argument("--base", required=True, type=Path, help="Base WAV file")
    parser.add_argument("--out", required=True, type=Path, help="Output WAV path")
    parser.add_argument("--snr-db", required=True, type=float, help="Target SNR in dB")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--noise", choices=["white"], help="Use white noise bursts")
    group.add_argument("--noise-wav", type=Path, help="Use an ATC noise WAV for bursts")

    args = parser.parse_args()
    noise_mode = "white" if args.noise == "white" else "atc"

    mix_bursty_noise(args.base, args.out, args.snr_db, noise_mode, args.noise_wav)


if __name__ == "__main__":
    main()
