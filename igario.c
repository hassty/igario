#define MEMORY_STDLIB
#define LOG_IMPLEMENTATION
#define ARENA_IMPLEMENTATION
#include "game.h"
#include "base/log.h"
#include "base/arena.h"
#include "base/base.h"

#include <errno.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stddef.h>
#include <stdlib.h>

#include "os.h"

#define MAP_BG_COLOR RAYWHITE
#define FOOD_ITEM_COLOR ORANGE

#define DIRECTIONS(_)                                                          \
    _(NONE)                                                                    \
    _(RIGHT)                                                                   \
    _(LEFT)                                                                    \
    _(DOWN)                                                                    \
    _(UP)

#define AS_MOVE_DIRECTION(x) MOVE_##x,
#define AS_STR(x) #x,

typedef enum { DIRECTIONS(AS_MOVE_DIRECTION) } MoveDirection;
// static const char *directions_str[] = {DIRECTIONS(AS_STR)};

#define KEY_MAPS_MAX 2

typedef i32 KeyCode;

struct {
    KeyCode keysRight[KEY_MAPS_MAX];
    KeyCode keysLeft[KEY_MAPS_MAX];
    KeyCode keysDown[KEY_MAPS_MAX];
    KeyCode keysUp[KEY_MAPS_MAX];
    KeyCode keysShift[KEY_MAPS_MAX];
} keyMaps = {
    .keysRight = {KEY_RIGHT, KEY_D},
    .keysLeft = {KEY_LEFT, KEY_A},
    .keysDown = {KEY_DOWN, KEY_S},
    .keysUp = {KEY_UP, KEY_W},
    .keysShift = {KEY_RIGHT_SHIFT, KEY_LEFT_SHIFT},
};

static void DrawBG(i32 slices, f32 spacing) {
    i32 halfSlices = slices / 2;

    rlBegin(RL_LINES);
    for (i32 i = -halfSlices; i <= halfSlices; i++) {
        rlColor3f(0.75f, 0.75f, 0.75f);

        // vertical lines
        rlVertex2f((f32)i * spacing, (f32)-halfSlices * spacing);
        rlVertex2f((f32)i * spacing, (f32)halfSlices * spacing);

        // horizontal lines
        rlVertex2f((f32)-halfSlices * spacing, (f32)i * spacing);
        rlVertex2f((f32)halfSlices * spacing, (f32)i * spacing);
    }
    rlEnd();
}

static const char *packetTypesClientStr[] = {
    PACKET_TYPES_CLIENT(AS_PACKET_STR) "UNKNOWN"};

static Map map = {
    .width = MAP_CELL_COUNT * CELL_SIZE,
    .height = MAP_CELL_COUNT * CELL_SIZE,
};

static Player player = {0};
static Player ghost = {0};

typedef PacketGameState GameStateClient;
GameStateClient *gameState = NULL;

Arena frameArena = {0};

static os_socket *sock = NULL;

static bool PacketSend(const PacketClient *packet, usize packetSize) {
    i32 txBytes = os_sendto(sock, (const u8*)packet, packetSize);
    if (txBytes == -1) {
        LOG_WRN("sendto: packet type %s, errno %d",
                packetTypesClientStr[packet->type], errno);
        return false;
    }
    LOG_DBG("tx: %s, %zu bytes", packetTypesClientStr[packet->type], packetSize);
    return true;
}

static const char* packetServerStr[PACKET_SERVER_COUNT+1] = {
    PACKET_TYPES_SERVER(AS_STR)
    "UNKNOWN"
};

static bool PacketServerHandle(const PacketServer* packet) {
    LOG_DBG("handle packet: %s", packetServerStr[packet->header.type]);
    switch (packet->header.type) {
        case PACKET_SERVER_GAME_STATE: {
            const PacketGameState* gs = &packet->payload.gameState;
            usize payloadSize = PacketGameStateSize(gs->playerCount, gs->foodItemCount);
            gameState = arena_alloc(&frameArena, payloadSize);
            // copy can be done as long as GameStateClient and PacketGameState have the same memory layout
            MEMORY_COPY(gameState, gs, payloadSize);
            for (usize i = 0; i < gameState->playerCount; ++i) {
                const Player *p = &gameState->players[i];
                if (player.id == p->id) {
                    // player = *p;
                    player.radius = p->radius;
                    ghost = *p;
                    ghost.color = CLITERAL(Rgba){.r = 69, .g = 69, .b = 69, .a = 123};
                }
            }
        } break;

        case PACKET_SERVER_PLAYER_DEAD: {
            // TODO: game over state
            exit(0);
        } break;
                                       
        case PACKET_SERVER_PLAYER_SPAWN: {
            const PacketPlayerSpawn *ps = &packet->payload.playerSpawn;
            player = ps->player;
            map = ps->map;
        } break;
        case PACKET_SERVER_EMPTY:
        case PACKET_SERVER_COUNT:
            break;
    }

    return true;
}

