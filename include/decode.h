/*
 * Public entry points for the SPRA096A-style DTMF decoder.
 *
 * The decoder operates on fixed 102-sample blocks at 8 kHz and exposes a small
 * state machine so callers can feed successive blocks of samples and receive
 * debounced ASCII digits. A CLI harness in src/decode_main.c demonstrates the
 * flow by walking through a WAV file and printing confirmed digits to stdout.
 */

#ifndef DTMF_DECODE_H
#define DTMF_DECODE_H

#include <stdint.h>

#define DTMF_N 102

typedef struct {
    double k;      /* Tuned bin index (can be fractional). */
    double coeff;  /* 2 * cos(omega) precomputed for the recurrence. */
} GoertzelConfig;

typedef struct {
    GoertzelConfig row[4];
    GoertzelConfig col[4];
    GoertzelConfig row2[4];
    GoertzelConfig col2[4];
} DtmfFilterConfig;

typedef struct {
    double row_energy[4];
    double col_energy[4];
    double row2_energy[4];
    double col2_energy[4];
} DtmfEnergyTemplate;

typedef enum {
    DTMF_STATE_IDLE = 0,
    DTMF_STATE_IN_DIGIT,
} DtmfStreamState;

typedef struct {
    const DtmfFilterConfig *cfg;
    DtmfStreamState state; /* Frame-level streaming state machine. */
    char current_digit;    /* Candidate digit currently accumulating. */
    int  current_frames;   /* Consecutive frames supporting current_digit. */
    int  gap_frames;       /* Consecutive empty frames since last digit. */
    char last_digit;       /* Last digit emitted to the caller. */
} DtmfDetectorState;

typedef struct {
    int    block_index;
    double row_peak;
    double col_peak;
    double row2_peak;
    double col2_peak;
    double row2_ratio_db;
    double col2_ratio_db;
    char   frame_digit;
    char   emitted_digit;
} DtmfFrameFeatures;

/* Initialize the precomputed Goertzel coefficients for tuned DTMF bins. */
void dtmf_init_filter_config(DtmfFilterConfig *cfg);

/* Run the Goertzel bank on one 102-sample block. */
void dtmf_compute_energy_block(const int16_t *samples,
                               const DtmfFilterConfig *cfg,
                               DtmfEnergyTemplate *out);

/* Reset detector bookkeeping before processing a stream. */
void dtmf_detector_init(DtmfDetectorState *st, const DtmfFilterConfig *cfg);

/*
 * Process one block; return 0 if no new digit was confirmed this block or the
 * ASCII digit if stability criteria were satisfied.
 */
char dtmf_detector_process_block(DtmfDetectorState *st,
                                const int16_t *samples,
                                DtmfFrameFeatures *features);

/* Decode a WAV file and print the detected DTMF digits to stdout. */
void decode_wav(const char *path);

#endif /* DTMF_DECODE_H */
