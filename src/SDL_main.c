// entrypoint and the implementation of the SDL platform backend

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include "Game_math.h"
#include "Game_platform.h"

#if defined(SDL_PLATFORM_WINDOWS)
#define GAME_LIB_PATH "game.dll"
#elif defined(SDL_PLATFORM_MACOS)
#define GAME_LIB_PATH "./game.dylib"
#else
#define GAME_LIB_PATH "./game.so"
#endif

static SDL_GPUDevice *device;

static SDL_GPUTexture *textures[256];
static int texture_count;

typedef struct {
    void *handle;
    Game_UpdateAndRender_Func update_and_render;
    Uint64 last_write_time;
} GameCode;

static Uint64 GetGameCodeWriteTime(const char *path) {
    SDL_PathInfo info;

    if (SDL_GetPathInfo(path, &info)) {
        return info.modify_time;
    }

    return 0;
}

static GameCode LoadGameCode(const char *path) {
    GameCode code = {
        .handle = SDL_LoadObject(path),
        .last_write_time = GetGameCodeWriteTime(path),
    };

    if (code.handle) {
        code.update_and_render = (Game_UpdateAndRender_Func) SDL_LoadFunction(code.handle, "UpdateAndRender");
    }

    if (!code.update_and_render) {
        SDL_Log("%s", SDL_GetError());
    }

    return code;
}

static void UnloadGameCode(GameCode *code) {
    if (code->handle) {
        SDL_UnloadObject(code->handle);
        code->handle = NULL;
        code->update_and_render = NULL;
    }
}

Vertex vertices[] = {
    {-1, -1, 1, 1, 0, 0, 1, 0, 1}, {1, -1, 1, 1, 0, 0, 1, 1, 1}, {1, 1, 1, 1, 0, 0, 1, 1, 0},
    {-1, 1, 1, 1, 0, 0, 1, 0, 0},
    {1, -1, -1, 0, 1, 0, 1, 0, 1}, {-1, -1, -1, 0, 1, 0, 1, 1, 1}, {-1, 1, -1, 0, 1, 0, 1, 1, 0},
    {1, 1, -1, 0, 1, 0, 1, 0, 0},
    {-1, 1, -1, 0, 0, 1, 1, 0, 0}, {-1, 1, 1, 0, 0, 1, 1, 0, 1}, {1, 1, 1, 0, 0, 1, 1, 1, 1},
    {1, 1, -1, 0, 0, 1, 1, 1, 0},
    {-1, -1, -1, 1, 1, 0, 1, 0, 1}, {1, -1, -1, 1, 1, 0, 1, 1, 1}, {1, -1, 1, 1, 1, 0, 1, 1, 0},
    {-1, -1, 1, 1, 1, 0, 1, 0, 0},
    {1, -1, -1, 1, 0, 1, 1, 1, 1}, {1, 1, -1, 1, 0, 1, 1, 1, 0}, {1, 1, 1, 1, 0, 1, 1, 0, 0},
    {1, -1, 1, 1, 0, 1, 1, 0, 1},
    {-1, -1, -1, 0, 1, 1, 1, 0, 1}, {-1, -1, 1, 0, 1, 1, 1, 1, 1}, {-1, 1, 1, 0, 1, 1, 1, 1, 0},
    {-1, 1, -1, 0, 1, 1, 1, 0, 0}
};

Uint16 indices[] = {
    0, 1, 2, 0, 2, 3, // front
    4, 5, 6, 4, 6, 7, // back
    8, 9, 10, 8, 10, 11, // top
    12, 13, 14, 12, 14, 15, // bottom
    16, 17, 18, 16, 18, 19, // right
    20, 21, 22, 20, 22, 23 // left
};

SDL_GPUShader *CreateGPUShader(const char *filepath, const SDL_ShaderCross_ShaderStage stage) {
    SDL_IOStream *io = SDL_IOFromFile(filepath, "rb");
    if (!io) {
        SDL_Log("%s", SDL_GetError());

        return NULL;
    }

    const Sint64 code_size = SDL_GetIOSize(io);

    Uint8 *code = SDL_malloc(code_size);
    SDL_ReadIO(io, code, code_size);
    SDL_CloseIO(io);

    SDL_ShaderCross_GraphicsShaderMetadata *metadata = SDL_ShaderCross_ReflectGraphicsSPIRV(code, code_size, 0);
    if (!metadata) {
        SDL_Log("%s", SDL_GetError());

        SDL_free(code);

        return NULL;
    }

    const SDL_ShaderCross_SPIRV_Info spirv_info = {
        .bytecode = code,
        .bytecode_size = code_size,
        .entrypoint = "main",
        .shader_stage = stage,
    };

    SDL_GPUShader *shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        device, &spirv_info, &metadata->resource_info, 0);

    SDL_free(metadata);
    SDL_free(code);

    if (!shader) {
        SDL_Log("%s", SDL_GetError());

        return NULL;
    }

    return shader;
}

