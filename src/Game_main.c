#include "Game_platform.h"

#define GRAVITY (-9.81f)
// eyes from feet
#define PLAYER_HEIGHT 1.5f
// collision width
#define PLAYER_RADIUS 0.5f
// free space above eyes
#define PLAYER_HEAD_CLEARANCE 0.2f

typedef struct {
    Vertex *vertices;
    unsigned short *indices;
    int vertex_count;
    int index_count;
} Mesh;

static Mesh LoadOBJ(const Game_Platform *platform, MemoryArena *perm_arena, MemoryArena *temp_arena, const char *path) {
    Mesh result = {0};

    const Game_FileResult file = platform->ReadEntireFile(path);
    if (!file.contents) {
        return result;
    }

    int v_count = 0, vt_count = 0, vn_count = 0, f_count = 0;
    char *at = file.contents;

    while (*at) {
        EatSpaces(&at);
        if (at[0] == 'v' && at[1] == ' ') { v_count++; } else if (at[0] == 'v' && at[1] == 't') { vt_count++; } else if
        (at[0] == 'v' && at[1] == 'n') { vn_count++; } else if (at[0] == 'f' && at[1] == ' ') { f_count++; }
        SkipLine(&at);
    }

    if (f_count * 3 > 65535) {
        platform->FreeFileMemory(file.contents);

        return result;
    }

    const unsigned long long temp_memory_mark = temp_arena->used;

    Vec3 *temp_v = PushArray(temp_arena, v_count + 1, Vec3);
    Vec2 *temp_vt = PushArray(temp_arena, vt_count + 1, Vec2);
    Vec3 *temp_vn = PushArray(temp_arena, vn_count + 1, Vec3);

    result.vertex_count = f_count * 3;
    result.index_count = f_count * 3;
    result.vertices = PushArray(perm_arena, result.vertex_count, Vertex);
    result.indices = PushArray(perm_arena, result.index_count, unsigned short);

    if (!temp_v || !temp_vt || !temp_vn || !result.vertices || !result.indices) {
        platform->FreeFileMemory(file.contents);

        return (Mesh){0};
    }

    int v_idx = 1, vt_idx = 1, vn_idx = 1;
    int out_v = 0;
    at = (char *) file.contents;

    while (*at) {
        EatSpaces(&at);
        if (at[0] == 'v' && at[1] == ' ') {
            at += 2;
            EatSpaces(&at);
            temp_v[v_idx].x = ParseFloat(&at);
            EatSpaces(&at);
            temp_v[v_idx].y = ParseFloat(&at);
            EatSpaces(&at);
            temp_v[v_idx].z = ParseFloat(&at);
            v_idx++;
        } else if (at[0] == 'v' && at[1] == 't') {
            at += 2;
            EatSpaces(&at);
            temp_vt[vt_idx].x = ParseFloat(&at);
            EatSpaces(&at);
            temp_vt[vt_idx].y = ParseFloat(&at);
            vt_idx++;
        } else if (at[0] == 'v' && at[1] == 'n') {
            at += 2;
            EatSpaces(&at);
            temp_vn[vn_idx].x = ParseFloat(&at);
            EatSpaces(&at);
            temp_vn[vn_idx].y = ParseFloat(&at);
            EatSpaces(&at);
            temp_vn[vn_idx].z = ParseFloat(&at);
            vn_idx++;
        } else if (at[0] == 'f' && at[1] == ' ') {
            at += 2;
            EatSpaces(&at);

            for (int i = 0; i < 3; ++i) {
                const int p_idx = ParseInt(&at);
                int uv_idx = 0;
                int n_idx = 0;

                if (*at == '/') {
                    at++;
                    if (*at != '/') uv_idx = ParseInt(&at);
                    if (*at == '/') {
                        at++;
                        n_idx = ParseInt(&at);
                    }
                }

                Vertex *vertex = &result.vertices[out_v];

                if (p_idx > 0 && p_idx <= v_count) {
                    vertex->x = temp_v[p_idx].x;
                    vertex->y = temp_v[p_idx].y;
                    vertex->z = temp_v[p_idx].z;
                }

                if (uv_idx > 0 && uv_idx <= vt_count) {
                    vertex->u = temp_vt[uv_idx].x;
                    vertex->v = temp_vt[uv_idx].y;
                }

                if (n_idx > 0 && n_idx <= vn_count) {
                    vertex->nx = temp_vn[n_idx].x;
                    vertex->ny = temp_vn[n_idx].y;
                    vertex->nz = temp_vn[n_idx].z;
                }

                vertex->r = 1.0f;
                vertex->g = 1.0f;
                vertex->b = 1.0f;
                vertex->a = 1.0f;

                result.indices[out_v] = (unsigned short) out_v;
                out_v++;
                EatSpaces(&at);
            }
        }
        SkipLine(&at);
    }

    platform->FreeFileMemory(file.contents);
    temp_arena->used = temp_memory_mark;

    return result;
}

