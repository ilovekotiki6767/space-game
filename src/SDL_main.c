// entrypoint and the implementation of the SDL platform backend

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "Game_math.h"
#include "Game_platform.h"

#if defined(SDL_PLATFORM_WINDOWS)
#define GAME_LIB_PATH "game.dll"
#elif defined(SDL_PLATFORM_MACOS)
#define GAME_LIB_PATH "./game.dylib"
#else
#define GAME_LIB_PATH "./game.so"
#endif

typedef struct {
    SDL_GPUTexture *atlas;
    Mat4X4 mvp;
    int index_offset;
    int index_count;
} TextDrawCall;

static SDL_GPUDevice *device;

static SDL_GPUTexture *textures[256];
static int texture_count;

static TTF_Font *fonts[16];
static int font_count;

static float text_vertices[16384 * 4];
static int text_indices[32768];

static TextDrawCall text_draw_calls[256];

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

typedef struct {
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    int index_count;
    SDL_GPUSampler *sampler;
} Render;

typedef struct {
    Mat4X4 mvp;
    Mat4X4 model;
    Vec4 camera;
} MeshVertexUBO;

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
                               SDL_GPURenderPass *render_pass) {
    if (platform->render_entry_count == 0) {
        return;
    }

    const Mat4X4 view_projection = platform->view_projection;
    const Mat4X4 orthographic = Matrix_OrthographicScreen(platform->width, platform->height);

    SDL_BindGPUGraphicsPipeline(render_pass, render->pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
                                 .buffer = render->vertex_buffer,
                                 .offset = 0,
                             }, 1);
    SDL_BindGPUIndexBuffer(render_pass, &(SDL_GPUBufferBinding){
                               .buffer = render->index_buffer,
                               .offset = 0,
                           }, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    const float ubo[4] = {platform->elapsed_time, 0.0f, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(command_buffer, 0, ubo, sizeof(ubo));

    for (int i = 0; i < platform->render_entry_count; ++i) {
        if (platform->render_entries[i].type != GAME_RENDER_ENTRY_MESH) {
            continue;
        }

        SDL_GPUTextureSamplerBinding samplers[4];
        for (int t = 0; t < 4; ++t) {
            int texture_index = 0;

            if (platform->render_entries[i].mesh.texture_handles[t] > 0) {
                texture_index = (int) platform->render_entries[i].mesh.texture_handles[t] - 1;
            } else if (t > 0 && platform->render_entries[i].mesh.texture_handles[0] > 0) {
                texture_index = (int) platform->render_entries[i].mesh.texture_handles[0] - 1;
            }

            samplers[t].texture = textures[texture_index];
            samplers[t].sampler = render->sampler;
        }

        SDL_BindGPUFragmentSamplers(render_pass, 0, samplers, 4);

        const Mat4X4 projection = platform->render_entries[i].mesh.screen_space ? orthographic : view_projection;
        Mat4X4 mvp = Matrix_Multiply(projection, platform->render_entries[i].mesh.transform);

        SDL_PushGPUVertexUniformData(command_buffer, 0, &mvp, sizeof(Mat4X4));
        SDL_DrawGPUIndexedPrimitives(render_pass,
                                     platform->render_entries[i].mesh.index_count, 1,
                                     platform->render_entries[i].mesh.index_offset,
                                     platform->render_entries[i].mesh.vertex_offset, 0);
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

    b.rasterizer.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
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

static void PipelineSetCullMode(PipelineBuilder *b, const SDL_GPUCullMode mode) {
    b->rasterizer.cull_mode = mode;
}

static void PipelineSetBlendState(PipelineBuilder *b, const SDL_GPUColorTargetBlendState blend) {
    b->blend_state = blend;
}

static void PipelineSetDepthState(PipelineBuilder *b, const SDL_GPUDepthStencilState depth) {
    b->depth_stencil = depth;
}

static void PipelineSetTargetFormat(PipelineBuilder *b, const SDL_GPUTextureFormat color_format,
                                    const SDL_GPUTextureFormat depth_format) {
    b->color_format = color_format;
    b->depth_format = depth_format;
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

static Game_FontHandle Platform_LoadFontFile(const char *path, const float size) {
    if (font_count >= ArrayCount(fonts)) {
        return 0;
    }

    TTF_Font *font = TTF_OpenFont(path, size);
    if (!font) {
        SDL_Log("%s", SDL_GetError());

        return 0;
    }

    const Game_FontHandle handle = font_count + 1;
    fonts[font_count] = font;
    font_count++;

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

int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    if (!SDL_ShaderCross_Init()) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    if (!TTF_Init()) {
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

    SDL_GPUShader *mesh_vertex_shader = CreateGPUShader("shaders/basic.vert.spv", SDL_SHADERCROSS_SHADERSTAGE_VERTEX);
    SDL_GPUShader *mesh_fragment_shader = CreateGPUShader("shaders/basic.frag.spv",
                                                          SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);

    if (!mesh_vertex_shader || !mesh_fragment_shader) {
        return 1;
        // errors were already logged by CreateGPUShader
    }

    SDL_GPUBuffer *mesh_vertex_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                                                .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                                                .size = Megabytes(1),
                                                            });

    SDL_GPUBuffer *mesh_index_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                                               .usage = SDL_GPU_BUFFERUSAGE_INDEX,
                                                               .size = Megabytes(1),
                                                           });

    SDL_GPUTransferBuffer *mesh_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device, &(SDL_GPUTransferBufferCreateInfo){
            .usage =
            SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = Megabytes(1) + Megabytes(1),
        });

    SDL_GPUSampler *texture_sampler = SDL_CreateGPUSampler(device, &(SDL_GPUSamplerCreateInfo){
                                                               .min_filter = SDL_GPU_FILTER_NEAREST,
                                                               .mag_filter = SDL_GPU_FILTER_NEAREST,
                                                               .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
                                                               .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                               .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                               .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                           });

    SDL_GPUTextureFormat swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUVertexBufferDescription mesh_vertex_buffer_description = {
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    SDL_GPUVertexAttribute mesh_vertex_attributes[4] = {0};
    mesh_vertex_attributes[0] = (SDL_GPUVertexAttribute){
        .location = 0,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        .offset = 0, // x, y, z
    };
    mesh_vertex_attributes[1] = (SDL_GPUVertexAttribute){
        .location = 1,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        .offset = sizeof(float) * 3, // nx, ny, nz
    };
    mesh_vertex_attributes[2] = (SDL_GPUVertexAttribute){
        .location = 2,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
        .offset = sizeof(float) * 6, // r, g, b, a
    };
    mesh_vertex_attributes[3] = (SDL_GPUVertexAttribute){
        .location = 3,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        .offset = sizeof(float) * 10, // u, v
    };

    PipelineBuilder mesh_pipeline_builder = BeginPipeline();
    PipelineSetShaders(&mesh_pipeline_builder, mesh_vertex_shader, mesh_fragment_shader);
    PipelineSetVertexInput(&mesh_pipeline_builder, &mesh_vertex_buffer_description, 1, mesh_vertex_attributes, 4);
    PipelineSetTargetFormat(&mesh_pipeline_builder, swapchain_texture_format, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);

    SDL_GPUGraphicsPipeline *mesh_pipeline = EndPipeline(&mesh_pipeline_builder);
    if (!mesh_pipeline) {
        return 1;
    }

    SDL_ReleaseGPUShader(device, mesh_vertex_shader);
    SDL_ReleaseGPUShader(device, mesh_fragment_shader);

    SDL_GPUShader *text_vertex_shader = CreateGPUShader("shaders/text.vert.spv", SDL_SHADERCROSS_SHADERSTAGE_VERTEX);
    SDL_GPUShader *text_fragment_shader =
            CreateGPUShader("shaders/text.frag.spv", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);

    SDL_GPUVertexBufferDescription text_vertex_buffer_description = {
        .slot = 0,
        .pitch = sizeof(float) * 4,
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    SDL_GPUVertexAttribute text_vertex_attributes[2];
    text_vertex_attributes[0] = (SDL_GPUVertexAttribute){
        .location = 0,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        .offset = 0,
    };
    text_vertex_attributes[1] = (SDL_GPUVertexAttribute){
        .location = 1,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        .offset = sizeof(float) * 2,
    };

    SDL_GPUColorTargetBlendState text_blend_state = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .color_write_mask = 0xF,
        .enable_blend = true,
    };

    PipelineBuilder text_pipeline_builder = BeginPipeline();
    PipelineSetShaders(&text_pipeline_builder, text_vertex_shader, text_fragment_shader);
    PipelineSetVertexInput(&text_pipeline_builder, &text_vertex_buffer_description, 1, text_vertex_attributes, 2);
    PipelineSetCullMode(&text_pipeline_builder, SDL_GPU_CULLMODE_NONE);
    PipelineSetBlendState(&text_pipeline_builder, text_blend_state);
    PipelineSetDepthState(&text_pipeline_builder, (SDL_GPUDepthStencilState){
                              .enable_depth_test = false,
                              .enable_depth_write = false,
                              .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
                          });
    PipelineSetTargetFormat(&text_pipeline_builder, swapchain_texture_format, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);

    SDL_GPUGraphicsPipeline *text_pipeline = EndPipeline(&text_pipeline_builder);
    if (!text_pipeline) {
        return 1;
    }

    SDL_ReleaseGPUShader(device, text_vertex_shader);
    SDL_ReleaseGPUShader(device, text_fragment_shader);

    SDL_GPUBuffer *text_vertex_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                                                .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                                                .size = Megabytes(1),
                                                            });
    SDL_GPUBuffer *text_index_buffer = SDL_CreateGPUBuffer(device, &(SDL_GPUBufferCreateInfo){
                                                               .usage = SDL_GPU_BUFFERUSAGE_INDEX,
                                                               .size = Megabytes(1),
                                                           });
    SDL_GPUTransferBuffer *text_transfer_buffer = SDL_CreateGPUTransferBuffer(
        device, &(SDL_GPUTransferBufferCreateInfo){
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = Megabytes(1) + Megabytes(1)
        });

    SDL_GPUSampler *text_sampler = SDL_CreateGPUSampler(device, &(SDL_GPUSamplerCreateInfo){
                                                            .min_filter = SDL_GPU_FILTER_LINEAR,
                                                            .mag_filter = SDL_GPU_FILTER_LINEAR,
                                                            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
                                                            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                                            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                                            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                                        });

    TTF_TextEngine *text_engine = TTF_CreateGPUTextEngine(device);
    if (!text_engine) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }
    TTF_SetGPUTextEngineWinding(text_engine, TTF_GPU_TEXTENGINE_WINDING_CLOCKWISE);

    Render render = {0};
    InitializeRender(&render, mesh_pipeline, mesh_vertex_buffer, mesh_index_buffer, 36, texture_sampler);

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
        .LoadFontFile = Platform_LoadFontFile,
        .ReadEntireFile = Platform_ReadEntireFile,
        .FreeFileMemory = Platform_FreeFileMemory,
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

    SDL_GPUTexture *depth_texture = NULL;
    int depth_texture_width = 0, depth_texture_height = 0;
    SDL_GPUTexture *msaa_texture = NULL;
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

        int text_vertex_count = 0;
        int text_index_count = 0;
        int text_draw_call_count = 0;

        Mat4X4 orthographic = Matrix_OrthographicScreen((float) width, (float) height);

        for (int i = 0; i < platform.render_entry_count; ++i) {
            Game_RenderEntry *entry = &platform.render_entries[i];

            if (entry->type == GAME_RENDER_ENTRY_TEXT) {
                if (entry->text.font_handle > 0 && entry->text.font_handle <= font_count) {
                    TTF_Font *font = fonts[entry->text.font_handle - 1];
                    TTF_Text *text = TTF_CreateText(text_engine, font, entry->text.text, 0);

                    if (text) {
                        int w, h;
                        TTF_GetTextSize(text, &w, &h);

                        TTF_GPUAtlasDrawSequence *sequence = TTF_GetGPUTextDrawData(text);
                        while (sequence) {
                            if (text_draw_call_count < ArrayCount(text_draw_calls)) {
                                TextDrawCall *draw_call = &text_draw_calls[text_draw_call_count];
                                draw_call->atlas = sequence->atlas_texture;

                                float px = entry->text.x, py = entry->text.y;
                                Mat4X4 translation = Matrix_Translation(Vector3(px, py, 0.0f));

                                draw_call->mvp = Matrix_Multiply(orthographic, translation);
                                draw_call->index_offset = text_index_count;
                                draw_call->index_count = 0;

                                int vertex_base = text_vertex_count;
                                for (int vertex = 0; vertex < sequence->num_vertices; ++vertex) {
                                    if (text_vertex_count < ArrayCount(text_vertices) / 4) {
                                        text_vertices[text_vertex_count * 4 + 0] = sequence->xy[vertex].x;
                                        text_vertices[text_vertex_count * 4 + 1] = -sequence->xy[vertex].y;
                                        text_vertices[text_vertex_count * 4 + 2] = sequence->uv[vertex].x;
                                        text_vertices[text_vertex_count * 4 + 3] = sequence->uv[vertex].y;
                                        text_vertex_count++;
                                    }
                                }

                                for (int index = 0; index < sequence->num_indices; ++index) {
                                    if (text_index_count < ArrayCount(text_indices)) {
                                        text_indices[text_index_count++] = sequence->indices[index] + vertex_base;
                                    }
                                }

                                draw_call->index_count = sequence->num_indices;

                                text_draw_call_count++;
                            }

                            sequence = sequence->next;
                        }
                        TTF_DestroyText(text);
                    }
                }
            }
        }

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
            size_t mesh_vertex_buffer_size = current_transient_vertex_count * sizeof(Vertex);
            size_t mesh_index_buffer_size = current_transient_index_count * sizeof(Uint16);

            Uint8 *mesh_data = SDL_MapGPUTransferBuffer(device, mesh_transfer_buffer, false);
            SDL_memcpy(mesh_data, platform.transient_vertices, mesh_vertex_buffer_size);
            SDL_memcpy(mesh_data + mesh_vertex_buffer_size, platform.transient_indices, mesh_index_buffer_size);
            SDL_UnmapGPUTransferBuffer(device, mesh_transfer_buffer);

            SDL_GPUCopyPass *mesh_copy_pass = SDL_BeginGPUCopyPass(command_buffer);
            SDL_UploadToGPUBuffer(mesh_copy_pass, &(SDL_GPUTransferBufferLocation){
                                      .transfer_buffer = mesh_transfer_buffer,
                                      .offset = 0
                                  }, &(SDL_GPUBufferRegion){
                                      .buffer = mesh_vertex_buffer,
                                      .offset = 0,
                                      .size = mesh_vertex_buffer_size
                                  }, true);
            SDL_UploadToGPUBuffer(mesh_copy_pass, &(SDL_GPUTransferBufferLocation){
                                      .transfer_buffer = mesh_transfer_buffer,
                                      .offset = (Uint32) mesh_vertex_buffer_size
                                  }, &(SDL_GPUBufferRegion){
                                      .buffer = mesh_index_buffer,
                                      .offset = 0,
                                      .size = mesh_index_buffer_size,
                                  }, true);
            SDL_EndGPUCopyPass(mesh_copy_pass);
        }

        if (text_draw_call_count > 0) {
            size_t text_vertex_buffer_size = text_vertex_count * 4 * sizeof(float);
            size_t text_index_buffer_size = text_index_count * 4 * sizeof(int);

            Uint8 *text_data = SDL_MapGPUTransferBuffer(device, text_transfer_buffer, false);
            SDL_memcpy(text_data, text_vertices, text_vertex_buffer_size);
            SDL_memcpy(text_data + text_vertex_buffer_size, text_indices, text_index_buffer_size);
            SDL_UnmapGPUTransferBuffer(device, text_transfer_buffer);

            SDL_GPUCopyPass *text_copy_pass = SDL_BeginGPUCopyPass(command_buffer);
            SDL_UploadToGPUBuffer(text_copy_pass, &(SDL_GPUTransferBufferLocation){
                                      .transfer_buffer = text_transfer_buffer,
                                      .offset = 0
                                  }, &(SDL_GPUBufferRegion){
                                      .buffer = text_vertex_buffer,
                                      .offset = 0,
                                      .size = text_vertex_buffer_size
                                  }, true);
            SDL_UploadToGPUBuffer(text_copy_pass, &(SDL_GPUTransferBufferLocation){
                                      .transfer_buffer = text_transfer_buffer,
                                      .offset = text_vertex_buffer_size
                                  }, &(SDL_GPUBufferRegion){
                                      .buffer = text_index_buffer,
                                      .offset = 0,
                                      .size = text_index_buffer_size,
                                  }, true);
            SDL_EndGPUCopyPass(text_copy_pass);
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
                .clear_depth = 0.0f,
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_DONT_CARE,
                .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
                .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
            };

            SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1,
                                                                    &depth_stencil_target_info);

            FlushRenderEntries(&render, &platform, command_buffer, render_pass);

            if (text_draw_call_count > 0) {
                SDL_BindGPUGraphicsPipeline(render_pass, text_pipeline);
                SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
                                             .buffer = text_vertex_buffer,
                                             .offset = 0
                                         }, 1);
                SDL_BindGPUIndexBuffer(render_pass, &(SDL_GPUBufferBinding){
                                           .buffer = text_index_buffer,
                                           .offset = 0
                                       }, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                for (int i = 0; i < text_draw_call_count; ++i) {
                    TextDrawCall *draw_call = &text_draw_calls[i];

                    SDL_BindGPUFragmentSamplers(render_pass, 0, &(SDL_GPUTextureSamplerBinding){
                                                    .texture = draw_call->atlas,
                                                    .sampler = text_sampler,
                                                }, 1);

                    SDL_PushGPUVertexUniformData(command_buffer, 0, &draw_call->mvp, sizeof(Mat4X4));
                    SDL_DrawGPUIndexedPrimitives(render_pass, draw_call->index_count, 1, draw_call->index_offset, 0, 0);
                }
            }

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
    for (int i = 0; i < font_count; ++i) {
        TTF_CloseFont(fonts[i]);
    }
    if (texture_sampler) {
        SDL_ReleaseGPUSampler(device, texture_sampler);
    }
    if (text_sampler) {
        SDL_ReleaseGPUSampler(device, text_sampler);
    }
    SDL_ReleaseGPUGraphicsPipeline(device, text_pipeline);
    SDL_ReleaseGPUGraphicsPipeline(device, mesh_pipeline);
    SDL_ReleaseGPUBuffer(device, text_vertex_buffer);
    SDL_ReleaseGPUBuffer(device, text_index_buffer);;
    SDL_ReleaseGPUBuffer(device, mesh_vertex_buffer);
    SDL_ReleaseGPUBuffer(device, mesh_index_buffer);
    SDL_ReleaseGPUTransferBuffer(device, text_transfer_buffer);
    SDL_ReleaseGPUTransferBuffer(device, mesh_transfer_buffer);
    TTF_DestroyGPUTextEngine(text_engine);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_ShaderCross_Quit();
    SDL_Quit();

    return 0;
}
