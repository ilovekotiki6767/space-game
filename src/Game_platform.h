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
    void *permanent_storage;
    unsigned long long permanent_storage_size;

    Game_RenderEntry render_entries[1024];
    int render_entry_count;
} Game_Platform;

static void Game_PushRenderEntry(Game_Platform *platform, const Mat4X4 transform) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        platform->render_entries[platform->render_entry_count++].transform = transform;
    }
}

typedef void (*Game_UpdateAndRender_Func)(Game_Platform *platform,  float delta_time);

#endif //GAMING_GAME_PLATFORM_H
