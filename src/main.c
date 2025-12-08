/*
 * Entry point for the DTMF Lab command-line utility.
 *
 * This file focuses purely on user interaction and translating CLI arguments
 * into the parameter structures consumed by the DTMF generation library. The
 * heavy lifting (tone synthesis and WAV serialization) is handled in dtmf.c;
 * here we validate inputs, provide help text, and orchestrate which library
 * functions to call based on whether the user asked for a single key or an
 * entire sequence.
 */

#include "dtmf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void print_usage(const char *prog_name) {
    /* Human-friendly help text that mirrors the README and --help output. */
    printf("DTMF Lab - DTMF Tone Generator\n");
    printf("Usage: %s [OPTIONS] <key or sequence>\n\n", prog_name);
    printf("Options:\n");
    printf("  -o, --output FILE     Output WAV filename (default: dtmf.wav)\n");
    printf("  -d, --duration MS     Duration per tone in milliseconds (default: 200)\n");
    printf("  -g, --gap MS          Gap between tones in sequence (default: 50)\n");
    printf("  -r, --rate RATE       Sample rate in Hz (default: 8000)\n");
    printf("  -a, --amplitude VAL   Amplitude 0.0-1.0 (default: 0.8)\n");
    printf("  -h, --help            Show this help message\n\n");
    printf("Valid keys: 0-9, A-D, *, #\n");
    printf("Examples:\n");
    printf("  %s 5                  Generate tone for key '5'\n", prog_name);
    printf("  %s -o test.wav 123    Generate sequence '123'\n", prog_name);
    printf("  %s -d 300 -g 100 911  Generate '911' with custom timing\n", prog_name);
}

int main(int argc, char *argv[]) {
    /* Default parameters that match the README guidance. The user can
     * override all of these via command-line flags. */
    char *output_file = "dtmf.wav";
    int duration_ms = 200;
    int gap_ms = 50;
    int sample_rate = 8000;
    double amplitude = 0.8;
    
    /* Long-form options mirror the short flags to make the interface easier
     * to discover (e.g., --duration as an alias for -d). */
    static struct option long_options[] = {
        {"output",    required_argument, 0, 'o'},
        {"duration",  required_argument, 0, 'd'},
        {"gap",       required_argument, 0, 'g'},
        {"rate",      required_argument, 0, 'r'},
        {"amplitude", required_argument, 0, 'a'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    /*
     * getopt_long iterates over arguments until all recognized flags are
     * consumed. Input validation happens immediately so we can fail early on
     * clearly invalid values (negative durations, out-of-range amplitudes,
     * etc.).
     */
    while ((opt = getopt_long(argc, argv, "o:d:g:r:a:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'o':
                output_file = optarg;
                break;
            case 'd':
                duration_ms = atoi(optarg);
                if (duration_ms <= 0) {
                    fprintf(stderr, "Error: Duration must be positive\n");
                    return 1;
                }
                break;
            case 'g':
                gap_ms = atoi(optarg);
                if (gap_ms < 0) {
                    fprintf(stderr, "Error: Gap must be non-negative\n");
                    return 1;
                }
                break;
            case 'r':
                sample_rate = atoi(optarg);
                if (sample_rate <= 0) {
                    fprintf(stderr, "Error: Sample rate must be positive\n");
                    return 1;
                }
                break;
            case 'a':
                amplitude = atof(optarg);
                if (amplitude < 0.0 || amplitude > 1.0) {
                    fprintf(stderr, "Error: Amplitude must be between 0.0 and 1.0\n");
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* After option parsing, argv[optind] points at the first non-option
     * argument. At least one key is required; otherwise we display the usage
     * banner and exit with an error. */
    if (optind >= argc) {
        fprintf(stderr, "Error: No key or sequence provided\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *input = argv[optind];
    
    /* Populate the tone parameters that will be shared across generation
     * calls. These settings apply to either a single tone or every element in
     * a multi-key sequence. */
    dtmf_params_t params;
    params.sample_rate = sample_rate;
    params.duration_ms = duration_ms;
    params.amplitude = amplitude;
    
    int result;
    
    if (strlen(input) == 1) {
        /* Single key */
        printf("Generating DTMF tone for key '%c'...\n", input[0]);
        result = dtmf_generate_wav(input[0], output_file, &params);
    } else {
        /* Sequence */
        printf("Generating DTMF sequence '%s'...\n", input);
        result = dtmf_generate_sequence_wav(input, output_file, &params, gap_ms);
    }
    
    if (result == 0) {
        printf("Successfully wrote to '%s'\n", output_file);
        printf("  Sample rate: %d Hz\n", sample_rate);
        printf("  Duration: %d ms per tone\n", duration_ms);
        if (strlen(input) > 1) {
            printf("  Gap: %d ms between tones\n", gap_ms);
        }
        printf("  Amplitude: %.2f\n", amplitude);
        return 0;
    } else {
        fprintf(stderr, "Error: Failed to generate DTMF tone(s)\n");
        return 1;
    }
}
