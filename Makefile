# DTMF Lab Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I./include
LDFLAGS = -lm

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

TARGET = dtmf-lab
SOURCES = $(SRC_DIR)/dtmf.c $(SRC_DIR)/main.c
OBJECTS = $(BUILD_DIR)/dtmf.o $(BUILD_DIR)/main.o
HEADERS = $(INC_DIR)/dtmf.h

.PHONY: all clean run test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET) *.wav

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
