#ifndef GAME_H
#define GAME_H

#include "base/util.h"

#define CELL_SIZE 40.f
#define MAP_CELL_COUNT 50
#define PLAYER_START_SPEED 180
#define PLAYER_START_RADIUS (CELL_SIZE / 2.f)
#define PLAYER_PROXIMITY_THRESHOLD 200
// TODO: rename
#define PLAYER_SHRINK_COEFFICIENT 1.5f
#define PLAYERS_MAX 20
#define PLAYER_RECONNECT_TIME_USEC SEC_TO_USEC(3)
#define FOOD_ITEMS_MAX 100
#define FOOD_ITEM_MIN_RADIUS (CELL_SIZE / 5.f)
#define FOOD_ITEM_MAX_RADIUS (PLAYER_START_RADIUS)
// TODO: explain why this is needed (tldr: less anoying gameplay)
#define FOOD_ITEM_PROXIMITY_THRESHOLD 200
#define FOOD_SPAWN_INTERVAL_MSEC SEC_TO_MSEC(2)

typedef usize PlayerId;

typedef struct {
    PlayerId id;
    Rgba color;
    Vec2 position;
    f32 radius;
} Player;

// TODO: maybe this should be a part of GameState?
typedef struct {
    f32 width;
    f32 height;
} Map;

f32 MapGetRightBound(const Map *map) { return map->width / 2.f; }

static inline f32 MapGetLeftBound(const Map *map) { return -map->width / 2.f; }

static inline f32 MapGetLowerBound(const Map *map) { return map->height / 2.f; }

static inline f32 MapGetUpperBound(const Map *map) {
    return -map->height / 2.f;
}

#define NET_PACKET_MAX_SIZE 2048

#pragma pack(push, 1)

#define PACKET_TYPES_CLIENT(_)                                                 \
    _(EMPTY)                                                                   \
    _(PLAYER_CONNECT)                                                          \
    _(PLAYER_MOVE)                                                             \
    _(PLAYER_SHRINK)

#define PACKET_TYPES_SERVER(_)                                                 \
    _(EMPTY)                                                                   \
    _(PLAYER_SPAWN)                                                            \
    _(PLAYER_DEAD)                                                             \
    _(GAME_STATE)

#define AS_PACKET_TYPE_CLIENT(x) PACKET_CLIENT_##x,
#define AS_PACKET_TYPE_SERVER(x) PACKET_SERVER_##x,
#define AS_PACKET_STR(x) #x,

typedef enum {
    PACKET_TYPES_CLIENT(AS_PACKET_TYPE_CLIENT)
    PACKET_CLIENT_COUNT
} PacketTypesClient;

typedef struct {
    Vec2 newPosition;
} PacketPlayerMove;

typedef struct {
    PacketTypesClient type;
    union {
        PacketPlayerMove playerMove;
    } data;
} PacketClient;

typedef enum {
    PACKET_TYPES_SERVER(AS_PACKET_TYPE_SERVER)
    PACKET_SERVER_COUNT
} PacketTypesServer;

typedef struct {
    Vec2 position;
    f32 radius;
} FoodItem;

typedef struct {
    u8 playerCount;
    u16 foodItemCount;
    Player players[1];
    FoodItem foodItems[1];
} PacketGameState;

static inline usize PacketGameStateSize(usize clientCount, usize foodItemCount) {
    return MEMBER_SIZE(PacketGameState, playerCount)
           + MEMBER_SIZE(PacketGameState, players) * (clientCount)
           + MEMBER_SIZE(PacketGameState, foodItemCount)
           + MEMBER_SIZE(PacketGameState, foodItems) * (foodItemCount);
}

typedef struct {
    Player player;
    Map map;
} PacketPlayerSpawn;

typedef struct {
    PacketTypesServer type;
    u16 payloadSize;
} PacketHeader;

#define PACKET_HEADER_SIZE MEMBER_OFFSET(PacketServer, payload)

typedef struct {
    PacketHeader header;
    union {
        PacketPlayerSpawn playerSpawn;
        PacketGameState gameState;
    } payload;
} PacketServer;

typedef struct {
    u8 count;
    PacketServer packets[1];
} PacketsServer;

#define PACKETS_HEADER_SIZE MEMBER_OFFSET(PacketsServer, packets)

#pragma pack(pop)

#endif /* GAME_H */
