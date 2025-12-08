/*
 * Public entry points for the DTMF decoder CLI.
 *
 * The decoder consumes 16-bit PCM mono WAV files and emits the detected DTMF
 * symbol stream to stdout. It is intentionally small: the heavy lifting lives in
 * src/decode.c while this header keeps the interface clear for any future
 * callers (tests, alternate UIs, etc.).
 */

#ifndef DTMF_DECODE_H
#define DTMF_DECODE_H

/* Decode a WAV file and print the detected DTMF digits to stdout. */
void decode_wav(const char *path);

#endif /* DTMF_DECODE_H */
