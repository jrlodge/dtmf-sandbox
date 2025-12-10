#!/usr/bin/env python3
"""Evaluate DTMF decoder accuracy on generated WAV fixtures.

The script walks through all WAV files under artifacts/wav/tests/, infers the
expected DTMF code from the filename, runs bin/dtmf-decode for each sample, and
compares the decoded output with the ground truth. It writes a detailed CSV
report to artifacts/wav/report.csv and prints aggregate accuracy summaries to
stdout. Any failure on clean samples causes a non-zero exit status so baseline
regressions are caught early.
"""

from __future__ import annotations

import csv
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

TEST_ROOT = Path("artifacts/wav/tests")
REPORT_PATH = Path("artifacts/wav/report.csv")


@dataclass
class SampleMetadata:
    path: Path
    condition: str
    code: str
    snr: Optional[str]
    noise_type: Optional[str]


@dataclass
class SampleResult:
    filename: str
    condition: str
    code: str
    snr: Optional[str]
    decoded_code: str
    success: bool
    error_type: str
    notes: str


CODE_REPLACEMENTS = {
    "star": "*",
    "hash": "#",
}


def normalize_code(raw: str) -> str:
    code = raw
    for key, value in CODE_REPLACEMENTS.items():
        code = code.replace(key, value)
    return code


def parse_metadata(path: Path) -> SampleMetadata:
    stem = path.stem
    tokens = stem.split("__")

    condition = tokens[0] if tokens else ""
    code: Optional[str] = None
    snr: Optional[str] = None
    noise_type: Optional[str] = None

    for token in tokens:
        if token.startswith("code_"):
            code = normalize_code(token[len("code_") :])
        elif token.startswith("snr_"):
            snr = token[len("snr_") :]
        elif token.startswith("noise_"):
            noise_type = token[len("noise_") :]
        elif token == "noise_only" or token.startswith("silence"):
            code = ""

    if code is None:
        # Default to no DTMF code when none is encoded in the filename.
        code = ""

    return SampleMetadata(path=path, condition=condition, code=code, snr=snr, noise_type=noise_type)


def parse_decoder_output(output: str) -> str:
    decoded = ""
    for line in output.splitlines():
        if line.startswith("Decoded:"):
            decoded = line.split("Decoded:", 1)[1].strip()
    return decoded


def decode_sample(path: Path) -> Tuple[str, str]:
    proc = subprocess.run(
        ["bin/dtmf-decode", str(path)],
        capture_output=True,
        text=True,
        check=False,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    decoded = parse_decoder_output(output)
    notes = ""
    if proc.returncode != 0:
        notes = f"decoder exited with {proc.returncode}"
    return decoded, notes


def classify_error(expected: str, decoded: str) -> str:
    if expected == decoded:
        return ""
    if not expected and decoded:
        return "false positive"
    if expected and not decoded:
        return "missing digits"
    if expected.startswith(decoded):
        return "missing digits"
    if decoded.startswith(expected):
        return "extra digits"
    return "wrong digits"


def record_stats(
    stats: Dict[str, Dict[str, int]], condition: str, snr: Optional[str], success: bool
) -> None:
    stats_cond = stats.setdefault(condition, {"total": 0, "success": 0})
    stats_cond["total"] += 1
    if success:
        stats_cond["success"] += 1

    if snr:
        key = f"{condition}@{snr}"
    else:
        key = f"{condition}@"
    stats_snr = stats.setdefault(key, {"total": 0, "success": 0})
    stats_snr["total"] += 1
    if success:
        stats_snr["success"] += 1


def summarize(stats: Dict[str, Dict[str, int]], label_filter: Iterable[str]) -> List[str]:
    lines = []
    for label in label_filter:
        if label in stats:
            total = stats[label]["total"]
            success = stats[label]["success"]
            rate = (success / total * 100) if total else 0.0
            lines.append(f"  {label}: {success}/{total} ({rate:.1f}% correct)")
    return lines


def write_csv(results: List[SampleResult]) -> None:
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with REPORT_PATH.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "filename",
            "condition",
            "code",
            "snr",
            "decoded_code",
            "success",
            "error_type",
            "notes",
        ])
        for res in results:
            writer.writerow(
                [
                    res.filename,
                    res.condition,
                    res.code if res.code else "NONE",
                    res.snr or "",
                    res.decoded_code if res.decoded_code else "NONE",
                    "yes" if res.success else "no",
                    res.error_type,
                    res.notes,
                ]
            )


def main() -> int:
    if not TEST_ROOT.exists():
        print(f"Test directory not found: {TEST_ROOT}", file=sys.stderr)
        return 1

    wav_files = sorted(TEST_ROOT.rglob("*.wav"))
    if not wav_files:
        print(f"No WAV files found under {TEST_ROOT}", file=sys.stderr)
        return 1

    results: List[SampleResult] = []
    stats: Dict[str, Dict[str, int]] = {}
    clean_failure = False

    for path in wav_files:
        meta = parse_metadata(path)
        expected_code = meta.code

        decoded, notes = decode_sample(path)
        error_type = classify_error(expected_code, decoded)
        success = error_type == ""

        if meta.condition == "clean" and not success:
            clean_failure = True

        record_stats(stats, meta.condition, meta.snr, success)

        if meta.noise_type:
            if notes:
                notes = f"{notes}; noise={meta.noise_type}"
            else:
                notes = f"noise={meta.noise_type}"

        results.append(
            SampleResult(
                filename=str(path.relative_to(TEST_ROOT)),
                condition=meta.condition,
                code=expected_code,
                snr=meta.snr,
                decoded_code=decoded,
                success=success,
                error_type=error_type,
                notes=notes,
            )
        )

    write_csv(results)

    print(f"Processed {len(results)} file(s).")
    condition_labels = sorted({res.condition for res in results})
    print("Per-condition accuracy:")
    for line in summarize(stats, condition_labels):
        print(line)

    snr_labels = sorted(label for label in stats if "@" in label and label.split("@", 1)[1])
    if snr_labels:
        print("Per-SNR accuracy:")
        for line in summarize(stats, snr_labels):
            print(line)

    if clean_failure:
        print("Failures detected on clean samples.", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
