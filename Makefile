# TODO: build types for game and its dependencies (for now debug libraylib.a is used)
CFLAGS = -std=c99 -Wall -Wextra -D_GNU_SOURCE
CC = gcc
GDB = gdb
BUILD_DIR = build
RAYLIB_DIR = vendor/raylib/src
RAYLIB_CFLAGS = PLATFORM=PLATFORM_DESKTOP
BUILD_MODE = RELEASE

# game
GAME_BINARY = igario
GAME_CFLAGS = $(CFLAGS)
GAME_INCLUDES = -isystem $(RAYLIB_DIR)
GAME_OPTIONS = -DOS_IMPLEMENTATION_LINUX -DMEMORY_MMAP -DLOG_IMPLEMENTATION -DARENA_IMPLEMENTATION
GAME_LDLIBS = -L./vendor/raylib/src/ -l:libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11

# server
SERVER_BINARY = server
SERVER_CFLAGS = $(CFLAGS)
SERVER_INCLUDES = -isystem vendor/rprand
SERVER_OPTIONS = -DOS_IMPLEMENTATION_LINUX -DMEMORY_MMAP -DLOG_IMPLEMENTATION -DARENA_IMPLEMENTATION -DRPRAND_IMPLEMENTATION
SERVER_LDLIBS = -lm -lpthread
SERVER_PORT ?= 1337

.PHONY: run debug clean server server_debug

ifeq ($(BUILD_MODE),DEBUG)
    CFLAGS += -g -O0
	RAYLIB_CFLAGS += RAYLIB_BUILD_MODE=DEBUG
endif

ifeq ($(BUILD_MODE),RELEASE)
    CFLAGS += -Os
	RAYLIB_CFLAGS += RAYLIB_BUILD_MODE=RELEASE
endif

run: build
	./$(BUILD_DIR)/$(GAME_BINARY)

debug: build
	$(GDB) ./$(BUILD_DIR)/$(GAME_BINARY)

server: build
	./$(BUILD_DIR)/$(SERVER_BINARY) $(SERVER_PORT)

server_debug: build
	$(GDB) ./$(BUILD_DIR)/$(SERVER_BINARY)

generate:
	mkdir -p $(BUILD_DIR)/generated

deps:
	$(MAKE) -C $(RAYLIB_DIR) $(RAYLIB_CFLAGS)

build: deps generate $(GAME_BINARY).c $(SERVER_BINARY).c
	$(CC) $(GAME_BINARY).c -o $(BUILD_DIR)/$(GAME_BINARY) $(GAME_CFLAGS) -I$(BUILD_DIR) $(GAME_INCLUDES) $(GAME_LDLIBS) $(GAME_OPTIONS)
	$(CC) $(SERVER_BINARY).c -o $(BUILD_DIR)/$(SERVER_BINARY) $(SERVER_CFLAGS) -I$(BUILD_DIR) $(SERVER_INCLUDES) $(SERVER_LDLIBS) $(SERVER_OPTIONS)

clean:
	$(MAKE) -C $(RAYLIB_DIR) clean
	$(RM) -rf $(BUILD_DIR)
