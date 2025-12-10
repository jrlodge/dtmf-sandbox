# DTMF Lab

A complete DTMF (Dual-Tone Multi-Frequency) tone generator and decoder toolkit written in C, with CLI and web-based GUI interfaces.

## Features

- **C Core Library**: Pure C implementation for DTMF tone generation
- **WAV File Output**: Generates 16-bit PCM WAV files
- **CLI Interface**: Command-line tool for generating tones and sequences
- **Goertzel Algorithm**: Foundation for DTMF tone decoding (ready for future implementation)
- **Web GUI**: Interactive keypad for real-time tone generation and playback
- **Minimal Build**: Simple Makefile-based build system

## DTMF Keys Supported

The standard 16-key DTMF keypad layout:
```
1 2 3 A
4 5 6 B  
7 8 9 C
* 0 # D
```

Each key produces two simultaneous tones (low frequency + high frequency):
- Row frequencies: 697 Hz, 770 Hz, 852 Hz, 941 Hz
- Column frequencies: 1209 Hz, 1336 Hz, 1477 Hz, 1633 Hz

## Building

### Prerequisites
- GCC compiler
- Make
- Math library (libm)

### Build Instructions
```bash
make            # Build the project (binaries land in bin/)
make clean      # Remove build artifacts
make test       # Build and run tests (WAVs saved to artifacts/wav/)
make help       # Show available make targets
```

The compiled binary will be placed in `bin/dtmf-lab`.

**Windows build:** A PowerShell equivalent to `build.sh` lives at `build.ps1`. With
PowerShell 7+ and GCC/Clang on your `PATH`, run:

```powershell
pwsh ./build.ps1
```

The script emits `.exe` binaries in `bin/` (e.g., `bin/dtmf-lab.exe`).

#### GCC "cheat sheet" (manual builds without `make`)
If you prefer to invoke the compiler directly, these one-liners mirror the
Makefile defaults and keep executables in `bin/` while object files stay in
`build/`:

```bash
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

# Build the silence generator
gcc -Wall -Wextra -O2 -I./include -c src/silence_gen.c -o build/silence_gen.o
gcc build/silence_gen.o build/dtmf.o -lm -o bin/silence-gen

# Run tests and keep generated WAVs under artifacts/wav/
mkdir -p artifacts/wav
./bin/dtmf-lab -o artifacts/wav/manual_test.wav 123
```

## Usage

### Command-Line Interface

#### Basic Usage
```bash
./bin/dtmf-lab <key or sequence>
```

#### Options
- `-o, --output FILE` - Output WAV filename (default: dtmf.wav)
- `-d, --duration MS` - Duration per tone in milliseconds (default: 200)
- `-g, --gap MS` - Gap between tones in sequence (default: 50)
- `-r, --rate RATE` - Sample rate in Hz (default: 8000)
- `-a, --amplitude VAL` - Amplitude 0.0-1.0 (default: 0.8)
- `-h, --help` - Show help message

#### Examples

Generate a single tone for key '5':
```bash
./bin/dtmf-lab 5
```

Generate a phone number sequence:
```bash
./bin/dtmf-lab -o artifacts/wav/phone.wav 5551234
```

Generate with custom timing:
```bash
./bin/dtmf-lab -d 300 -g 100 -o artifacts/wav/custom.wav 911
```

High-quality audio (44.1kHz):
```bash
./bin/dtmf-lab -r 44100 -o artifacts/wav/hq.wav 123
```

Generate special keys:
```bash
./bin/dtmf-lab '*'
./bin/dtmf-lab '#'
./bin/dtmf-lab ABCD
```

## Test sample generation

- ATC background noise clips live under `testdata/atc/` (mono 8 kHz 16-bit PCM WAV).
- DTMF sequences used for bulk test generation are defined in `testdata/codes.txt` (one code per line; lines starting with `#` are ignored).

To build the helper binaries and generate the full stress-test WAV set:

```bash
bash build.sh
bash tools/gen_dtmf_tests.sh
```

The generated WAVs land in `artifacts/wav/tests/` (ignored by git) and include clean, white-noise, ATC-noise, noise-only, and silence cases across multiple spacing regimes.

One-step evaluation wrappers are available for macOS (bash) and Windows (PowerShell):

```bash
# macOS
bash tools/run_evaluation_macos.sh

# Windows
pwsh tools/run_evaluation_windows.ps1
```

## Current evaluation

An automated harness generates noisy test fixtures and measures decoder accuracy:

- `tools/gen_dtmf_tests.sh` builds the helper binaries, then emits clean/white-noise/ATC-noise/noise-only/silence WAVs under `artifacts/wav/tests/` using the sequences in `testdata/codes.txt`.
- `tools/evaluate_dtmf.py` infers ground truth from filenames, runs `bin/dtmf-decode` over every WAV, writes a CSV report, and prints per-condition/per-SNR accuracy.

Run the full loop with:

```bash
bash build.sh
bash tools/gen_dtmf_tests.sh
python3 tools/evaluate_dtmf.py
```

Current baseline (latest run):

- Clean: 100% sequence accuracy on all samples.
- White noise: ~72% sequence accuracy across SNR sweeps.
- ATC noise: ~65% sequence accuracy across SNR sweeps.
- Silence / noise-only: generally low false positives; see the CSV for details.

