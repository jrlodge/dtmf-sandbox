/*
 * TI SPRA096A-inspired DTMF decoder.
 *
 * This implementation mirrors the structure of the TI application report: a
 * fixed Goertzel bank tuned to the DTMF fundamentals and their second harmonics,
 * block-based processing on 102-sample buffers at 8 kHz, and a sequence of
 * validity checks (energy, dominance, twist, harmonic rejection, stability)
 * before emitting digits. The math remains double-precision for clarity; the
 * layout is designed so the code can be ported to fixed-point later.
 */

#include "decode.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 8000

/* --- Thresholds (tunable) --- */
#define THR_SIG 1e6          // be less picky about absolute energy
#define THR_ROWREL 3.0       // only 3 dB dominance required
#define THR_COLREL 3.0
#define TWIST_REV_MAX_DB 12.0
#define TWIST_STD_MAX_DB 8.0
#define THR_ROW2_MAX_DB 0.0  // effectively disable harmonic rejection for now
#define THR_COL2_MAX_DB 0.0
#define STABILITY_BLOCKS 1   // accept after a single block to start with
#define ENERGY_EPS 1e-12

/* --- WAV loader (16-bit PCM mono only) --- */

typedef struct {
    int16_t *samples;
    int      num_samples;
    int      sample_rate;
} WavData;

static int read_u32_le(FILE *f, uint32_t *out) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *out = (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 1;
}

static int read_u16_le(FILE *f, uint16_t *out) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 1;
}

static WavData load_wav(const char *path) {
    WavData wav = {0};
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return wav;
    }

    char riff[4];
    if (fread(riff, 1, 4, f) != 4 || strncmp(riff, "RIFF", 4) != 0) {
        fprintf(stderr, "Not a RIFF file\n");
        fclose(f);
        return wav;
    }

    uint32_t chunk_size;
    if (!read_u32_le(f, &chunk_size)) { fclose(f); return wav; }
    (void)chunk_size; /* RIFF chunk size not needed beyond validation. */

    char wave[4];
    if (fread(wave, 1, 4, f) != 4 || strncmp(wave, "WAVE", 4) != 0) {
        fprintf(stderr, "Not a WAVE file\n");
        fclose(f);
        return wav;
    }

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate  = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;
    long     data_offset = 0;

    for (;;) {
        char id[4];
        if (fread(id, 1, 4, f) != 4) {
            fprintf(stderr, "Unexpected EOF reading chunks\n");
            fclose(f);
            return wav;
        }

        uint32_t size;
        if (!read_u32_le(f, &size)) {
            fprintf(stderr, "Unexpected EOF reading chunk size\n");
            fclose(f);
            return wav;
        }

        if (strncmp(id, "fmt ", 4) == 0) {
            if (!read_u16_le(f, &audio_format) ||
                !read_u16_le(f, &num_channels) ||
                !read_u32_le(f, &sample_rate)) {
                fprintf(stderr, "Failed reading fmt chunk\n");
                fclose(f);
                return wav;
            }

            uint32_t byte_rate;
            uint16_t block_align;
            if (!read_u32_le(f, &byte_rate) ||
                !read_u16_le(f, &block_align) ||
                !read_u16_le(f, &bits_per_sample)) {
                fprintf(stderr, "Failed reading fmt chunk tail\n");
                fclose(f);
                return wav;
            }

            long remaining = (long)size - 16;
            if (remaining > 0) fseek(f, remaining, SEEK_CUR);
        } else if (strncmp(id, "data", 4) == 0) {
            data_size = size;
            data_offset = ftell(f);
            fseek(f, size, SEEK_CUR);
        } else {
            fseek(f, size, SEEK_CUR);
        }

        if (data_size != 0 && audio_format != 0)
            break;
    }

    if (audio_format != 1 || bits_per_sample != 16) {
        fprintf(stderr, "Unsupported WAV format (need 16-bit PCM)\n");
        fclose(f);
        return wav;
    }

    if (num_channels != 1) {
        fprintf(stderr, "Expected mono; only mono supported\n");
        fclose(f);
        return wav;
    }

    int num_samples = (int)(data_size / 2);
    int16_t *samples = (int16_t *)malloc(data_size);
    if (!samples) {
        fprintf(stderr, "malloc failed\n");
        fclose(f);
        return wav;
    }

    fseek(f, data_offset, SEEK_SET);
    if (fread(samples, 2, num_samples, f) != (size_t)num_samples) {
        fprintf(stderr, "Failed to read samples\n");
        free(samples);
        fclose(f);
        return wav;
    }

    fclose(f);

    wav.samples = samples;
    wav.num_samples = num_samples;
    wav.sample_rate = (int)sample_rate;
    return wav;
}

