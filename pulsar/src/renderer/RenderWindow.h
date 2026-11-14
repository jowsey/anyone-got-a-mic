#pragma once

#include "../env/BatchedEnvironment.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_video.h"
#include "glm/fwd.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#include <filesystem>

namespace pulsar::Renderer
{
    class RenderWindow
    {
        struct alignas(16) Vertex
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
        static constexpr std::uint32_t IndexBufferSize = 1024 * 128;  // 128 KB
        static constexpr std::uint32_t MaxDrawnObjects = 1024;

        SDL_Window* window;
        SDL_GPUDevice* device;
        SDL_GPUGraphicsPipeline* pipeline;

        SDL_GPUBuffer* vertexBuffer;
        SDL_GPUBuffer* indexBuffer;
        SDL_GPUBuffer* indirectBuffer;
        SDL_GPUBuffer* objectDataBuffer;
        SDL_GPUTransferBuffer* scratchTransferBuffer;
        SDL_GPUTexture* depthTexture;

        std::vector<MeshHandle> meshRegistry{};

        SceneConstants sceneConstants{};
        std::vector<SDL_GPUIndexedIndirectDrawCommand> drawCommands{};
        std::vector<ObjectData> objectData{};

        std::size_t envIndex;
        std::atomic<bool> isQuitting;

        std::uint64_t lastSnapshotTimeNS;
        std::uint64_t snapshotUpdateHz;
        flecs::system snapshotSystem;

        std::mutex snapshotMutex;

        [[nodiscard]] SDL_GPUShader* LoadShader(std::filesystem::path path, SDL_GPUShaderStage stage, const std::string& entryPoint, uint32_t numUniformBuffers, uint32_t numStorageBuffers) const;

      public:
        struct MeshData
        {
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;
        };

        explicit RenderWindow(const ThreadBatch& batch, std::size_t envIndex);

        void pollEvents();
        void render();
        void dispose() const;

        [[nodiscard]] std::uint32_t RegisterMesh(const MeshData& data);
        [[nodiscard]] bool getIsQuitting() const;
    };
}
