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

static Mesh LoadOBJ(Game_Platform *platform, const char *path) {
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

    const TemporaryMemory temporary_memory = BeginTemporaryMemory(&platform->transient_memory);

    Vec3 *temp_v = PushArray(&platform->transient_memory, v_count + 1, Vec3);
    Vec2 *temp_vt = PushArray(&platform->transient_memory, vt_count + 1, Vec2);
    Vec3 *temp_vn = PushArray(&platform->transient_memory, vn_count + 1, Vec3);

    result.vertex_count = f_count * 3;
    result.index_count = f_count * 3;
    result.vertices = PushArray(&platform->permanent_memory, result.vertex_count, Vertex);
    result.indices = PushArray(&platform->permanent_memory, result.index_count, unsigned short);

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
    EndTemporaryMemory(temporary_memory);

    return result;
}

typedef enum {
    ENTITY_NONE,
    ENTITY_MESH,
} EntityType;

typedef struct {
    Bool active;
    EntityType type;

    double angular_velocity;

    Game_PipelineHandle pipeline_handle;
    Game_TextureHandle texture_handles[4];
    Mesh mesh;
    Vec3d position;
    Vec3 scale;
    Vec3 dim;
} Entity;

#if 0
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

static AABB GetEntityAABB(const Entity *entity) {
    const Vec3 dim = Vec3_Scale(entity->dim, 0.5f);

    return (AABB){
        .min = Vec3_Sub(entity->position, dim),
        .max = Vec3_Add(entity->position, dim)
    };
}
#endif

typedef enum {
    MODE_MENU,
    MODE_PLAYING
} Mode;

