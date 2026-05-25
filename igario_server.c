#include "game.h"

#include <rprand.h>

#include "base/os.h"
#include "base/log.h"
#include "base/arena.h"
#include "base/base.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define FOOD_ITEMS_START_COUNT 2
#define CLIENTS_MAX 100

typedef u32 Slot;

typedef struct {
    Slot index;
    u32 generation;
} Ref;

static const Ref nilRef = {0};

typedef struct {
    struct sockaddr_in addr;
    Player player;
    u64 lastPacketTimeUsec;
} Client;

typedef struct {
    Client clients[CLIENTS_MAX];
    bool connected[CLIENTS_MAX];
    Slot gen[CLIENTS_MAX];
    Slot firstDisconnected;
    Slot nextDisconnected[CLIENTS_MAX];
    usize count;
} Clients;

static Clients clients = {0};

#define CLIENTS_REF(i) ((Ref){(i), clients.gen[(i)]})
#define CLIENTS_DEREF(ref) \
    (((ref).index > 0 \
   && (ref).index < CLIENTS_MAX \
   && clients.connected[(ref).index] \
   && clients.gen[(ref).index] == (ref).generation) \
     ? ((ref).index) : 0)

static Slot ClientsFirstDisconnected(void) {
    if (clients.firstDisconnected) {
        return clients.firstDisconnected;
    } else if (clients.count + 1 < CLIENTS_MAX) {
        return clients.count + 1;
    }
    return 0;
}

static Ref ClientsAdd(Client* newClient) {
    Slot slot = ClientsFirstDisconnected();
    if (!slot) {
        return nilRef;
    }

    clients.clients[slot] = *newClient;
    clients.connected[slot] = true;
    clients.gen[slot] += 1;
    clients.count += 1;
    return CLIENTS_REF(slot);
}

static Client* ClientsGet(Ref ref) {
    return &clients.clients[CLIENTS_DEREF(ref)];
}

static void ClientsRemove(Ref ref) {
    Slot slot = CLIENTS_DEREF(ref);
    if (!slot) {
        return;
    }

    clients.connected[slot] = false;
    clients.count -= 1;
    if (clients.firstDisconnected) {
        clients.nextDisconnected[slot] = clients.firstDisconnected;
    }
    clients.firstDisconnected = slot;
}

static Client *ClientsFindByAddr(const struct sockaddr_in *addr) {
    for (usize i = 1; i < CLIENTS_MAX; ++i) {
        if (!clients.connected[i]) {
            continue;
        }
        Client *c = ClientsGet(CLIENTS_REF(i));
        if (c->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            c->addr.sin_port == addr->sin_port) {
            return c;
        }
    }
    // null is cringe
    return NULL;
}

static Map map = {
    .width = MAP_CELL_COUNT * CELL_SIZE,
    .height = MAP_CELL_COUNT * CELL_SIZE,
};

typedef struct _FoodItemLL {
    struct _FoodItemLL *next;
    struct _FoodItemLL *prev;
    Vec2 position;
    f32 radius;
} FoodItemLL;

// TODO: move to base
typedef struct {
    u64 startTimeMsec;
} Timer;

static void TimerStart(Timer* timer) {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    timer->startTimeMsec = SEC_TO_MSEC(ts.tv_sec) + NSEC_TO_MSEC(ts.tv_nsec);
}

static u64 TimerGetElapsedMsec(const Timer* timer) {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    u64 msec = SEC_TO_MSEC(ts.tv_sec) + NSEC_TO_MSEC(ts.tv_nsec) - timer->startTimeMsec;
    return msec;
}

typedef struct {
    Timer foodSpawnTimer;
    Arena foodItemsArena;
    struct {
        FoodItemLL *first;
        FoodItemLL *last;
        usize count;
    } foodItemsLL;
    FoodItemLL *firstFreeFoodItem;
} GameState;

static GameState gameState = {0};

typedef struct {
    bool shutdown;
    i32 sock;
    f64 tickDurationSec;
    u64 tickBeginUsec;
    u64 tickEndUsec;
    Timer timer;
} ServerState;

static ServerState serverState = {.sock = -1 };

// TODO: random seed
static i32 GetRandomValue(i32 min, i32 max) {
    if (min > max) {
        i32 tmp = max;
        max = min;
        min = tmp;
    }
    return rprand_get_value(min, max);
}

// TODO: choose from predefined set of colors to avoid bad player visibility
static Rgba GetRandomColor(void) {
    return (Rgba){
        .r = GetRandomValue(0, 255),
        .g = GetRandomValue(0, 255),
        .b = GetRandomValue(0, 255),
        .a = 255,
    };
}

