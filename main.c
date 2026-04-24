#include <SDL3/SDL.h>

int main(void) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Game", 1280, 720, 0);

    SDL_GPUDevice *device = SDL_CreateGPUDevice(/* TODO: Use SDL_Shadercross */ SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!device) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    bool running = true;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: {
                    running = false;
                } break;

                default: break;
            }
        }

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        if (!command_buffer) {
            SDL_Log("%s", SDL_GetError());

            continue;
        }

        SDL_GPUTexture *swapchain_texture;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL);

        if (swapchain_texture) {
            SDL_GPUColorTargetInfo color_target_info = {
                .texture = swapchain_texture,
                .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
            };

            SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1, NULL);
            SDL_EndGPURenderPass(render_pass);
        }

        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    return 0;
}