#include "Physics.h"

#include "../../util/FormatConversion.h"
#include "../components/BoxCollider.h"
#include "../components/RigidBody.h"
#include "../components/Transform.h"
#include "box3d/box3d.h"
#include "glm/gtx/io.hpp"
#include "spdlog/spdlog.h"

void pulsar::Systems::Physics::Register(const flecs::world& world, const std::vector<b3WorldId>& worldIds, const float deltaTime)
{
    // Physics component lifecycle hooks
    world.observer<const Components::RigidBody, Components::BoxCollider>()
        .term_at(0)
        .filter() // ignore rb changes
        .event(flecs::OnSet)
        .each([](flecs::entity /*e*/, const Components::RigidBody& body, Components::BoxCollider& box) {
            // spdlog::debug("Creating box collider on entity {}", e.name().c_str());

            b3ShapeDef shapeDef = b3DefaultShapeDef();
            shapeDef.density = 1.0f;
            shapeDef.baseMaterial.friction = 0.5f;

            b3BoxHull hull = b3MakeOffsetBoxHull(box.extents.x, box.extents.y, box.extents.z, Utils::Formats::glmToB3Vec3(box.center));
            box.shapeId = b3CreateHullShape(body.bodyId, &shapeDef, &hull.base);
        });

    world.observer<const Components::Transform, const Components::RigidBody>()
        .term_at(0)
        .filter() // ignore transform changes
        .event(flecs::OnSet)
        .each([](flecs::entity /*e*/, const Components::Transform& transform, const Components::RigidBody& body) {
            // Initial teleport to transform
            // todo not sure if this is bad
            b3Body_SetTransform(
                body.bodyId,
                Utils::Formats::glmToB3Vec3(transform.getWorldPosition()),
                Utils::Formats::glmToB3Quat(transform.getWorldRotation())
            );
        });

    world.observer<const Components::RigidBody>()
        .event(flecs::OnRemove)
        .each([](const flecs::entity e, const Components::RigidBody& body) {
            spdlog::debug("Destroying rigidbody on entity {}", e.name().c_str());
            b3DestroyBody(body.bodyId);
        });

    // General physics syncing
    world.system<Components::Transform, Components::RigidBody>("PhysicsSystem")
        .kind(flecs::PostUpdate)
        .run([&, deltaTime](flecs::iter& it) {
            while (it.next())
            {
                const auto transform = it.field<Components::Transform>(0);
                const auto body = it.field<Components::RigidBody>(1);

                for (const auto i : it)
                {
                    const auto worldPos = transform[i].getWorldPosition();
                    const auto worldRot = transform[i].getWorldRotation();

                    auto physicsPose = b3Body_GetTransform(body[i].bodyId);
                    const auto physPos = Utils::Formats::b3ToGlmVec3(physicsPose.p);
                    const auto physRot = Utils::Formats::b3ToGlmQuat(physicsPose.q);

                    constexpr float posEpsilonSq = 1e-10f; // 1e-5
                    constexpr float rotEps = 1e-6f;

                    const bool posChanged = glm::length2(worldPos - physPos) > posEpsilonSq;
                    const bool rotChanged = 1.0f - std::abs(glm::dot(worldRot, physRot)) > rotEps;

                    // Handle teleport
                    if (posChanged || rotChanged)
                    {
                        spdlog::trace("Entity '{}' (w {}, b {}) teleported by {} units and {} degrees",
                                     it.entity(i).name().c_str(), body[i].worldId.index1, body[i].bodyId.index1,
                                     glm::length(worldPos - physPos), glm::degrees(glm::acos(glm::clamp(glm::dot(worldRot, physRot), 0.0f, 1.0f))) * 2.0f);

                        b3Body_SetTransform(body[i].bodyId, Utils::Formats::glmToB3Vec3(worldPos), Utils::Formats::glmToB3Quat(worldRot));
                    }
                }
            }

            for (const b3WorldId worldId : worldIds)
            {
                b3World_Step(worldId, deltaTime, 4);

                const b3BodyEvents bodyEvents = b3World_GetBodyEvents(worldId);
                for (int i = 0; i < bodyEvents.moveCount; i++)
                {
                    const auto event = bodyEvents.moveEvents[i];
                    const auto newPos = event.transform.p;
                    const auto newRot = event.transform.q;

                    const auto rawId = reinterpret_cast<flecs::entity_t>(event.userData);
                    flecs::entity entity(it.world(), rawId);

                    if (!entity.is_alive())
                    {
                        spdlog::warn("Entity {} ({}) is in physics but not alive!", rawId, entity.name().c_str());
                        continue;
                    }

                    auto& transform = entity.get_mut<Components::Transform>();
                    transform.setLocalPose(
                        Utils::Formats::b3ToGlmVec3(newPos),
                        Utils::Formats::b3ToGlmQuat(newRot)
                    );
                }
            }
        });
}