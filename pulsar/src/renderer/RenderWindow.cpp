#include "RenderWindow.h"

#include "../env/components/Environment.h"
#include "../env/components/EnvironmentId.h"
#include "../env/components/MeshRenderer.h"
#include "../env/components/Transform.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_timer.h"
#include "spdlog/spdlog.h"

#include <filesystem>

SDL_GPUShader* pulsar::Renderer::RenderWindow::LoadShader(std::filesystem::path path, const SDL_GPUShaderStage stage, const std::string& entryPoint, const std::uint32_t numUniformBuffers, const std::uint32_t numStorageBuffers) const
{
    SDL_GPUShaderFormat supportedFormat = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat format;
    std::string ext;

    if (supportedFormat & SDL_GPU_SHADERFORMAT_SPIRV)
    {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        ext = "spv";
    }
    else if (supportedFormat & SDL_GPU_SHADERFORMAT_MSL)
    {
        format = SDL_GPU_SHADERFORMAT_MSL;
        ext = "msl";
    }
    else
    {
        spdlog::error("No supported shader format found? (supported: {})", supportedFormat);
        return nullptr;
    }

    std::size_t codeSize;
    void* code = SDL_LoadFile(path.replace_extension(ext).string().c_str(), &codeSize);
    if (!code) return nullptr;

    const SDL_GPUShaderCreateInfo shaderCI{
        .code_size = codeSize,
        .code = static_cast<const std::uint8_t*>(code),
        .entrypoint = entryPoint.c_str(),
        .format = format,
        .stage = stage,
        .num_storage_buffers = numStorageBuffers,
        .num_uniform_buffers = numUniformBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderCI);
    SDL_free(code);
    return shader;
}

pulsar::Renderer::RenderWindow::RenderWindow(const ThreadBatch& batch, const std::size_t envIndex)
    : envIndex(envIndex), lastSnapshotTimeNS(SDL_GetTicksNS()), snapshotUpdateHz(60)
{
    // Init
    SDL_Init(SDL_INIT_VIDEO);

    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
    window = SDL_CreateWindow("pulsar", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    SDL_ClaimWindowForGPUDevice(device, window);

    // Shaders + pipeline
    const auto basePath = std::filesystem::path(SDL_GetBasePath());
    SDL_GPUShader* vertShader = LoadShader(basePath / "shaders/lit_vert", SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 1, 1);
    SDL_GPUShader* fragShader = LoadShader(basePath / "shaders/lit_frag", SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 0, 0);

    SDL_GPUColorTargetDescription colorDesc{.format = SDL_GetGPUSwapchainTextureFormat(device, window)};

    std::array<SDL_GPUVertexBufferDescription, 1> vertexBufferDescriptions{
        SDL_GPUVertexBufferDescription{
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        }
    };

    std::array<SDL_GPUVertexAttribute, 2> vertexAttributes{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = offsetof(Vertex, position)
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = offsetof(Vertex, colour)
        }
    };

    auto depthTextureFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    SDL_GPUTextureCreateInfo depthCI{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = depthTextureFormat,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = 1280, // todo handle resizing
        .height = 720,
        .layer_count_or_depth = 1,
        .num_levels = 1
    };
    depthTexture = SDL_CreateGPUTexture(device, &depthCI);

    SDL_GPUGraphicsPipelineCreateInfo pipelineCI{
        .vertex_shader = vertShader,
        .fragment_shader = fragShader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = vertexBufferDescriptions.data(),
            .num_vertex_buffers = vertexBufferDescriptions.size(),
            .vertex_attributes = vertexAttributes.data(),
            .num_vertex_attributes = vertexAttributes.size(),
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
        },
        .depth_stencil_state = {.compare_op = SDL_GPU_COMPAREOP_LESS, .enable_depth_test = true, .enable_depth_write = true},
        .target_info = {.color_target_descriptions = &colorDesc, .num_color_targets = 1, .depth_stencil_format = depthTextureFormat, .has_depth_stencil_target = true}
    };

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineCI);

    SDL_ReleaseGPUShader(device, vertShader);
    SDL_ReleaseGPUShader(device, fragShader);

    // Buffers
    SDL_GPUBufferCreateInfo vertexBufferCI{.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = VertexBufferSize};
    SDL_GPUBufferCreateInfo indexBufferCI{.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = IndexBufferSize};
    SDL_GPUBufferCreateInfo indirectBufferCI{.usage = SDL_GPU_BUFFERUSAGE_INDIRECT, .size = MaxDrawnObjects * sizeof(SDL_GPUIndexedIndirectDrawCommand)};
    SDL_GPUBufferCreateInfo objectDataBufferCI{.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, .size = MaxDrawnObjects * sizeof(ObjectData)};
    vertexBuffer = SDL_CreateGPUBuffer(device, &vertexBufferCI);
    indexBuffer = SDL_CreateGPUBuffer(device, &indexBufferCI);
    indirectBuffer = SDL_CreateGPUBuffer(device, &indirectBufferCI);
    objectDataBuffer = SDL_CreateGPUBuffer(device, &objectDataBufferCI);

    SDL_GPUTransferBufferCreateInfo transferBufferCI{.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = vertexBufferCI.size + indexBufferCI.size + indirectBufferCI.size + objectDataBufferCI.size};
    scratchTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferCI);

    // Hook into ECS
    snapshotSystem =
        batch.sharedEcs.system<const Components::EnvironmentId, const Components::Transform, const Components::MeshRenderer>("SnapshotSystem")
            .kind(flecs::PostUpdate)
            .run([this](flecs::iter& it) {
                const auto now = SDL_GetTicksNS();
                if (now - lastSnapshotTimeNS >= 1'000'000'000 / snapshotUpdateHz)
                {
                    std::lock_guard lock(snapshotMutex);

                    lastSnapshotTimeNS = now;

                    drawCommands.clear();
                    objectData.clear();

                    while (it.next())
                    {
                        const auto envId = it.field<Components::EnvironmentId>(0);
                        const auto transform = it.field<Components::Transform>(1);
                        const auto meshRenderer = it.field<Components::MeshRenderer>(2);

                        for (const auto i : it)
                        {
                            if (envId[i].envId != this->envIndex) continue; // todo we can almost certainly filter this at an ECS level

                            const auto& meshHandle = meshRegistry[meshRenderer[i].meshIndex];
                            drawCommands.emplace_back(meshHandle.indexCount, 1, meshHandle.indexOffset, meshHandle.vertexOffset, i);
                            objectData.emplace_back(transform[i].worldMatrix);
                        }
                    }
                }
                else
                {
                    it.fini();
                }
            });
}

