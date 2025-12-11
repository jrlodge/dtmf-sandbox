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
COMPLEX_SNR_SUBSET=(10 5)

JITTER_TONES=(60 80 120 150)
JITTER_GAPS=(40 100 250 400)
OFFSET_SILENCE_MAX=1000
BURST_SILENCE_MS=500

mkdir -p "${OUTPUT_DIR}"

sanitize_code() {
    local code="$1"
    code="${code//\*/star}"
    code="${code//#/hash}"
    echo "$code"
}

random_choice() {
    local -a options=($@)
    local count=${#options[@]}
    local idx=$((RANDOM % count))
    echo "${options[$idx]}"
}

random_between() {
    local min="$1"
    local max="$2"
    echo $((min + RANDOM % (max - min + 1)))
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

ensure_silence_ms() {
    local duration_ms="$1"
    local path="${OUTPUT_DIR}/silence__${duration_ms}ms.wav"
    if [[ ! -f "$path" ]]; then
        echo "[silence] creating ${duration_ms} ms silence -> ${path}" >&2
        bin/silence-gen -o "$path" --duration-ms "$duration_ms" --sample-rate "$SILENCE_RATE" >/dev/null
    fi
    echo "$path"
}

concat_wavs() {
    local output="$1"
    shift
    python3 tools/wav_concat.py -o "$output" "$@"
}

# Ensure base silence exists (used for noise-only and pure silence cases)
SILENCE_PATH="${OUTPUT_DIR}/silence__${SILENCE_MS}ms.wav"
SILENCE_PATH="$(ensure_silence_ms "$SILENCE_MS")"

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

    # Jittered timing: random tone/gap per sequence
    jitter_tone="$(random_choice "${JITTER_TONES[@]}")"
    jitter_gap="$(random_choice "${JITTER_GAPS[@]}")"
    jitter_path="${OUTPUT_DIR}/clean__jitter__code_${safe_code}.wav"
    echo "[clean jitter] code=${code}, tone=${jitter_tone} ms, gap=${jitter_gap} ms -> ${jitter_path}"
    bin/dtmf-lab -d "$jitter_tone" -g "$jitter_gap" -o "$jitter_path" "$code"

    # Offset within longer silence: random lead/trail silence up to 1s
    lead_ms="$(random_between 0 "$OFFSET_SILENCE_MAX")"
    trail_ms="$(random_between 0 "$OFFSET_SILENCE_MAX")"
    lead_silence="$(ensure_silence_ms "$lead_ms")"
    trail_silence="$(ensure_silence_ms "$trail_ms")"
    offset_path="${OUTPUT_DIR}/clean__offset__code_${safe_code}.wav"
    echo "[clean offset] code=${code}, lead=${lead_ms} ms, trail=${trail_ms} ms -> ${offset_path}"
    concat_wavs "$offset_path" "$lead_silence" "$dense_clean" "$trail_silence"

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

    # Complex multi-noise mixes: sequentially layer combinations
    complex_sets=("white+ATIS_Schiphol" "white+Inner_marker" "ATIS_Schiphol+Inner_marker")
    for combo in "${complex_sets[@]}"; do
        IFS='+' read -r first_noise second_noise third_noise <<<"$combo"
        for snr in "${COMPLEX_SNR_SUBSET[@]}"; do
            combo_out="${OUTPUT_DIR}/complex__dense__code_${safe_code}__noise_${combo}__snr_${snr}dB.wav"
            echo "[complex] code=${code}, combo=${combo}, snr=${snr} dB -> ${combo_out}"

            current="$dense_clean"
            tmp_paths=()
            success=1
            for noise_name in "$first_noise" "$second_noise" "$third_noise"; do
                [[ -z "$noise_name" ]] && continue
                tmp_file="${OUTPUT_DIR}/complex_tmp_${RANDOM}.wav"
                tmp_paths+=("$tmp_file")

                if [[ "$noise_name" == "white" ]]; then
                    if ! run_noise_mix "complex white layer" bin/noise-mix -i "$current" -o "$tmp_file" --snr-db "$snr" --mode white; then
                        success=0
                        break
                    fi
                else
                    noise_path="${ATC_DIR}/${noise_name}.wav"
                    if [[ ! -f "$noise_path" ]]; then
                        echo "[complex] skipping combo=${combo}; missing ${noise_name}.wav"
                        success=0
                        break
                    fi
                    if ! run_noise_mix "complex atc layer ${noise_name}" bin/noise-mix -i "$current" -o "$tmp_file" --snr-db "$snr" --noise-wav "$noise_path"; then
                        success=0
                        break
                    fi
                fi
                current="$tmp_file"
            done

            if [[ "$success" -eq 1 && -f "$current" ]]; then
                mv "$current" "$combo_out"
                # remove intermediate temps except final (now moved)
                for f in "${tmp_paths[@]}"; do
                    [[ -f "$f" ]] && rm -f "$f"
                done
            else
                for f in "${tmp_paths[@]}"; do
                    [[ -f "$f" ]] && rm -f "$f"
                done
            fi
        done
    done

    # Bursty overlays: sandwich with silence then mask with intermittent noise
    burst_base="${OUTPUT_DIR}/bursty_base__code_${safe_code}.wav"
    lead_burst_silence="$(ensure_silence_ms "$BURST_SILENCE_MS")"
    tail_burst_silence="$(ensure_silence_ms "$BURST_SILENCE_MS")"
    concat_wavs "$burst_base" "$lead_burst_silence" "$dense_clean" "$tail_burst_silence"

    for snr in "${ATC_SNR_SUBSET[@]}"; do
        burst_white_out="${OUTPUT_DIR}/bursty__code_${safe_code}__noise_white__snr_${snr}dB.wav"
        echo "[bursty] code=${code}, noise=white, snr=${snr} dB -> ${burst_white_out}"
        python3 tools/bursty_noise_overlay.py --base "$burst_base" --out "$burst_white_out" --snr-db "$snr" --noise white || true
    done

    shopt -s nullglob
    for noise_file in "${ATC_DIR}"/*.wav; do
        noise_base="$(basename "$noise_file" .wav)"
        for snr in "${ATC_SNR_SUBSET[@]}"; do
            burst_atc_out="${OUTPUT_DIR}/bursty__code_${safe_code}__noise_${noise_base}__snr_${snr}dB.wav"
            echo "[bursty] code=${code}, noise=${noise_base}, snr=${snr} dB -> ${burst_atc_out}"
            python3 tools/bursty_noise_overlay.py --base "$burst_base" --out "$burst_atc_out" --snr-db "$snr" --noise-wav "$noise_file" || true
        done
    done
    shopt -u nullglob

    rm -f "$burst_base"

done < <(read_codes)
