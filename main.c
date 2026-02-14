#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stddef.h>

#define ARENA_IMPLEMENTATION
#include "base/arena.h"
#include "base/util.h"

#define CELL_SIZE 40.f
#define MAP_BG_COLOR RAYWHITE
#define MAP_CELL_COUNT 50
#define PLAYER_START_SPEED 180
#define PLAYER_START_RADIUS (CELL_SIZE / 2.f)
// TODO: rename
#define PLAYER_DASH_COEFFICIENT 1.5f
#define FOOD_ITEMS_MAX 50
#define FOOD_ITEM_MIN_RADIUS (CELL_SIZE / 5.f)
#define FOOD_ITEM_MAX_RADIUS (PLAYER_START_RADIUS)
#define FOOD_ITEM_COLOR ORANGE
// TODO: explain why this is needed (tldr: less anoying gameplay)
#define FOOD_ITEM_PROXIMITY_THRESHOLD 200
#define FOOD_SPAWN_INTERVAL_S 1.0f

typedef struct {
    Color color;
    Vector2 position;
    f32 radius;
} Player;

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

typedef struct _FoodItem {
    struct _FoodItem *next;
    struct _FoodItem *prev;
    Vector2 position;
    f32 radius;
} FoodItem;

// TODO: maybe this should be a part of GameState?
struct Map {
    f32 width;
    f32 height;
} map = {
    .width = MAP_CELL_COUNT * CELL_SIZE,
    .height = MAP_CELL_COUNT * CELL_SIZE,
};

static inline f32 MapGetRightBound(const struct Map *map) {
    return map->width / 2.f;
}

static inline f32 MapGetLeftBound(const struct Map *map) {
    return -map->width / 2.f;
}

static inline f32 MapGetLowerBound(const struct Map *map) {
    return map->height / 2.f;
}

static inline f32 MapGetUpperBound(const struct Map *map) {
    return -map->height / 2.f;
}

struct GameState {
    f32 timer;
    Arena foodItemsArena;
    struct {
        FoodItem *first;
        FoodItem *last;
        usize count;
    } foodItems;
    FoodItem *firstFreeFoodItem;
} gameState = {0};

FoodItem *FoodItemSpawn(struct GameState *gameState, const struct Map *map) {
    FoodItem *f = NULL;
    if (gameState->firstFreeFoodItem != NULL) {
        f = gameState->firstFreeFoodItem;
        SLL_STACK_POP(gameState->firstFreeFoodItem);
    } else {
        f = arena_alloc(&gameState->foodItemsArena, sizeof(FoodItem));
    }
    MEMORY_ZERO_STRUCT(f);
    f32 radius = GetRandomValue(FOOD_ITEM_MIN_RADIUS, FOOD_ITEM_MAX_RADIUS);
    f->position =
        CLITERAL(Vector2){.x = GetRandomValue(MapGetLeftBound(map) + radius,
                                              MapGetRightBound(map) - radius),
                          .y = GetRandomValue(MapGetUpperBound(map) + radius,
                                              MapGetLowerBound(map) - radius)};
    f->radius = radius;
    return f;
}

bool PlayerCanConsumeFood(const Player *player, const FoodItem *food) {
    if (food->radius > player->radius) {
        return false;
    }
    f32 dx = player->position.x - food->position.x;
    f32 dy = player->position.y - food->position.y;

    f32 radiusDelta = player->radius - food->radius;

    return dx * dx + dy * dy <=
           radiusDelta * radiusDelta + FOOD_ITEM_PROXIMITY_THRESHOLD;
}

// TODO: choose from predefined set of colors to avoid bad player visibility
Color GetRandomColor(void) {
    return CLITERAL(Color){
        .r = GetRandomValue(0, 255),
        .g = GetRandomValue(0, 255),
        .b = GetRandomValue(0, 255),
        .a = 255,
    };
}

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

i32 main(void) {
    // TODO: define constants
    InitWindow(800, 600, "igario");
    SetTargetFPS(60);

    gameState.foodItemsArena = arena_create(sizeof(FoodItem) * FOOD_ITEMS_MAX);

    // TODO: zoom out when player grows, but clamp zoom to world bounds
    Camera2D camera = {.zoom = 1.f};

    Player player = CLITERAL(Player){
        .color = GetRandomColor(),
        .position = CLITERAL(
            Vector2){.x = GetRandomValue(
                         MapGetLeftBound(&map) + PLAYER_START_RADIUS,
                         MapGetRightBound(&map) - PLAYER_START_RADIUS),
                     .y = GetRandomValue(
                         MapGetUpperBound(&map) + PLAYER_START_RADIUS,
                         MapGetLowerBound(&map) - PLAYER_START_RADIUS)},
        .radius = PLAYER_START_RADIUS,
    };

    MoveDirection horizontalDir = MOVE_NONE;
    MoveDirection verticalDir = MOVE_NONE;

    while (!WindowShouldClose()) {
        f32 dt = GetFrameTime();
        gameState.timer += dt;

        if (gameState.timer >= FOOD_SPAWN_INTERVAL_S &&
            gameState.foodItems.count < FOOD_ITEMS_MAX) {
            FoodItem *f = FoodItemSpawn(&gameState, &map);
            DLL_PUSH_BACK(gameState.foodItems.first, gameState.foodItems.last,
                          f);
            gameState.timer = 0;
            gameState.foodItems.count += 1;
        }

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

        for (usize i = 0; i < KEY_MAPS_MAX; ++i) {
            if (IsKeyPressed(keyMaps.keysShift[i]) &&
                player.radius >=
                    PLAYER_START_RADIUS * PLAYER_DASH_COEFFICIENT) {
                player.radius /= PLAYER_DASH_COEFFICIENT;
                break;
            }
        }

        // TODO: this is for testing only and should be removed
        if (IsKeyDown(KEY_MINUS)) {
            camera.zoom -= 0.5f * dt;
        } else if (IsKeyDown(KEY_EQUAL)) {
            camera.zoom += 0.5f * dt;
        }
        camera.zoom = Clamp(camera.zoom, 0.1, 10);

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

        // TODO: remove hardcoded value
        // TODO: add walk/crouch option to slow down?
        // TODO: add inertia?
        f32 movement_distance =
            (PLAYER_START_SPEED / (player.radius / 50)) * dt;
        player.position.x += velocity.x * movement_distance;
        player.position.y += velocity.y * movement_distance;

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

        for (FoodItem *f = gameState.foodItems.first; f != NULL; f = f->next) {
            if (PlayerCanConsumeFood(&player, f)) {
                DLL_REMOVE(gameState.foodItems.first, gameState.foodItems.last,
                           f);
                SLL_STACK_PUSH(gameState.firstFreeFoodItem, f);
                player.radius += f->radius / 10;
                gameState.foodItems.count -= 1;
                break;
            }
        }

        f32 screenW = GetScreenWidth();
        f32 screenH = GetScreenHeight();
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

        BeginDrawing();
        ClearBackground(MAP_BG_COLOR);

        BeginMode2D(camera);
        DrawBG(MAP_CELL_COUNT, CELL_SIZE);

        for (FoodItem *f = gameState.foodItems.first; f != NULL; f = f->next) {
            DrawCircleV(f->position, f->radius, FOOD_ITEM_COLOR);
        }

        DrawCircleV(player.position, player.radius, player.color);
        EndMode2D();

        // DEBUG INFO
        const char *text = TextFormat("x: %.2f\ny: %.2f", player.position.x,
                                      player.position.y);
        DrawText(text, 10, 10, 24, BLACK);

        EndDrawing();
    }

    CloseWindow();
}