typedef struct {
    Vec3 min;
    Vec3 max;
} AABB;

static AABB GetPlayerAABB(const Vec3 position) {
    return (AABB){
        .min = Vector3(position.x - PLAYER_RADIUS, position.y - PLAYER_HEIGHT, position.z - PLAYER_RADIUS),
        .max = Vector3(position.x + PLAYER_RADIUS, position.y + PLAYER_HEAD_CLEARANCE, position.z + PLAYER_RADIUS)
    };
}

static Bool OverlapAABB(const AABB a, const AABB b) {
    return (a.min.x < b.max.x && a.max.x > b.min.x &&
            a.min.y < b.max.y && a.max.y > b.min.y &&
            a.min.z < b.max.z && a.max.z > b.min.z);
}

typedef enum {
    ENTITY_NONE,
    ENTITY_MESH,
} EntityType;

typedef struct {
    Bool active;
    EntityType type;

    Mesh mesh;
    Vec3 position;
    Vec3 dim;
} Entity;

static AABB GetEntityAABB(const Entity *entity) {
    const Vec3 dim = Vec3_Scale(entity->dim, 0.5f);

    return (AABB){
        .min = Vec3_Sub(entity->position, dim),
        .max = Vec3_Add(entity->position, dim)
    };
}

typedef enum {
    MODE_MENU,
    MODE_PLAYING,
    MODE_EDITOR,
} Mode;

typedef struct {
    Mode mode;

    Vec3 position;
    Vec3 velocity;

    float pitch, yaw;
    Bool grounded;

#if 0
    float sine;
#endif

    Entity entities[256];
    int entity_count;

    Game_FontHandle debug_font;
    Bool initialized;
} State;

Entity *AddEntity(State *state) {
    if (state->entity_count < ArrayCount(state->entities)) {
        Entity *entity = &state->entities[state->entity_count++];
        entity->active = True;

        return entity;
    }

    return 0;
}

