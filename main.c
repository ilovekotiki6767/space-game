#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

// NOTE: see https://github.com/libsdl-org/SDL_ttf/blob/main/examples/testgputext/SDL_math3d.h

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

static Vec3 Vector3(const float x, const float y, const float z) {
    return (Vec3){.x = x, .y = y, .z = z};
}

static Vec3 Vec3_Sub(const Vec3 vec1, const Vec3 vec2) {
    return Vector3(vec1.x - vec2.x, vec1.y - vec2.y, vec1.z - vec2.z);
}

static Vec3 Vec3_Cross(const Vec3 vec1, const Vec3 vec2) {
    return Vector3(
        vec1.y * vec2.z - vec1.z * vec2.y,
        vec1.z * vec2.x - vec1.x * vec2.z,
        vec1.x * vec2.y - vec1.y * vec2.x
    );
}

static float Vec3_Magnitude(const Vec3 vec) {
    return SDL_sqrtf(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
}

static Vec3 Vec3_Normalize(const Vec3 vec) {
    const float mag = Vec3_Magnitude(vec);

    if (mag == 0) {
        return (Vec3){0, 0, 0};
    }

    if (mag == 1) {
        return vec;
    }

    return (Vec3){vec.x / mag, vec.y / mag, vec.z / mag};
}

static float Vec3_Dot(const Vec3 vec1, const Vec3 vec2) {
    return vec1.x * vec2.x + vec1.y * vec2.y + vec1.z * vec2.z;
}

/**
 * The matrix is stored in column major format
 **/
typedef struct {
    union {
        float m[4][4];
    };
} Mat4X4;

static Mat4X4 Matrix4X4(
    const float m00, const float m10, const float m20, const float m30,
    const float m01, const float m11, const float m21, const float m31,
    const float m02, const float m12, const float m22, const float m32,
    const float m03, const float m13, const float m23, const float m33
) {
    return (Mat4X4){
        .m[0][0] = m00, .m[1][0] = m10, .m[2][0] = m20, .m[3][0] = m30,
        .m[0][1] = m01, .m[1][1] = m11, .m[2][1] = m21, .m[3][1] = m31,
        .m[0][2] = m02, .m[1][2] = m12, .m[2][2] = m22, .m[3][2] = m32,
        .m[0][3] = m03, .m[1][3] = m13, .m[2][3] = m23, .m[3][3] = m33
    };
}

static Mat4X4 Matrix_Perspective(const float fov_y, const float aspect_ratio, const float near, const float far) {
    const float n = near;
    const float f = far;
    const float t = SDL_tanf(fov_y / 2.0f) * n;
    const float b = -t;
    const float r = t * aspect_ratio;
    const float l = -r;

    return Matrix4X4(
        2 * n / (r - l), 0, (r + l) / (r - l), 0,
        0, 2 * n / (t - b), (t + b) / (t - b), 0,
        0, 0, -(f + n) / (f - n), -(2 * n * f) / (f - n),
        0, 0, -1, 1
    );
}

static Mat4X4 Matrix_LookAt(const Vec3 pos, const Vec3 target, const Vec3 up) {
    const Vec3 d = Vec3_Normalize(Vec3_Sub(target, pos));
    Vec3 u = Vec3_Normalize(up);
    const Vec3 r = Vec3_Normalize(Vec3_Cross(u, d));
    u = Vec3_Cross(r, d);

    return Matrix4X4(
        r.x, r.y, r.z, -Vec3_Dot(r, pos),
        u.x, u.y, u.z, -Vec3_Dot(u, pos),
        -d.x, -d.y, -d.z, Vec3_Dot(d, pos),
        0, 0, 0, 1
    );
}

static Mat4X4 Matrix_RotationY(const float angle) {
    const float cos = SDL_cosf(angle);
    const float sin = SDL_sinf(angle);

    return Matrix4X4(
        cos, 0, sin, 0,
        0, 1, 0, 0,
        -sin, 0, cos, 0,
        0, 0, 0, 1
    );
}

static Mat4X4 Matrix_Multiply(const Mat4X4 mat1, const Mat4X4 mat2) {
    Mat4X4 res;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int x = 0; x < 4; x++) {
                sum += mat1.m[x][j] * mat2.m[i][x];
            }
            res.m[i][j] = sum;
        }
    }

    return res;
}

typedef struct {
    float x, y, z;
    float r, g, b, a;
} Vertex;

