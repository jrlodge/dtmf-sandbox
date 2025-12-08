/*
 * Noise Mixing Utility
 *
 * Standalone command-line tool that reads a 16-bit PCM mono WAV file, adds
 * synthetic white noise at a requested SNR, and writes a new WAV file. The
 * implementation mirrors the minimal WAV parsing style used by the decoder so
 * the tool remains dependency-free and easy to inspect.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>

#define CLAMP_INT16(v) (((v) > 32767) ? 32767 : (((v) < -32768) ? -32768 : (v)))

typedef struct {
    int16_t *samples;
    int      num_samples;
    int      sample_rate;
} WavData;

/* --- Little-endian helpers --- */

static int read_u32_le(FILE *f, uint32_t *out) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int read_u16_le(FILE *f, uint16_t *out) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 1;
}

static int write_u32_le(FILE *f, uint32_t value) {
    uint8_t b[4];
    b[0] = (uint8_t)(value & 0xFF);
    b[1] = (uint8_t)((value >> 8) & 0xFF);
    b[2] = (uint8_t)((value >> 16) & 0xFF);
    b[3] = (uint8_t)((value >> 24) & 0xFF);
    return fwrite(b, 1, 4, f) == 4;
}

static int write_u16_le(FILE *f, uint16_t value) {
    uint8_t b[2];
    b[0] = (uint8_t)(value & 0xFF);
    b[1] = (uint8_t)((value >> 8) & 0xFF);
    return fwrite(b, 1, 2, f) == 2;
}

/* --- WAV I/O --- */

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
        fprintf(stderr, "Expected mono WAV file\n");
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

static int write_wav(const char *path, const int16_t *samples, int num_samples, int sample_rate) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        return 0;
    }

    uint32_t data_bytes = (uint32_t)num_samples * 2;
    uint32_t riff_size = 36 + data_bytes;

    if (fwrite("RIFF", 1, 4, f) != 4 ||
        !write_u32_le(f, riff_size) ||
        fwrite("WAVE", 1, 4, f) != 4) {
        fclose(f);
        return 0;
    }

    if (fwrite("fmt ", 1, 4, f) != 4 ||
        !write_u32_le(f, 16) ||             /* PCM chunk size */
        !write_u16_le(f, 1) ||              /* PCM format */
        !write_u16_le(f, 1) ||              /* mono */
        !write_u32_le(f, (uint32_t)sample_rate) ||
        !write_u32_le(f, (uint32_t)sample_rate * 2) ||
        !write_u16_le(f, 2) ||              /* block align */
        !write_u16_le(f, 16)) {             /* bits per sample */
        fclose(f);
        return 0;
    }

    if (fwrite("data", 1, 4, f) != 4 ||
        !write_u32_le(f, data_bytes)) {
        fclose(f);
        return 0;
    }

    if (fwrite(samples, 2, num_samples, f) != (size_t)num_samples) {
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
}

/* --- Noise mixing --- */

static int mix_white_noise(const WavData *wav, double snr_db, int16_t **out_samples) {
    if (!wav || !wav->samples || wav->num_samples <= 0) {
        return 0;
    }

    double signal_power = 0.0;
    for (int i = 0; i < wav->num_samples; i++) {
        double s = (double)wav->samples[i];
        signal_power += s * s;
    }
    signal_power /= (double)wav->num_samples;

    if (signal_power <= 0.0) {
        fprintf(stderr, "Signal power is zero; cannot compute SNR\n");
        return 0;
    }

    double target_noise_power = signal_power / pow(10.0, snr_db / 10.0);

    double *noise = (double *)malloc(sizeof(double) * (size_t)wav->num_samples);
    if (!noise) {
        fprintf(stderr, "malloc failed\n");
        return 0;
    }

    double raw_power = 0.0;
    for (int i = 0; i < wav->num_samples; i++) {
        double r = ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;
        noise[i] = r;
        raw_power += r * r;
    }
    raw_power /= (double)wav->num_samples;

    if (raw_power <= 0.0) {
        fprintf(stderr, "Noise generation failed (zero raw power)\n");
        free(noise);
        return 0;
    }

    double scale = sqrt(target_noise_power / raw_power);

    int16_t *mixed = (int16_t *)malloc(sizeof(int16_t) * (size_t)wav->num_samples);
    if (!mixed) {
        fprintf(stderr, "malloc failed\n");
        free(noise);
        return 0;
    }

    for (int i = 0; i < wav->num_samples; i++) {
        double n = noise[i] * scale;
        double m = (double)wav->samples[i] + n;
        mixed[i] = (int16_t)CLAMP_INT16((int)m);
    }

    free(noise);
    *out_samples = mixed;
    return 1;
}

/* --- CLI --- */

static void print_usage(const char *prog_name) {
    printf("Noise Mixing Utility\n");
    printf("Usage: %s -i input.wav -o output.wav --snr-db <value> --mode <white>\n", prog_name);
    printf("\nOptions:\n");
    printf("  -i, --input     Path to clean input WAV (mono, 16-bit PCM)\n");
    printf("  -o, --output    Path to output WAV\n");
    printf("  --snr-db <dB>   Target signal-to-noise ratio in decibels\n");
    printf("  --mode <white>  Noise type (only 'white' supported)\n");
}

int main(int argc, char *argv[]) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *mode = NULL;
    double snr_db = 0.0;
    int snr_set = 0;

    static struct option long_options[] = {
        {"input",   required_argument, 0, 'i'},
        {"output",  required_argument, 0, 'o'},
        {"snr-db",  required_argument, 0,  1 },
        {"mode",    required_argument, 0,  2 },
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "i:o:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i':
                input_path = optarg;
                break;
            case 'o':
                output_path = optarg;
                break;
            case 1:
                snr_db = atof(optarg);
                snr_set = 1;
                break;
            case 2:
                mode = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!input_path || !output_path || !mode || !snr_set) {
        fprintf(stderr, "Error: Missing required arguments.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(mode, "white") != 0) {
        fprintf(stderr, "Error: Unsupported mode '%s'. Only 'white' is available.\n", mode);
        return 1;
    }

    if (!isfinite(snr_db)) {
        fprintf(stderr, "Error: Invalid SNR value.\n");
        return 1;
    }

    srand((unsigned int)time(NULL));

    WavData wav = load_wav(input_path);
    if (!wav.samples) {
        fprintf(stderr, "Failed to read input WAV '%s'\n", input_path);
        return 1;
    }

    int16_t *mixed = NULL;
    if (!mix_white_noise(&wav, snr_db, &mixed)) {
        free_wav(&wav);
        return 1;
    }

    int ok = write_wav(output_path, mixed, wav.num_samples, wav.sample_rate);
    free_wav(&wav);
    free(mixed);

    if (!ok) {
        fprintf(stderr, "Failed to write output WAV '%s'\n", output_path);
        return 1;
    }

    printf("Wrote noisy file to '%s' (SNR %.2f dB, mode %s)\n", output_path, snr_db, mode);
    return 0;
}
