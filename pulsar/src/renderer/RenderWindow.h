#pragma once

#include "../env/BatchedEnvironment.h"
#include "MeshData.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_video.h"
#include "flecs.h"
#include "glm/detail/type_quat.hpp"
#include "glm/fwd.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <queue>

namespace pulsar::Renderer
{
    class RenderWindow
    {
      public:
        struct alignas(16) SceneConstants
        {
            glm::mat4 viewProj;
        };

        struct alignas(16) ObjectData
        {
            glm::mat4 modelMatrix;
        };

        struct GPUMeshHandle
        {
            std::uint32_t vertexOffset;
            std::uint32_t vertexCount;

            std::uint32_t indexOffset;
            std::uint32_t indexCount;
        };

        static constexpr std::uint32_t VertexBufferSize = 1024 * 1024 * 8; // 8 MB
        static constexpr std::uint32_t IndexBufferSize = 1024 * 1024 * 8;  // 8 MB
        static constexpr std::uint32_t MaxDrawnObjects = 16384;            // todo better way to size this?

      private:
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

        SceneConstants sceneConstants{};
        std::vector<SDL_GPUIndexedIndirectDrawCommand> drawCommands{};
        std::vector<ObjectData> objectData{};

        std::vector<Vertex> vertexUploads{};
        std::vector<std::uint32_t> indexUploads{};

        std::vector<std::size_t> queuedGpuMeshUploadHashes{};
        std::unordered_map<std::size_t, GPUMeshHandle> gpuMeshHandles{};

        std::uint32_t gpuMeshVertexOffset{0};
        std::uint32_t gpuMeshIndexOffset{0};

        struct
        {
            glm::vec3 position{0, 2.5, -10};
            glm::quat rotation{1, 0, 0, 0};
        } cameraState;

        const ThreadBatch& threadBatch;
        std::size_t envIndex;

        std::uint64_t lastSnapshotTimeNS;
        std::uint32_t snapshotUpdateHz;
        flecs::system snapshotSystem;

        std::mutex renderMutex;
        std::atomic<bool> isQuitting;

        bool showFullGrid{false};

        [[nodiscard]] SDL_GPUShader* LoadShader(std::filesystem::path path, SDL_GPUShaderStage stage, const std::string& entryPoint, uint32_t numUniformBuffers, uint32_t numStorageBuffers) const;

      public:
        explicit RenderWindow(const ThreadBatch& threadBatch, std::size_t envIndex);

        void pollEvents();
        void render(float deltaTime);
        void dispose();

        [[nodiscard]] const GPUMeshHandle& RequestUploadedGPUMesh(const std::size_t& hash);
        [[nodiscard]] bool getIsQuitting() const;
    };
}