static void NetworkThreadFn(void *arg) {
    UNUSED(arg);

    while (true) {
        u8 buf[NET_PACKET_MAX_SIZE] = {0};
        i32 rxBytes = os_recvfrom(sock, buf, sizeof(buf));
        if (rxBytes == -1) {
            LOG_WRN("recvfrom: errno %d", errno);
            exit(EXIT_FAILURE);
        }
        PacketsServer* packets = (PacketsServer*)buf;
        LOG_DBG("rx: %d bytes, %d packets", rxBytes, packets->count);

        u8* offset = (u8*)packets->packets;
        PacketServer* pkt = (PacketServer*)offset;
        // TODO: not secure
        while (offset + PACKET_HEADER_SIZE + pkt->header.payloadSize <= ((u8*)packets + rxBytes)) {
            PacketServerHandle(pkt);
            offset += PACKET_HEADER_SIZE + pkt->header.payloadSize;
            pkt = (PacketServer*)offset;
        }
    }
    LOG_ERR("NetworkThread finished");
    exit(EXIT_FAILURE);
}

static void ConnectToServer(const char *hostname, const char *port) {
    sock = os_udp_socket(hostname, port);

    PacketClient connect = {
        .type = PACKET_CLIENT_PLAYER_CONNECT,
    };
    PacketSend(&connect, sizeof(connect));

    u8 buf[NET_PACKET_MAX_SIZE] = {0};
    i32 rxBytes = os_recvfrom(sock, buf, sizeof(buf));
    if (rxBytes == -1) {
        LOG_WRN("recvfrom: errno %d", errno);
        return;
    }
    PacketsServer *packets = (PacketsServer *)buf;
    LOG_DBG("rx: %d bytes, %d packets", rxBytes, packets->count);
    if (packets->count < 1) {
        LOG_ERR("malformed packet: invalid count");
        exit(EXIT_FAILURE);
    }
    u8* offset = (u8*)packets->packets;
    PacketServer* pkt = (PacketServer*)offset;
    // TODO: not secure
    while (offset + PACKET_HEADER_SIZE + pkt->header.payloadSize <= ((u8*)packets + rxBytes)) {
        PacketServerHandle(pkt);
        offset += PACKET_HEADER_SIZE + pkt->header.payloadSize;
        pkt = (PacketServer*)offset;
    }

    os_create_thread(NetworkThreadFn, NULL);
}

#define CAST_PTR_TO_TYPE(ptr, type) ((*((type*)(&(ptr)))))
#define RGBA_TO_COLOR(rgba) CAST_PTR_TO_TYPE(rgba, Color)

// TODO: dry?
static void PlayerDraw(const Player *p) {
    DrawCircle((i32)p->position.x, (i32)p->position.y, p->radius, RGBA_TO_COLOR(p->color));
}

static void FoodItemDraw(const FoodItem *f) {
    DrawCircle((i32)f->position.x, (i32)f->position.y, f->radius, FOOD_ITEM_COLOR);
}

static bool PlayerCanShrink(const Player *p) {
    return p->radius >= PLAYER_START_RADIUS * PLAYER_SHRINK_COEFFICIENT;
}

