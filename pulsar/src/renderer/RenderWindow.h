#pragma once

#include "../env/BatchedEnvironment.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_video.h"
#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"

#include <filesystem>

namespace pulsar::Renderer
{
    class RenderWindow
    {
        struct Vertex
        {
            glm::vec3 position;
            glm::vec4 colour;
        };

        struct alignas(16) SceneConstants
        {
            glm::mat4 viewProj;
        };

        struct alignas(16) ObjectData
        {
            glm::mat4 modelMatrix;
        };

        struct MeshHandle
        {
            std::uint32_t vertexOffset;
            std::uint32_t vertexCount;

            std::uint32_t indexOffset;
            std::uint32_t indexCount;
        };

        static constexpr std::uint32_t VertexBufferSize = 1024 * 256; // 256 KB
        static constexpr std::uint32_t IndexBufferSize = 1024 * 128; // 128 KB
        static constexpr std::uint32_t MaxDrawnObjects = 1024;

        SDL_Window* window{nullptr};
        SDL_GPUDevice* device{nullptr};
        SDL_GPUGraphicsPipeline* pipeline{nullptr};

        SDL_GPUBuffer* vertexBuffer{nullptr};
        SDL_GPUBuffer* indexBuffer{nullptr};
        SDL_GPUBuffer* indirectBuffer{nullptr};
        SDL_GPUBuffer* objectDataBuffer{nullptr};
        SDL_GPUTransferBuffer* scratchTransferBuffer{nullptr};
        SDL_GPUTexture* depthTexture{nullptr};

        SDL_GPUTextureFormat depthTextureFormat{SDL_GPU_TEXTUREFORMAT_D16_UNORM};

        std::vector<MeshHandle> meshRegistry{};

        SceneConstants sceneConstants{};
        std::vector<SDL_GPUIndexedIndirectDrawCommand> drawCommands{};
        std::vector<ObjectData> objectData{};

        struct
        {
            glm::vec3 position{0};
            glm::quat rotation{1, 0, 0, 0};
        } cameraState;

        std::size_t envIndex;
        std::atomic<bool> isQuitting;

        std::uint64_t lastSnapshotTimeNS;
        std::uint32_t snapshotUpdateHz;
        flecs::system snapshotSystem;

        std::mutex renderMutex;

        [[nodiscard]] SDL_GPUShader* LoadShader(std::filesystem::path path, SDL_GPUShaderStage stage, const std::string& entryPoint, uint32_t numUniformBuffers, uint32_t numStorageBuffers) const;

    public:
        struct MeshData
        {
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;
        };

        explicit RenderWindow(const ThreadBatch& batch, std::size_t envIndex);

        void pollEvents();
        void render(float deltaTime);
        void dispose();

        [[nodiscard]] std::uint32_t RegisterMesh(const MeshData& data);
        [[nodiscard]] bool getIsQuitting() const;
    };
}