#!/usr/bin/env bash
set -euo pipefail
shopt -s extglob

CODES_FILE="testdata/codes.txt"
ATC_DIR="testdata/atc"
OUTPUT_DIR="artifacts/wav/tests"

TONE_MS=100
DENSE_GAP_MS=60
SPARSE_GAP_MS=400
SILENCE_MS=3000
SILENCE_RATE=8000

SNR_LIST=(20 15 10 7 5 3 0 -3)
WHITE_SNR_SUBSET=(20 10 5 0)
ATC_SNR_SUBSET=(20 10 5)

mkdir -p "${OUTPUT_DIR}"

sanitize_code() {
    local code="$1"
    code="${code//\*/star}"
    code="${code//#/hash}"
    echo "$code"
}

run_noise_mix() {
    local description="$1"
    shift
    if ! "$@"; then
        echo "Warning: noise-mix failed for ${description}; skipping."
        return 1
    fi
    return 0
}

read_codes() {
    local line
    while IFS= read -r line || [ -n "$line" ]; do
        # Trim leading/trailing whitespace
        line="${line##+([[:space:]])}"
        line="${line%%+([[:space:]])}"
        if [[ -z "$line" || ${line:0:1} == "#" ]]; then
            continue
        fi
        echo "$line"
    done <"$CODES_FILE"
}

# Ensure base silence exists (used for noise-only and pure silence cases)
SILENCE_PATH="${OUTPUT_DIR}/silence__${SILENCE_MS}ms.wav"
if [[ ! -f "$SILENCE_PATH" ]]; then
    echo "[silence] creating ${SILENCE_MS} ms silence -> ${SILENCE_PATH}"
    bin/silence-gen -o "$SILENCE_PATH" --duration-ms "$SILENCE_MS" --sample-rate "$SILENCE_RATE"
fi

# Noise-only variants (white noise at various levels on top of silence)
for snr in "${WHITE_SNR_SUBSET[@]}"; do
    out_path="${OUTPUT_DIR}/white__noise_only__snr_${snr}dB.wav"
    echo "[white noise-only] snr=${snr} dB -> ${out_path}"
    if ! run_noise_mix "white noise-only snr=${snr} dB" bin/noise-mix -i "$SILENCE_PATH" -o "$out_path" --snr-db "$snr" --mode white; then
        continue
    fi
done

# ATC noise-only variants: tile/truncate each ATC clip into the silence duration
shopt -s nullglob
for noise_file in "${ATC_DIR}"/*.wav; do
    noise_base="$(basename "$noise_file" .wav)"
    out_path="${OUTPUT_DIR}/atc__noise_only__noise_${noise_base}.wav"
    echo "[atc noise-only] noise=${noise_base} -> ${out_path}"
    if ! run_noise_mix "atc noise-only noise=${noise_base}" bin/noise-mix -i "$SILENCE_PATH" -o "$out_path" --snr-db 0 --noise-wav "$noise_file"; then
        continue
    fi
done
shopt -u nullglob

while IFS= read -r code; do
    safe_code="$(sanitize_code "$code")"

    dense_clean="${OUTPUT_DIR}/clean__dense__code_${safe_code}.wav"
    sparse_clean="${OUTPUT_DIR}/clean__sparse__code_${safe_code}.wav"

    echo "[clean dense] code=${code} -> ${dense_clean}"
    bin/dtmf-lab -d "$TONE_MS" -g "$DENSE_GAP_MS" -o "$dense_clean" "$code"

    echo "[clean sparse] code=${code} -> ${sparse_clean}"
    bin/dtmf-lab -d "$TONE_MS" -g "$SPARSE_GAP_MS" -o "$sparse_clean" "$code"

    # White noise mixes (dense)
    for snr in "${WHITE_SNR_SUBSET[@]}"; do
        out_path="${OUTPUT_DIR}/white__dense__code_${safe_code}__snr_${snr}dB.wav"
        echo "[white dense] code=${code}, snr=${snr} dB -> ${out_path}"
        if ! run_noise_mix "white dense code=${code} snr=${snr} dB" bin/noise-mix -i "$dense_clean" -o "$out_path" --snr-db "$snr" --mode white; then
            continue
        fi
    done

    # ATC mixes (dense)
    shopt -s nullglob
    for noise_file in "${ATC_DIR}"/*.wav; do
        noise_base="$(basename "$noise_file" .wav)"
        for snr in "${ATC_SNR_SUBSET[@]}"; do
            out_path="${OUTPUT_DIR}/atc__dense__code_${safe_code}__noise_${noise_base}__snr_${snr}dB.wav"
            echo "[atc dense]   code=${code}, noise=${noise_base}, snr=${snr} dB -> ${out_path}"
            if ! run_noise_mix "atc dense code=${code} noise=${noise_base} snr=${snr} dB" bin/noise-mix -i "$dense_clean" -o "$out_path" --snr-db "$snr" --noise-wav "$noise_file"; then
                continue
            fi
        done
    done
    shopt -u nullglob

done < <(read_codes)
