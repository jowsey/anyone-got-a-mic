#include "BatchedEnvironment.h"

#include "../renderer/Primitives.h"
#include "../util/FormatConversion.h"
#include "../util/Path.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "box3d/box3d.h"
#include "box3d/types.h"
#include "components/BoxCollider.h"
#include "components/Environment.h"
#include "components/EnvironmentId.h"
#include "components/MeshCollider.h"
#include "components/MeshRenderer.h"
#include "components/RigidBody.h"
#include "components/Transform.h"
#include "flecs.h"
#include "spdlog/spdlog.h"
#include "systems/Physics.h"
#include "systems/Transform.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <barrier>
#include <filesystem>
#include <latch>
#include <random>
#include <ranges>

/// Create a new batched environment
/// @param batchName Descriptive name for this batch
/// @param batchSize Number of environments to simulate per thread
/// @param numThreads Number of parallel threads to use
/// @param fixedDeltaTime Fixed tick time-step
pulsar::BatchedEnvironment::BatchedEnvironment(const std::string& batchName, const std::size_t batchSize, const std::uint32_t numThreads, const float fixedDeltaTime)
    : name(batchName), jobsAvailableBarrier(numThreads + 1), workDoneBarrier(numThreads + 1)
{
    spdlog::info("Launching batch '{}' with {} environments across {} thread(s)", batchName, batchSize * numThreads, numThreads);
    assert(batchSize * numThreads <= B3_MAX_WORLDS && "Batch size exceeds max Box3D worlds. Increase B3_MAX_WORLDS in CMakeLists.txt.");

    // Register primitive meshes
    for (const auto& [meshName, meshData] : Renderer::PrimitiveMeshes)
    {
        meshCache.emplace(std::hash<std::string>{}("primitive::" + meshName), meshData);
    }

    // Data
    constexpr std::pair team1SpawnBounds = {glm::vec3{-20.0f, 5.0f, -21.0f}, glm::vec3{-15.0f, 5.0f, -18.0f}};
    // constexpr std::pair team2SpawnBounds = {glm::vec3{5.0f, -1.0f, 55.0f}, glm::vec3{11.0f, -1.0f, 60.0f}};

    // Launch worker pool
    threadBatches.reserve(numThreads);
    std::latch threadsReady(numThreads);

    // mesh hash -> b3 data
    std::unordered_map<std::size_t, b3MeshData*> meshColliderCache{};
    std::mutex meshColliderCacheMutex{};

    for (std::size_t threadI = 0; threadI < numThreads; ++threadI)
    {
        auto& threadBatch = threadBatches.emplace_back();
        threadBatch.envRoots.reserve(batchSize);
        threadBatch.b3WorldIds.reserve(batchSize);
        threadBatch.agentObservations.resize(batchSize);

        for (std::size_t envI = 0; envI < batchSize; ++envI)
        {
            // Create physics world
            b3WorldDef worldDef = b3DefaultWorldDef();
            worldDef.gravity = b3Vec3{.x = 0.0f, .y = -9.81f, .z = 0.0f};
            worldDef.workerCount = 1;

            const auto b3WorldId = b3CreateWorld(&worldDef);
            threadBatch.b3WorldIds.push_back(b3WorldId);

            // Create env root
            const auto root = threadBatch.sharedWorld.entity()
                                  .set<Components::Environment>({b3WorldId})
                                  .set<Components::EnvironmentId>({envI});
            threadBatch.envRoots.push_back(root);
        }

        // Transform system
        Systems::Transform::Register(threadBatch.sharedWorld);
        // Physics system
        Systems::Physics::Register(threadBatch.sharedWorld, threadBatch.b3WorldIds, fixedDeltaTime);

        // Start worker
        threadBatch.worker = std::jthread([&, threadI, fixedDeltaTime](const std::stop_token& stopToken) mutable {
            spdlog::debug("[Worker {}] Worker starting...", threadI + 1);

            // todo ideally deduplicate this across threads
            const auto dust2Prefab = buildPrefabFromAsset(Utils::Path::getEnginePath() / "resources/models/maps/Dust2/Dust2.obj", threadBatch.sharedWorld);
            dust2Prefab->set<Components::Transform>({.localRotation = glm::quat_cast(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)))}); // -Z up -> +Y up

            // Recursively generate map colliders, todo profile
            auto recursiveBuildMapBodies = [&](this auto&& self, const flecs::entity& entity) -> void {
                const auto meshRenderer = entity.try_get<Components::MeshRenderer>();

                if (meshRenderer && meshRenderer->mesh.has_value())
                {
                    const auto meshHash = meshRenderer->mesh.value();

                    {
                        std::lock_guard lock(meshColliderCacheMutex);

                        if (!meshColliderCache.contains(meshHash))
                        {
                            auto* meshData = getCachedMesh(meshHash);
                            auto b3Vertices = meshData->vertices | std::views::transform(&Renderer::Vertex::position) | std::views::transform(Utils::Formats::glmToB3Vec3) | std::ranges::to<std::vector>();
                            auto b3Indices = meshData->indices | std::ranges::to<std::vector<int32_t>>();

                            const b3MeshDef meshDef{
                                .vertices = b3Vertices.data(),
                                .indices = b3Indices.data(),
                                .weldTolerance = 0.0001f,
                                .vertexCount = static_cast<int>(b3Vertices.size()),
                                .triangleCount = static_cast<int>(b3Indices.size() / 3),
                                .weldVertices = true,
                                .identifyEdges = true,
                            };

                            meshColliderCache[meshHash] = b3CreateMesh(&meshDef, nullptr, 0);
                        }

                        auto& meshCollider = entity.ensure<Components::MeshCollider>();
                        meshCollider.meshData = meshColliderCache[meshHash];
                    }
                }

                entity.children([&](const flecs::entity& child) {
                    self(child);
                });
            };

            recursiveBuildMapBodies(dust2Prefab.value());

            // Build envs
            for (std::size_t envI = 0; envI < batchSize; ++envI)
            {
                const auto root = threadBatch.envRoots[envI];
                const auto b3WorldId = threadBatch.b3WorldIds[envI];

                // Create map
                auto map = threadBatch.sharedWorld.entity(flecs::Parent{root}, ("Dust2Map" + std::to_string(envI)).c_str())
                               .is_a(dust2Prefab.value());

                // Build map for this env
                const auto recursiveBuildMapForEnv = [&](this auto&& self, const flecs::entity& entity) -> void {
                    entity.set<Components::EnvironmentId>({.envId = envI});

                    if (const auto meshCollider = entity.try_get_mut<Components::MeshCollider>())
                    {
                        b3BodyDef bodyDef = b3DefaultBodyDef();
                        bodyDef.type = b3_staticBody;

                        const b3BodyId bodyId = b3CreateBody(b3WorldId, &bodyDef);

                        const b3ShapeDef shapeDef = b3DefaultShapeDef();
                        const b3ShapeId shapeId = b3CreateMeshShape(bodyId, &shapeDef, meshCollider->meshData, b3Vec3(1, 1, 1));
                        meshCollider->shapeId = shapeId;

                        entity.set<Components::RigidBody>({.worldId = b3WorldId, .bodyId = bodyId});
                    }

                    entity.children([&](const flecs::entity& child) {
                        self(child);
                    });
                };

                recursiveBuildMapForEnv(map);

                // Create falling blocks
                for (std::size_t i = 0; i < 10; ++i)
                {
                    std::mt19937 rng{std::random_device{}()};
                    std::uniform_real_distribution xDist(team1SpawnBounds.first.x, team1SpawnBounds.second.x);
                    std::uniform_real_distribution zDist(team1SpawnBounds.first.z, team1SpawnBounds.second.z);

                    b3BodyDef blockBodyDef = b3DefaultBodyDef();
                    blockBodyDef.type = b3_dynamicBody;

                    auto block = createEnvObject("Block" + std::to_string(envI) + "_" + std::to_string(i + 1), root);
                    blockBodyDef.userData = reinterpret_cast<void*>(block.id());

                    b3BodyId blockBodyId = b3CreateBody(b3WorldId, &blockBodyDef);

                    (void)block
                        .set<Components::Transform>({.localPosition = glm::vec3{xDist(rng), team1SpawnBounds.first.y, zDist(rng)}})
                        .set<Components::RigidBody>({.worldId = b3WorldId, .bodyId = blockBodyId})
                        .set<Components::BoxCollider>({.extents = glm::vec3{0.5f}})
                        .set<Components::MeshRenderer>({.mesh = std::hash<std::string>{}("primitive::cube")});
                }
            }

            spdlog::debug("[Worker {}] Worker ready", threadI + 1);
            threadsReady.count_down();

            while (true)
            {
                jobsAvailableBarrier.arrive_and_wait();

                if (stopToken.stop_requested()) break;

                const auto ecsContinue = threadBatch.sharedWorld.progress(fixedDeltaTime);
                if (!ecsContinue)
                {
                    spdlog::warn("[Worker {}] ECS has quit?", threadI + 1);
                }

                workDoneBarrier.arrive_and_wait();
            }

            spdlog::debug("[Worker {}] Worker stopped", threadI + 1);
        });
    }

    threadsReady.wait();
}