void pulsar::Renderer::RenderWindow::pollEvents()
{
    if (isQuitting) return;

    SDL_Event event{};

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            isQuitting = true;
            break;
        }
    }
}

void pulsar::Renderer::RenderWindow::render()
{
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUTexture* swapchainTexture;

    if (SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, nullptr, nullptr) && swapchainTexture)
    {
        std::lock_guard lock(snapshotMutex);

        const auto view = glm::mat4(1.0f);
        const auto proj = glm::perspective(glm::radians(90.0f), 1280.0f / 720.0f, 0.1f, 100.0f);

        sceneConstants = {
            .viewProj = proj * view
        };

        // Copy buffers
        auto* scratchPtr = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device, scratchTransferBuffer, true));
        const std::uint32_t drawCommandsBytes = drawCommands.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand);
        const std::uint32_t objectDataBytes = objectData.size() * sizeof(ObjectData);
        SDL_memcpy(scratchPtr, drawCommands.data(), drawCommandsBytes); // todo we should check if this actually changed
        SDL_memcpy(scratchPtr + drawCommandsBytes, objectData.data(), objectDataBytes);
        SDL_UnmapGPUTransferBuffer(device, scratchTransferBuffer);

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

        const SDL_GPUTransferBufferLocation drawCommandsSrc{.transfer_buffer = scratchTransferBuffer, .offset = 0};
        const SDL_GPUBufferRegion drawCommandsDst{.buffer = indirectBuffer, .offset = 0, .size = drawCommandsBytes};
        const SDL_GPUTransferBufferLocation objectDataSrc{.transfer_buffer = scratchTransferBuffer, .offset = drawCommandsBytes};
        const SDL_GPUBufferRegion objectDataDst{.buffer = objectDataBuffer, .offset = 0, .size = objectDataBytes};
        SDL_UploadToGPUBuffer(copyPass, &drawCommandsSrc, &drawCommandsDst, true);
        SDL_UploadToGPUBuffer(copyPass, &objectDataSrc, &objectDataDst, true);

        SDL_EndGPUCopyPass(copyPass);

        const SDL_GPUColorTargetInfo colourTargetInfo{
            .texture = swapchainTexture,
            .clear_color = {.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f},
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
        };

        const SDL_GPUDepthStencilTargetInfo depthTargetInfo{
            .texture = depthTexture,
            .clear_depth = 1.0f,
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_DONT_CARE,
        };

        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colourTargetInfo, 1, &depthTargetInfo);
        SDL_BindGPUGraphicsPipeline(renderPass, pipeline);

        const SDL_GPUBufferBinding vertexBufferBinding{.buffer = vertexBuffer, .offset = 0};
        const SDL_GPUBufferBinding indexBufferBinding{.buffer = indexBuffer, .offset = 0};
        SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);
        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_PushGPUVertexUniformData(commandBuffer, 0, &sceneConstants, sizeof(SceneConstants));
        SDL_BindGPUVertexStorageBuffers(renderPass, 0, &objectDataBuffer, 1);

        SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, indirectBuffer, 0, drawCommands.size());
        SDL_EndGPURenderPass(renderPass);
    }

    SDL_SubmitGPUCommandBuffer(commandBuffer);

    // Un-hide after first render, prevents black flicker
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN)
    {
        SDL_ShowWindow(window);
    }
}

