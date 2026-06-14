CFLAGS = -std=c99 -Wall -Wextra -D_GNU_SOURCE
CC = gcc
GDB = gdb
BUILD_DIR = build
RAYLIB_DIR = vendor/raylib/src
RAYLIB_CFLAGS = PLATFORM=PLATFORM_DESKTOP
BUILD_MODE = RELEASE

# client
CLIENT_BINARY = igario
CLIENT_CFLAGS = $(CFLAGS)
CLIENT_INCLUDES = -isystem $(RAYLIB_DIR)
CLIENT_LDLIBS = -L$(RAYLIB_DIR) -l:libraylib.a -lm -ldl -lpthread -lX11 -lxcb -lGL -lGLX -lXext -lGLdispatch -lXau -lXdmcp -lX11

# server
SERVER_BINARY = igario_server
SERVER_CFLAGS = $(CFLAGS)
SERVER_INCLUDES = -isystem $(RAYLIB_DIR)/external
SERVER_OPTIONS = -DOS_IMPLEMENTATION_LINUX -DMEMORY_MMAP -DLOG_IMPLEMENTATION -DARENA_IMPLEMENTATION -DRPRAND_IMPLEMENTATION
SERVER_LDLIBS = -lm -lpthread

.PHONY: build client server generate raylib clean

ifeq ($(BUILD_MODE),DEBUG)
    CFLAGS += -g -O0 -DDEBUG
	RAYLIB_CFLAGS += RAYLIB_BUILD_MODE=DEBUG
endif

ifeq ($(BUILD_MODE),RELEASE)
    CFLAGS += -Oz -s
	RAYLIB_CFLAGS += RAYLIB_BUILD_MODE=RELEASE
endif

build: server client

client: raylib generate $(CLIENT_BINARY).c
	$(CC) os_linux.c $(CLIENT_BINARY).c -o $(BUILD_DIR)/$(CLIENT_BINARY) $(CLIENT_CFLAGS) -I$(BUILD_DIR) $(CLIENT_INCLUDES) $(CLIENT_LDLIBS) $(CLIENT_OPTIONS) -flto

server: generate $(SERVER_BINARY).c
	$(CC) $(SERVER_BINARY).c -o $(BUILD_DIR)/$(SERVER_BINARY) $(SERVER_CFLAGS) -I$(BUILD_DIR) $(SERVER_INCLUDES) $(SERVER_LDLIBS) $(SERVER_OPTIONS)

generate:
	mkdir -p $(BUILD_DIR)/generated

raylib:
	$(MAKE) -C $(RAYLIB_DIR) $(RAYLIB_CFLAGS)

clean:
	$(MAKE) -C $(RAYLIB_DIR) clean
	$(RM) -rf $(BUILD_DIR)
