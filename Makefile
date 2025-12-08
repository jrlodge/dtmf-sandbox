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
BIN_DIR = bin
WAV_DIR = artifacts/wav

TARGET = dtmf-lab
DECODER_TARGET = dtmf-decode
NOISE_TARGET = noise-mix

TARGET_PATH = $(BIN_DIR)/$(TARGET)
DECODER_TARGET_PATH = $(BIN_DIR)/$(DECODER_TARGET)
NOISE_TARGET_PATH = $(BIN_DIR)/$(NOISE_TARGET)

SOURCES = $(SRC_DIR)/dtmf.c $(SRC_DIR)/main.c
DECODER_SOURCES = $(SRC_DIR)/dtmf.c $(SRC_DIR)/decode.c $(SRC_DIR)/decode_main.c
NOISE_SOURCES = $(SRC_DIR)/noise_mix.c

OBJECTS = $(BUILD_DIR)/dtmf.o $(BUILD_DIR)/main.o
DECODER_OBJECTS = $(BUILD_DIR)/dtmf.o $(BUILD_DIR)/decode.o $(BUILD_DIR)/decode_main.o
NOISE_OBJECTS = $(BUILD_DIR)/noise_mix.o

HEADERS = $(INC_DIR)/dtmf.h $(INC_DIR)/decode.h

.PHONY: all clean run test

all: $(TARGET_PATH) $(DECODER_TARGET_PATH) $(NOISE_TARGET_PATH)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(WAV_DIR):
	mkdir -p $(WAV_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_PATH): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(DECODER_TARGET_PATH): $(DECODER_OBJECTS) | $(BIN_DIR)
	$(CC) $(DECODER_OBJECTS) $(LDFLAGS) -o $@

$(NOISE_TARGET_PATH): $(NOISE_OBJECTS) | $(BIN_DIR)
	$(CC) $(NOISE_OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(WAV_DIR) *.wav

run: $(TARGET_PATH)
	./$(TARGET_PATH) --help

test: $(TARGET_PATH) | $(WAV_DIR)
	@echo "Testing single key..."
	./$(TARGET_PATH) -o $(WAV_DIR)/test_5.wav 5
	@echo ""
	@echo "Testing sequence..."
	./$(TARGET_PATH) -o $(WAV_DIR)/test_sequence.wav 1234567890
	@echo ""
	@echo "Testing special keys..."
	./$(TARGET_PATH) -o $(WAV_DIR)/test_star.wav '*'
	./$(TARGET_PATH) -o $(WAV_DIR)/test_hash.wav '#'
	./$(TARGET_PATH) -o $(WAV_DIR)/test_abcd.wav ABCD
	@echo ""
	@echo "All tests completed! Generated WAV files:"
	@ls -lh $(WAV_DIR)/test_*.wav

install: $(TARGET_PATH)
	install -m 755 $(TARGET_PATH) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: help
help:
	@echo "DTMF Lab Build System"
	@echo "Usage:"
	@echo "  make          - Build the project (binaries in $(BIN_DIR)/)"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make run      - Build and show help"
	@echo "  make test     - Build and run tests (WAVs in $(WAV_DIR)/)"
	@echo "  make install  - Install to /usr/local/bin"
	@echo "  make uninstall- Remove from /usr/local/bin"