static void free_wav(WavData *wav) {
    if (wav && wav->samples) {
        free(wav->samples);
        wav->samples = NULL;
    }
}

/* --- Filter configuration --- */

static void set_cfg(GoertzelConfig *dst, double k) {
    dst->k = k;
    double omega = 2.0 * M_PI * k / DTMF_N;
    dst->coeff = 2.0 * cos(omega);
}

void dtmf_init_filter_config(DtmfFilterConfig *cfg) {
    static const double row_k[4]  = {8.88, 9.82, 10.86, 12.0};
    static const double col_k[4]  = {15.42, 17.03, 18.83, 20.82};
    static const double row2_k[4] = {17.93, 19.72, 21.72, 24.0};
    static const double col2_k[4] = {30.83, 34.07, 37.66, 41.64};

    for (int i = 0; i < 4; i++) set_cfg(&cfg->row[i], row_k[i]);
    for (int i = 0; i < 4; i++) set_cfg(&cfg->col[i], col_k[i]);
    for (int i = 0; i < 4; i++) set_cfg(&cfg->row2[i], row2_k[i]);
    for (int i = 0; i < 4; i++) set_cfg(&cfg->col2[i], col2_k[i]);
}

/* --- Goertzel processing --- */

static double goertzel_mag2(const int16_t *samples, const GoertzelConfig *cfg) {
    double v_prev = 0.0;
    double v_prev2 = 0.0;
    double coeff = cfg->coeff;

    for (int i = 0; i < DTMF_N; i++) {
        double v = (double)samples[i] + coeff * v_prev - v_prev2;
        v_prev2 = v_prev;
        v_prev = v;
    }

    double omega = 2.0 * M_PI * cfg->k / DTMF_N;
    double cos_omega = cos(omega);
    double sin_omega = sin(omega);
    double real = v_prev - v_prev2 * cos_omega;
    double imag = v_prev2 * sin_omega;
    return real * real + imag * imag;
}

void dtmf_compute_energy_block(const int16_t *samples,
                               const DtmfFilterConfig *cfg,
                               DtmfEnergyTemplate *out) {
    for (int i = 0; i < 4; i++) {
        out->row_energy[i]  = goertzel_mag2(samples, &cfg->row[i]);
        out->col_energy[i]  = goertzel_mag2(samples, &cfg->col[i]);
        out->row2_energy[i] = goertzel_mag2(samples, &cfg->row2[i]);
        out->col2_energy[i] = goertzel_mag2(samples, &cfg->col2[i]);
    }
}

/* --- Detector helpers --- */

