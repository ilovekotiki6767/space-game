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

    if (!platform->mouse_locked) {
        if (WasPressed(platform->input[GAME_KEY_MOUSE_LEFT])) {
            platform->mouse_locked = True;
        }
    } else {
        if (WasPressed(platform->input[GAME_KEY_ESCAPE])) {
            platform->mouse_locked = False;
        }

        state->rotation += platform->mouse_delta_x * 0.001f;
    }

    Game_PushMeshRenderEntry(platform, Matrix_RotationY(state->rotation), TEXTURE_HANDLE_MAGIC_PIXEL);
    Game_PushTextRenderEntryF(platform, state->debug_font, 10.0f, 10.0f,
                              "%.1fms", platform->frame_time_ms);
}
