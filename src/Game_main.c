#include "Game_platform.h"

#define GRAVITY (-9.81f)
// eyes from feet
#define PLAYER_HEIGHT 1.5f
// collision width
#define PLAYER_RADIUS 0.5f
// free space above eyes
#define PLAYER_HEAD_CLEARANCE 0.2f

static Bool OverlapAABB(const Vec3 min1, const Vec3 max1, const Vec3 min2, const Vec3 max2) {
    return (min1.x <= max2.x && max1.x >= min2.x &&
            min1.y <= max2.y && max1.y >= min2.y &&
            min1.z <= max2.z && max1.z >= min2.z);
}

typedef struct {
    Vec3 position;
    Vec3 velocity;

    float pitch, yaw;
    Bool grounded;

    Game_FontHandle debug_font;
    Bool initialized;
} State;

void UpdateAndRender(Game_Platform *platform) {
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        state->debug_font = platform->LoadFontFile("jetbrains_mono.ttf", 24.0f);

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

    const Vec3 up = Vector3(0, 1, 0);

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

    // TODO: unhardcode
    const Vec3 center = Vector3(5, 1, 0);
    const Vec3 half = Vector3(1, 1, 1);
    const Vec3 min = Vec3_Sub(center, half);
    const Vec3 max = Vec3_Add(center, half);

    state->position.x += delta_position.x;
    Vec3 p_min = Vector3(state->position.x - PLAYER_RADIUS, state->position.y - PLAYER_HEIGHT,
                         state->position.z - PLAYER_RADIUS);
    Vec3 p_max = Vector3(state->position.x + PLAYER_RADIUS, state->position.y + PLAYER_HEAD_CLEARANCE,
                         state->position.z + PLAYER_RADIUS);
    if (OverlapAABB(p_min, p_max, min, max)) {
        state->position.x -= delta_position.x;
    }

    state->position.y += delta_position.y;
    p_min = Vector3(state->position.x - PLAYER_RADIUS, state->position.y - PLAYER_HEIGHT,
                    state->position.z - PLAYER_RADIUS);
    p_max = Vector3(state->position.x + PLAYER_RADIUS, state->position.y + PLAYER_HEAD_CLEARANCE,
                    state->position.z + PLAYER_RADIUS);
    state->grounded = False;

    if (p_min.y < 0.0f) {
        state->position.y = 0.0f + PLAYER_HEIGHT;
        state->velocity.y = 0.0f;
        state->grounded = True;
    } else if (OverlapAABB(p_min, p_max, min, max)) {
        if (delta_position.y < 0) {
            state->position.y = max.y + PLAYER_HEIGHT;
            state->grounded = True;
        } else {
            state->position.y = min.y - PLAYER_HEAD_CLEARANCE;
        }
        state->velocity.y = 0.0f;
    }

    state->position.z += delta_position.z;
    p_min = Vector3(state->position.x - PLAYER_RADIUS, state->position.y - PLAYER_HEIGHT,
                    state->position.z - PLAYER_RADIUS);
    p_max = Vector3(state->position.x + PLAYER_RADIUS, state->position.y + PLAYER_HEAD_CLEARANCE,
                    state->position.z + PLAYER_RADIUS);
    if (OverlapAABB(p_min, p_max, min, max)) {
        state->position.z -= delta_position.z;
    }

    const Vec3 target = Vec3_Add(state->position, forward);
    const Mat4X4 view = Matrix_LookAt(state->position, target, Vector3(0, 1, 0));
    const Mat4X4 projection = Matrix_Perspective(PI / 3.0f, platform->width / platform->height, 0.1f, 100.0f);

    platform->view_projection = Matrix_Multiply(projection, view);

    Game_PushMeshRenderEntry(platform, Matrix_Translation(center.x, center.y, center.z), TEXTURE_HANDLE_MAGIC_PIXEL);
    Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                              "%.1fms", platform->frame_time_ms);
}
