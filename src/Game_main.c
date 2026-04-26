#include "Game_platform.h"

typedef struct {
    Vec3 position;
    float pitch, yaw;

    Game_FontHandle debug_font;
    Bool initialized;
} State;

void UpdateAndRender(Game_Platform *platform) {
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        state->debug_font = platform->LoadFontFile("jetbrains_mono.ttf", 24.0f);

        state->position = Vector3(0, 0, 5);
        state->yaw = 0.0f;
        state->pitch = 0.0f;

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

        state->yaw += platform->mouse_delta_x * 0.002f;
        state->pitch -= platform->mouse_delta_y * 0.002f;

        state->pitch = Clamp(state->pitch, -(PI / 2.0f - 0.1f), PI / 2.0f - 0.1f);
    }

    Vec3 forward = Vector3(-Cos(state->pitch) * Sin(state->yaw), Sin(state->pitch),
                           -Cos(state->pitch) * Cos(state->yaw));
    forward = Vec3_Normalize(forward);

    Vec3 right = Vector3(Cos(state->yaw), 0, -Sin(state->yaw));
    right = Vec3_Normalize(right);

    const Vec3 up = Vector3(0, 1, 0);

    const float speed = 5.0f * platform->delta_time;

    if (IsDown(platform->input[GAME_KEY_W])) {
        state->position = Vec3_Add(state->position, Vec3_Scale(forward, speed));
    }
    if (IsDown(platform->input[GAME_KEY_S])) {
        state->position = Vec3_Sub(state->position, Vec3_Scale(forward, speed));
    }
    if (IsDown(platform->input[GAME_KEY_A])) {
        state->position = Vec3_Add(state->position, Vec3_Scale(right, speed));
    }
    if (IsDown(platform->input[GAME_KEY_D])) {
        state->position = Vec3_Sub(state->position, Vec3_Scale(right, speed));
    }
    if (IsDown(platform->input[GAME_KEY_SPACE])) {
        state->position = Vec3_Add(state->position, Vec3_Scale(up, speed));
    }
    if (IsDown(platform->input[GAME_KEY_LEFT_CTRL])) {
        state->position = Vec3_Sub(state->position, Vec3_Scale(up, speed));
    }

    const Vec3 target = Vec3_Add(state->position, forward);
    const Mat4X4 view = Matrix_LookAt(state->position, target, Vector3(0, 1, 0));
    const Mat4X4 projection = Matrix_Perspective(PI / 4.0f, platform->width / platform->height, 0.1f, 100.0f);

    platform->view_projection = Matrix_Multiply(projection, view);

    Game_PushMeshRenderEntry(platform, Matrix_Translation(0, 0, 0), TEXTURE_HANDLE_MAGIC_PIXEL);
    Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                              "%.1fms", platform->frame_time_ms);
}
