#include "RenderWindow.h"

#include "../env/components/EnvironmentId.h"
#include "../env/components/MeshRenderer.h"
#include "../env/components/Transform.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_timer.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "spdlog/spdlog.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <algorithm>
#include <filesystem>
#include <ranges>

SDL_GPUShader* pulsar::Renderer::RenderWindow::LoadShader(std::filesystem::path path, const SDL_GPUShaderStage stage, const std::string& entryPoint, const std::uint32_t numUniformBuffers, const std::uint32_t numStorageBuffers, const std::uint32_t numSamplers) const
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
        .num_samplers = numSamplers,
        .num_storage_buffers = numStorageBuffers,
        .num_uniform_buffers = numUniformBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderCI);
    SDL_free(code);
    return shader;
}

pulsar::Renderer::RenderWindow::RenderWindow(const ThreadBatch& threadBatch, std::size_t envIndex)
    : threadBatch(threadBatch), envIndex(envIndex), lastSnapshotTimeNS(SDL_GetTicksNS()), snapshotUpdateHz(60)
{
    // Init
    SDL_Init(SDL_INIT_VIDEO);

    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
    window = SDL_CreateWindow("pulsar", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_ClaimWindowForGPUDevice(device, window);

    const auto distPath = std::filesystem::path(SDL_GetBasePath()).parent_path().parent_path();

    // ImGui
    ImGui::CreateContext();
    IMGUI_CHECKVERSION();

    ImGuiIO& imguiIo = ImGui::GetIO();
    imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo imguiInitInfo{
        .Device = device,
        .ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window),
        .MSAASamples = SDL_GPU_SAMPLECOUNT_1,
        .SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        .PresentMode = SDL_GPU_PRESENTMODE_VSYNC
    };
    ImGui_ImplSDLGPU3_Init(&imguiInitInfo);

#pragma region imgui styling
    imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imguiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    imguiIo.Fonts->AddFontFromFileTTF((distPath / "resources/fonts/Inter_18pt-Regular.ttf").string().c_str());

    // mirrored from https://github.com/jowsey/fate
    constexpr auto brand = ImVec4(219 / 255.0f, 106 / 255.0f, 247 / 255.0f, 1);

    constexpr auto brandWhite = ImVec4(255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 1);
    constexpr auto brandLightest = ImVec4(56 / 255.0f, 56 / 255.0f, 56 / 255.0f, 1);
    constexpr auto brandLighter = ImVec4(48 / 255.0f, 48 / 255.0f, 48 / 255.0f, 1);
    constexpr auto brandLight = ImVec4(42 / 255.0f, 42 / 255.0f, 42 / 255.0f, 1);
    constexpr auto brandNeutral = ImVec4(32 / 255.0f, 32 / 255.0f, 32 / 255.0f, 1);
    constexpr auto brandDark = ImVec4(26 / 255.0f, 26 / 255.0f, 26 / 255.0f, 1);
    constexpr auto brandDarker = ImVec4(24 / 255.0f, 24 / 255.0f, 24 / 255.0f, 1);
    // constexpr auto brandDarkest = ImVec4(12 / 255.0f, 12 / 255.0f, 12 / 255.0f, 1);

    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 16.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    style.WindowRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.FrameRounding = 4.0f;

    style.PopupBorderSize = 0.0f;

    // Windows
    style.Colors[ImGuiCol_WindowBg] = brandLight;
    style.Colors[ImGuiCol_TitleBg] = brandDark;
    style.Colors[ImGuiCol_TitleBgActive] = brandDark;
    style.Colors[ImGuiCol_MenuBarBg] = brandDark;
    style.Colors[ImGuiCol_Border] = brandLightest;
    style.Colors[ImGuiCol_PopupBg] = brandDarker;

    // Value frames
    style.Colors[ImGuiCol_FrameBg] = brandNeutral;
    style.Colors[ImGuiCol_FrameBgHovered] = brandDark;
    style.Colors[ImGuiCol_FrameBgActive] = brandDarker;

    // Tabs
    style.Colors[ImGuiCol_Tab] = brandLight;
    style.Colors[ImGuiCol_TabHovered] = brandLight;
    style.Colors[ImGuiCol_TabDimmed] = brandLight;

    style.Colors[ImGuiCol_TabSelected] = brandLighter;
    style.Colors[ImGuiCol_TabSelectedOverline] = brand;
    style.Colors[ImGuiCol_TabDimmedSelected] = brandLighter;
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = brandLighter;

    // Checkboxes
    style.Colors[ImGuiCol_CheckMark] = brandWhite;
    style.Colors[ImGuiCol_CheckboxSelectedBg] = brandNeutral;

    float contentScale = SDL_GetWindowDisplayScale(window);
    style.FontScaleDpi = contentScale;
    style.ScaleAllSizes(contentScale);
#pragma endregion

    // Shaders + pipeline
    SDL_GPUShader* vertShader = LoadShader(distPath / "resources/shaders/lit_vert", SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 1, 1, 0);
    SDL_GPUShader* fragShader = LoadShader(distPath / "resources/shaders/lit_frag", SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 0, 0, 8);

    SDL_GPUColorTargetDescription colorDesc{.format = SDL_GetGPUSwapchainTextureFormat(device, window)};

    std::array<SDL_GPUVertexBufferDescription, 1> vertexBufferDescriptions{
        SDL_GPUVertexBufferDescription{
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        }
    };

    auto vertexAttributes = std::to_array<SDL_GPUVertexAttribute>(
        {SDL_GPUVertexAttribute{
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
         },
         SDL_GPUVertexAttribute{
             .location = 2,
             .buffer_slot = 0,
             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
             .offset = offsetof(Vertex, uv)
         }}
    );

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
    spdlog::debug("Transfer buffer size: {:.2f} KB", transferBufferCI.size / 1024.0);
    scratchTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferBufferCI);

    // Sampler
    SDL_GPUSamplerCreateInfo samplerCI{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .max_anisotropy = 16.0f,
        .max_lod = FLT_MAX,
        .enable_anisotropy = true
    };

    linearSampler = SDL_CreateGPUSampler(device, &samplerCI);

    SDL_GPUTextureCreateInfo dummyTextureArrayCI{
        .type = SDL_GPU_TEXTURETYPE_2D_ARRAY,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = 1,
        .height = 1,
        .layer_count_or_depth = 1,
        .num_levels = 1
    };
    dummyTextureArray = SDL_CreateGPUTexture(device, &dummyTextureArrayCI);

    // Hook into ECS
    snapshotSystem =
        threadBatch.sharedWorld
            .system<const Components::EnvironmentId, const Components::Transform, const Components::MeshRenderer>("SnapshotSystem")
            .kind(flecs::PostUpdate)
            .run([this](flecs::iter& it) {
                const auto now = SDL_GetTicksNS();
                if (now - lastSnapshotTimeNS >= 1'000'000'000 / snapshotUpdateHz)
                {
                    std::lock_guard lock(renderMutex);

                    lastSnapshotTimeNS = now;

                    drawCommands.clear();
                    objectData.clear();

                    while (it.next())
                    {
                        const auto envId = it.field<const Components::EnvironmentId>(0);
                        const auto transform = it.field<const Components::Transform>(1);
                        const auto meshRenderer = it.field<const Components::MeshRenderer>(2);

                        for (const auto i : it)
                        {
                            if (!showFullGrid && envId[i].envId != this->envIndex) continue;
                            if (meshRenderer[i].meshes.empty()) continue;

                            glm::mat4 envOffset{1.0f};

                            if (showFullGrid)
                            {
                                constexpr float cellSize = 128;
                                constexpr std::size_t rowCount = 11;
                                const std::size_t row = envId[i].envId / rowCount;
                                const std::size_t col = envId[i].envId % rowCount;
                                envOffset = glm::translate(glm::mat4(1.0), glm::vec3(col, 0, row) * cellSize);
                            }

                            for (const auto& handles : meshRenderer[i].meshes)
                            {
                                if (handles.mesh == std::nullopt) continue;

                                auto meshHandle = RequestUploadedGPUMesh(handles.mesh.value());
                                drawCommands.emplace_back(meshHandle.indexCount, 1, meshHandle.indexOffset, meshHandle.vertexOffset, objectData.size());

                                Material material{};

                                if (handles.albedo.has_value())
                                {
                                    material.hasMapMask |= 1 << 0;
                                    const auto albedoHandle = RequestUploadedGPUTexture(handles.albedo.value());

                                    // todo this should be essentially guaranteed to exist but maybe harden anyway
                                    material.albedoTexture = std::distance(textureBuckets.begin(), textureBuckets.find(albedoHandle.bucketRes));
                                    material.albedoLayer = albedoHandle.index;
                                }

                                objectData.emplace_back(envOffset * transform[i].worldMatrix, material);
                            }
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

    std::lock_guard lock(renderMutex);
    SDL_Event event{};

    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)))
        {
            isQuitting = true;
            break;
        }
    }
}

void pulsar::Renderer::RenderWindow::render(float deltaTime)
{
    // Camera control
    constexpr float moveSpeed = 5.0f;

    float dx, dy;
    SDL_MouseButtonFlags mouseButtons = SDL_GetRelativeMouseState(&dx, &dy);

    if (mouseButtons & SDL_BUTTON_RMASK)
    {
        if (!SDL_GetWindowRelativeMouseMode(window))
        {
            SDL_SetWindowRelativeMouseMode(window, true);
        }

        // Rotate
        constexpr float sensitivity = 0.75f;
        float yaw = dx * sensitivity * deltaTime;
        float pitch = dy * sensitivity * deltaTime;

        glm::quat yawQuat = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        glm::quat pitchQuat = glm::angleAxis(pitch, glm::vec3(1, 0, 0));
        cameraState.rotation = yawQuat * cameraState.rotation * pitchQuat;

        // Pan
        auto kbState = SDL_GetKeyboardState(nullptr);
        glm::vec3 moveDir{0};
        if (kbState[SDL_SCANCODE_W]) moveDir += cameraState.rotation * glm::vec3(0, 0, 1);
        if (kbState[SDL_SCANCODE_S]) moveDir += cameraState.rotation * glm::vec3(0, 0, -1);
        if (kbState[SDL_SCANCODE_A]) moveDir += cameraState.rotation * glm::vec3(-1, 0, 0);
        if (kbState[SDL_SCANCODE_D]) moveDir += cameraState.rotation * glm::vec3(1, 0, 0);
        if (kbState[SDL_SCANCODE_Q]) moveDir += cameraState.rotation * glm::vec3(0, -1, 0);
        if (kbState[SDL_SCANCODE_E]) moveDir += cameraState.rotation * glm::vec3(0, 1, 0);

        if (moveDir != glm::vec3(0.0f))
        {
            float multiplier = kbState[SDL_SCANCODE_LSHIFT] ? 5 : 1;
            cameraState.position += glm::normalize(moveDir) * moveSpeed * multiplier * deltaTime;
        }
    }
    else if (SDL_GetWindowRelativeMouseMode(window))
    {
        SDL_SetWindowRelativeMouseMode(window, false);
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUTexture* swapchainTexture;

    static std::uint32_t lastWindowWidth;
    static std::uint32_t lastWindowHeight;

    std::uint32_t windowWidth;
    std::uint32_t windowHeight;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &windowWidth, &windowHeight))
    {
        spdlog::warn("Failed to acquire swapchain texture!");
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return;
    }

    // window minimized or otherwise not rendering
    if (!swapchainTexture || windowWidth == 0 || windowHeight == 0)
    {
        // spdlog::warn("Failed to acquire swapchain texture (should be fine?)");
        SDL_SubmitGPUCommandBuffer(commandBuffer);
        return;
    }

    if (windowWidth != lastWindowWidth || windowHeight != lastWindowHeight || !depthTexture)
    {
        spdlog::trace("Rebuilding depth texture ({}x{})", windowWidth, windowHeight);

        lastWindowWidth = windowWidth;
        lastWindowHeight = windowHeight;

        if (depthTexture)
        {
            SDL_ReleaseGPUTexture(device, depthTexture);
            depthTexture = nullptr;
        }

        const SDL_GPUTextureCreateInfo depthCI{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = depthTextureFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
            .width = windowWidth,
            .height = windowHeight,
            .layer_count_or_depth = 1,
            .num_levels = 1
        };
        depthTexture = SDL_CreateGPUTexture(device, &depthCI);
    }

    {
        std::lock_guard lock(renderMutex);

        // ImGui UI
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::Begin("pulsar");

        ImGui::Text("%.1f FPS (%zu draws)", io.Framerate, drawCommands.size());
        ImGui::DragFloat3("Camera Position", &cameraState.position.x, 0.1f);

        ImGui::SeparatorText("Environment");
        ImGui::Checkbox("Show full grid", &showFullGrid);

        if (showFullGrid)
        {
            ImGui::BeginDisabled();
        }

        auto numEnvs = threadBatch.envRoots.size();
        if (ImGui::ArrowButton("prevenv", ImGuiDir_Left))
        {
            if (envIndex > 0)
                envIndex--;
            else
                envIndex = numEnvs - 1;
        }
        ImGui::SameLine();
        ImGui::Text("Environment %zu / %zu", envIndex + 1, numEnvs);
        ImGui::SameLine();
        if (ImGui::ArrowButton("nextenv", ImGuiDir_Right))
        {
            if (envIndex < numEnvs - 1)
                envIndex++;
            else
                envIndex = 0;
        }

        if (showFullGrid)
        {
            ImGui::EndDisabled();
        }

        ImGui::End();
        ImGui::Render();

        // Update + upload GPU buffers
        const auto view = glm::mat4_cast(glm::conjugate(cameraState.rotation)) * glm::translate(glm::mat4(1.0f), -cameraState.position);
        const auto proj = glm::perspective(glm::radians(60.0f), windowWidth / static_cast<float>(windowHeight), 0.1f, 2500.0f);
        sceneConstants = {
            .viewProj = proj * view
        };

        // Preprocess upload queue
        vertexUploadScratch.clear();
        indexUploadScratch.clear();
        textureUploadScratch.clear();

        for (auto [hash, type] : queuedGPUUploadHashes)
        {
            if (type == GPUUploadType::Mesh)
            {
                auto* meshData = BatchedEnvironment::getCachedMesh(hash);

                vertexUploadScratch.insert(vertexUploadScratch.end(), meshData->vertices.begin(), meshData->vertices.end());
                indexUploadScratch.insert(indexUploadScratch.end(), meshData->indices.begin(), meshData->indices.end());
            }
            else if (type == GPUUploadType::Texture)
            {
                auto* textureData = BatchedEnvironment::getCachedTexture(hash);
                const auto& handle = gpuTextureHandles[hash];

                // Initialize new bucket if required
                if (!textureBuckets[handle.bucketRes].texture)
                {
                    spdlog::debug("Creating new texture bucket for size {0}x{0}", handle.bucketRes);

                    SDL_GPUTextureCreateInfo textureCI{
                        .type = SDL_GPU_TEXTURETYPE_2D_ARRAY,
                        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
                        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                        .width = handle.bucketRes,
                        .height = handle.bucketRes,
                        .layer_count_or_depth = MaxTexturesPerBucket,
                        .num_levels = static_cast<std::uint32_t>(std::log2(handle.bucketRes) - 1) // stop at 4x4
                    };
                    textureBuckets[handle.bucketRes].texture = SDL_CreateGPUTexture(device, &textureCI);
                }

                // Upload new texture to scratch
                std::size_t offset = textureUploadScratch.size();
                textureUploadScratch.resize(offset + (handle.bucketRes * handle.bucketRes * 4));

                if (textureData->width != handle.bucketRes || textureData->height != handle.bucketRes)
                {
                    spdlog::trace("Resizing texture {} from {}x{} to {}x{}", hash, textureData->width, textureData->height, handle.bucketRes, handle.bucketRes);

                    stbir_resize_uint8_srgb(
                        textureData->data.data(),
                        textureData->width,
                        textureData->height,
                        0,
                        textureUploadScratch.data() + offset,
                        handle.bucketRes,
                        handle.bucketRes,
                        0,
                        STBIR_RGBA
                    );
                }
                else
                {
                    SDL_memcpy(textureUploadScratch.data() + offset, textureData->data.data(), textureData->data.size());
                }

                textureBuckets[handle.bucketRes].hasChanged = true;
            }
        }

        // Uploads + copies
        auto* scratchPtr = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device, scratchTransferBuffer, true));

        // Upload draw commands and object data
        const std::uint32_t drawCommandsBytes = drawCommands.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand);
        const std::uint32_t objectDataBytes = objectData.size() * sizeof(ObjectData);
        const std::uint32_t vertexUploadBytes = vertexUploadScratch.size() * sizeof(Vertex);
        const std::uint32_t indexUploadBytes = indexUploadScratch.size() * sizeof(std::uint32_t);
        const std::uint32_t textureUploadBytes = textureUploadScratch.size() * sizeof(std::uint8_t);

        // todo we should track if these actually changed (dirty flag?)
        if (drawCommandsBytes > 0)
            SDL_memcpy(scratchPtr, drawCommands.data(), drawCommandsBytes);
        if (objectDataBytes > 0)
            SDL_memcpy(scratchPtr + drawCommandsBytes, objectData.data(), objectDataBytes);

        if (vertexUploadBytes > 0)
            SDL_memcpy(scratchPtr + drawCommandsBytes + objectDataBytes, vertexUploadScratch.data(), vertexUploadBytes);
        if (indexUploadBytes > 0)
            SDL_memcpy(scratchPtr + drawCommandsBytes + objectDataBytes + vertexUploadBytes, indexUploadScratch.data(), indexUploadBytes);

        if (textureUploadBytes > 0)
            SDL_memcpy(scratchPtr + drawCommandsBytes + objectDataBytes + vertexUploadBytes + indexUploadBytes, textureUploadScratch.data(), textureUploadBytes);

        SDL_UnmapGPUTransferBuffer(device, scratchTransferBuffer);

        const std::uint32_t totalBytes = drawCommandsBytes + objectDataBytes + vertexUploadBytes + indexUploadBytes + textureUploadBytes;
        if (totalBytes > 0)
        {
            spdlog::trace("Transferring {} bytes ({:.2f} KB)", totalBytes, totalBytes / 1024.0);
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

        if (drawCommandsBytes > 0)
        {
            const SDL_GPUTransferBufferLocation drawCommandsSrc{.transfer_buffer = scratchTransferBuffer, .offset = 0};
            const SDL_GPUBufferRegion drawCommandsDst{.buffer = indirectBuffer, .offset = 0, .size = drawCommandsBytes};
            SDL_UploadToGPUBuffer(copyPass, &drawCommandsSrc, &drawCommandsDst, true);
        }

        if (objectDataBytes > 0)
        {
            const SDL_GPUTransferBufferLocation objectDataSrc{.transfer_buffer = scratchTransferBuffer, .offset = drawCommandsBytes};
            const SDL_GPUBufferRegion objectDataDst{.buffer = objectDataBuffer, .offset = 0, .size = objectDataBytes};
            SDL_UploadToGPUBuffer(copyPass, &objectDataSrc, &objectDataDst, true);
        }

        if (!queuedGPUUploadHashes.empty())
        {
            std::size_t numMeshes{0};
            std::size_t numTextures{0};

            for (auto type : queuedGPUUploadHashes | std::views::values)
            {
                if (type == GPUUploadType::Mesh)
                    numMeshes++;
                else if (type == GPUUploadType::Texture)
                    numTextures++;
            }

            spdlog::debug("Uploading {} meshes, {} textures", numMeshes, numTextures);

            std::uint32_t cumulativeVertexBytes = 0;
            std::uint32_t cumulativeIndexBytes = 0;
            std::uint32_t cumulativeTextureBytes = 0;

            for (auto [hash, type] : queuedGPUUploadHashes)
            {
                if (type == GPUUploadType::Mesh)
                {
                    auto* meshData = BatchedEnvironment::getCachedMesh(hash);
                    auto bufferHandle = gpuMeshHandles[hash];

                    std::uint32_t numVertices = meshData->vertices.size();
                    std::uint32_t numIndices = meshData->indices.size();

                    if (numVertices > 0)
                    {
                        std::uint32_t byteOffset = bufferHandle.vertexOffset * sizeof(Vertex);
                        std::uint32_t byteSize = numVertices * sizeof(Vertex);

                        const SDL_GPUTransferBufferLocation vertexSrc{.transfer_buffer = scratchTransferBuffer, .offset = drawCommandsBytes + objectDataBytes + cumulativeVertexBytes};
                        const SDL_GPUBufferRegion vertexDst{.buffer = vertexBuffer, .offset = byteOffset, .size = byteSize};
                        SDL_UploadToGPUBuffer(copyPass, &vertexSrc, &vertexDst, false);
                    }

                    if (numIndices > 0)
                    {
                        std::uint32_t byteOffset = bufferHandle.indexOffset * sizeof(std::uint32_t);
                        std::uint32_t byteSize = numIndices * sizeof(std::uint32_t);

                        const SDL_GPUTransferBufferLocation indexSrc{.transfer_buffer = scratchTransferBuffer, .offset = drawCommandsBytes + objectDataBytes + vertexUploadBytes + cumulativeIndexBytes};
                        const SDL_GPUBufferRegion indexDst{.buffer = indexBuffer, .offset = byteOffset, .size = byteSize};
                        SDL_UploadToGPUBuffer(copyPass, &indexSrc, &indexDst, false);
                    }

                    cumulativeVertexBytes += meshData->vertices.size() * sizeof(Vertex);
                    cumulativeIndexBytes += meshData->indices.size() * sizeof(std::uint32_t);
                }
                else if (type == GPUUploadType::Texture)
                {
                    const auto& bufferHandle = gpuTextureHandles[hash];

                    const SDL_GPUTextureTransferInfo textureSrc{
                        .transfer_buffer = scratchTransferBuffer,
                        .offset = drawCommandsBytes + objectDataBytes + vertexUploadBytes + indexUploadBytes + cumulativeTextureBytes,
                    };
                    const SDL_GPUTextureRegion textureDst{.texture = textureBuckets[bufferHandle.bucketRes].texture, .layer = bufferHandle.index, .x = 0, .y = 0, .w = bufferHandle.bucketRes, .h = bufferHandle.bucketRes, .d = 1};
                    SDL_UploadToGPUTexture(copyPass, &textureSrc, &textureDst, false);

                    cumulativeTextureBytes += bufferHandle.bucketRes * bufferHandle.bucketRes * 4;
                }
            }
        }

        queuedGPUUploadHashes.clear();
        SDL_EndGPUCopyPass(copyPass);

        // Generate updated mipmaps
        for (auto [res, bucket] : textureBuckets)
        {
            if (bucket.hasChanged)
            {
                spdlog::trace("Generating mipmaps for {0}x{0} bucket", res);
                SDL_GenerateMipmapsForGPUTexture(commandBuffer, bucket.texture);
                bucket.hasChanged = false;
            }
        }

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

        constexpr std::size_t requiredTextureBuckets = 8;
        if (textureBuckets.size() > requiredTextureBuckets)
        {
            spdlog::warn("Too many texture buckets ({}), can only bind the first {}", textureBuckets.size(), requiredTextureBuckets);
        }

        std::array<SDL_GPUTextureSamplerBinding, requiredTextureBuckets> samplerBindings{};

        for (std::size_t i = 0; i < requiredTextureBuckets; i++)
        {
            if (i < textureBuckets.size())
            {
                SDL_GPUTextureSamplerBinding samplerBinding = {
                    .texture = textureBuckets.values()[i].texture,
                    .sampler = linearSampler
                };

                samplerBindings[i] = samplerBinding;
            }
            else
            {
                SDL_GPUTextureSamplerBinding dummySamplerBinding = {
                    .texture = dummyTextureArray,
                    .sampler = linearSampler
                };

                samplerBindings[i] = dummySamplerBinding;
            }
        }

        SDL_BindGPUFragmentSamplers(renderPass, 0, samplerBindings.data(), 8);

        SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, indirectBuffer, 0, drawCommands.size());
        SDL_EndGPURenderPass(renderPass);

        ImDrawData* imguiDrawData = ImGui::GetDrawData();
        if (imguiDrawData && imguiDrawData->TotalVtxCount > 0)
        {
            ImGui_ImplSDLGPU3_PrepareDrawData(imguiDrawData, commandBuffer);

            const SDL_GPUColorTargetInfo imguiColourTargetInfo{
                .texture = swapchainTexture,
                .load_op = SDL_GPU_LOADOP_LOAD,
                .store_op = SDL_GPU_STOREOP_STORE,
            };

            SDL_GPURenderPass* imguiRenderPass = SDL_BeginGPURenderPass(commandBuffer, &imguiColourTargetInfo, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(imguiDrawData, commandBuffer, imguiRenderPass);
            SDL_EndGPURenderPass(imguiRenderPass);
        }
    }

    SDL_SubmitGPUCommandBuffer(commandBuffer);
}