static void GameStatePack(PacketGameState *gs) {
    gs->playerCount = clients.count;
    usize players = 0;
    for (usize i = 1; i < CLIENTS_MAX; ++i) {
        if (!clients.connected[i]) {
            continue;
        }
        gs->players[players] = ClientsGet(CLIENTS_REF(i))->player;
        players += 1;
    }
    usize foodItemCount = 0;
    gs->foodItemCount = gameState.foodItemsLL.count;
    FoodItem *firstFoodItem = (FoodItem *)(gs->players + gs->playerCount);
    for (FoodItemLL *fll = gameState.foodItemsLL.first; fll != NULL;
         fll = fll->next) {
        FoodItem f = {
            .position = fll->position,
            .radius = fll->radius,
        };
        MEMORY_COPY(firstFoodItem + foodItemCount, &f, sizeof(f));
        foodItemCount += 1;
    }
}

static Player PlayerSpawn(void) {
    return (Player){
        .id = ClientsFirstDisconnected(),
        .color = GetRandomColor(),
        .position = (Vec2){.x = GetRandomValue(
                         MapGetLeftBound(&map) + PLAYER_START_RADIUS,
                         MapGetRightBound(&map) - PLAYER_START_RADIUS),
                     .y = GetRandomValue(
                         MapGetUpperBound(&map) + PLAYER_START_RADIUS,
                         MapGetLowerBound(&map) - PLAYER_START_RADIUS)},
        .radius = PLAYER_START_RADIUS,
    };
}

static void PacketsSendToClient(const PacketsServer *packets, usize packetsSize, const Client* client) {
    if (packetsSize > NET_PACKET_MAX_SIZE) {
        LOG_ERR("packet is too big: %zu", packetsSize);
        return;
    }
    i32 txBytes = sendto(serverState.sock, packets, packetsSize, 0,
                         (struct sockaddr *)&client->addr, sizeof(client->addr));
    if (txBytes == -1) {
        LOG_WRN("sendto: client %s:%d, errno %d",
                inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), errno);
        return;
    }
    LOG_DBG("tx: %d bytes to %s:%d", txBytes, inet_ntoa(client->addr.sin_addr),
            ntohs(client->addr.sin_port));
}

static void PacketsBroadcast(const PacketsServer *packets, usize packetsSize) {
    for (usize i = 1; i < CLIENTS_MAX; ++i) {
        if (!clients.connected[i]) {
            continue;
        }
        PacketsSendToClient(packets, packetsSize, ClientsGet(CLIENTS_REF(i)));
    }
}

