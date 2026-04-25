#include "Game_platform.h"

typedef struct {
    float rotation;

    Bool initialized;
} State;

void UpdateAndRender(Game_Platform *platform, const float delta_time) {
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        state->rotation = 0.0f;

        // this must always be here!
        state->initialized = True;
    }

    if (IsDown(platform->input[GAME_KEY_A])) {
        state->rotation -= 2.0f * delta_time;
    }
    if (IsDown(platform->input[GAME_KEY_D])) {
        state->rotation += 2.0f * delta_time;
    }

    Game_PushRenderEntry(platform, Matrix_RotationY(state->rotation));
}