static SDL_GPUTexture *CreateMagicPixel(void) {
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo){
                                                       .type = SDL_GPU_TEXTURETYPE_2D,
                                                       .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                       .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                                       .width = 1,
                                                       .height = 1,
                                                       .layer_count_or_depth = 1,
                                                       .num_levels = 1,
                                                       .sample_count = SDL_GPU_SAMPLECOUNT_1,
                                                   });

    if (!texture) {
        SDL_Log("%s", SDL_GetError());

        return NULL;
    }

    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(device, &(SDL_GPUTransferBufferCreateInfo){
                                                                             .usage =
                                                                             SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                                             .size = 4,
                                                                         });

    Uint8 *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    data[0] = 255;
    data[1] = 255;
    data[2] = 255;
    data[3] = 255;
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);

    SDL_UploadToGPUTexture(copy_pass,
                           &(SDL_GPUTextureTransferInfo){
                               .transfer_buffer = transfer_buffer,
                               .offset = 0,
                               .pixels_per_row = 0,
                               .rows_per_layer = 0
                           },
                           &(SDL_GPUTextureRegion){
                               .texture = texture,
                               .mip_level = 0,
                               .layer = 0,
                               .x = 0, .y = 0, .z = 0,
                               .w = 1, .h = 1, .d = 1
                           },
                           false);

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(command_buffer);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

    return texture;
}

typedef struct {
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    int index_count;
    SDL_GPUSampler *sampler;
} Render;

static void InitializeRender(Render *render, SDL_GPUGraphicsPipeline *pipeline, SDL_GPUBuffer *vertex_buffer,
                             SDL_GPUBuffer *index_buffer, const int index_count, SDL_GPUSampler *sampler) {
    render->pipeline = pipeline;
    render->vertex_buffer = vertex_buffer;
    render->index_buffer = index_buffer;
    render->index_count = index_count;
    render->sampler = sampler;
}

static void FlushRenderEntries(const Render *render, const Game_Platform *platform,
                               SDL_GPUCommandBuffer *command_buffer,
                               SDL_GPURenderPass *render_pass,
                               const Mat4X4 view_projection) {
    if (platform->render_entry_count == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, render->pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
                                 .buffer = render->vertex_buffer,
                                 .offset = 0,
                             }, 1);
    SDL_BindGPUIndexBuffer(render_pass, &(SDL_GPUBufferBinding){
                               .buffer = render->index_buffer,
                               .offset = 0,
                           }, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    for (int i = 0; i < platform->render_entry_count; ++i) {
        const Game_TextureHandle texture_handle = platform->render_entries[i].texture_handle;

        int texture_index = 0;

        if (texture_handle > 0 && texture_handle <= texture_count) {
            texture_index = (int) texture_handle - 1;
        }

        SDL_BindGPUFragmentSamplers(render_pass, 0, &(SDL_GPUTextureSamplerBinding){
                                        .texture = textures[texture_index],
                                        .sampler = render->sampler,
                                    }, 1);

        Mat4X4 mvp = Matrix_Multiply(view_projection, platform->render_entries[i].transform);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &mvp, sizeof(Mat4X4));
        SDL_DrawGPUIndexedPrimitives(render_pass, render->index_count, 1, 0, 0, 0);
    }
}

static Game_Key SDLKeyToGameKey(const SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_W: return GAME_KEY_W;
        case SDL_SCANCODE_A: return GAME_KEY_A;
        case SDL_SCANCODE_S: return GAME_KEY_S;
        case SDL_SCANCODE_D: return GAME_KEY_D;
        case SDL_SCANCODE_SPACE: return GAME_KEY_SPACE;
        case SDL_SCANCODE_LCTRL: return GAME_KEY_LEFT_CTRL;

        default: return GAME_KEY_NONE;
    }
}

// Platform API implementation

static Game_TextureHandle Platform_LoadImageTexture(const char *path) {
    if (texture_count >= ArrayCount(textures)) {
        return 0;
    }

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);

    int w, h;
    SDL_GPUTexture *texture = IMG_LoadGPUTexture(device, copy_pass, path, &w, &h);

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(command_buffer);

    if (!texture) {
        SDL_Log("%s", SDL_GetError());

        return 0;
    }

    const Game_TextureHandle handle = texture_count + 1;
    textures[texture_count] = texture;
    texture_count++;

    return handle;
}

