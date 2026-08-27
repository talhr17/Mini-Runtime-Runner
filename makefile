# Flags and Compiler
CC= gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -Iheader

# files and products
BIN_DIR = bin
OBJ_DIR = obj
TARGET  = $(BIN_DIR)/taskrunner
SRCS    = $(wildcard src/*.c)
OBJS    = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))
HDRS    = $(wildcard header/*.h)

.PHONY: all test clean

# Rules
all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: src/%.c $(HDRS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./tests/run_tests.sh

# Clean 
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) run