/*
 * Core DTMF synthesis and analysis routines.
 *
 * This module provides the reusable building blocks for generating tones and
 * saving them as WAV files, along with scaffolding for future decoding using
 * the Goertzel algorithm. The functions here are intentionally library-like so
 * they can be used by both the CLI (src/main.c) and any future consumers such
 * as tests or other front-ends.
 */

#include "dtmf.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Static lookup table mapping each DTMF key to its low/high frequency pair.
 * The final sentinel entry with key == 0 marks the end of the table for the
 * iteration logic inside dtmf_get_frequencies.
 */
static const struct {
    char key;
    double low;
    double high;
} dtmf_table[] = {
    {'1', 697, 1209}, {'2', 697, 1336}, {'3', 697, 1477}, {'A', 697, 1633},
    {'4', 770, 1209}, {'5', 770, 1336}, {'6', 770, 1477}, {'B', 770, 1633},
    {'7', 852, 1209}, {'8', 852, 1336}, {'9', 852, 1477}, {'C', 852, 1633},
    {'*', 941, 1209}, {'0', 941, 1336}, {'#', 941, 1477}, {'D', 941, 1633},
    {0, 0, 0}
};

int dtmf_get_frequencies(char key, dtmf_freq_t *freq) {
    if (!freq) return -1;

    /* Convert to uppercase for consistency */
    if (key >= 'a' && key <= 'd') {
        key = key - 'a' + 'A';
    }
    
    for (int i = 0; dtmf_table[i].key != 0; i++) {
        if (dtmf_table[i].key == key) {
            freq->low_freq = dtmf_table[i].low;
            freq->high_freq = dtmf_table[i].high;
            return 0;
        }
    }
    
    return -1;  /* Key not found */
}

int dtmf_generate_tone(const dtmf_freq_t *freq, const dtmf_params_t *params, int16_t *samples) {
    if (!freq || !params || !samples) return -1;
    if (params->sample_rate <= 0 || params->duration_ms <= 0) return -1;

    int num_samples = (params->sample_rate * params->duration_ms) / 1000;
    double amplitude = params->amplitude;

    /* Clamp amplitude to valid range */
    if (amplitude < 0.0) amplitude = 0.0;
    if (amplitude > 1.0) amplitude = 1.0;

    /*
     * Generate two-tone signal.
     * DTMF tones are the sum of one "row" and one "column" frequency. We
     * sample both sine waves at the requested rate, mix them together, and
     * scale the result into 16-bit integer space.
     */
    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / params->sample_rate;
        double low_tone = sin(2.0 * M_PI * freq->low_freq * t);
        double high_tone = sin(2.0 * M_PI * freq->high_freq * t);

        /* Mix the two tones and scale to 16-bit range */
        double mixed = (low_tone + high_tone) * 0.5 * amplitude;
        samples[i] = (int16_t)(mixed * 32767.0);
    }
    
    return num_samples;
}

