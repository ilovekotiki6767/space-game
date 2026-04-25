#ifndef GAMING_GAME_PLATFORM_H
#define GAMING_GAME_PLATFORM_H

#include "Game_math.h"

#define Bool int
#define True 1
#define False 0

#define ArrayCount(array) (sizeof(array)/sizeof(array[0]))

typedef struct {
    Mat4X4 transform;
} Game_RenderEntry;

typedef struct {
    int half_transition_count;
    Bool ended_down;
} Game_ButtonState;

typedef enum {
    GAME_KEY_NONE,

    // don't forget to map the keys:
    // * SDLKeyToGameKey in SDL_main.c
    GAME_KEY_W, GAME_KEY_A, GAME_KEY_S, GAME_KEY_D,
    GAME_KEY_SPACE, GAME_KEY_LEFT_CTRL,

    GAME_KEY_COUNT,
} Game_Key;

#define IsDown(button) ((button).ended_down)
#define WasPressed(button) (((button).half_transition_count > 1) || ((button).half_transition_count == 1 && (button).ended_down))

typedef struct {
    void *permanent_storage;
    unsigned long long permanent_storage_size;

    Game_ButtonState input[GAME_KEY_COUNT];

    Game_RenderEntry render_entries[1024];
    int render_entry_count;
} Game_Platform;

static void Game_PushRenderEntry(Game_Platform *platform, const Mat4X4 transform) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        platform->render_entries[platform->render_entry_count++].transform = transform;
    }
}

typedef void (*Game_UpdateAndRender_Func)(Game_Platform *platform, float delta_time);

#endif //GAMING_GAME_PLATFORM_H
