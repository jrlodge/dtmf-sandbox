// src/decode_main.c
#include <stdio.h>

void decode_wav(const char *path);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: dtmf-decode <input.wav>\n");
        return 1;
    }

    decode_wav(argv[1]);
    return 0;
}
