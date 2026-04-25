#ifndef GAMING_GAME_PLATFORM_H
#define GAMING_GAME_PLATFORM_H

#include "Game_math.h"

#define ArrayCount(array) (sizeof(array)/sizeof(array[0]))

typedef struct {
    Mat4X4 transform;
} Game_RenderEntry;

typedef struct {
    Game_RenderEntry render_entries[1024];
    int render_entry_count;
} Game_Platform;

static void Game_PushRenderEntry(Game_Platform *platform, const Mat4X4 transform) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        platform->render_entries[platform->render_entry_count++].transform = transform;
    }
}

#endif //GAMING_GAME_PLATFORM_H
