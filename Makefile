# TODO: build types for game and its dependencies (for now debug libraylib.a is used)
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wno-gnu-zero-variadic-macro-arguments -g -O0
INCLUDES = -isystem vendor/raylib/src/
OPTIONS = -DMEMORY_MMAP -DLOG_STDLIB
CC = clang
LDLIBS = -L./vendor/raylib/src/ -l:libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11
BUILD_DIR = build
# TODO: this will not work with gcc
EXTRA_CFLAGS = -MJ $(BUILD_DIR)/compile_commands.json
PROJECT = igario

.PHONY: run clean

run: build
	./$(BUILD_DIR)/$(PROJECT)

debug: build
	gdb ./$(BUILD_DIR)/$(PROJECT)

generate:
	mkdir -p $(BUILD_DIR)/generated

build: generate main.c
	$(CC) main.c -o $(BUILD_DIR)/$(PROJECT) $(CFLAGS) -I$(BUILD_DIR) $(INCLUDES) $(LDLIBS) $(EXTRA_CFLAGS) $(OPTIONS)
	sed -e 's/^/[/' -e 's/,$$/]/' -i "$(BUILD_DIR)/compile_commands.json"

clean:
	rm -rf $(BUILD_DIR)
