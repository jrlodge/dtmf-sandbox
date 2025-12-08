#!/usr/bin/env bash
set -e

# Create output folders (if they don't exist yet)
mkdir -p build bin

# Build the main generator
gcc -Wall -Wextra -O2 -I./include -c src/dtmf.c -o build/dtmf.o
gcc -Wall -Wextra -O2 -I./include -c src/main.c -o build/main.o
gcc build/dtmf.o build/main.o -lm -o bin/dtmf-lab

# Build the decoder CLI
gcc -Wall -Wextra -O2 -I./include -c src/decode.c -o build/decode.o
gcc -Wall -Wextra -O2 -I./include -c src/decode_main.c -o build/decode_main.o
gcc build/dtmf.o build/decode.o build/decode_main.o -lm -o bin/dtmf-decode

# Build the noise mixer utility
gcc -Wall -Wextra -O2 -I./include -c src/noise_mix.c -o build/noise_mix.o
gcc build/noise_mix.o -lm -o bin/noise-mix

