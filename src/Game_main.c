#include "Game_platform.h"

static float time = 0.0f;

static void UpdateAndRender(Game_Platform *platform, const float delta_time) {
    time += delta_time;

    Game_PushRenderEntry(platform, Matrix_RotationY(time));
}