int dtmf_write_wav(const char *filename, const int16_t *samples, int num_samples, int sample_rate) {
    if (!filename || !samples || num_samples <= 0 || sample_rate <= 0) return -1;
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    wav_header_t header;
    uint32_t data_size = num_samples * sizeof(int16_t);
    
    /*
     * Fill WAV header. The packed wav_header_t mirrors the canonical PCM
     * layout: RIFF chunk, fmt subchunk, then data. Sizes are computed from the
     * provided sample buffer to keep the header consistent regardless of tone
     * length or sample rate.
     */
    memcpy(header.riff, "RIFF", 4);
    header.file_size = data_size + sizeof(wav_header_t) - 8;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;  /* PCM */
    header.num_channels = 1;  /* Mono */
    header.sample_rate = sample_rate;
    header.bits_per_sample = 16;
    header.byte_rate = sample_rate * header.num_channels * (header.bits_per_sample / 8);
    header.block_align = header.num_channels * (header.bits_per_sample / 8);
    memcpy(header.data, "data", 4);
    header.data_size = data_size;
    
    /* Write header and data */
    if (fwrite(&header, sizeof(wav_header_t), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    
    if (fwrite(samples, sizeof(int16_t), num_samples, fp) != (size_t)num_samples) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    return 0;
}

int dtmf_generate_wav(char key, const char *filename, const dtmf_params_t *params) {
    if (!filename || !params) return -1;

    dtmf_freq_t freq;
    if (dtmf_get_frequencies(key, &freq) != 0) {
        return -1;  /* Invalid key */
    }

    int num_samples = (params->sample_rate * params->duration_ms) / 1000;
    int16_t *samples = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (!samples) return -1;

    if (dtmf_generate_tone(&freq, params, samples) < 0) {
        free(samples);
        return -1;
    }

    int result = dtmf_write_wav(filename, samples, num_samples, params->sample_rate);
    free(samples);

    return result;
}

int dtmf_generate_sequence_wav(const char *sequence, const char *filename,
                                const dtmf_params_t *params, int gap_ms) {
    if (!sequence || !filename || !params) return -1;
    if (params->sample_rate <= 0 || params->duration_ms <= 0) return -1;

    int seq_len = strlen(sequence);
    if (seq_len == 0) return -1;

    int samples_per_tone = (params->sample_rate * params->duration_ms) / 1000;
    int samples_per_gap = (params->sample_rate * gap_ms) / 1000;
    int total_samples = seq_len * samples_per_tone + (seq_len - 1) * samples_per_gap;

    /* Pre-zero the buffer via calloc so the gap segments naturally render as
     * silence without needing an explicit fill. */
    int16_t *all_samples = (int16_t *)calloc(total_samples, sizeof(int16_t));
    if (!all_samples) return -1;

    int offset = 0;
    for (int i = 0; i < seq_len; i++) {
        dtmf_freq_t freq;
        if (dtmf_get_frequencies(sequence[i], &freq) != 0) {
            free(all_samples);
            return -1;  /* Invalid key in sequence */
        }

        if (dtmf_generate_tone(&freq, params, all_samples + offset) < 0) {
            free(all_samples);
            return -1;
        }

        offset += samples_per_tone;

        /* Add gap between tones (silence is already zeroed by calloc) */
        if (i < seq_len - 1) {
            offset += samples_per_gap;
        }
    }
    
    int result = dtmf_write_wav(filename, all_samples, total_samples, params->sample_rate);
    free(all_samples);
    
    return result;
}

/* Goertzel algorithm implementation */

void goertzel_init(goertzel_state_t *state, double target_freq, int sample_rate, int n) {
    if (!state) return;

    /*
     * The Goertzel algorithm efficiently evaluates a single DFT bin. Here we
     * pre-compute the coefficient required for the recurrence relation based
     * on the target frequency and sample window length.
     */
    double k = (n * target_freq) / sample_rate;
    double w = (2.0 * M_PI * k) / n;
    state->coeff = 2.0 * cos(w);
    state->q1 = 0.0;
    state->q2 = 0.0;
    state->n = n;

    /* Precompute sin and cos for magnitude calculation */
    double angle = 2.0 * M_PI / n;
    state->cosine = cos(angle);
    state->sine = sin(angle);
}

void goertzel_process_sample(goertzel_state_t *state, double sample) {
    if (!state) return;

    /* One iteration of the Goertzel recurrence. This accumulates enough state
     * to compute the magnitude without storing the full sample buffer. */
    double q0 = state->coeff * state->q1 - state->q2 + sample;
    state->q2 = state->q1;
    state->q1 = q0;
}

double goertzel_magnitude(const goertzel_state_t *state) {
    if (!state) return 0.0;

    /* Use precomputed sine and cosine values */
    double real = state->q1 - state->q2 * state->cosine;
    double imag = state->q2 * state->sine;

    return sqrt(real * real + imag * imag);
}
