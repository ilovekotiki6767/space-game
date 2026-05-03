#ifndef GAMING_GAME_PLATFORM_H
#define GAMING_GAME_PLATFORM_H

#include "Game_math.h"
#include "Game_math64.h"
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
typedef unsigned int Game_PipelineHandle;

// Structures

typedef struct {
    unsigned char *base;
    unsigned long long size;
    unsigned long long used;
    /// how many nested temporary scopes are active
    int temp_count;
} MemoryArena;

typedef struct {
    MemoryArena *arena;
    unsigned long long mark;
} TemporaryMemory;

static TemporaryMemory BeginTemporaryMemory(MemoryArena *arena) {
    const TemporaryMemory temporary_memory = {
        .arena = arena,
        .mark = arena->used,
    };
    arena->temp_count++;

    return temporary_memory;
}

static void EndTemporaryMemory(const TemporaryMemory temporary_memory) {
    temporary_memory.arena->used = temporary_memory.mark;
    temporary_memory.arena->temp_count--;
}

static void InitializeArena(MemoryArena *arena, void *base, const unsigned long long size) {
    arena->base = (unsigned char *) base;
    arena->size = size;
    arena->used = 0;
}

#define PushStruct(arena, type) (type *)PushSize(arena, sizeof(type))
#define PushArray(arena, count, type) (type *)PushSize(arena, (count) * sizeof(type))

static void *PushSize(MemoryArena *arena, const unsigned long long size) {
    unsigned long long padding = 0;
    if (arena->used & 7) {
        padding = 8 - (arena->used & 7);
    }

    if (arena->used + padding + size <= arena->size) {
        arena->used += padding;
        void *result = arena->base + arena->used;
        arena->used += size;

        unsigned char *byte = result;
        for (unsigned long long i = 0; i < size; ++i) {
            byte[i] = 0;
        }

        return result;
    }

    return 0;
}

typedef struct {
    unsigned int contents_size;
    void *contents;
} Game_FileResult;

typedef enum {
    GAME_RENDER_ENTRY_MESH,
    GAME_RENDER_ENTRY_SET_TARGET,
    GAME_RENDER_ENTRY_FULLSCREEN_QUAD,
} Game_RenderEntryType;

typedef struct {
    Game_RenderEntryType type;

    union {
        struct Game_RenderEntry_Mesh {
            Mat4X4 transform;
            Game_TextureHandle texture_handles[4];
            Game_PipelineHandle pipeline_handle;
            float vertex_uniforms[32];
            int vertex_uniform_count;
            float fragment_uniforms[32];
            int fragment_uniform_count;
            int index_offset;
            int index_count;
            int vertex_offset;
        } mesh;

        struct Game_RenderEntry_SetTarget {
            Game_TextureHandle color_target;
            Bool clear;
            Vec4 clear_color;
        } set_target;

        struct Game_RenderEntry_FullscreenQuad {
            Game_PipelineHandle pipeline_handle;
            Game_TextureHandle input_textures[4];
        } fullscreen_quad;
    };
} Game_RenderEntry;

typedef struct {
    int samples_per_second;
    // how many samples the game should write this exact frame
    int sample_count;
    int channels;
    // the backing buffer to write into
    short *samples;
} Game_SoundBuffer;

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

    GAME_KEY_F11,

    GAME_KEY_COUNT,
} Game_Key;

typedef enum {
    GAME_CULL_MODE_BACK,
    GAME_CULL_MODE_NONE,
} Game_CullMode;

typedef enum {
    GAME_BLEND_MODE_OPAQUE,
    GAME_BLEND_MODE_ALPHA,
} Game_BlendMode;

typedef struct {
    MemoryArena permanent_memory;
    MemoryArena transient_memory;

    Game_SoundBuffer sound_buffer;

    Game_ButtonState input[GAME_KEY_COUNT];

    Vertex transient_vertices[16384];
    int transient_vertex_count;

    unsigned short transient_indices[32768];
    int transient_index_count;

    Game_RenderEntry render_entries[1024];
    int render_entry_count;

    float delta_time;
    /// updated every half a second
    float frame_time_ms;
    double elapsed_time;

    Bool fullscreen;

    Bool mouse_locked;
    float mouse_delta_x, mouse_delta_y;

    float width, height;
    Mat4X4 view_projection;

    // NOTE: the function pointers below should only be used if something you need cannot be represented by a simple
    // data type or a few fields. for example, there is no point in adding a `GetX` function here if `X` can just be a
    // direct field

    // Platform API
    Game_TextureHandle (*LoadImageFile)(const char *path);

    Game_PipelineHandle (*CreatePipeline)(const char *vertex_spirv_path, const char *fragment_spirv_path,
                                          Game_CullMode cull_mode, Game_BlendMode blend_mode);

    Game_TextureHandle (*CreateRenderTarget)(Vec2 size);

    Game_FileResult (*ReadEntireFile)(const char *path);

    void (*FreeFileMemory)(void *memory);
} Game_Platform;

// Functions

static Game_RenderEntry *Game_PushMeshRenderEntry(Game_Platform *platform, const Vertex *vertices,
                                                  const int vertex_count, const unsigned short *indices,
                                                  const int index_count) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries) &&
        platform->transient_vertex_count + vertex_count <= ArrayCount(platform->transient_vertices) &&
        platform->transient_index_count + index_count <= ArrayCount(platform->transient_indices)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];

        entry->type = GAME_RENDER_ENTRY_MESH;
        entry->mesh.vertex_offset = platform->transient_vertex_count;
        entry->mesh.index_offset = platform->transient_index_count;
        entry->mesh.index_count = index_count;

        for (int i = 0; i < vertex_count; ++i) {
            platform->transient_vertices[platform->transient_vertex_count++] = vertices[i];
        }

        for (int i = 0; i < index_count; ++i) {
            platform->transient_indices[platform->transient_index_count++] = indices[i];
        }

        return entry;
    }

    return 0;
}

static void Game_PushSetTarget(Game_Platform *platform, const Game_TextureHandle color_target, const Bool clear,
                               const Vec4 clear_color) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];
        entry->type = GAME_RENDER_ENTRY_SET_TARGET;
        entry->set_target.color_target = color_target;
        entry->set_target.clear = clear;
        entry->set_target.clear_color = clear_color;
    }
}

static void Game_PushFullscreenQuad(Game_Platform *platform, const Game_PipelineHandle pipeline_handle,
                                    const Game_TextureHandle *inputs, const int input_count) {
    if (platform->render_entry_count < ArrayCount(platform->render_entries)) {
        Game_RenderEntry *entry = &platform->render_entries[platform->render_entry_count++];
        entry->type = GAME_RENDER_ENTRY_FULLSCREEN_QUAD;
        entry->fullscreen_quad.pipeline_handle = pipeline_handle;

        for (int i = 0; i < 4; ++i) {
            entry->fullscreen_quad.input_textures[i] = (i < input_count && inputs) ? inputs[i] : 0;
        }
    }
}

typedef void (*Game_UpdateAndRender_Func)(Game_Platform *platform);

#endif //GAMING_GAME_PLATFORM_H
