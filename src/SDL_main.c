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

/// standard vertex buffer description for the Vertex structure (see `Game_math.h`)
static SDL_GPUVertexBufferDescription vertex_buffer_description;
/// standard vertex attributes for the Vertex structure (see `Game_math.h.`)
static SDL_GPUVertexAttribute vertex_attributes[4];
static SDL_GPUTextureFormat swapchain_texture_format;

static SDL_GPUTexture *msaa_texture;
static SDL_GPUTexture *depth_texture;

static SDL_GPUSampler *texture_sampler;

static SDL_GPUBuffer *vertex_buffer;
static SDL_GPUBuffer *index_buffer;

static SDL_GPUDevice *device;

static SDL_GPUTexture *textures[256];
static int texture_count;

static SDL_GPUGraphicsPipeline *pipelines[64];
static int pipeline_count;

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


static void FlushRenderEntries(const Game_Platform *platform, SDL_GPUCommandBuffer *command_buffer,
                               SDL_Window *window) {
    if (platform->render_entry_count == 0) {
        return;
    }
    // the current render pass
    SDL_GPURenderPass *render_pass = NULL;

    for (int i = 0; i < platform->render_entry_count; ++i) {
        const Game_RenderEntry *entry = (Game_RenderEntry *) &platform->render_entries[i];

        switch (entry->type) {
            case GAME_RENDER_ENTRY_MESH: {
                if (!render_pass) {
                    break;
                }

                SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
                                             .buffer = vertex_buffer, .offset = 0,
                                         }, 1);
                SDL_BindGPUIndexBuffer(render_pass, &(SDL_GPUBufferBinding){
                                           .buffer = index_buffer, .offset = 0,
                                       }, SDL_GPU_INDEXELEMENTSIZE_16BIT);

                const struct Game_RenderEntry_Mesh *mesh = &entry->mesh;
                if (mesh->pipeline_handle > 0 && mesh->pipeline_handle <= pipeline_count) {
                    SDL_BindGPUGraphicsPipeline(render_pass, pipelines[mesh->pipeline_handle - 1]);

                    SDL_GPUTextureSamplerBinding sampler_bindings[4];
                    for (int j = 0; j < 4; ++j) {
                        const int texture = (mesh->texture_handles[j] > 0) ? (int) (mesh->texture_handles[j] - 1) : 0;

                        sampler_bindings[j].texture = textures[texture];
                        sampler_bindings[j].sampler = texture_sampler;
                    }
                    SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, 4);

                    Mat4X4 mvp = Matrix_Multiply(platform->view_projection, mesh->transform);
                    SDL_PushGPUVertexUniformData(command_buffer, 0, &mvp, sizeof(Mat4X4));

                    if (mesh->fragment_uniform_count > 0) {
                        SDL_PushGPUFragmentUniformData(command_buffer, 0,
                                                       mesh->fragment_uniforms,
                                                       (Uint32) (mesh->fragment_uniform_count * sizeof(float)));
                    }

                    SDL_DrawGPUIndexedPrimitives(render_pass, mesh->index_count, 1, mesh->index_offset,
                                                 mesh->vertex_offset, 0);
                }
            }
            break;
            case GAME_RENDER_ENTRY_SET_TARGET: {
                if (render_pass) {
                    SDL_EndGPURenderPass(render_pass);
                    render_pass = NULL;
                }

                SDL_GPUColorTargetInfo color_target_info = {
                    .load_op = entry->set_target.clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
                    .store_op = SDL_GPU_STOREOP_STORE,
                    .clear_color = (SDL_FColor){
                        entry->set_target.clear_color.x, entry->set_target.clear_color.y,
                        entry->set_target.clear_color.z, entry->set_target.clear_color.w
                    },
                };

                const SDL_GPUDepthStencilTargetInfo *depth_stencil_target_info_ptr = NULL;
                SDL_GPUDepthStencilTargetInfo depth_stencil_target_info = {0};

                if (entry->set_target.color_target == 0) {
                    SDL_GPUTexture *swapchain_texture;
                    if (SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL) &&
                        swapchain_texture) {
                        color_target_info.texture = msaa_texture;
                        color_target_info.resolve_texture = swapchain_texture;
                        color_target_info.store_op = SDL_GPU_STOREOP_RESOLVE;
                        // attach depth buffer to the main pass
                        depth_stencil_target_info.texture = depth_texture;
                        depth_stencil_target_info.load_op = entry->set_target.clear
                                                                ? SDL_GPU_LOADOP_CLEAR
                                                                : SDL_GPU_LOADOP_LOAD;
                        depth_stencil_target_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
                        depth_stencil_target_info.clear_depth = 0.0f;
                        depth_stencil_target_info_ptr = &depth_stencil_target_info;
                    } else {
                        continue;
                    }
                } else {
                    const int texture = (int) entry->set_target.color_target - 1;

                    if (texture >= 0 && texture < texture_count) {
                        color_target_info.texture = textures[texture];
                    } else {
                        continue;
                    }
                }

                render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1,
                                                     depth_stencil_target_info_ptr);
            }
            break;
            case GAME_RENDER_ENTRY_FULLSCREEN_QUAD: {
                if (!render_pass) {
                    break;
                }

                const struct Game_RenderEntry_FullscreenQuad *fullscreen_quad = &entry->fullscreen_quad;
                if (fullscreen_quad->pipeline_handle > 0 && fullscreen_quad->pipeline_handle <= pipeline_count) {
                    SDL_BindGPUGraphicsPipeline(render_pass, pipelines[fullscreen_quad->pipeline_handle - 1]);

                    SDL_GPUTextureSamplerBinding sampler_bindings[4];
                    for (int j = 0; j < 4; ++j) {
                        const int texture = (fullscreen_quad->input_textures[j] > 0)
                                                ? (int) (fullscreen_quad->input_textures[j] - 1)
                                                : 0;

                        sampler_bindings[j].texture = textures[texture];
                        sampler_bindings[j].sampler = texture_sampler;
                    }
                    SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, 4);
                    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
                }
            }
            break;
        }
    }

    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
    }
}