typedef struct {
    Mode mode;

    Vec3d position;
    Vec3d velocity;

    float pitch, yaw;
    Bool grounded;

#if 0
    float sine;
#endif

    Entity entities[256];
    int entity_count;

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
        state->mode = MODE_PLAYING;
#if 0
        state->sine = 0.0f;
#endif

#if 0
        Entity *earth = AddEntity(state);
        earth->type = ENTITY_MESH;
        earth->position = Vector3d(0.0, 6371000.0, 0.0);
        earth->dim = Vector3(1.0f, 1.0f, 1.0f);
        earth->scale = Vector3(6371000.0f, 6371000.0f, 6371000.0f);
        earth->angular_velocity = (2.0 * PI) / 86400.0;
        earth->mesh = LoadOBJ(platform, "assets/models/sphere.obj");
        earth->pipeline_handle = platform->CreatePipeline("assets/shaders/earth.vert.spv",
                                                          "assets/shaders/earth.frag.spv");
        earth->texture_handles[0] = platform->LoadImageFile("assets/images/earth.jpg");
        earth->texture_handles[1] = platform->LoadImageFile("assets/images/earth_clouds.jpg");
        earth->texture_handles[2] = platform->LoadImageFile("assets/images/earth_specular.tif");
        earth->texture_handles[3] = platform->LoadImageFile("assets/images/earth_normal.tif");
#endif

#if 0
        Entity *saturn = AddEntity(state);
        saturn->type = ENTITY_MESH;
        saturn->position = Vector3d(0.0, 0.0, 0.0);
        saturn->dim = Vector3(1.0f, 1.0f, 1.0f);
        saturn->scale = Vector3(60268000.0f, 54364000.0f, 60268000.0f);
        saturn->angular_velocity = (2.0 * PI) / 38520.0;
        saturn->mesh = LoadOBJ(platform, "assets/models/sphere.obj");
        saturn->pipeline_handle = platform->CreatePipeline("assets/shaders/planet.vert.spv",
                                                           "assets/shaders/planet.frag.spv", GAME_CULL_MODE_BACK,
                                                           GAME_BLEND_MODE_OPAQUE);
        saturn->texture_handles[0] = platform->LoadImageFile("assets/images/saturn.jpg");

        Entity *rings = AddEntity(state);
        rings->type = ENTITY_MESH;
        rings->position = saturn->position;
        rings->dim = Vector3(1.0f, 1.0f, 1.0f);
        rings->scale = Vector3(60268000.0f, 60268000.0f, 60268000.0f);
        rings->angular_velocity = saturn->angular_velocity;
        rings->mesh = LoadOBJ(platform, "assets/models/rings.obj");
        rings->pipeline_handle = platform->CreatePipeline("assets/shaders/rings.vert.spv",
                                                          "assets/shaders/rings.frag.spv", GAME_CULL_MODE_NONE,
                                                          GAME_BLEND_MODE_ALPHA);
        rings->texture_handles[0] = platform->LoadImageFile("assets/images/saturn_ring.png");
#endif

#if 0
        Entity *jupiter = AddEntity(state);
        jupiter->type = ENTITY_MESH;
        jupiter->position = Vector3d(0.0, 0.0, 0.0);
        jupiter->dim = Vector3(1.0f, 1.0f, 1.0f);
        jupiter->scale = Vector3(71492000.0f, 66854000.0f, 71492000.0f);
        jupiter->angular_velocity = (2.0 * PI) / 35730.0;
        jupiter->mesh = LoadOBJ(platform, "assets/models/sphere.obj");
        jupiter->pipeline_handle = platform->CreatePipeline("assets/shaders/jupiter.vert.spv",
                                                            "assets/shaders/jupiter.frag.spv",
                                                            GAME_CULL_MODE_BACK,
                                                            GAME_BLEND_MODE_OPAQUE);
        jupiter->texture_handles[0] = platform->LoadImageFile("assets/images/jupiter.jpg");
#endif


#if 0
        Entity *neptune = AddEntity(state);
        neptune->type = ENTITY_MESH;
        neptune->position = Vector3d(0.0, 0.0, 0.0);
        neptune->dim = Vector3(1.0f, 1.0f, 1.0f);
        neptune->scale = Vector3(24622000.0f, 24622000.0f, 24622000.0f);
        neptune->angular_velocity = (2.0 * PI) / 57996.0;
        neptune->mesh = LoadOBJ(platform, "assets/models/sphere.obj");
        neptune->pipeline_handle = platform->CreatePipeline("assets/shaders/planet.vert.spv",
                                                            "assets/shaders/planet.frag.spv",
                                                            GAME_CULL_MODE_BACK,
                                                            GAME_BLEND_MODE_OPAQUE);
        neptune->texture_handles[0] = platform->LoadImageFile("assets/images/neptune.jpg");
#endif

        state->position = Vector3d(0.0, 54364001.5, 0.0);
        state->velocity = Vector3d(0.0, 0.0, 0.0);
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
        state->pitch += platform->mouse_delta_y * 0.0015f;

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

        case MODE_PLAYING: {
            Vec3 forward = Vector3(-Cos(state->pitch) * Sin(state->yaw), Sin(state->pitch),
                                   -Cos(state->pitch) * Cos(state->yaw));
            forward = Vec3_Normalize(forward);

            Vec3 right = Vector3(Cos(state->yaw), 0, -Sin(state->yaw));
            right = Vec3_Normalize(right);

            const Vec3 up = Vector3(0, -1, 0);

            const float speed = 50000000.0f * platform->delta_time;

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
                state->position = Vec3d_Add(state->position, Vec3d_Scale(Vec3_Cast64(direction), speed));
            }

            platform->view_projection = Matrix_Multiply(
                Matrix_Perspective(PI / 3.0f, platform->width / platform->height, 1.0f, 1000000000000.0f),
                Matrix_LookAt(Vector3(0, 0, 0), forward, Vector3(0, 1, 0))
            );
        }
        break;
    }

    Game_PushSetTarget(platform, 0, True, BLACK);

    for (int i = 0; i < state->entity_count; ++i) {
        const Entity *entity = &state->entities[i];

        if (entity->active && entity->type == ENTITY_MESH) {
            const Mesh mesh = entity->mesh;

            const Vec3d relative_position = Vec3d_Sub(entity->position, state->position);
            const Vec3 render_position = Vec3d_Cast32(relative_position);

            const float rotation_angle = (float) (platform->elapsed_time * entity->angular_velocity);

            const Mat4X4 transform = Matrix_Multiply(
                Matrix_Translation(render_position),
                Matrix_Multiply(
                    Matrix_RotationY(rotation_angle),
                    Matrix_Scale(entity->scale)
                )
            );

            Game_RenderEntry *entry = Game_PushMeshRenderEntry(platform, mesh.vertices, mesh.vertex_count, mesh.indices,
                                                               mesh.index_count);
            if (entry) {
                entry->mesh.transform = transform;
                entry->mesh.pipeline_handle = entity->pipeline_handle;
                for (int j = 0; j < 4; ++j) {
                    entry->mesh.texture_handles[j] = entity->texture_handles[j];
                }

                entry->mesh.fragment_uniforms[0] = (float) (platform->elapsed_time * entity->angular_velocity);
                entry->mesh.fragment_uniform_count = 1;

#if 0
                const Vec3 d = Vec3_Normalize(Vec3d_Cast32(Vec3d_Sub(state->position, entity->position)));
                entry->mesh.fragment_uniforms[0] = (float) platform->elapsed_time;
                entry->mesh.fragment_uniforms[1] = d.x;
                entry->mesh.fragment_uniforms[2] = d.y;
                entry->mesh.fragment_uniforms[3] = d.z;
                entry->mesh.fragment_uniforms[4] = (float) (platform->elapsed_time * entity->angular_velocity);
                entry->mesh.fragment_uniform_count = 5;
#endif
            }
        }
    }
}
