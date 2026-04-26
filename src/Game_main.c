#include "Game_platform.h"

typedef struct {
    float rotation;

    Game_FontHandle debug_font;
    Bool initialized;
} State;

void UpdateAndRender(Game_Platform *platform) {
    State *state = platform->permanent_storage;

    if (!state->initialized) {
        state->debug_font = platform->LoadFontFile("jetbrains_mono.ttf", 24.0f);

        state->rotation = 0.0f;
        // this must always be here!
        state->initialized = True;
    }

    if (IsDown(platform->input[GAME_KEY_A])) {
        state->rotation -= 2.0f * platform->delta_time;
    }
    if (IsDown(platform->input[GAME_KEY_D])) {
        state->rotation += 2.0f * platform->delta_time;
    }

    Game_PushMeshRenderEntry(platform, Matrix_RotationY(state->rotation), TEXTURE_HANDLE_MAGIC_PIXEL);
    Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                              "%.1fms", platform->frame_time_ms);
}
