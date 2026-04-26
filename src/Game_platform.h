#ifndef GAMING_GAME_PLATFORM_H
#define GAMING_GAME_PLATFORM_H

#include "Game_math.h"
#include "Game_string.h"
// one of the C headers that are available without standard library
#include <stdarg.h>

// Macros

#define Bool int
#define True 1
#define False 0

#define ArrayCount(array) (sizeof(array)/sizeof(array[0]))

#define IsDown(button) ((button).ended_down)
#define WasPressed(button) (((button).half_transition_count > 1) || ((button).half_transition_count == 1 && (button).ended_down))
/// reserved texture handle, a 1x1 white pixel texture
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

    union {
        struct {
            Mat4X4 transform;
            Game_TextureHandle texture_handle;
        } mesh;

        struct {
            Game_FontHandle font_handle;
            float x, y;
            char text[256];
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
    GAME_KEY_ESCAPE, GAME_KEY_SPACE, GAME_KEY_LEFT_CTRL,
    GAME_KEY_MOUSE_LEFT, GAME_KEY_MOUSE_RIGHT,

    GAME_KEY_COUNT,
} Game_Key;

typedef struct {
    void *permanent_storage;
    unsigned long long permanent_storage_size;

    Game_ButtonState input[GAME_KEY_COUNT];

    Game_RenderEntry render_entries[1024];
    int render_entry_count;

    float delta_time;
    /// updated every half a second
    float frame_time_ms;

    Bool mouse_locked;
    float mouse_delta_x, mouse_delta_y;

    // NOTE: the function pointers below should only be used if something you need cannot be represented by a simple
    // data type or a few fields. for example, there is no point in adding a `GetX` function here if `X` can just be a
    // direct field

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

static void Game_PushTextRenderEntry(Game_Platform *platform, const Game_FontHandle font_handle, const float x,
                                     const float y, const char *text) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];

        entry->type = GAME_RENDER_ENTRY_TEXT;
        entry->text.font_handle = font_handle;
        entry->text.x = x;
        entry->text.y = y;

        char *destination = entry->text.text;
        const char *end = destination + ArrayCount(entry->text.text) - 1;

        while (*text && destination < end) {
            *destination++ = *text++;
        }

        *destination = '\0';
    }
}

static void Game_PushTextRenderEntryF(Game_Platform *platform, const Game_FontHandle font_handle,
                                      const float x, const float y, const char *fmt, ...) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];

        entry->type = GAME_RENDER_ENTRY_TEXT;
        entry->text.font_handle = font_handle;
        entry->text.x = x;
        entry->text.y = y;

        va_list args;
        va_start(args, fmt);

        char *destination = entry->text.text;
        const char *end = destination + ArrayCount(entry->text.text) - 1;

        while (*fmt && destination < end) {
            if (*fmt != '%') {
                *destination++ = *fmt++;
                continue;
            }

            fmt++;

            int precision = 6;
            if (*fmt == '.') {
                fmt++;
                precision = 0;

                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }

            if (*fmt == 'f') {
                const double value = va_arg(args, double);

                destination = WriteFloat(destination, end, value, precision);
                fmt++;
            } else if (*fmt == '%') {
                if (destination < end) {
                    *destination++ = '%';
                }

                fmt++;
            } else {
                if (destination < end) {
                    *destination++ = '%';
                }

                if (*fmt && destination < end) {
                    *destination++ = *fmt++;
                }
            }
        }

        *destination = '\0';
        va_end(args);
    }
}

typedef void (*Game_UpdateAndRender_Func)(Game_Platform *platform);

#endif //GAMING_GAME_PLATFORM_H
