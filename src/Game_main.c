#include "Game_platform.h"

typedef struct {
    float rotation;

    Game_FontHandle debug_font;
    Bool initialized;
} State;

void UpdateAndRender(Game_Platform *platform, const float delta_time) {
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        state->debug_font = platform->LoadFontFile("jetbrains_mono.ttf", 32.0f);

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

    Game_PushMeshRenderEntry(platform, Matrix_RotationY(state->rotation), TEXTURE_HANDLE_MAGIC_PIXEL);
    Game_PushTextRenderEntry(platform, state->debug_font, 0.0f, 0.0f, "Blah ttchef the goat\n");
}