void pulsar::Renderer::RenderWindow::dispose()
{
    if (snapshotSystem.is_alive()) snapshotSystem.destruct();

    SDL_WaitForGPUIdle(device);

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseGPUTexture(device, depthTexture);

    SDL_ReleaseGPUBuffer(device, vertexBuffer);
    SDL_ReleaseGPUBuffer(device, indexBuffer);
    SDL_ReleaseGPUBuffer(device, indirectBuffer);
    SDL_ReleaseGPUBuffer(device, objectDataBuffer);
    SDL_ReleaseGPUTransferBuffer(device, scratchTransferBuffer);
    SDL_ReleaseGPUSampler(device, linearSampler);
    SDL_ReleaseGPUTexture(device, dummyTextureArray);

    for (const auto& bucket : textureBuckets | std::views::values)
    {
        SDL_ReleaseGPUTexture(device, bucket.texture);
    }

    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);

    SDL_Quit();
}

const pulsar::Renderer::GPUMeshHandle& pulsar::Renderer::RenderWindow::RequestUploadedGPUMesh(const std::size_t& hash)
{
    if (gpuMeshHandles.contains(hash))
    {
        return gpuMeshHandles[hash];
    }

    const auto meshData = BatchedEnvironment::getCachedMesh(hash);

    const auto vertexOffset = nextVertexBufferOffset;
    const auto indexOffset = nextIndexBufferOffset;

    const std::uint32_t numVertices = meshData->vertices.size();
    const std::uint32_t numIndices = meshData->indices.size();

    const auto data = GPUMeshHandle{
        .vertexOffset = vertexOffset,
        .vertexCount = numVertices,
        .indexOffset = indexOffset,
        .indexCount = numIndices
    };

    nextVertexBufferOffset += numVertices;
    nextIndexBufferOffset += numIndices;

    gpuMeshHandles[hash] = data;
    queuedGPUUploadHashes.emplace_back(hash, GPUUploadType::Mesh);

    return gpuMeshHandles[hash];
}

const pulsar::Renderer::GPUTextureHandle& pulsar::Renderer::RenderWindow::RequestUploadedGPUTexture(const std::size_t& hash)
{
    if (gpuTextureHandles.contains(hash))
    {
        return gpuTextureHandles[hash];
    }

    const auto textureData = BatchedEnvironment::getCachedTexture(hash);
    const std::uint32_t res = std::bit_ceil(std::max(textureData->width, textureData->height));

    if (!textureBuckets.contains(res))
    {
        textureBuckets[res] = {};
    }

    // todo this feels massively over-engineered, simplify
    const auto data = GPUTextureHandle{
        .bucketRes = res,
        .index = textureBuckets[res].size++
    };

    gpuTextureHandles[hash] = data;
    queuedGPUUploadHashes.emplace_back(hash, GPUUploadType::Texture);

    return gpuTextureHandles[hash];
}

bool pulsar::Renderer::RenderWindow::getIsQuitting() const
{
    return isQuitting;
}