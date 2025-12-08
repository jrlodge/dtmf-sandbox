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
make            # Build the project
make clean      # Remove build artifacts
make test       # Build and run tests
make help       # Show available make targets
```

The compiled binary will be named `dtmf-lab`.

## Usage

### Command-Line Interface

#### Basic Usage
```bash
./dtmf-lab <key or sequence>
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
./dtmf-lab 5
```

Generate a phone number sequence:
```bash
./dtmf-lab -o phone.wav 5551234
```

Generate with custom timing:
```bash
./dtmf-lab -d 300 -g 100 -o custom.wav 911
```

High-quality audio (44.1kHz):
```bash
./dtmf-lab -r 44100 -o hq.wav 123
```

Generate special keys:
```bash
./dtmf-lab '*'
./dtmf-lab '#'
./dtmf-lab ABCD
```

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

This will generate several test WAV files:
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