#pragma once
#include <thread>
#include <vector>

#include "../renderer/MeshData.h"
#include "box3d/id.h"
#include "flecs.h"
#include "flecs/addons/cpp/world.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/fwd.hpp"

#include <barrier>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace pulsar
{
    struct AgentObservation
    {
        float placeholder;
    };

    struct ThreadBatch
    {
        flecs::world sharedWorld;
        std::vector<flecs::entity> envRoots;
        std::vector<b3WorldId> b3WorldIds;
        std::vector<std::vector<AgentObservation>> agentObservations;

        std::jthread worker;
    };

    class BatchedEnvironment
    {
        std::string name;
        std::vector<ThreadBatch> threadBatches;

        std::barrier<> jobsAvailableBarrier;
        std::barrier<> workDoneBarrier;

        inline static std::unordered_map<std::size_t, Renderer::MeshData> meshCache;
        // static std::unordered_map<std::size_t, Renderer::RenderWindow::Texture2D> textureCache; // todo

      public:
        explicit BatchedEnvironment(const std::string& batchName, std::size_t batchSize, std::uint32_t numThreads, float fixedDeltaTime);
        ~BatchedEnvironment();

        void stepAll();

        [[nodiscard]] const std::vector<ThreadBatch>& getThreadBatches() const;
        [[nodiscard]] const ThreadBatch& getThreadBatch(std::size_t index) const;

        static flecs::entity createWorldObject(const std::string& name, const flecs::entity& parent);
        static flecs::entity createEnvObject(const std::string& name, const flecs::entity& parent);
        static std::optional<flecs::entity> buildPrefabFromAsset(const std::filesystem::path& path, const flecs::world& world);

        static const Renderer::MeshData* getCachedMesh(std::size_t hash);
    };
}