Vertex vertices[] = {
    // Front (Red)
    {-1, -1, 1, 1, 0, 0, 1}, {1, -1, 1, 1, 0, 0, 1}, {1, 1, 1, 1, 0, 0, 1}, {-1, 1, 1, 1, 0, 0, 1},
    // Back (Green)
    {1, -1, -1, 0, 1, 0, 1}, {-1, -1, -1, 0, 1, 0, 1}, {-1, 1, -1, 0, 1, 0, 1}, {1, 1, -1, 0, 1, 0, 1},
    // Top (Blue)
    {-1, 1, -1, 0, 0, 1, 1}, {-1, 1, 1, 0, 0, 1, 1}, {1, 1, 1, 0, 0, 1, 1}, {1, 1, -1, 0, 0, 1, 1},
    // Bottom (Yellow)
    {-1, -1, -1, 1, 1, 0, 1}, {1, -1, -1, 1, 1, 0, 1}, {1, -1, 1, 1, 1, 0, 1}, {-1, -1, 1, 1, 1, 0, 1},
    // Right (Magenta)
    {1, -1, -1, 1, 0, 1, 1}, {1, 1, -1, 1, 0, 1, 1}, {1, 1, 1, 1, 0, 1, 1}, {1, -1, 1, 1, 0, 1, 1},
    // Left (Cyan)
    {-1, -1, -1, 0, 1, 1, 1}, {-1, -1, 1, 0, 1, 1, 1}, {-1, 1, 1, 0, 1, 1, 1}, {-1, 1, -1, 0, 1, 1, 1}
};

Uint16 indices[] = {
    0, 1, 2, 0, 2, 3, // front
    4, 5, 6, 4, 6, 7, // back
    8, 9, 10, 8, 10, 11, // top
    12, 13, 14, 12, 14, 15, // bottom
    16, 17, 18, 16, 18, 19, // right
    20, 21, 22, 20, 22, 23 // left
};

SDL_GPUShader *CreateGPUShader(SDL_GPUDevice *device, const char *filepath, const SDL_ShaderCross_ShaderStage stage) {
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

    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_ShaderCross_GetSPIRVShaderFormats(), true, NULL);
    if (!device) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("%s", SDL_GetError());

        return 1;
    }

    SDL_GPUShader *vertex_shader = CreateGPUShader(device, "shaders/vertex.spv", SDL_SHADERCROSS_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fragment_shader = CreateGPUShader(device, "shaders/fragment.spv",
                                                     SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);

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

    SDL_GPUVertexBufferDescription vertex_buffer_description = {
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
    };

    SDL_GPUVertexAttribute vertex_attributes[2] = {0};
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
            .num_vertex_attributes = 2
        },

        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,

        .rasterizer_state = (SDL_GPURasterizerState){
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },

        .multisample_state = (SDL_GPUMultisampleState){
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
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

    SDL_GPUTexture *depth_texture = NULL;
    int depth_texture_width = 0, depth_texture_height = 0;

    bool running = true;
    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: {
                    running = false;
                }
                break;

                case SDL_EVENT_WINDOW_RESIZED: {
                    width = event.window.data1, height = event.window.data2;
                } break;

                default: break;
            }
        }

        if (width != depth_texture_width || height != depth_texture_height) {
            if (depth_texture) {
                SDL_ReleaseGPUTexture(device, depth_texture);
            }

            depth_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo){
                                                     .type = SDL_GPU_TEXTURETYPE_2D,
                                                     .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                                                     .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                                     .width = width,
                                                     .height = height,
                                                     .layer_count_or_depth = 1,
                                                     .num_levels = 1,
                                                     .sample_count = SDL_GPU_SAMPLECOUNT_1,
                                                 });
            depth_texture_width = width, depth_texture_height = height;
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
                .texture = swapchain_texture,
                .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
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

            // TODO: delta time
            float time = (float) SDL_GetTicks() / 1000.0f;
            Mat4X4 model = Matrix_RotationY(time);
            Mat4X4 view = Matrix_LookAt(Vector3(0, 0, 5), Vector3(0, 0, 0), Vector3(0, 1, 0));
            Mat4X4 projection = Matrix_Perspective(SDL_PI_F / 4.0f, (float) width / (float) height, 0.1f, 100.0f);
            Mat4X4 mvp = Matrix_Multiply(projection, Matrix_Multiply(view, model));

            SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
            SDL_PushGPUVertexUniformData(command_buffer, 0, &mvp, sizeof(Mat4X4));
            SDL_BindGPUVertexBuffers(render_pass, 0, &(SDL_GPUBufferBinding){
                                         .buffer = vertex_buffer,
                                         .offset = 0,
                                     }, 1);
            SDL_BindGPUIndexBuffer(render_pass, &(SDL_GPUBufferBinding){
                                       .buffer = index_buffer,
                                       .offset = 0,
                                   }, SDL_GPU_INDEXELEMENTSIZE_16BIT);

            SDL_DrawGPUIndexedPrimitives(render_pass, 36, 1, 0, 0, 0);

            SDL_EndGPURenderPass(render_pass);
        }

        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    if (depth_texture) {
        SDL_ReleaseGPUTexture(device, depth_texture);
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