static Game_Key SDLKeyToGameKey(const SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_W: return GAME_KEY_W;
        case SDL_SCANCODE_A: return GAME_KEY_A;
        case SDL_SCANCODE_S: return GAME_KEY_S;
        case SDL_SCANCODE_D: return GAME_KEY_D;
        case SDL_SCANCODE_ESCAPE: return GAME_KEY_ESCAPE;
        case SDL_SCANCODE_SPACE: return GAME_KEY_SPACE;
        case SDL_SCANCODE_LCTRL: return GAME_KEY_LEFT_CTRL;
        case SDL_SCANCODE_F1: return GAME_KEY_F1;
        case SDL_SCANCODE_F2: return GAME_KEY_F2;
        case SDL_SCANCODE_F3: return GAME_KEY_F3;
        case SDL_SCANCODE_F4: return GAME_KEY_F4;
        case SDL_SCANCODE_F5: return GAME_KEY_F5;
        case SDL_SCANCODE_F6: return GAME_KEY_F6;
        case SDL_SCANCODE_F7: return GAME_KEY_F7;
        case SDL_SCANCODE_F8: return GAME_KEY_F8;
        case SDL_SCANCODE_F9: return GAME_KEY_F9;
        case SDL_SCANCODE_F10: return GAME_KEY_F10;
        case SDL_SCANCODE_F11: return GAME_KEY_F11;

        default: return GAME_KEY_NONE;
    }
}

// Pipeline builder

typedef struct {
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexInputState vertex_input;
    SDL_GPUPrimitiveType primitive_type;
    SDL_GPURasterizerState rasterizer;
    SDL_GPUMultisampleState multisample;
    SDL_GPUDepthStencilState depth_stencil;
    SDL_GPUColorTargetBlendState blend_state;
    bool has_depth_target;
    SDL_GPUTextureFormat depth_format;
    SDL_GPUTextureFormat color_format;
} PipelineBuilder;