pulsar::BatchedEnvironment::~BatchedEnvironment()
{
    // Finish all work
    for (auto& batch : threadBatches)
    {
        batch.worker.request_stop();
    }

    jobsAvailableBarrier.arrive_and_wait();

    for (auto& batch : threadBatches)
    {
        if (batch.worker.joinable())
        {
            batch.worker.join();
        }

        for (const auto& b3WorldId : batch.b3WorldIds)
        {
            b3DestroyWorld(b3WorldId);
        }
    }
}

void pulsar::BatchedEnvironment::stepAll()
{
    jobsAvailableBarrier.arrive_and_wait();
    workDoneBarrier.arrive_and_wait();
}

const pulsar::ThreadBatch& pulsar::BatchedEnvironment::getThreadBatch(const std::size_t index) const
{
    return threadBatches.at(index);
}

const std::vector<pulsar::ThreadBatch>& pulsar::BatchedEnvironment::getThreadBatches() const
{
    return threadBatches;
}

flecs::entity pulsar::BatchedEnvironment::createWorldObject(const std::string& name, const flecs::entity& parent)
{
    std::uint32_t duplicateSiblingCount = 0;
    std::string deduplicatedName = name;

    while (parent.lookup(deduplicatedName.c_str()).is_valid())
    {
        deduplicatedName = std::format("{} ({})", name, ++duplicateSiblingCount);
    }

    if (duplicateSiblingCount > 0)
    {
        spdlog::warn("Entity with name '{}' already exists under parent '{}', using '({})'", name, parent.name().c_str(), duplicateSiblingCount);
    }

    return parent.world()
        .entity(flecs::Parent{parent}, deduplicatedName.c_str())
        .add<Components::EnvironmentId>()
        .add<Components::Transform>();
}

