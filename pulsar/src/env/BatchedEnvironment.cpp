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

    // Register Box3D default defs
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = b3Vec3{.x = 0.0f, .y = -9.81f, .z = 0.0f};
    worldDef.workerCount = 1;

    b3BodyDef floorBodyDef = b3DefaultBodyDef();
    floorBodyDef.type = b3_staticBody;
    b3BodyDef blockBodyDef = b3DefaultBodyDef();
    blockBodyDef.type = b3_dynamicBody;

    std::latch threadsReady(numThreads);

    // Launch worker pool
    threadBatches.reserve(numThreads);
    for (std::size_t threadI = 0; threadI < numThreads; ++threadI)
    {
        auto& threadBatch = threadBatches.emplace_back();
        threadBatch.envRoots.reserve(batchSize);
        threadBatch.b3WorldIds.reserve(batchSize);
        threadBatch.agentObservations.resize(batchSize);

        for (std::size_t envI = 0; envI < batchSize; ++envI)
        {
            // Create physics world
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
        threadBatch.worker = std::jthread([&, threadI, fixedDeltaTime, floorBodyDef, blockBodyDef](const std::stop_token& stopToken) mutable {
            spdlog::debug("[Worker {}] Worker starting...", threadI + 1);

            const auto dust2Prefab = buildPrefabFromAsset(Utils::Path::getEnginePath() / "resources/models/maps/Dust2/Dust2.obj", threadBatch.sharedWorld);
            dust2Prefab->set<Components::Transform>({.localRotation = glm::quat_cast(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)))}); // -Z up -> +Y up

            for (std::size_t envI = 0; envI < batchSize; ++envI)
            {
                const auto root = threadBatch.envRoots[envI];
                const auto b3WorldId = threadBatch.b3WorldIds[envI];

                const auto cubeHash = std::hash<std::string>{}("primitive::cube");

                // Create ground floor
                auto floor = createEnvObject("EnvFloor" + std::to_string(envI), root);
                floorBodyDef.userData = reinterpret_cast<void*>(floor.id());

                Components::MeshHandleSet cubeHandleSet{.mesh = cubeHash};

                (void)floor
                    .set<Components::Transform>({.localPosition = glm::vec3{0.0f, -0.25f, 0.0f}, .localScale = glm::vec3{10.0f, 0.25f, 10.0f}})
                    .set<Components::RigidBody>({.worldId = b3WorldId, .bodyId = b3CreateBody(b3WorldId, &floorBodyDef)})
                    .set<Components::BoxCollider>({.extents = glm::vec3{5.0f, 0.125f, 5.0f}})
                    .set<Components::MeshRenderer>({.meshes = {cubeHandleSet}});

                // Create falling blocks
                for (std::size_t i = 0; i < 5; ++i)
                {
                    std::mt19937 rng{std::random_device{}()};
                    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

                    auto block = createEnvObject("Block" + std::to_string(envI) + "_" + std::to_string(i + 1), root);
                    blockBodyDef.userData = reinterpret_cast<void*>(block.id());

                    (void)block
                        .set<Components::Transform>({.localPosition = glm::vec3{dist(rng), 5.0f, dist(rng)}})
                        .set<Components::RigidBody>({.worldId = b3WorldId, .bodyId = b3CreateBody(b3WorldId, &blockBodyDef)})
                        .set<Components::BoxCollider>({.extents = glm::vec3{0.5f}})
                        .set<Components::MeshRenderer>({.meshes = {cubeHandleSet}});
                }

                // Create map
                auto map = threadBatch.sharedWorld.entity(flecs::Parent{root}, ("Dust2Map" + std::to_string(envI)).c_str())
                               .is_a(dust2Prefab.value());

                // recursively update env ids
                const auto updateEnvIds = [&](this auto&& self, const flecs::entity& entity) -> void {
                    entity.set<Components::EnvironmentId>({.envId = envI});
                    entity.children([&](const flecs::entity& child) {
                        self(child);
                    });
                };

                updateEnvIds(map);
            }

            spdlog::debug("[Worker {}] Worker ready", threadI + 1);
            threadsReady.arrive_and_wait();

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
    return parent.world()
        .entity(flecs::Parent{parent}, name.c_str())
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
        auto& meshRenderer = entity.ensure<Components::MeshRenderer>();

        for (std::uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const auto meshIndex = node->mMeshes[i];
            const auto mesh = scene->mMeshes[meshIndex];

            std::string meshPath = path.generic_string() + ":mesh" + std::to_string(meshIndex);
            std::size_t meshHash = std::hash<std::string>{}(meshPath);

            Components::MeshHandleSet handleSet{.mesh = meshHash};

            // Add mesh to cache if necessary
            if (!meshCache.contains(meshHash))
            {
                // Geometry
                // spdlog::debug("({}) Mesh {}: {} vertices, {} faces", node->mName.C_Str(), meshIndex, mesh->mNumVertices, mesh->mNumFaces);

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

            // Materials
            constexpr auto textureTypes = std::to_array<std::pair<aiTextureType, std::optional<std::size_t> Components::MeshHandleSet::*>>(
                {{aiTextureType_DIFFUSE, &Components::MeshHandleSet::albedo},
                 {aiTextureType_BASE_COLOR, &Components::MeshHandleSet::albedo}}
            );

            for (const auto& [type, handleMember] : textureTypes)
            {
                if (handleSet.*handleMember != std::nullopt)
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

                handleSet.*handleMember = textureHash;

                if (textureCache.contains(textureHash))
                {
                    // spdlog::debug("({}) Mesh {}: Texture already cached at {}", node->mName.C_Str(), meshIndex, texturePath.C_Str());
                    continue;
                }

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

            meshRenderer.meshes.push_back(handleSet);
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
    return textureCache.contains(hash) ? &textureCache.at(hash) : nullptr;
}

const pulsar::Renderer::MeshData* pulsar::BatchedEnvironment::getCachedMesh(const std::size_t hash)
{
    return meshCache.contains(hash) ? &meshCache.at(hash) : nullptr;
}