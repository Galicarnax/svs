CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

SRC_DIR = src
BUILD_DIR = build
INSTALL_DIR = /usr/local/bin

SRC = $(SRC_DIR)/svs.c
OBJ = $(BUILD_DIR)/svs.o
TARGET = $(BUILD_DIR)/svs

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(OBJ): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

.PHONY: install
install: $(TARGET)
	install -d $(INSTALL_DIR)
	install -m 755 $(TARGET) $(INSTALL_DIR)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