int main(void) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    if (!SDL_ShaderCross_Init()) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    int width = 1280, height = 720;
    SDL_Window *window = SDL_CreateWindow("Game", width, height, SDL_WINDOW_RESIZABLE);

    device = SDL_CreateGPUDevice(SDL_ShaderCross_GetSPIRVShaderFormats(), true, NULL);
    if (!device) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    SDL_GPUShader *vertex_shader = CreateGPUShader("shaders/vertex.spv", SDL_SHADERCROSS_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fragment_shader = CreateGPUShader("shaders/fragment.spv", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);

    if (!vertex_shader || !fragment_shader) {
        return 1;
        // errors were already logged by CreateGPUShader
    }

    SDL_GPUBuffer *vertex_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                                           .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                                           .size = sizeof(vertices)
                                                       });

    SDL_GPUBuffer *index_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                                          .usage = SDL_GPU_BUFFERUSAGE_INDEX,
                                                          .size = sizeof(indices)
                                                      });

    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(device, &(SDL_GPUTransferBufferCreateInfo){
                                                                             .usage =
                                                                             SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                                             .size = sizeof(vertices) + sizeof(indices),
                                                                         });

    Uint8 *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    SDL_memcpy(data, vertices, sizeof(vertices));
    SDL_memcpy(data + sizeof(vertices), indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    SDL_GPUCommandBuffer *upload_command_buffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(upload_command_buffer);

    SDL_UploadToGPUBuffer(copy_pass, &(SDL_GPUTransferBufferLocation){
                              .transfer_buffer = transfer_buffer,
                              .offset = 0
                          },
                          &(SDL_GPUBufferRegion){
                              .buffer = vertex_buffer,
                              .offset = 0,
                              .size = sizeof(vertices)
                          }, true);

    SDL_UploadToGPUBuffer(copy_pass, &(SDL_GPUTransferBufferLocation){
                              .transfer_buffer = transfer_buffer,
                              .offset = sizeof(vertices)
                          },
                          &(SDL_GPUBufferRegion){
                              .buffer = index_buffer,
                              .offset = 0,
                              .size = sizeof(indices)
                          }, true);

    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_command_buffer);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &(SDL_GPUSamplerCreateInfo){
                                                       .min_filter = SDL_GPU_FILTER_LINEAR,
                                                       .mag_filter = SDL_GPU_FILTER_LINEAR,
                                                       .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
                                                       .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                       .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                       .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                   });

    SDL_GPUVertexBufferDescription vertex_buffer_description = {
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    SDL_GPUVertexAttribute vertex_attributes[3] = {0};
    vertex_attributes[0] = (SDL_GPUVertexAttribute){
        .location = 0,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        .offset = 0,
    };
    vertex_attributes[1] = (SDL_GPUVertexAttribute){
        .location = 1,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
        .offset = sizeof(float) * 3,
    };
    vertex_attributes[2] = (SDL_GPUVertexAttribute){
        .location = 2,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        .offset = sizeof(float) * 7, // position (4) and color (3)
    };

    SDL_GPUColorTargetBlendState blend_state = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .color_write_mask = 0xF
    };

    SDL_GPUTextureFormat swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,

        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &vertex_buffer_description,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertex_attributes,
            .num_vertex_attributes = 3
        },

        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,

        .rasterizer_state = (SDL_GPURasterizerState){
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
        },

        .multisample_state = (SDL_GPUMultisampleState){
            .sample_count = SDL_GPU_SAMPLECOUNT_4,
        },

        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test = true,
            .enable_depth_write = true,
            .compare_op = SDL_GPU_COMPAREOP_LESS,
        },

        .target_info = (SDL_GPUGraphicsPipelineTargetInfo){
            .has_depth_stencil_target = true,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
            .num_color_targets = 1,

            .color_target_descriptions = &(SDL_GPUColorTargetDescription){
                .format = swapchain_texture_format,
                .blend_state = blend_state,
            }
        },
    };

    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_create_info);
    if (!pipeline) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    // now that the pipeline is created the shaders can be released
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);

    Render render = {0};
    InitializeRender(&render, pipeline, vertex_buffer, index_buffer, 36, sampler);

    int permanent_storage_size = Megabytes(64);
    void *permanent_storage = SDL_malloc(permanent_storage_size);
    SDL_memset(permanent_storage, 0, permanent_storage_size);

    textures[TEXTURE_HANDLE_MAGIC_PIXEL] = CreateMagicPixel();
    texture_count = 1; // index 0 is now reserved

    Game_Platform platform = {
        .permanent_storage = permanent_storage,
        .permanent_storage_size = permanent_storage_size,

        .LoadImageTexture = Platform_LoadImageTexture
    };

    GameCode game_code = LoadGameCode(GAME_LIB_PATH);

    SDL_GPUTexture *depth_texture = NULL;
    int depth_texture_width = 0, depth_texture_height = 0;
    SDL_GPUTexture *msaa_texture = NULL;
    SDL_GPUSampleCount sample_count = SDL_GPU_SAMPLECOUNT_4;

    Uint64 last_counter = SDL_GetPerformanceCounter();
    Uint64 performance_frequency = SDL_GetPerformanceFrequency();

    bool running = true;
    while (running) {
        for (int i = 0; i < GAME_KEY_COUNT; ++i) {
            platform.input[i].half_transition_count = 0;
        }

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: {
                    running = false;
                }
                break;

                case SDL_EVENT_WINDOW_RESIZED: {
                    width = event.window.data1, height = event.window.data2;
                }
                break;


                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    if (!event.key.repeat) {
                        Game_Key key = SDLKeyToGameKey(event.key.scancode);

                        if (key != GAME_KEY_NONE) {
                            Game_ButtonState *button = &platform.input[key];

                            if (event.type == SDL_EVENT_KEY_DOWN) {
                                button->ended_down = true;
                            } else {
                                button->ended_down = false;
                            }
                            button->half_transition_count++;
                        }
                    }
                }
                break;
                default: break;
            }
        }

        Uint64 new_write_time = GetGameCodeWriteTime(GAME_LIB_PATH);
        if (new_write_time != game_code.last_write_time) {
            UnloadGameCode(&game_code);
            game_code = LoadGameCode(GAME_LIB_PATH);
        }

        Uint64 current_counter = SDL_GetPerformanceCounter();
        float delta_time = (float) (current_counter - last_counter) / (float) performance_frequency;
        last_counter = current_counter;

        if (width != depth_texture_width || height != depth_texture_height) {
            if (depth_texture) {
                SDL_ReleaseGPUTexture(device, depth_texture);
            }
            if (msaa_texture) {
                SDL_ReleaseGPUTexture(device, msaa_texture);
            }

            depth_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo){
                                                     .type = SDL_GPU_TEXTURETYPE_2D,
                                                     .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                                                     .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                                     .width = width,
                                                     .height = height,
                                                     .layer_count_or_depth = 1,
                                                     .num_levels = 1,
                                                     .sample_count = sample_count,
                                                 });

            msaa_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo){
                                                    .type = SDL_GPU_TEXTURETYPE_2D,
                                                    .format = swapchain_texture_format,
                                                    .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                                    .width = width,
                                                    .height = height,
                                                    .layer_count_or_depth = 1,
                                                    .num_levels = 1,
                                                    .sample_count = sample_count,
                                                });

            depth_texture_width = width, depth_texture_height = height;
        }

        platform.render_entry_count = 0;

        if (game_code.update_and_render) {
            game_code.update_and_render(&platform, delta_time);
        }

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        if (!command_buffer) {
            SDL_Log("%s", SDL_GetError());

            continue;
        }

        SDL_GPUTexture *swapchain_texture;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL)) {
            SDL_Log("%s", SDL_GetError());

            continue;
        }

        if (swapchain_texture) {
            SDL_GPUColorTargetInfo color_target_info = {
                .texture = msaa_texture,
                .resolve_texture = swapchain_texture,
                .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_RESOLVE,
                .cycle = true,
            };

            SDL_GPUDepthStencilTargetInfo depth_stencil_target_info = {
                .texture = depth_texture,
                .clear_depth = 1.0f,
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_DONT_CARE,
                .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
                .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
            };

            SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1,
                                                                    &depth_stencil_target_info);

            Mat4X4 view = Matrix_LookAt(Vector3(0, 0, 5), Vector3(0, 0, 0), Vector3(0, 1, 0));
            Mat4X4 projection = Matrix_Perspective(SDL_PI_F / 4.0f, (float) width / (float) height, 0.1f, 100.0f);
            Mat4X4 view_projection = Matrix_Multiply(projection, view);

            FlushRenderEntries(&render, &platform, command_buffer, render_pass, view_projection);;

            SDL_EndGPURenderPass(render_pass);
        }

        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    SDL_free(permanent_storage);
    if (depth_texture) {
        SDL_ReleaseGPUTexture(device, depth_texture);
    }
    if (msaa_texture) {
        SDL_ReleaseGPUTexture(device, msaa_texture);
    }
    for (int i = 0; i < texture_count; ++i) {
        SDL_ReleaseGPUTexture(device, textures[i]);
    }
    if (sampler) {
        SDL_ReleaseGPUSampler(device, sampler);
    }
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseGPUBuffer(device, index_buffer);;
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_ShaderCross_Quit();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    return 0;
}