void UpdateAndRender(Game_Platform *platform) {
    if (platform->permanent_memory.used == 0) {
        PushStruct(&platform->permanent_memory, State);
    }

    State *state = (State *) platform->permanent_memory.base;

    if (!state->initialized) {
        state->debug_font = platform->LoadFontFile("jetbrains_mono.ttf", 24.0f);

        state->mode = MODE_EDITOR;
#if 0
        state->sine = 0.0f;
#endif

        Entity *entity = AddEntity(state);
        entity->type = ENTITY_MESH;
        entity->position = Vector3(0.0f, 0.0f, 0.0f);
        entity->dim = Vector3(3.0f, 3.0f, 3.0f);
        entity->mesh = LoadOBJ(platform, &platform->permanent_memory, &platform->transient_memory, "model.obj");

        state->position = Vector3(0, 1.5f, 5.0f);
        state->velocity = Vector3(0, 0, 0);
        state->yaw = 0.0f;
        state->pitch = 0.0f;
        state->grounded = False;

        // this must always be here!
        state->initialized = True;
    }

    if (!platform->mouse_locked) {
        if (WasPressed(platform->input[GAME_KEY_MOUSE_LEFT])) {
            platform->mouse_locked = True;
        }
    } else {
        if (WasPressed(platform->input[GAME_KEY_ESCAPE])) {
            platform->mouse_locked = False;
        }

        state->yaw += platform->mouse_delta_x * 0.0015f;
        state->pitch -= platform->mouse_delta_y * 0.0015f;

        state->pitch = Clamp(state->pitch, -(PI / 2.0f - 0.1f), PI / 2.0f - 0.1f);
    }

    if (!platform->fullscreen) {
        if (WasPressed(platform->input[GAME_KEY_F11])) {
            platform->fullscreen = True;
        }
    } else {
        if (WasPressed(platform->input[GAME_KEY_F11])) {
            platform->fullscreen = False;
        }
    }

#if 0
    Game_SoundBuffer *sound_buffer = &platform->sound_buffer;

    int tone_hz = 256;
    int tone_volume = 3000;
    int wave_period = sound_buffer->samples_per_second / tone_hz;

    short *sample_out = sound_buffer->samples;
    for (int i = 0; i < sound_buffer->sample_count; ++i) {
        float sine_value = Sin(state->sine);
        short sample_value = (short) (sine_value * tone_volume);

        *sample_out++ = sample_value;
        *sample_out++ = sample_value;

        state->sine += 2.0f * PI * 1.0f / (float) wave_period;
        if (state->sine > 2.0f * PI) {
            state->sine -= 2.0f * PI;
        }
    }
#endif

    switch (state->mode) {
        case MODE_MENU: {
            // TODO
        }
        break;

        case MODE_EDITOR: {
            Vec3 forward = Vector3(-Cos(state->pitch) * Sin(state->yaw), Sin(state->pitch),
                                   -Cos(state->pitch) * Cos(state->yaw));
            forward = Vec3_Normalize(forward);

            Vec3 right = Vector3(Cos(state->yaw), 0, -Sin(state->yaw));
            right = Vec3_Normalize(right);

            const Vec3 up = Vector3(0, 1, 0);

            const float speed = 10.0f * platform->delta_time;

            Vec3 direction = Vector3(0, 0, 0);

            if (IsDown(platform->input[GAME_KEY_W])) {
                direction = Vec3_Add(direction, forward);
            }
            if (IsDown(platform->input[GAME_KEY_S])) {
                direction = Vec3_Sub(direction, forward);
            }
            if (IsDown(platform->input[GAME_KEY_A])) {
                direction = Vec3_Add(direction, right);
            }
            if (IsDown(platform->input[GAME_KEY_D])) {
                direction = Vec3_Sub(direction, right);
            }
            if (IsDown(platform->input[GAME_KEY_SPACE])) {
                direction = Vec3_Add(direction, up);
            }
            if (IsDown(platform->input[GAME_KEY_LEFT_CTRL])) {
                direction = Vec3_Sub(direction, up);
            }

            if (direction.x != 0.0f || direction.y != 0.0f || direction.z != 0.0f) {
                direction = Vec3_Normalize(direction);
                state->position = Vec3_Add(state->position, Vec3_Scale(direction, speed));
            }

            const Vec3 target = Vec3_Add(state->position, forward);
            platform->view_projection = Matrix_Multiply(
                Matrix_Perspective(PI / 3.0f, platform->width / platform->height, 0.1f, 100.0f),
                Matrix_LookAt(state->position, target, Vector3(0, 1, 0)));

            Game_PushTextRenderEntry(platform, state->debug_font, platform->width / 2, platform->height / 2, "+");
            // Game_PushSprite(platform, TEXTURE_HANDLE_MAGIC_PIXEL,
            //                 platform->width / 2.0f - 2.0f,
            //                 platform->height / 2.0f - 2.0f,
            //                 4.0f, 4.0f, GREEN);
            Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                                      "%.1fms", platform->frame_time_ms);
            Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 50.0f,
                          "%.1f, %.1f, %.1f", state->position.x, state->position.y, state->position.z);
        }
        break;

        case MODE_PLAYING: {
            // this includes pitch
            Vec3 forward = Vector3(-Cos(state->pitch) * Sin(state->yaw), Sin(state->pitch),
                                   -Cos(state->pitch) * Cos(state->yaw));
            forward = Vec3_Normalize(forward);

            Vec3 move_forward = Vector3(-Sin(state->yaw), 0, -Cos(state->yaw));
            move_forward = Vec3_Normalize(move_forward);

            Vec3 right = Vector3(Cos(state->yaw), 0, -Sin(state->yaw));
            right = Vec3_Normalize(right);

            const float speed = 5.0f * platform->delta_time;

            Vec3 delta_position = Vector3(0, 0, 0);

            if (IsDown(platform->input[GAME_KEY_W])) {
                delta_position = Vec3_Add(delta_position, Vec3_Scale(move_forward, speed));
            }
            if (IsDown(platform->input[GAME_KEY_S])) {
                delta_position = Vec3_Sub(delta_position, Vec3_Scale(move_forward, speed));
            }
            if (IsDown(platform->input[GAME_KEY_A])) {
                delta_position = Vec3_Add(delta_position, Vec3_Scale(right, speed));
            }
            if (IsDown(platform->input[GAME_KEY_D])) {
                delta_position = Vec3_Sub(delta_position, Vec3_Scale(right, speed));
            }

            if (!state->grounded) {
                state->velocity.y += GRAVITY * platform->delta_time;
            }

            delta_position = Vec3_Scale(Vec3_Normalize(delta_position), speed);
            delta_position.y += state->velocity.y * platform->delta_time;

            state->position.x += delta_position.x;
            for (int i = 0; i < state->entity_count; ++i) {
                const Entity *entity = &state->entities[i];

                if (entity->active && OverlapAABB(GetPlayerAABB(state->position), GetEntityAABB(entity))) {
                    state->position.x -= delta_position.x;
                }
            }

            state->position.y += delta_position.y;
            state->grounded = False;
            for (int i = 0; i < state->entity_count; ++i) {
                const Entity *entity = &state->entities[i];

                if (!entity->active) {
                    continue;
                }

                const AABB aabb = GetEntityAABB(entity);
                if (OverlapAABB(GetPlayerAABB(state->position), aabb)) {
                    if (delta_position.y <= 0) {
                        state->position.y = aabb.max.y + PLAYER_HEIGHT;
                        state->grounded = True;
                    } else {
                        state->position.y = aabb.min.y - PLAYER_HEAD_CLEARANCE;
                    }

                    state->velocity.y = 0.0f;
                    break;
                }
            }

            state->position.z += delta_position.z;
            for (int i = 0; i < state->entity_count; ++i) {
                const Entity *entity = &state->entities[i];

                if (entity->active && OverlapAABB(GetPlayerAABB(state->position), GetEntityAABB(entity))) {
                    state->position.z -= delta_position.z;
                }
            }

            const Vec3 target = Vec3_Add(state->position, forward);
            platform->view_projection = Matrix_Multiply(
                Matrix_Perspective(PI / 3.0f, platform->width / platform->height, 0.1f, 100.0f),
                Matrix_LookAt(state->position, target, Vector3(0, 1, 0)));

            Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                                      "%.1fms", platform->frame_time_ms);
        }
        break;
        default: break; // TODO: InvalidCodePath
    }

    for (int i = 0; i < state->entity_count; ++i) {
        const Entity *entity = &state->entities[i];

        if (entity->active && entity->type == ENTITY_MESH) {
            Mesh mesh = entity->mesh;
            Game_PushMeshRenderEntry(
                platform, Matrix_Translation(entity->position.x, entity->position.y, entity->position.z),
                TEXTURE_HANDLE_MAGIC_PIXEL, mesh.vertices, mesh.vertex_count, mesh.indices, mesh.index_count,
                PUSH_MESH_REGULAR);
        }
    }
}