void pulsar::Renderer::RenderWindow::dispose()
{
    snapshotSystem.destruct();

    SDL_WaitForGPUIdle(device);

    SDL_ReleaseGPUTexture(device, depthTexture);

    SDL_ReleaseGPUBuffer(device, vertexBuffer);
    SDL_ReleaseGPUBuffer(device, indexBuffer);
    SDL_ReleaseGPUBuffer(device, indirectBuffer);
    SDL_ReleaseGPUBuffer(device, objectDataBuffer);
    SDL_ReleaseGPUTransferBuffer(device, scratchTransferBuffer);

    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);

    SDL_Quit();
}

std::uint32_t pulsar::Renderer::RenderWindow::RegisterMesh(const MeshData& data)
{
    std::uint32_t vertexOffset = meshRegistry.empty() ? 0 : meshRegistry.back().vertexOffset + meshRegistry.back().vertexCount;
    std::uint32_t indexOffset = meshRegistry.empty() ? 0 : meshRegistry.back().indexOffset + meshRegistry.back().indexCount;

    meshRegistry.emplace_back(vertexOffset, data.vertices.size(), indexOffset, data.indices.size());

    const auto scratchPtr = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device, scratchTransferBuffer, true));
    const std::uint32_t vertexBytes = data.vertices.size() * sizeof(Vertex);
    const std::uint32_t indexBytes = data.indices.size() * sizeof(std::uint32_t);

    SDL_memcpy(scratchPtr, data.vertices.data(), vertexBytes);
    SDL_memcpy(scratchPtr + vertexBytes, data.indices.data(), indexBytes);

    SDL_UnmapGPUTransferBuffer(device, scratchTransferBuffer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    const SDL_GPUTransferBufferLocation vertexSrc{.transfer_buffer = scratchTransferBuffer, .offset = 0};
    const SDL_GPUBufferRegion vertexDst{.buffer = vertexBuffer, .offset = static_cast<std::uint32_t>(vertexOffset * sizeof(Vertex)), .size = vertexBytes};
    SDL_UploadToGPUBuffer(copyPass, &vertexSrc, &vertexDst, false);

    const SDL_GPUTransferBufferLocation indexSrc{.transfer_buffer = scratchTransferBuffer, .offset = vertexBytes};
    const SDL_GPUBufferRegion indexDst{.buffer = indexBuffer, .offset = static_cast<std::uint32_t>(indexOffset * sizeof(std::uint32_t)), .size = indexBytes};
    SDL_UploadToGPUBuffer(copyPass, &indexSrc, &indexDst, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);

    return meshRegistry.size() - 1;
}

bool pulsar::Renderer::RenderWindow::getIsQuitting() const
{
    return isQuitting;
}