static void *NetworkThreadFn(void *arg) {
    UNUSED(arg);

    u8 buf[NET_PACKET_MAX_SIZE] = {0};
    struct sockaddr_in clientAddr = {0};
    socklen_t clientAddrLen = sizeof(clientAddr);
    // TODO: define constant for arena size
    Arena packetArena = arena_create(MB(4));
    while (true) {
        i32 rxBytes = recvfrom(serverState.sock, buf, sizeof(buf), 0,
                               (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (rxBytes == -1) {
            LOG_ERR("recvfrom: %d", errno);
            close(serverState.sock);
            exit(EXIT_FAILURE);
        }

        LOG_DBG("rx: %d bytes from %s:%d", rxBytes,
                inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        PacketClient *packet = (PacketClient *)buf;
        LOG_DBG("packet.type: %d", packet->type);
        Client *client = ClientsFindByAddr(&clientAddr);
        Player *player = NULL;

        u64 ts = os_now_usec();
        if (client) {
            client->lastPacketTimeUsec = ts;
            player = &client->player;
        }
        switch (packet->type) {
        case PACKET_CLIENT_PLAYER_CONNECT: {
            Client newClient = {
                .player = PlayerSpawn(),
                .addr = clientAddr,
                .lastPacketTimeUsec = ts,
            };
            ClientsAdd(&newClient);
            LOG_INF("player connected");

            usize playerSpawnPacketSize = sizeof(PacketPlayerSpawn);
            usize gameStatePacketSize = PacketGameStateSize(clients.count, gameState.foodItemsLL.count);
            usize packetsSize = PACKETS_HEADER_SIZE
                                + PACKET_HEADER_SIZE + playerSpawnPacketSize
                                + PACKET_HEADER_SIZE + gameStatePacketSize;
            PacketsServer *packets = arena_alloc(&packetArena, packetsSize);
            packets->count = 2;
            PacketServer* playerSpawnPacket = &packets->packets[0];

            playerSpawnPacket->header = (PacketHeader){
                .type = PACKET_SERVER_PLAYER_SPAWN,
                .payloadSize = playerSpawnPacketSize,
            };
            playerSpawnPacket->payload.playerSpawn = (PacketPlayerSpawn){
                .player = newClient.player,
                .map = map,
            };
            PacketServer *gameStatePacket = (PacketServer*)(((u8*)packets) + PACKETS_HEADER_SIZE
                                                            + PACKET_HEADER_SIZE + playerSpawnPacketSize);
            gameStatePacket->header = (PacketHeader){
                .type = PACKET_SERVER_GAME_STATE,
                .payloadSize = gameStatePacketSize,
            };
            GameStatePack(&gameStatePacket->payload.gameState);

            PacketsSendToClient(packets, packetsSize, &newClient);
        } break;

        case PACKET_CLIENT_PLAYER_MOVE: {
            if (!player) {
                break;
            }
            player->position = packet->data.playerMove.newPosition;
            LOG_DBG("player moved[%zu]: %.2f, %.2f", player->id,
                    player->position.x, player->position.y);
        } break;

        case PACKET_CLIENT_PLAYER_SHRINK: {
            if (!player) {
                break;
            }
            player->radius /= PLAYER_SHRINK_COEFFICIENT;
        } break;

        case PACKET_CLIENT_EMPTY:
        case PACKET_CLIENT_COUNT:
            LOG_ERR("invalid packet type");
            break;
        }
        arena_reset(&packetArena);
    }
    LOG_ERR("NetworkThread finished");
    exit(EXIT_FAILURE);
}

static FoodItemLL *FoodItemSpawn(GameState *gameState, const Map *map) {
    FoodItemLL *f = NULL;
    if (gameState->firstFreeFoodItem != NULL) {
        f = gameState->firstFreeFoodItem;
        SLL_STACK_POP(gameState->firstFreeFoodItem);
    } else {
        f = arena_alloc(&gameState->foodItemsArena, sizeof(*f));
    }
    MEMORY_ZERO_STRUCT(f);
    f32 radius = GetRandomValue(FOOD_ITEM_MIN_RADIUS, FOOD_ITEM_MAX_RADIUS);
    f->position = (Vec2){
        .x = GetRandomValue(MapGetLeftBound(map) + radius,
                            MapGetRightBound(map) - radius),
        .y = GetRandomValue(MapGetUpperBound(map) + radius,
                            MapGetLowerBound(map) - radius)
    };
    f->radius = radius;

    DLL_PUSH_BACK(gameState->foodItemsLL.first, gameState->foodItemsLL.last, f);
    gameState->foodItemsLL.count += 1;

    return f;
}

// TODO: dry?
static bool PlayerCanConsumePlayer(const Player *p1, const Player *p2) {
    if (p2->radius > p1->radius) {
        return false;
    }
    f32 dx = p1->position.x - p2->position.x;
    f32 dy = p1->position.y - p2->position.y;

    f32 radiusDelta = p1->radius - p2->radius;

    return dx * dx + dy * dy <=
           radiusDelta * radiusDelta + PLAYER_PROXIMITY_THRESHOLD;
}

static bool PlayerCanConsumeFood(const Player *player, const FoodItemLL *food) {
    if (food->radius > player->radius) {
        return false;
    }
    f32 dx = player->position.x - food->position.x;
    f32 dy = player->position.y - food->position.y;

    f32 radiusDelta = player->radius - food->radius;

    return dx * dx + dy * dy <=
           radiusDelta * radiusDelta + FOOD_ITEM_PROXIMITY_THRESHOLD;
}

static void ServerSetTickrate(u16 hz) {
    serverState.tickDurationSec = 1.0/(double)hz;
    LOG_INF("server tickrate: %dhz, tick duration: %.2fms",
            hz, SEC_TO_MSEC(serverState.tickDurationSec));
}

static void ServerTickBegin(void) {
    serverState.tickBeginUsec = os_now_usec();
}

static void ServerTickEnd(void) {
    serverState.tickEndUsec = os_now_usec();

    u64 deltaUsec = serverState.tickEndUsec - serverState.tickBeginUsec;
    if (deltaUsec >= SEC_TO_USEC(serverState.tickDurationSec)){
        return;
    }

    u64 sleepUsec = SEC_TO_USEC(serverState.tickDurationSec) - deltaUsec;
    os_sleep_usec(sleepUsec);
}

void SigintHandler(i32 signo) {
    UNUSED(signo);

    serverState.shutdown = true;
}

i32 main(i32 argc, const char *argv[]) {
    if (argc != 2) {
        LOG_ERR("port not specified");
        return EXIT_FAILURE;
    }
    const char *port = argv[1];

#ifdef DEBUG
    log_init(LOG_DBG);
#else
    log_init(LOG_INF);
#endif
    ServerSetTickrate(60);
    TimerStart(&serverState.timer);

    signal(SIGINT, SigintHandler);

    // TODO: define constant for arena size
    Arena tickArena = arena_create(MB(4));
    gameState = (GameState){0};
    gameState.foodItemsArena =
        arena_create(sizeof(FoodItemLL) * FOOD_ITEMS_MAX);
    for (usize i = 0; i < FOOD_ITEMS_START_COUNT; ++i) {
        FoodItemSpawn(&gameState, &map);
    }

    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_flags = AI_PASSIVE,
    };
    struct addrinfo *result = NULL;
    i32 err = getaddrinfo(NULL, port, &hints, &result);
    if (err != 0) {
        LOG_ERR("getaddrinfo: %s", gai_strerror(err));
        exit(EXIT_FAILURE);
    }
    struct addrinfo *r = NULL;
    for (r = result; r != NULL; r = r->ai_next) {
        serverState.sock = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (serverState.sock == -1) {
            continue;
        }

        if (bind(serverState.sock, r->ai_addr, r->ai_addrlen) == 0) {
            break;
        }

        close(serverState.sock);
    }

    if (r == NULL) {
        LOG_ERR("failed to bind socket on port %s", port);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(result);

    LOG_INF("listening on port %s", port);
    pthread_t networkThread = 0;
    err = pthread_create(&networkThread, NULL, NetworkThreadFn, (void *)&gameState);
    if (err != 0) {
        LOG_ERR("pthread_create: %d", err);
        return EXIT_FAILURE;
    }

    TimerStart(&gameState.foodSpawnTimer);
    while (!serverState.shutdown) {
        ServerTickBegin();

        if (clients.count < 1) {
            LOG_DBG("waiting for clients to connect");
            goto sleep;
        }

        for (usize i = 1; i < CLIENTS_MAX; ++i) {
            Client* c = ClientsGet(CLIENTS_REF(i));
            if (!clients.connected[i]) {
                continue;
            }

            if (serverState.tickBeginUsec - c->lastPacketTimeUsec >= PLAYER_RECONNECT_TIME_USEC) {
                LOG_INF("player %zu disconnected", c->player.id);
                ClientsRemove(CLIENTS_REF(i));
                if (clients.count == 0) {
                    LOG_INF("last client disconnected, shutting down server");
                    return 0;
                }
                break;
            }

            Player *p = &c->player;

            for (FoodItemLL *f = gameState.foodItemsLL.first; f != NULL; f = f->next) {
                if (PlayerCanConsumeFood(p, f)) {
                    DLL_REMOVE(gameState.foodItemsLL.first,
                               gameState.foodItemsLL.last, f);
                    SLL_STACK_PUSH(gameState.firstFreeFoodItem, f);
                    p->radius += f->radius / 10;
                    gameState.foodItemsLL.count -= 1;
                    break;
                }
            }

            for (usize j = 1; j < CLIENTS_MAX; ++j) {
                if (!clients.connected[j] || j == i) {
                    continue;
                }
                Player *p2 = &ClientsGet(CLIENTS_REF(j))->player;
                if (PlayerCanConsumePlayer(p, p2)) {
                    PacketsServer p = {
                        .count = 1,
                        .packets[0].header = {
                            .type = PACKET_SERVER_PLAYER_DEAD,
                            .payloadSize = 0,
                        },
                    };
                    PacketsSendToClient(&p, sizeof(p), ClientsGet(CLIENTS_REF(j)));
                    ClientsRemove(CLIENTS_REF(j));
                    break;
                }
            }
        }

        if (TimerGetElapsedMsec(&gameState.foodSpawnTimer) >= FOOD_SPAWN_INTERVAL_MSEC &&
            gameState.foodItemsLL.count < FOOD_ITEMS_MAX) {
            FoodItemLL *f = FoodItemSpawn(&gameState, &map);
            TimerStart(&gameState.foodSpawnTimer);
            LOG_DBG("food item spawned at %.2f,%.2f", f->position.x, f->position.y);
        }

        usize gameStatePacketSize = PacketGameStateSize(clients.count, gameState.foodItemsLL.count);
        usize packetsSize = MEMBER_OFFSET(PacketsServer, packets)
                            + PACKET_HEADER_SIZE + gameStatePacketSize;
        PacketsServer* packets = arena_alloc(&tickArena, packetsSize);
        packets->count = 1;
        packets->packets[0].header = (PacketHeader){
            .type = PACKET_SERVER_GAME_STATE,
            .payloadSize = gameStatePacketSize,
        };
        GameStatePack(&packets->packets[0].payload.gameState);
        PacketsBroadcast(packets, packetsSize);

    sleep:
        arena_reset(&tickArena);
        ServerTickEnd();
    }

    LOG_INF("server shutting down");
    close(serverState.sock);
}
