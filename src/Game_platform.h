#ifndef GAMING_GAME_PLATFORM_H
#define GAMING_GAME_PLATFORM_H

#include "Game_math.h"

// Macros

#define Bool int
#define True 1
#define False 0

#define ArrayCount(array) (sizeof(array)/sizeof(array[0]))

#define IsDown(button) ((button).ended_down)
#define WasPressed(button) (((button).half_transition_count > 1) || ((button).half_transition_count == 1 && (button).ended_down))

// Handles

typedef unsigned int Game_TextureHandle;

// Structures

typedef struct {
    Mat4X4 transform;
    Game_TextureHandle texture_handle;
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

typedef struct {
    void *permanent_storage;
    unsigned long long permanent_storage_size;

    Game_ButtonState input[GAME_KEY_COUNT];

    Game_RenderEntry render_entries[1024];
    int render_entry_count;

    // Platform API
    Game_TextureHandle (*LoadImageTexture)(const char *path);
} Game_Platform;

// Functions

static void Game_PushRenderEntry(Game_Platform *platform, const Mat4X4 transform, const Game_TextureHandle texture_handle) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        platform->render_entries[platform->render_entry_count].transform = transform;
        platform->render_entries[platform->render_entry_count].texture_handle = texture_handle;
        platform->render_entry_count++;
    }
}

typedef void (*Game_UpdateAndRender_Func)(Game_Platform *platform, float delta_time);

#endif //GAMING_GAME_PLATFORM_H
