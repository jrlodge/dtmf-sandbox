// src/decode.c
// Simple DTMF decoder for clean lab signals.
// - Loads a 16-bit PCM mono WAV.
// - Splits into frames.
// - Runs Goertzel on 8 DTMF frequencies.
// - Emits digits when tones are present.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int16_t *samples;
    int      num_samples;
    int      sample_rate;
} WavData;

/* --- Minimal WAV loader for 16-bit PCM mono files --- */

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

    // parse chunks
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

            // skip any extra fmt bytes
            long remaining = (long)size - 16;
            if (remaining > 0) fseek(f, remaining, SEEK_CUR);
        } else if (strncmp(id, "data", 4) == 0) {
            data_size = size;
            data_offset = ftell(f);
            fseek(f, size, SEEK_CUR); // skip for now
        } else {
            // skip unknown chunk
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
        fprintf(stderr, "Expected mono, got %u channels; using left channel only not implemented\n", num_channels);
        fclose(f);
        return wav;
    }

    int num_samples = (int)(data_size / 2); // 16-bit mono
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

/* --- Goertzel implementation --- */

static double goertzel_mag2(const int16_t *samples, int N, double freq, int sample_rate) {
    double s_prev = 0.0;
    double s_prev2 = 0.0;
    double k = 0.5 + (N * freq) / sample_rate;
    double w = (2.0 * M_PI * k) / N;
    double coeff = 2.0 * cos(w);

    for (int i = 0; i < N; i++) {
        double x = (double)samples[i];
        double s = x + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }

    double real = s_prev - s_prev2 * cos(w);
    double imag = s_prev2 * sin(w);
    return real * real + imag * imag;
}

/* --- DTMF decoding --- */

#define FRAME_SIZE   205       // ~25.6 ms at 8 kHz
#define MIN_ENERGY   1e7       // tweak as needed
#define MIN_RATIO_DB 6.0       // dominant vs next-bin

static const double dtmf_freqs[8] = {
    697.0, 770.0, 852.0, 941.0,     // rows
    1209.0, 1336.0, 1477.0, 1633.0  // cols
};

static const char dtmf_map[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static char detect_frame(const int16_t *frame, int sample_rate) {
    double mags[8];

    for (int i = 0; i < 8; i++)
        mags[i] = goertzel_mag2(frame, FRAME_SIZE, dtmf_freqs[i], sample_rate);

    // find row max
    int row_idx = 0;
    double row_peak = mags[0];
    double row_next = 0.0;
    for (int i = 1; i < 4; i++) {
        if (mags[i] > row_peak) {
            row_next = row_peak;
            row_peak = mags[i];
            row_idx = i;
        } else if (mags[i] > row_next) {
            row_next = mags[i];
        }
    }

    // find col max
    int col_idx = 4;
    double col_peak = mags[4];
    double col_next = 0.0;
    for (int i = 5; i < 8; i++) {
        if (mags[i] > col_peak) {
            col_next = col_peak;
            col_peak = mags[i];
            col_idx = i;
        } else if (mags[i] > col_next) {
            col_next = mags[i];
        }
    }

    if (row_peak < MIN_ENERGY || col_peak < MIN_ENERGY)
        return 0;

    // ratio test (dominant vs next strongest in that group)
    double row_ratio_db = 10.0 * log10(row_peak / (row_next + 1.0));
    double col_ratio_db = 10.0 * log10(col_peak / (col_next + 1.0));

    if (row_ratio_db < MIN_RATIO_DB || col_ratio_db < MIN_RATIO_DB)
        return 0;

    int row = row_idx;
    int col = col_idx - 4;
    return dtmf_map[row][col];
}

void decode_wav(const char *path) {
    WavData w = load_wav(path);
    if (!w.samples) {
        fprintf(stderr, "Failed to load WAV: %s\n", path);
        return;
    }

    int frames = w.num_samples / FRAME_SIZE;
    int stable_count = 0;
    char last_symbol = 0;
    char current_symbol = 0;

    printf("Decoding %s (sample_rate=%d, frames=%d)\n", path, w.sample_rate, frames);
    printf("Decoded: ");

    for (int f = 0; f < frames; f++) {
        const int16_t *frame = &w.samples[f * FRAME_SIZE];
        char sym = detect_frame(frame, w.sample_rate);

        if (sym == current_symbol) {
            if (sym != 0) stable_count++;
        } else {
            current_symbol = sym;
            stable_count = (sym != 0) ? 1 : 0;
        }

        // require a few consecutive frames of same tone before emitting
        if (current_symbol != 0 && stable_count == 3 && current_symbol != last_symbol) {
            putchar(current_symbol);
            last_symbol = current_symbol;
        }

        // reset last_symbol when silence
        if (current_symbol == 0) {
            last_symbol = 0;
        }
    }

    putchar('\n');
    free_wav(&w);
}
