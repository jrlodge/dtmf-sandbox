/*
 * Public API surface for the DTMF Lab library.
 *
 * This header exposes the data structures and functions shared between the
 * command-line application and any other consumers (tests, GUI front-ends,
 * etc.). The focus is on small, composable primitives: mapping keys to
 * frequencies, generating raw samples, writing WAV files, and initializing the
 * Goertzel detector for future decoding work.
 */

#ifndef DTMF_H
#define DTMF_H

#include <stdint.h>
#include <stdio.h>

/* DTMF frequency pairs (Hz) */
typedef struct {
    double low_freq;
    double high_freq;
} dtmf_freq_t;

/* DTMF tone parameters */
typedef struct {
    int sample_rate;      /* Samples per second (e.g., 8000, 44100) */
    int duration_ms;      /* Duration in milliseconds */
    double amplitude;     /* Amplitude (0.0 to 1.0). Mixed tones are scaled by this. */
} dtmf_params_t;

/* WAV file header structure */
typedef struct __attribute__((packed)) {
    char riff[4];              /* "RIFF" */
    uint32_t file_size;        /* File size - 8 */
    char wave[4];              /* "WAVE" */
    char fmt[4];               /* "fmt " */
    uint32_t fmt_size;         /* Format chunk size (16 for PCM) */
    uint16_t audio_format;     /* Audio format (1 = PCM) */
    uint16_t num_channels;     /* Number of channels */
    uint32_t sample_rate;      /* Sample rate */
    uint32_t byte_rate;        /* Byte rate */
    uint16_t block_align;      /* Block align */
    uint16_t bits_per_sample;  /* Bits per sample */
    char data[4];              /* "data" */
    uint32_t data_size;        /* Data size */
} wav_header_t;

/* Function declarations */

/**
 * Get DTMF frequency pair for a given key
 * @param key: Character (0-9, A-D, *, #)
 * @param freq: Pointer to frequency structure to fill
 * @return: 0 on success, -1 on error
 */
int dtmf_get_frequencies(char key, dtmf_freq_t *freq);

/**
 * Generate DTMF tone samples
 * @param freq: DTMF frequency pair
 * @param params: Tone parameters
 * @param samples: Output buffer for samples (must be pre-allocated)
 * @return: Number of samples generated, or -1 on error
 */
int dtmf_generate_tone(const dtmf_freq_t *freq, const dtmf_params_t *params, int16_t *samples);

/**
 * Write WAV file with given samples
 * @param filename: Output filename
 * @param samples: Sample data
 * @param num_samples: Number of samples
 * @param sample_rate: Sample rate in Hz
 * @return: 0 on success, -1 on error
 */
int dtmf_write_wav(const char *filename, const int16_t *samples, int num_samples, int sample_rate);

/**
 * Generate DTMF tone and write to WAV file
 * @param key: DTMF key
 * @param filename: Output filename
 * @param params: Tone parameters
 * @return: 0 on success, -1 on error
 */
int dtmf_generate_wav(char key, const char *filename, const dtmf_params_t *params);

/**
 * Generate DTMF sequence and write to WAV file
 * @param sequence: String of DTMF keys
 * @param filename: Output filename
 * @param params: Tone parameters (per key)
 * @param gap_ms: Gap between tones in milliseconds
 * @return: 0 on success, -1 on error
 */
int dtmf_generate_sequence_wav(const char *sequence, const char *filename, 
                                const dtmf_params_t *params, int gap_ms);

/* Goertzel algorithm structures for future decoding */
typedef struct {
    double coeff;
    double q1;
    double q2;
    int n;
    double cosine;  /* Precomputed cos(2*PI/n) */
    double sine;    /* Precomputed sin(2*PI/n) */
} goertzel_state_t;

/**
 * Initialize Goertzel detector for a specific frequency
 * @param state: Goertzel state structure
 * @param target_freq: Target frequency to detect
 * @param sample_rate: Sample rate
 * @param n: Number of samples to process
 */
void goertzel_init(goertzel_state_t *state, double target_freq, int sample_rate, int n);

/**
 * Process a sample with Goertzel algorithm
 * @param state: Goertzel state structure
 * @param sample: Input sample
 */
void goertzel_process_sample(goertzel_state_t *state, double sample);

/**
 * Get magnitude from Goertzel detector
 * @param state: Goertzel state structure
 * @return: Magnitude
 */
double goertzel_magnitude(const goertzel_state_t *state);

#endif /* DTMF_H */
