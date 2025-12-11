#!/usr/bin/env python3
"""Concatenate multiple mono PCM WAV files.

This helper is intentionally simple and only supports 16-bit PCM mono
inputs that share the same sample rate and sample width. It avoids
external dependencies so it can run on macOS and Windows without
additional tooling.
"""
from __future__ import annotations

import argparse
import wave
from pathlib import Path


def concat_wavs(output: Path, inputs: list[Path]) -> None:
    if not inputs:
        raise ValueError("No input WAVs provided")

    params = None
    frames: list[bytes] = []
    for path in inputs:
        with wave.open(str(path), "rb") as wf:
            if wf.getnchannels() != 1:
                raise ValueError(f"{path} is not mono")
            if wf.getsampwidth() != 2:
                raise ValueError(f"{path} is not 16-bit PCM")
            current_params = (
                wf.getnchannels(),
                wf.getsampwidth(),
                wf.getframerate(),
            )
            if params is None:
                params = current_params
            elif params != current_params:
                raise ValueError(
                    f"Input format mismatch: {current_params} does not match {params}"
                )
            frames.append(wf.readframes(wf.getnframes()))

    assert params is not None
    nchannels, sampwidth, framerate = params

    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), "wb") as out:
        out.setnchannels(nchannels)
        out.setsampwidth(sampwidth)
        out.setframerate(framerate)
        for chunk in frames:
            out.writeframes(chunk)


def main() -> None:
    parser = argparse.ArgumentParser(description="Concatenate WAV files")
    parser.add_argument("inputs", nargs="+", type=Path, help="Input WAVs in order")
    parser.add_argument(
        "-o", "--output", required=True, type=Path, help="Output WAV path"
    )
    args = parser.parse_args()

    concat_wavs(args.output, args.inputs)


if __name__ == "__main__":
    main()