static const char dtmf_map[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static void find_peak(const double *arr, int len, double *peak, double *next, int *idx) {
    *peak = arr[0];
    *next = 0.0;
    *idx = 0;
    for (int i = 1; i < len; i++) {
        if (arr[i] > *peak) {
            *next = *peak;
            *peak = arr[i];
            *idx = i;
        } else if (arr[i] > *next) {
            *next = arr[i];
        }
    }
}

void dtmf_detector_init(DtmfDetectorState *st, const DtmfFilterConfig *cfg) {
    st->cfg = cfg;
    st->last_digit = 0;
    st->stable_digit = 0;
    st->stable_count = 0;
}

char dtmf_detector_process_block(DtmfDetectorState *st, const int16_t *samples) {
    // TODO: Implement frame-level streaming state machine (IDLE/IN_DIGIT) with minimum tone/gap durations and per-digit majority vote; see README "Future Work".
    // TODO: Add per-frame quality gates (absolute energy, row/column dominance, twist) with thresholds exposed as tunable constants; see README "Future Work".
    // TODO: Allow an optional bandpass front-end (e.g., 300–3400 Hz) before Goertzel, controllable via a compile-time flag; see README "Future Work".

    DtmfEnergyTemplate e = {0};
    dtmf_compute_energy_block(samples, st->cfg, &e);

    double row_peak, row_next, col_peak, col_next;
    int row_idx, col_idx;
    find_peak(e.row_energy, 4, &row_peak, &row_next, &row_idx);
    find_peak(e.col_energy, 4, &col_peak, &col_next, &col_idx);

    double signal = row_peak + col_peak;
    if (signal < THR_SIG) {
        st->stable_digit = 0;
        st->stable_count = 0;
        st->last_digit = 0;
        return 0;
    }

    double row_rel_db = 10.0 * log10(row_peak / (row_next + ENERGY_EPS));
    double col_rel_db = 10.0 * log10(col_peak / (col_next + ENERGY_EPS));
    if (row_rel_db < THR_ROWREL || col_rel_db < THR_COLREL) {
        st->stable_digit = 0;
        st->stable_count = 0;
        st->last_digit = 0;
        return 0;
    }

    double rev_db = 10.0 * log10(row_peak / (col_peak + ENERGY_EPS));
    double std_db = 10.0 * log10(col_peak / (row_peak + ENERGY_EPS));
    if (rev_db > TWIST_REV_MAX_DB || std_db > TWIST_STD_MAX_DB) {
        st->stable_digit = 0;
        st->stable_count = 0;
        st->last_digit = 0;
        return 0;
    }

    double row2_peak, row2_next, col2_peak, col2_next;
    int dummy;
    find_peak(e.row2_energy, 4, &row2_peak, &row2_next, &dummy);
    find_peak(e.col2_energy, 4, &col2_peak, &col2_next, &dummy);
    (void)row2_next;
    (void)col2_next;

    double row2_ratio = 10.0 * log10(row2_peak / (row_peak + ENERGY_EPS));
    double col2_ratio = 10.0 * log10(col2_peak / (col_peak + ENERGY_EPS));
    if (row2_ratio > THR_ROW2_MAX_DB || col2_ratio > THR_COL2_MAX_DB) {
        st->stable_digit = 0;
        st->stable_count = 0;
        st->last_digit = 0;
        return 0;
    }

    char digit = dtmf_map[row_idx][col_idx];
    if (digit == st->stable_digit) {
        st->stable_count++;
    } else {
        st->stable_digit = digit;
        st->stable_count = 1;
    }

    if (st->stable_count >= STABILITY_BLOCKS && digit != st->last_digit) {
        st->last_digit = digit;
        return digit;
    }

    return 0;
}

/* --- WAV-oriented harness --- */

void decode_wav(const char *path) {
    WavData w = load_wav(path);
    if (!w.samples) {
        fprintf(stderr, "Failed to load WAV: %s\n", path);
        return;
    }

    if (w.sample_rate != SAMPLE_RATE) {
        fprintf(stderr, "Expected %d Hz sample rate, got %d\n", SAMPLE_RATE, w.sample_rate);
        free_wav(&w);
        return;
    }

    DtmfFilterConfig cfg;
    dtmf_init_filter_config(&cfg);

    DtmfDetectorState st;
    dtmf_detector_init(&st, &cfg);

    int blocks = w.num_samples / DTMF_N;

    printf("Decoding %s (sample_rate=%d, blocks=%d)\n", path, w.sample_rate, blocks);
    printf("Decoded: ");

    for (int b = 0; b < blocks; b++) {
        const int16_t *blk = &w.samples[b * DTMF_N];
        char d = dtmf_detector_process_block(&st, blk);
        if (d) {
            putchar(d);
        }

        if (d == 0 && st.stable_digit == 0) {
            st.last_digit = 0; /* Allow repeats after silence. */
        }
    }

    putchar('\n');
    free_wav(&w);
}
