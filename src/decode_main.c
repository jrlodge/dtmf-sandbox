/*
 * Thin CLI wrapper around the decoder implementation. Keeping this file tiny
 * mirrors the generator's main.c: it only parses argv and hands control to the
 * library-style decode_wav function so the core logic stays reusable.
 */

#include <stdio.h>

#include "decode.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: dtmf-decode <input.wav>\n");
        return 1;
    }

    decode_wav(argv[1]);
    return 0;
}
