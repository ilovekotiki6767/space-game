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
// reserved texture handle, a 1x1 white pixel texture
#define TEXTURE_HANDLE_MAGIC_PIXEL 0

// Handles

typedef unsigned int Game_TextureHandle;
typedef unsigned int Game_FontHandle;

// Structures

typedef enum {
    GAME_RENDER_ENTRY_MESH,
    GAME_RENDER_ENTRY_TEXT,
} Game_RenderEntryType;

typedef struct {
    Game_RenderEntryType type;

    union  {
        struct {
            Mat4X4 transform;
            Game_TextureHandle texture_handle;
        } mesh;

        struct {
            Game_FontHandle font_handle;
            float x, y;
            const char *text;
        } text;
    };
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
    Game_TextureHandle (*LoadImageFile)(const char *path);
    Game_FontHandle (*LoadFontFile)(const char *path, float size);
} Game_Platform;

// Functions

static void Game_PushMeshRenderEntry(Game_Platform *platform, const Mat4X4 transform,
                                 const Game_TextureHandle texture_handle) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];

        entry->type = GAME_RENDER_ENTRY_MESH;
        entry->mesh.transform = transform;
        entry->mesh.texture_handle = texture_handle;
    }
}

static void Game_PushTextRenderEntry(Game_Platform *platform, Game_FontHandle font_handle, float x, float y, const char *text) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];

        entry->type = GAME_RENDER_ENTRY_TEXT;
        entry->text.font_handle = font_handle;
        entry->text.x = x;
        entry->text.y = y;
        entry->text.text = text;
    }
}

typedef void (*Game_UpdateAndRender_Func)(Game_Platform *platform, float delta_time);

#endif //GAMING_GAME_PLATFORM_H
