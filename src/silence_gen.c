#include "dtmf.h"
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog_name) {
    printf("Silence generator\n");
    printf("Usage: %s -o output.wav --duration-ms <ms> [--sample-rate <Hz>]\n", prog_name);
    printf("\nOptions:\n");
    printf("  -o, --output       Path to output WAV (required)\n");
    printf("      --duration-ms  Duration of silence in milliseconds (required)\n");
    printf("      --sample-rate  Sample rate in Hz (default: 8000)\n");
    printf("  -h, --help         Show this message\n");
}

int main(int argc, char *argv[]) {
    const char *output_path = NULL;
    int duration_ms = -1;
    int sample_rate = 8000;

    static struct option long_options[] = {
        {"output",      required_argument, 0, 'o'},
        {"duration-ms", required_argument, 0,  1 },
        {"sample-rate", required_argument, 0,  2 },
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "o:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'o':
                output_path = optarg;
                break;
            case 1:
                duration_ms = atoi(optarg);
                break;
            case 2:
                sample_rate = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!output_path || duration_ms <= 0 || sample_rate <= 0) {
        fprintf(stderr, "Error: output, duration-ms (>0), and sample-rate (>0) are required.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    int num_samples = (sample_rate * duration_ms) / 1000;
    if (num_samples <= 0) {
        fprintf(stderr, "Error: computed sample count is non-positive.\n");
        return 1;
    }

    int16_t *samples = (int16_t *)calloc((size_t)num_samples, sizeof(int16_t));
    if (!samples) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    if (dtmf_write_wav(output_path, samples, num_samples, sample_rate) != 0) {
        fprintf(stderr, "Failed to write WAV to %s\n", output_path);
        free(samples);
        return 1;
    }

    printf("Wrote %d ms of silence to %s (sample rate %d Hz)\n", duration_ms, output_path, sample_rate);
    free(samples);
    return 0;
}