static PipelineBuilder BeginPipeline(void) {
    PipelineBuilder b = {0};

    b.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    b.rasterizer.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    b.rasterizer.cull_mode = SDL_GPU_CULLMODE_BACK;

    b.multisample.sample_count = SDL_GPU_SAMPLECOUNT_4;

    b.depth_stencil.enable_depth_test = true;
    b.depth_stencil.enable_depth_write = true;
    b.depth_stencil.compare_op = SDL_GPU_COMPAREOP_GREATER;

    b.has_depth_target = true;
    b.depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    b.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    b.blend_state.color_write_mask = 0xF;

    return b;
}

static void PipelineSetShaders(PipelineBuilder *b, SDL_GPUShader *vertex_shader, SDL_GPUShader *fragment_shader) {
    b->vertex_shader = vertex_shader;
    b->fragment_shader = fragment_shader;
}

static void PipelineSetVertexInput(PipelineBuilder *b,
                                   const SDL_GPUVertexBufferDescription *vertex_buffer_descriptions,
                                   const Uint32 num_vertex_buffers,
                                   const SDL_GPUVertexAttribute *vertex_attributes,
                                   const Uint32 num_vertex_attributes) {
    b->vertex_input.vertex_buffer_descriptions = vertex_buffer_descriptions;
    b->vertex_input.num_vertex_buffers = num_vertex_buffers;
    b->vertex_input.vertex_attributes = vertex_attributes;
    b->vertex_input.num_vertex_attributes = num_vertex_attributes;
}

static void PipelineSetTargetFormat(PipelineBuilder *b, const SDL_GPUTextureFormat color_format,
                                    const SDL_GPUTextureFormat depth_format) {
    b->color_format = color_format;
    b->depth_format = depth_format;
}

static void PipelineSetCullMode(PipelineBuilder *b, const SDL_GPUCullMode cull_mode) {
    b->rasterizer.cull_mode = cull_mode;
}

static SDL_GPUGraphicsPipeline *EndPipeline(const PipelineBuilder *b) {
    SDL_GPUColorTargetDescription color_target_description = {
        .format = b->color_format,
        .blend_state = b->blend_state,
    };

    const SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {
        .vertex_shader = b->vertex_shader,
        .fragment_shader = b->fragment_shader,
        .vertex_input_state = b->vertex_input,
        .primitive_type = b->primitive_type,
        .rasterizer_state = b->rasterizer,
        .multisample_state = b->multisample,
        .depth_stencil_state = b->depth_stencil,
        .target_info = {
            .has_depth_stencil_target = b->has_depth_target,
            .depth_stencil_format = b->depth_format,
            .num_color_targets = 1,
            .color_target_descriptions = &color_target_description,
        }
    };

    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_create_info);
    if (!pipeline) {
        SDL_Log("%s", SDL_GetError());
    }
    return pipeline;
}

// Platform API implementation

