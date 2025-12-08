# DTMF Lab Makefile
#
# This build script keeps the project intentionally simple: a handful of
# variables describe where source, headers, and build artifacts live, and a few
# phony targets cover the common workflows (build, clean, run help text, and a
# small smoke-test suite that emits WAV files). The resulting binary is
# self-contained and depends only on the C standard library and libm.

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include
LDFLAGS = -lm

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

TARGET = dtmf-lab
DECODER_TARGET = dtmf-decode
NOISE_TARGET = noise-mix

SOURCES = $(SRC_DIR)/dtmf.c $(SRC_DIR)/main.c
DECODER_SOURCES = $(SRC_DIR)/dtmf.c $(SRC_DIR)/decode.c $(SRC_DIR)/decode_main.c
NOISE_SOURCES = $(SRC_DIR)/noise_mix.c

OBJECTS = $(BUILD_DIR)/dtmf.o $(BUILD_DIR)/main.o
DECODER_OBJECTS = $(BUILD_DIR)/dtmf.o $(BUILD_DIR)/decode.o $(BUILD_DIR)/decode_main.o
NOISE_OBJECTS = $(BUILD_DIR)/noise_mix.o

HEADERS = $(INC_DIR)/dtmf.h $(INC_DIR)/decode.h

.PHONY: all clean run test

all: $(TARGET) $(DECODER_TARGET) $(NOISE_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

$(DECODER_TARGET): $(DECODER_OBJECTS)
	$(CC) $(DECODER_OBJECTS) $(LDFLAGS) -o $(DECODER_TARGET)

$(NOISE_TARGET): $(NOISE_OBJECTS)
	$(CC) $(NOISE_OBJECTS) $(LDFLAGS) -o $(NOISE_TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(DECODER_TARGET) $(NOISE_TARGET) *.wav

run: $(TARGET)
	./$(TARGET) --help

test: $(TARGET)
	@echo "Testing single key..."
	./$(TARGET) -o test_5.wav 5
	@echo ""
	@echo "Testing sequence..."
	./$(TARGET) -o test_sequence.wav 1234567890
	@echo ""
	@echo "Testing special keys..."
	./$(TARGET) -o test_star.wav '*'
	./$(TARGET) -o test_hash.wav '#'
	./$(TARGET) -o test_abcd.wav ABCD
	@echo ""
	@echo "All tests completed! Generated WAV files:"
	@ls -lh test_*.wav

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: help
help:
	@echo "DTMF Lab Build System"
	@echo "Usage:"
	@echo "  make          - Build the project"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make run      - Build and show help"
	@echo "  make test     - Build and run tests"
	@echo "  make install  - Install to /usr/local/bin"
	@echo "  make uninstall- Remove from /usr/local/bin"