flecs::entity pulsar::BatchedEnvironment::createEnvObject(const std::string& name, const flecs::entity& parent)
{
    const auto envId = parent.get<Components::EnvironmentId>().envId;
    return createWorldObject(name, parent)
        .set<Components::EnvironmentId>({.envId = envId});
}

std::optional<flecs::entity> pulsar::BatchedEnvironment::buildPrefabFromAsset(const std::filesystem::path& path, const flecs::world& world)
{
    spdlog::trace("Importing asset prefab from {}", path.lexically_normal().string());

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ConvertToLeftHanded |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        spdlog::error("Failed to import asset at {}: {}", path.lexically_normal().string(), importer.GetErrorString());
        return std::nullopt;
    }

    const auto recursiveBuild = [&](this auto&& self, const aiNode* node, const flecs::entity& parent) -> flecs::entity {
        const auto entity = createWorldObject(node->mName.C_Str(), parent).add(flecs::Prefab);

        for (std::uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const auto meshEntity = i == 0 ? entity : createWorldObject("Mesh" + std::to_string(i), entity).add(flecs::Prefab);
            auto& meshRenderer = meshEntity.ensure<Components::MeshRenderer>();

            const auto meshIndex = node->mMeshes[i];
            const auto mesh = scene->mMeshes[meshIndex];

            std::string meshPath = path.generic_string() + ":mesh" + std::to_string(meshIndex);
            std::size_t meshHash = std::hash<std::string>{}(meshPath);

            std::unique_lock meshCacheLock(meshCacheMutex);

            // Add mesh to cache if necessary
            if (!meshCache.contains(meshHash))
            {
                // Geometry
                auto geometry = Renderer::MeshData{
                    .vertices = std::vector<Renderer::Vertex>(mesh->mNumVertices),
                    .indices = std::vector<std::uint32_t>(mesh->mNumFaces * 3)
                };

                for (std::uint32_t v = 0; v < mesh->mNumVertices; ++v)
                {
                    const auto& pos = mesh->mVertices[v];
                    const auto& col = mesh->HasVertexColors(0) ? mesh->mColors[0][v] : aiColor4D{1.0f, 1.0f, 1.0f, 1.0f};
                    const auto& uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D{0.0f, 0.0f, 0.0f};
                    // const auto& normal = mesh->mNormals[v];

                    geometry.vertices[v] = Renderer::Vertex{
                        .position = glm::vec3{pos.x, pos.y, pos.z},
                        .colour = glm::vec4{col.r, col.g, col.b, col.a},
                        .uv = glm::vec2{uv.x, uv.y}
                    };
                }

                for (std::uint32_t f = 0; f < mesh->mNumFaces; ++f)
                {
                    const auto& face = mesh->mFaces[f];
                    geometry.indices[f * 3 + 0] = face.mIndices[0];
                    geometry.indices[f * 3 + 1] = face.mIndices[1];
                    geometry.indices[f * 3 + 2] = face.mIndices[2];
                }

                meshCache[meshHash] = std::move(geometry);
            }

            meshCacheLock.unlock();

            meshRenderer.mesh = meshHash;

            // Materials
            constexpr auto textureTypes = std::to_array<std::pair<aiTextureType, std::optional<std::size_t> Components::MeshRenderer::*>>(
                {{aiTextureType_DIFFUSE, &Components::MeshRenderer::albedo},
                 {aiTextureType_BASE_COLOR, &Components::MeshRenderer::albedo}}
            );

            for (const auto& [type, handleMember] : textureTypes)
            {
                if (meshRenderer.*handleMember != std::nullopt)
                {
                    // spdlog::debug("({}) Mesh {}: Texture of type {} already set, skipping fallback", node->mName.C_Str(), meshIndex, aiTextureTypeToString(type));
                    continue;
                }

                aiString texturePath;
                if (scene->mMaterials[mesh->mMaterialIndex]->GetTexture(type, 0, &texturePath) != AI_SUCCESS)
                {
                    spdlog::warn("({}) Mesh {}: Failed to get texture of type {}", node->mName.C_Str(), meshIndex, aiTextureTypeToString(type));
                    continue;
                }

                // spdlog::debug("({}) Mesh {}: Found texture of type {} at {}", node->mName.C_Str(), meshIndex, aiTextureTypeToString(type), texturePath.C_Str());

                std::filesystem::path texPath = path.parent_path() / texturePath.C_Str();
                std::size_t textureHash = std::hash<std::string>{}(texPath.generic_string());

                std::unique_lock texCacheLock(textureCacheMutex);

                if (!textureCache.contains(textureHash))
                {
                    int texWidth;
                    int texHeight;
                    auto pixels = stbi_load(texPath.string().c_str(), &texWidth, &texHeight, nullptr, 4);
                    if (!pixels)
                    {
                        spdlog::warn("({}) Mesh {}: Failed to load texture at {}", node->mName.C_Str(), meshIndex, texturePath.C_Str());
                        continue;
                    }

                    auto textureData = Renderer::TextureData{
                        .width = static_cast<std::uint32_t>(texWidth),
                        .height = static_cast<std::uint32_t>(texHeight),
                        .data = std::vector(pixels, pixels + (texWidth * texHeight * 4))
                    };

                    stbi_image_free(pixels);

                    textureCache[textureHash] = std::move(textureData);
                }
                else
                {
                    // spdlog::debug("({}) Mesh {}: Texture already cached at {}", node->mName.C_Str(), meshIndex, texturePath.C_Str());
                }

                texCacheLock.unlock();

                meshRenderer.*handleMember = textureHash;
            }
        }

        for (std::uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            self(node->mChildren[i], entity);
        }

        return entity;
    };

    auto entity = recursiveBuild(scene->mRootNode, world.entity(flecs::World));
    return entity;
}

const pulsar::Renderer::TextureData* pulsar::BatchedEnvironment::getCachedTexture(const std::size_t hash)
{
    std::lock_guard lock(textureCacheMutex);
    return textureCache.contains(hash) ? &textureCache.at(hash) : nullptr;
}

const pulsar::Renderer::MeshData* pulsar::BatchedEnvironment::getCachedMesh(const std::size_t hash)
{
    std::lock_guard lock(meshCacheMutex);
    return meshCache.contains(hash) ? &meshCache.at(hash) : nullptr;
}