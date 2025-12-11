#!/usr/bin/env python3
"""Summarise harmonic ratio distributions from decoder dumps.

This helper consumes CSVs generated when running the decoder with
DTMF_DUMP_HARMONICS=1. It separates frames into digit vs non-digit
buckets and reports basic statistics for the second-harmonic ratios so
thresholds can be tuned from data instead of guesswork.
"""

from __future__ import annotations

import csv
import statistics
import sys
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

HARMONICS_ROOT = Path("artifacts/harmonics")


def normalize_code(token: str) -> str:
    token = token.replace("star", "*").replace("hash", "#")
    return token


def parse_metadata(path: Path) -> Dict[str, str]:
    stem = path.stem
    tokens = stem.split("__")

    code = ""
    for token in tokens:
        if token.startswith("code_"):
            code = normalize_code(token[len("code_") :])
        elif token == "noise_only" or token.startswith("silence"):
            code = ""

    # noise_only and silence files may not use the code_ prefix; encode their
    # intent via code="" so downstream buckets treat all frames as non-digits.
    return {"code": code, "stem": stem}


def percentile(values: Sequence[float], pct: float) -> float:
    if not values:
        return float("nan")
    if len(values) == 1:
        return values[0]

    ordered = sorted(values)
    k = (len(ordered) - 1) * pct / 100.0
    lower = int(k)
    upper = min(lower + 1, len(ordered) - 1)
    weight = k - lower
    return ordered[lower] * (1 - weight) + ordered[upper] * weight


def compute_stats(values: List[float]) -> Dict[str, float]:
    if not values:
        return {"count": 0}

    return {
        "count": len(values),
        "mean": statistics.fmean(values),
        "stdev": statistics.pstdev(values),
        "min": min(values),
        "max": max(values),
        "p5": percentile(values, 5),
        "p50": percentile(values, 50),
        "p95": percentile(values, 95),
    }


def format_stat_line(label: str, stats: Dict[str, float]) -> str:
    if not stats.get("count"):
        return f"  {label}: N=0"

    return (
        f"  {label}:    N={stats['count']}, "
        f"row2_ratio_db median={stats['p50']:.2f} dB, 5–95%=[{stats['p5']:.2f}, {stats['p95']:.2f}]\n"
        f"               col2_ratio_db median={stats['col_p50']:.2f} dB, 5–95%=[{stats['col_p5']:.2f}, {stats['col_p95']:.2f}]"
    )


def summarise_file(path: Path) -> Tuple[List[float], List[float], List[float], List[float]]:
    meta = parse_metadata(path)
    has_code = bool(meta["code"])

    digit_row2: List[float] = []
    digit_col2: List[float] = []
    noise_row2: List[float] = []
    noise_col2: List[float] = []

    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                row2 = float(row["row2_ratio_db"])
                col2 = float(row["col2_ratio_db"])
            except (KeyError, ValueError):
                continue

            emitted = row.get("emitted_digit", "0")
            is_digit_frame = emitted != "0" if has_code else False

            if is_digit_frame:
                digit_row2.append(row2)
                digit_col2.append(col2)
            else:
                noise_row2.append(row2)
                noise_col2.append(col2)

    return digit_row2, digit_col2, noise_row2, noise_col2


def attach_col_stats(stats: Dict[str, float], col_values: List[float]) -> Dict[str, float]:
    if not stats:
        stats = {"count": 0}
    if not col_values:
        stats.update({"col_count": 0})
        return stats

    stats.update(
        {
            "col_count": len(col_values),
            "col_mean": statistics.fmean(col_values),
            "col_stdev": statistics.pstdev(col_values),
            "col_min": min(col_values),
            "col_max": max(col_values),
            "col_p5": percentile(col_values, 5),
            "col_p50": percentile(col_values, 50),
            "col_p95": percentile(col_values, 95),
        }
    )
    return stats


def main() -> int:
    if not HARMONICS_ROOT.exists():
        print(f"Harmonics directory not found: {HARMONICS_ROOT}", file=sys.stderr)
        return 1

    csv_files = sorted(HARMONICS_ROOT.glob("*.csv"))
    if not csv_files:
        print(f"No CSV files found under {HARMONICS_ROOT}", file=sys.stderr)
        return 1

    overall_digit_row2: List[float] = []
    overall_digit_col2: List[float] = []
    overall_noise_row2: List[float] = []
    overall_noise_col2: List[float] = []

    for csv_path in csv_files:
        digit_row2, digit_col2, noise_row2, noise_col2 = summarise_file(csv_path)

        digit_stats = attach_col_stats(compute_stats(digit_row2), digit_col2)
        noise_stats = attach_col_stats(compute_stats(noise_row2), noise_col2)

        print(f"File: {csv_path.name}")
        print(format_stat_line("digit_frames", digit_stats))
        print(format_stat_line("non_digit_frames", noise_stats))
        print()

        overall_digit_row2.extend(digit_row2)
        overall_digit_col2.extend(digit_col2)
        overall_noise_row2.extend(noise_row2)
        overall_noise_col2.extend(noise_col2)

    if overall_digit_row2 or overall_noise_row2:
        print("Overall:")
        digit_stats = attach_col_stats(compute_stats(overall_digit_row2), overall_digit_col2)
        noise_stats = attach_col_stats(compute_stats(overall_noise_row2), overall_noise_col2)
        print(format_stat_line("digit_frames", digit_stats))
        print(format_stat_line("non_digit_frames", noise_stats))

    return 0


if __name__ == "__main__":
    sys.exit(main())
