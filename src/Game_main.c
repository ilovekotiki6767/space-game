#include "Game_platform.h"

#define GRAVITY (-9.81f)
// eyes from feet
#define PLAYER_HEIGHT 1.5f
// collision width
#define PLAYER_RADIUS 0.5f
// free space above eyes
#define PLAYER_HEAD_CLEARANCE 0.2f

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
    ENTITY_CUBE
} EntityType;

typedef struct {
    Bool active;
    EntityType type;

    Vec3 position;
    Vec3 dim;
    Vec4 color;
} Entity;

static AABB GetEntityAABB(const Entity *entity) {
    const Vec3 dim = Vec3_Scale(entity->dim, 0.5f);

    return (AABB){
        .min = Vec3_Sub(entity->position, dim),
        .max = Vec3_Add(entity->position, dim)
    };
}

typedef struct {
    Vec3 position;
    Vec3 velocity;

    float pitch, yaw;
    Bool grounded;

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
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        state->debug_font = platform->LoadFontFile("jetbrains_mono.ttf", 24.0f);

        state->position = Vector3(0, 1.5f, 5.0f);
        state->velocity = Vector3(0, 0, 0);
        state->yaw = 0.0f;
        state->pitch = 0.0f;
        state->grounded = False;

        Entity *floor = AddEntity(state);
        floor->type = ENTITY_CUBE;
        floor->position = Vector3(0, -2.0f, 0);
        floor->dim = Vector3(20, 2, 20);
        floor->color = GRAY;

        Entity *cube1 = AddEntity(state);
        cube1->type = ENTITY_CUBE;
        cube1->position = Vector3(5, 1, 0);
        cube1->dim = Vector3(2, 2, 2);
        cube1->color = RED;

        Entity *cube2 = AddEntity(state);
        cube2->type = ENTITY_CUBE;
        cube2->position = Vector3(-3, 2, -2);
        cube2->dim = Vector3(2, 4, 2);
        cube2->color = BLUE;

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

        state->yaw += platform->mouse_delta_x * 0.001f;
        state->pitch -= platform->mouse_delta_y * 0.001f;

        state->pitch = Clamp(state->pitch, -(PI / 2.0f - 0.1f), PI / 2.0f - 0.1f);
    }

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

    for (int i = 0; i < state->entity_count; ++i) {
        const Entity *entity = &state->entities[i];

        if (entity->active && entity->type == ENTITY_CUBE) {
            const Vec3 dim = Vec3_Scale(entity->dim, 0.5f);

            const float r = entity->color.x;
            const float g = entity->color.y;
            const float b = entity->color.z;
            const float a = entity->color.w;

            const Vertex vertices[] = {
                {-dim.x, -dim.y, dim.z, r, g, b, a, 0, 1}, {dim.x, -dim.y, dim.z, r, g, b, a, 1, 1},
                {dim.x, dim.y, dim.z, r, g, b, a, 1, 0}, {-dim.x, dim.y, dim.z, r, g, b, a, 0, 0},
                {dim.x, -dim.y, -dim.z, r, g, b, a, 0, 1}, {-dim.x, -dim.y, -dim.z, r, g, b, a, 1, 1},
                {-dim.x, dim.y, -dim.z, r, g, b, a, 1, 0}, {dim.x, dim.y, -dim.z, r, g, b, a, 0, 0},
                {-dim.x, dim.y, -dim.z, r, g, b, a, 0, 0}, {-dim.x, dim.y, dim.z, r, g, b, a, 0, 1},
                {dim.x, dim.y, dim.z, r, g, b, a, 1, 1}, {dim.x, dim.y, -dim.z, r, g, b, a, 1, 0},
                {-dim.x, -dim.y, -dim.z, r, g, b, a, 0, 1}, {dim.x, -dim.y, -dim.z, r, g, b, a, 1, 1},
                {dim.x, -dim.y, dim.z, r, g, b, a, 1, 0}, {-dim.x, -dim.y, dim.z, r, g, b, a, 0, 0},
                {dim.x, -dim.y, -dim.z, r, g, b, a, 1, 1}, {dim.x, dim.y, -dim.z, r, g, b, a, 1, 0},
                {dim.x, dim.y, dim.z, r, g, b, a, 0, 0}, {dim.x, -dim.y, dim.z, r, g, b, a, 0, 1},
                {-dim.x, -dim.y, -dim.z, r, g, b, a, 0, 1}, {-dim.x, -dim.y, dim.z, r, g, b, a, 1, 1},
                {-dim.x, dim.y, dim.z, r, g, b, a, 1, 0}, {-dim.x, dim.y, -dim.z, r, g, b, a, 0, 0}
            };
            const unsigned short indices[] = {
                0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7,
                8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15,
                16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
            };

            Game_PushMeshRenderEntry(
                platform, Matrix_Translation(entity->position.x, entity->position.y, entity->position.z),
                TEXTURE_HANDLE_MAGIC_PIXEL, vertices, 24, indices, 36);
        }
    }

    Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                              "%.1fms", platform->frame_time_ms);
}
