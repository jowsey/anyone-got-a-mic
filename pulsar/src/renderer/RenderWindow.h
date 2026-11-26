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
#include <flat_map>
#include <map>
#include <mutex>
#include <queue>

namespace pulsar::Renderer
{
    struct GPUMeshHandle
    {
        std::uint32_t vertexOffset;
        std::uint32_t vertexCount;

        std::uint32_t indexOffset;
        std::uint32_t indexCount;
    };

    struct GPUTextureHandle
    {
        std::uint32_t bucketRes;
        std::uint32_t index;
    };

    enum class GPUUploadType
    {
        Mesh,
        Texture
    };

    class RenderWindow
    {
        struct alignas(16) SceneConstants
        {
            glm::mat4 viewProj;
        };

        struct alignas(16) Material
        {
            // albedo, -, -, -, -, -, -, -
            std::uint32_t hasMapMask{0};

            std::uint32_t albedoTexture{0};
            std::uint32_t albedoLayer{0};
        };

        struct alignas(16) ObjectData
        {
            glm::mat4 modelMatrix{1.0f};
            Material material;
        };

        struct TextureBucket
        {
            SDL_GPUTexture* texture{nullptr};
            std::uint32_t size{0};
            bool hasChanged{false};
        };

      public:
        static constexpr std::uint32_t VertexBufferSize = 1024 * 1024 * 8; // 8 MB
        static constexpr std::uint32_t IndexBufferSize = 1024 * 1024 * 8;  // 8 MB
        static constexpr std::uint32_t MaxDrawnObjects = 16384;            // todo better way to auto-size these?
        static constexpr std::uint32_t MaxTexturesPerBucket = 64;

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
        SDL_GPUTextureFormat depthTextureFormat{SDL_GPU_TEXTUREFORMAT_D32_FLOAT};

        SDL_GPUSampler* linearSampler{nullptr};

        std::flat_map<std::uint32_t, TextureBucket> textureBuckets{};

        SDL_GPUTexture* dummyTextureArray{nullptr};

        SceneConstants sceneConstants{};
        std::vector<SDL_GPUIndexedIndirectDrawCommand> drawCommands{};
        std::vector<ObjectData> objectData{};

        std::unordered_map<std::size_t, GPUMeshHandle> gpuMeshHandles{};
        std::unordered_map<std::size_t, GPUTextureHandle> gpuTextureHandles{};

        std::vector<std::pair<std::size_t, GPUUploadType>> queuedGPUUploadHashes{};

        std::vector<Vertex> vertexUploadScratch{};
        std::vector<std::uint32_t> indexUploadScratch{};
        std::vector<std::uint8_t> textureUploadScratch{};

        std::uint32_t nextVertexBufferOffset{0};
        std::uint32_t nextIndexBufferOffset{0};

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

        [[nodiscard]] SDL_GPUShader* LoadShader(std::filesystem::path path, SDL_GPUShaderStage stage, const std::string& entryPoint, uint32_t numUniformBuffers, uint32_t numStorageBuffers, uint32_t numSamplers) const;

      public:
        explicit RenderWindow(const ThreadBatch& threadBatch, std::size_t envIndex);

        void pollEvents();
        void render(float deltaTime);
        void dispose();

        [[nodiscard]] const GPUMeshHandle& RequestUploadedGPUMesh(const std::size_t& hash);
        [[nodiscard]] const GPUTextureHandle& RequestUploadedGPUTexture(const std::size_t& hash);
        [[nodiscard]] bool getIsQuitting() const;
    };
}