static Game_TextureHandle Platform_LoadImageFile(const char *path) {
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

static void Platform_FreeFileMemory(void *memory) {
    if (memory) {
        SDL_free(memory);
    }
}

static Game_FileResult Platform_ReadEntireFile(const char *path) {
    Game_FileResult result = {0};

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (io) {
        const Sint64 size = SDL_GetIOSize(io);

        if (size > 0) {
            // allocate +1 for a null terminator just in case it is used
            result.contents = SDL_malloc((size_t) size + 1);

            if (result.contents) {
                const size_t bytes_read = SDL_ReadIO(io, result.contents, (size_t) size);

                if (bytes_read == (size_t) size) {
                    result.contents_size = (unsigned int) size;
                    ((char *) result.contents)[size] = '\0';
                } else {
                    Platform_FreeFileMemory(result.contents);

                    result.contents = NULL;
                    result.contents_size = 0;
                }
            }
            SDL_CloseIO(io);
        }
    }

    return result;
}

static Game_PipelineHandle Platform_CreatePipeline(const char *vertex_spirv_path, const char *fragment_spirv_path,
                                                   const Game_CullMode cull_mode, const Game_BlendMode blend_mode) {
    if (pipeline_count >= ArrayCount(pipelines)) {
        return 0;
    }

    SDL_GPUShader *vertex_shader = CreateGPUShader(vertex_spirv_path, SDL_SHADERCROSS_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fragment_shader = CreateGPUShader(fragment_spirv_path, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);

    if (!vertex_shader || !fragment_shader) {
        return 0;
    }

    PipelineBuilder builder = BeginPipeline();
    PipelineSetShaders(&builder, vertex_shader, fragment_shader);
    PipelineSetCullMode(&builder, cull_mode == GAME_CULL_MODE_BACK ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE);
    PipelineSetVertexInput(&builder, &vertex_buffer_description, 1, vertex_attributes, 4);
    if (blend_mode == GAME_BLEND_MODE_ALPHA) {
        builder.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        builder.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        builder.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        builder.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }
    // TODO: check for support
    PipelineSetTargetFormat(&builder, swapchain_texture_format, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);

    SDL_GPUGraphicsPipeline *pipeline = EndPipeline(&builder);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);

    if (!pipeline) {
        return 0;
    }

    pipelines[pipeline_count] = pipeline;
    // 1 based
    return ++pipeline_count;
}

static Game_TextureHandle Platform_CreateRenderTarget(const Vec2 size) {
    if (texture_count >= ArrayCount(textures)) {
        return 0;
    }

    SDL_GPUTextureCreateInfo texture_create_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (Uint32) size.x,
        .height = (Uint32) size.y,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_create_info);
    if (!texture) {
        return 0;
    }

    textures[texture_count] = texture;
    return ++texture_count;
}

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
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

    SDL_Log("SDL_GPU driver: %s\n", SDL_GetGPUDeviceDriver(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    vertex_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                            .size = Megabytes(1),
                                        });

    index_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                           .usage = SDL_GPU_BUFFERUSAGE_INDEX,
                                           .size = Megabytes(1),
                                       });

    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(
        device, &(SDL_GPUTransferBufferCreateInfo){
            .usage =
            SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = Megabytes(1) + Megabytes(1),
        });

    texture_sampler = SDL_CreateGPUSampler(device, &(SDL_GPUSamplerCreateInfo){
                                               .min_filter = SDL_GPU_FILTER_NEAREST,
                                               .mag_filter = SDL_GPU_FILTER_NEAREST,
                                               .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
                                               .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                               .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                               .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                           });

    swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(device, window);

    vertex_buffer_description = (SDL_GPUVertexBufferDescription){
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    SDL_memset(&vertex_attributes, 0, sizeof(vertex_attributes));

    vertex_attributes[0] = (SDL_GPUVertexAttribute){
        .location = 0,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        .offset = 0, // x, y, z
    };
    vertex_attributes[1] = (SDL_GPUVertexAttribute){
        .location = 1,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        .offset = sizeof(float) * 3, // nx, ny, nz
    };
    vertex_attributes[2] = (SDL_GPUVertexAttribute){
        .location = 2,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
        .offset = sizeof(float) * 6, // r, g, b, a
    };
    vertex_attributes[3] = (SDL_GPUVertexAttribute){
        .location = 3,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        .offset = sizeof(float) * 10, // u, v
    };

    textures[TEXTURE_HANDLE_MAGIC_PIXEL] = CreateMagicPixel();
    texture_count = 1; // index 0 is now reserved

    int permanent_storage_size = Megabytes(64);
    void *permanent_storage = SDL_malloc(permanent_storage_size);
    SDL_memset(permanent_storage, 0, permanent_storage_size);

    int transient_storage_size = Megabytes(256);
    void *transient_storage = SDL_malloc(transient_storage_size);
    SDL_memset(transient_storage, 0, transient_storage_size);

    Game_Platform platform = {
        .LoadImageFile = Platform_LoadImageFile,
        .CreateRenderTarget = Platform_CreateRenderTarget,
        .ReadEntireFile = Platform_ReadEntireFile,
        .FreeFileMemory = Platform_FreeFileMemory,
        .CreatePipeline = Platform_CreatePipeline,
    };

    InitializeArena(&platform.permanent_memory, permanent_storage, permanent_storage_size);
    InitializeArena(&platform.transient_memory, transient_storage, transient_storage_size);

    GameCode game_code = LoadGameCode(GAME_LIB_PATH);

    SDL_AudioSpec audio_spec = {
        .format = SDL_AUDIO_S16LE,
        .channels = 2,
        .freq = 48000,
    };

    SDL_AudioStream *audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);

    if (!audio_stream) {
        SDL_Log("%s", SDL_GetError());
    } else {
        // since the streams are opened paused
        SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(audio_stream));
    }

    int max_audio_samples = audio_spec.freq * audio_spec.channels;
    short *audio_backing_buffer = SDL_malloc(max_audio_samples * sizeof(short));
    SDL_memset(audio_backing_buffer, 0, max_audio_samples * sizeof(short));

    depth_texture = NULL;
    int depth_texture_width = 0, depth_texture_height = 0;
    msaa_texture = NULL;
    SDL_GPUSampleCount sample_count = SDL_GPU_SAMPLECOUNT_4;

    Uint64 last_counter = SDL_GetPerformanceCounter();
    Uint64 performance_frequency = SDL_GetPerformanceFrequency();

    float frame_time_accumulator = 0.0f;
    int frame_count = 0;

    bool running = true;
    while (running) {
        platform.transient_memory.used = 0;

        for (int i = 0; i < GAME_KEY_COUNT; ++i) {
            platform.input[i].half_transition_count = 0;
        }

        platform.mouse_delta_x = 0.0f, platform.mouse_delta_y = 0.0f;

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

                case SDL_EVENT_WINDOW_FOCUS_LOST: {
                    platform.mouse_locked = false;
                    SDL_SetWindowRelativeMouseMode(window, false);
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

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    Game_Key key = GAME_KEY_NONE;
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        key = GAME_KEY_MOUSE_LEFT;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        key = GAME_KEY_MOUSE_RIGHT;
                    }

                    if (key != GAME_KEY_NONE) {
                        Game_ButtonState *button = &platform.input[key];
                        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                            button->ended_down = true;
                        } else {
                            button->ended_down = false;
                        }
                        button->half_transition_count++;
                    }
                }
                break;

                case SDL_EVENT_MOUSE_MOTION: {
                    platform.mouse_delta_x += event.motion.xrel, platform.mouse_delta_y += event.motion.yrel;
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
        platform.delta_time = (float) (current_counter - last_counter) / (float) performance_frequency;
        last_counter = current_counter;

        platform.elapsed_time += platform.delta_time;

        frame_time_accumulator += platform.delta_time;
        frame_count++;

        if (frame_time_accumulator >= 0.5f) {
            platform.frame_time_ms = (frame_time_accumulator * 1000.0f) / (float) frame_count;
            frame_time_accumulator = 0.0f;
            frame_count = 0;
        }

        if (width != depth_texture_width || height != depth_texture_height) {
            if (depth_texture) {
                SDL_ReleaseGPUTexture(device, depth_texture);
            }
            if (msaa_texture) {
                SDL_ReleaseGPUTexture(device, msaa_texture);
            }

            depth_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo){
                                                     .type = SDL_GPU_TEXTURETYPE_2D,
                                                     .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
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

        platform.width = (float) width;
        platform.height = (float) height;

        platform.render_entry_count = 0;

        platform.sound_buffer.samples = audio_backing_buffer;
        platform.sound_buffer.samples_per_second = audio_spec.freq;
        platform.sound_buffer.channels = audio_spec.channels;
        platform.sound_buffer.sample_count = 0;

        if (audio_stream) {
            int target_queue_bytes = (int) (audio_spec.freq * audio_spec.channels * sizeof(short)) / 15;
            int queued_bytes = SDL_GetAudioStreamQueued(audio_stream);
            int bytes_to_write = target_queue_bytes - queued_bytes;

            // don't ask for negative samples just in case there's an overshot
            if (bytes_to_write < 0) {
                bytes_to_write = 0;
            }

            int sample_count_to_write = bytes_to_write / (audio_spec.channels * (int) sizeof(short));
            platform.sound_buffer.sample_count = sample_count_to_write;
        }

        if (game_code.update_and_render) {
            game_code.update_and_render(&platform);
        }

        if (audio_stream && platform.sound_buffer.sample_count > 0) {
            int bytes_written = platform.sound_buffer.sample_count * audio_spec.channels * (int) sizeof(short);
            SDL_PutAudioStreamData(audio_stream, platform.sound_buffer.samples, bytes_written);
        }

        SDL_SetWindowRelativeMouseMode(window, platform.mouse_locked);
        SDL_SetWindowFullscreen(window, platform.fullscreen);

        int current_transient_vertex_count = platform.transient_vertex_count;
        int current_transient_index_count = platform.transient_index_count;
        platform.transient_vertex_count = 0;
        platform.transient_index_count = 0;

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        if (!command_buffer) {
            SDL_Log("%s", SDL_GetError());

            continue;
        }

        if (current_transient_vertex_count > 0 && current_transient_index_count > 0) {
            size_t vertex_buffer_size = current_transient_vertex_count * sizeof(Vertex);
            size_t index_buffer_size = current_transient_index_count * sizeof(Uint16);

            Uint8 *dest = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
            SDL_memcpy(dest, platform.transient_vertices, vertex_buffer_size);
            SDL_memcpy(dest + vertex_buffer_size, platform.transient_indices, index_buffer_size);
            SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

            SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
            SDL_UploadToGPUBuffer(copy_pass, &(SDL_GPUTransferBufferLocation){
                                      .transfer_buffer = transfer_buffer,
                                      .offset = 0
                                  }, &(SDL_GPUBufferRegion){
                                      .buffer = vertex_buffer,
                                      .offset = 0,
                                      .size = vertex_buffer_size
                                  }, true);
            SDL_UploadToGPUBuffer(copy_pass, &(SDL_GPUTransferBufferLocation){
                                      .transfer_buffer = transfer_buffer,
                                      .offset = (Uint32) vertex_buffer_size
                                  }, &(SDL_GPUBufferRegion){
                                      .buffer = index_buffer,
                                      .offset = 0,
                                      .size = index_buffer_size,
                                  }, true);
            SDL_EndGPUCopyPass(copy_pass);
        }

        FlushRenderEntries(&platform, command_buffer, window);

        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    SDL_free(permanent_storage);
    SDL_free(transient_storage);
    if (depth_texture) {
        SDL_ReleaseGPUTexture(device, depth_texture);
    }
    if (msaa_texture) {
        SDL_ReleaseGPUTexture(device, msaa_texture);
    }
    for (int i = 0; i < texture_count; ++i) {
        SDL_ReleaseGPUTexture(device, textures[i]);
    }
    if (texture_sampler) {
        SDL_ReleaseGPUSampler(device, texture_sampler);
    }
    for (int i = 0; i < pipeline_count; ++i) {
        SDL_ReleaseGPUGraphicsPipeline(device, pipelines[i]);
    }
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseGPUBuffer(device, index_buffer);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_ShaderCross_Quit();
    SDL_Quit();

    return 0;
}
