#include "Game_platform.h"

typedef struct {
    float rotation;

    Bool initialized;
} State;

void UpdateAndRender(Game_Platform *platform, const float delta_time) {
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        // this must always be here!
        state->initialized = True;
    }

    state->rotation += 1.0f * delta_time;

    Game_PushRenderEntry(platform, Matrix_RotationY(state->rotation));
}
