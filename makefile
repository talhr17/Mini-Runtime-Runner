# Flags and Compiler
CC= gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g

# files and products
BIN_DIR = bin
TARGET  = $(BIN_DIR)/taskrunner
SRCS    = main.c parser.c reporter.c runner.c
OBJS    = $(SRCS:.c=.o)
HDRS    = job.h parser.h runner.h reporter.h

.PHONY: all test clean

# Rules
all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./tests/run_tests.sh

# Clean 
clean:
	rm -rf $(OBJS) $(BIN_DIR) run