Use the CSV (`artifacts/wav/report.csv`) and the printed summary from `tools/evaluate_dtmf.py` to track regressions or improvements.

## Future Work / Planned Improvements

1. **Frame-level streaming state machine** (initial implementation landed): The decoder now runs as a streaming IDLE/IN_DIGIT state machine that clusters frames into digits with minimum tone and gap durations. Next steps are tuning and experimenting with stronger quality gating.
2. **Per-frame quality gates and sanity checks**: Before a frame proposes a digit, enforce an absolute energy threshold, row/column dominance gaps, and twist limits (row vs. column balance). Expose these thresholds as configurable constants near the top of the decoder for tuning with `tools/evaluate_dtmf.py`. Goal: reduce mis-classifications and false positives on noise-only/silence and low-SNR signals.
3. **Optional bandpass filter pre-processing**: Add an optional 300–3400 Hz bandpass (e.g., one or two biquad IIR stages) ahead of the Goertzel bank, controlled by a compile-time flag so we can A/B benchmark. Goal: improve robustness to out-of-band rumble and ATC chatter with strong low-frequency content.

Each step will be evaluated incrementally with `tools/evaluate_dtmf.py`, watching per-condition accuracy (clean, white, ATC, noise-only, silence) and per-SNR trends to confirm improvements.

### Web-Based GUI

Open `web/keypad.html` in a modern web browser:
```bash
firefox web/keypad.html
# or
chromium web/keypad.html
# or simply open the file in your browser
```

Features:
- Click buttons to hear DTMF tones in real-time
- Build sequences by clicking multiple keys
- Play entire sequence with one button
- Keyboard support (press keys on your keyboard)
- Visual feedback when keys are pressed

## Project Structure

```
dtmf-sandbox/
├── include/
│   └── dtmf.h              # Header file with API definitions
├── src/
│   ├── dtmf.c              # Core DTMF generation and Goertzel implementation
│   └── main.c              # CLI application
├── web/
│   └── keypad.html         # Web-based GUI keypad
├── Makefile                # Build system
└── README.md              # This file
```

## API Documentation

### Core Functions

#### `dtmf_get_frequencies()`
Get the frequency pair for a given DTMF key.
```c
int dtmf_get_frequencies(char key, dtmf_freq_t *freq);
```

#### `dtmf_generate_tone()`
Generate DTMF tone samples in memory.
```c
int dtmf_generate_tone(const dtmf_freq_t *freq, const dtmf_params_t *params, int16_t *samples);
```

#### `dtmf_write_wav()`
Write samples to a WAV file.
```c
int dtmf_write_wav(const char *filename, const int16_t *samples, int num_samples, int sample_rate);
```

#### `dtmf_generate_wav()`
Generate a single DTMF tone and save to WAV file.
```c
int dtmf_generate_wav(char key, const char *filename, const dtmf_params_t *params);
```

#### `dtmf_generate_sequence_wav()`
Generate a sequence of DTMF tones with gaps and save to WAV file.
```c
int dtmf_generate_sequence_wav(const char *sequence, const char *filename, 
                                const dtmf_params_t *params, int gap_ms);
```

### Goertzel Algorithm Functions

Foundation for DTMF decoding (future implementation):

#### `goertzel_init()`
Initialize a Goertzel detector for a specific frequency.
```c
void goertzel_init(goertzel_state_t *state, double target_freq, int sample_rate, int n);
```

#### `goertzel_process_sample()`
Process a sample with the Goertzel algorithm.
```c
void goertzel_process_sample(goertzel_state_t *state, double sample);
```

#### `goertzel_magnitude()`
Get the magnitude result from the Goertzel detector.
```c
double goertzel_magnitude(const goertzel_state_t *state);
```

## Testing

Run the included test suite:
```bash
make test
```

This will generate several test WAV files in `artifacts/wav/`:
- `test_5.wav` - Single key test
- `test_sequence.wav` - Numeric sequence (0-9)
- `test_star.wav` - Star key (*)
- `test_hash.wav` - Hash key (#)
- `test_abcd.wav` - Extended keys (A-D)

## Technical Details

### WAV Format
- Format: PCM (uncompressed)
- Bit depth: 16-bit signed integer
- Channels: Mono
- Default sample rate: 8000 Hz (telephone quality)
- Configurable sample rates: Any positive value

### DTMF Generation
- Uses sine wave generation for each frequency component
- Mixes low and high frequency tones at equal amplitude
- Supports configurable amplitude, duration, and sample rate
- No filtering applied (pure tones)

### Goertzel Algorithm
- Efficient frequency detection algorithm
- Alternative to FFT for detecting specific frequencies
- Ideal for DTMF decoding
- Currently implemented as foundation for future decoder

## Future Enhancements

- [ ] Complete DTMF decoder implementation
- [ ] Real-time audio input/output support
- [ ] Additional audio format support (MP3, OGG, etc.)
- [ ] Noise filtering and signal processing
- [ ] GUI applications (Qt, GTK)
- [ ] Python/Node.js bindings
- [ ] DTMF sequence validation and error correction

## License

This is a sandbox project for educational and experimental purposes.

## Contributing

Feel free to submit issues and enhancement requests!