i32 main(i32 argc, const char *argv[]) {
    if (argc != 3) {
        LOG_ERR("missing host and port");
        exit(EXIT_FAILURE);
    }
#ifdef DEBUG
    log_init(LOG_DBG);
    SetTraceLogLevel(LOG_DEBUG);
#else
    log_init(LOG_INF);
    SetTraceLogLevel(LOG_NONE);
#endif
    frameArena = arena_create(MB(4));

    const char *hostname = argv[1];
    const char *port = argv[2];
    ConnectToServer(hostname, port);

    // TODO: define constants
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(0, 0, "igario");
    HideCursor();
    SetTargetFPS(60);

    // TODO: zoom out when player grows, but clamp zoom to world bounds
    Camera2D camera = {.zoom = 1.f};

    MoveDirection horizontalDir = MOVE_NONE;
    MoveDirection verticalDir = MOVE_NONE;

    while (!WindowShouldClose()) {
        f32 dt = GetFrameTime();

        /* TODO:
                    ->       = ->
                    <-       = <-
                    -> ->    = ->
                    <- <-    = <-
                    -> <-    = <-
                    ->       = ->
                    <- ->    = ->
                    <-       = <-
                    -> -> <- = <-
                    -> ->    = ->
                    <- <- -> = ->
                    <- <-    = <-
       */
        for (usize i = 0; i < KEY_MAPS_MAX; ++i) {
            if (IsKeyDown(keyMaps.keysRight[i])) {
                horizontalDir = MOVE_RIGHT;
                break;
            } else if (IsKeyDown(keyMaps.keysLeft[i])) {
                horizontalDir = MOVE_LEFT;
                break;
            }
            horizontalDir = MOVE_NONE;
        }
        for (usize i = 0; i < KEY_MAPS_MAX; ++i) {
            if (IsKeyDown(keyMaps.keysDown[i])) {
                verticalDir = MOVE_DOWN;
                break;
            } else if (IsKeyDown(keyMaps.keysUp[i])) {
                verticalDir = MOVE_UP;
                break;
            }
            verticalDir = MOVE_NONE;
        }

#ifdef DEBUG
        if (IsKeyDown(KEY_MINUS)) {
            camera.zoom -= 0.5f * dt;
        } else if (IsKeyDown(KEY_EQUAL)) {
            camera.zoom += 0.5f * dt;
        }
        camera.zoom = Clamp(camera.zoom, 0.1f, 10.0f);
#endif

        Vector2 velocity = {0};
        if (horizontalDir == MOVE_RIGHT) {
            velocity.x = 1;
        } else if (horizontalDir == MOVE_LEFT) {
            velocity.x = -1;
        } else {
            velocity.x = 0;
        }
        if (verticalDir == MOVE_DOWN) {
            velocity.y = 1;
        } else if (verticalDir == MOVE_UP) {
            velocity.y = -1;
        } else {
            velocity.y = 0;
        }

        if (velocity.x != 0 && velocity.y != 0) {
            // without normalization diagonal movement is too fast
            velocity = Vector2Normalize(velocity);
        }

        if (velocity.x != 0 || velocity.y != 0) {
            for (usize i = 0; i < KEY_MAPS_MAX; ++i) {
                if (IsKeyPressed(keyMaps.keysShift[i]) && PlayerCanShrink(&player)) {
                    PacketClient pkt = {
                        .type = PACKET_CLIENT_PLAYER_SHRINK,
                    };
                    PacketSend(&pkt, sizeof(pkt));
                    break;
                }
            }
        }

        // TODO: remove hardcoded value
        // TODO: add walk/crouch option to slow down?
        // TODO: add inertia?
        f32 movementDistance = (PLAYER_START_SPEED / (player.radius / 50)) * dt;
        player.position.x += velocity.x * movementDistance;
        player.position.y += velocity.y * movementDistance;

        // warp around map
        if (player.position.x > MapGetRightBound(&map) + player.radius) {
            player.position.x = MapGetLeftBound(&map) - player.radius;
        } else if (player.position.x < MapGetLeftBound(&map) - player.radius) {
            player.position.x = MapGetRightBound(&map) + player.radius;
        }
        if (player.position.y > MapGetLowerBound(&map) + player.radius) {
            player.position.y = MapGetUpperBound(&map) - player.radius;
        } else if (player.position.y < MapGetUpperBound(&map) - player.radius) {
            player.position.y = MapGetLowerBound(&map) + player.radius;
        }

        i32 screenW = GetScreenWidth();
        i32 screenH = GetScreenHeight();
        camera.offset = CLITERAL(Vector2){
            .x = screenW / 2.f,
            .y = screenH / 2.f,
        };

        // clamp camera to map edges
        f32 halfW = screenW / (2 * camera.zoom);
        f32 halfH = screenH / (2 * camera.zoom);
        camera.target.x =
            Clamp(player.position.x, MapGetLeftBound(&map) + halfW,
                  MapGetRightBound(&map) - halfW);
        camera.target.y =
            Clamp(player.position.y, MapGetUpperBound(&map) + halfH,
                  MapGetLowerBound(&map) - halfH);

        PacketClient packetMove = {
            .type = PACKET_CLIENT_PLAYER_MOVE,
            .data.playerMove.newPosition = player.position,
        };
        PacketSend(&packetMove, sizeof(packetMove));

        BeginDrawing();
        ClearBackground(MAP_BG_COLOR);

        BeginMode2D(camera);
        DrawBG(MAP_CELL_COUNT, CELL_SIZE);

        const FoodItem *firstFoodItem =
            (FoodItem *)(gameState->players + gameState->playerCount);
        for (usize i = 0; i < gameState->foodItemCount; ++i) {
            FoodItemDraw(firstFoodItem + i);
        }

#ifdef DEBUG
        PlayerDraw(&ghost);
#endif
        if (PlayerCanShrink(&player)) {
            Rgba borderColor = {
                .r = player.color.r + 50,
                .g = player.color.g + 50,
                .b = player.color.b + 50,
                .a = player.color.a,
            };
            DrawCircle((i32)player.position.x, (i32)player.position.y, player.radius, RGBA_TO_COLOR(borderColor));
            DrawCircle((i32)player.position.x, (i32)player.position.y, player.radius - 2, RGBA_TO_COLOR(player.color));
        } else {
            PlayerDraw(&player);
        }
        for (usize i = 0; i < gameState->playerCount; ++i) {
            const Player *p = &gameState->players[i];
            if (player.id == p->id) {
                continue;
            }
            PlayerDraw(p);
        }

        EndMode2D();

#ifdef DEBUG
        const char *text = TextFormat("x: %.2f\ny: %.2f\nid: %d\nplayers: %d",
                                      player.position.x, player.position.y,
                                      player.id, gameState->playerCount);
        DrawText(text, 10, 10, 24, BLACK);
        DrawFPS(screenW - 90, 10);
#endif

        arena_reset(&frameArena);
        EndDrawing();
    }

    CloseWindow();
}
