#include "Physics.h"

#include "../../util/FormatConversion.h"
#include "../components/BoxCollider.h"
#include "../components/Environment.h"
#include "../components/RigidBody.h"
#include "../components/Transform.h"
#include "box3d/box3d.h"
#include "spdlog/spdlog.h"

void pulsar::Systems::Physics::Register(const flecs::world& world, const std::vector<b3WorldId>& worldIds, const float deltaTime)
{
    // Physics component lifecycle hooks
    world.observer<const Components::RigidBody, Components::BoxCollider>()
        .term_at(0)
        .filter() // ignore rb changes
        .event(flecs::OnSet)
        .each([](flecs::entity /*e*/, const Components::RigidBody& body, Components::BoxCollider& box) {
            spdlog::debug("Creating box collider for body {}", body.bodyId.index1);

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
                Utils::Formats::glmToB3Vec3(transform.localPosition),
                Utils::Formats::glmToB3Quat(transform.localRotation)
            );
        });

    world.observer<const Components::RigidBody>()
        .event(flecs::OnRemove)
        .each([](const flecs::entity /*e*/, const Components::RigidBody& body) {
            spdlog::debug("Destroying body {}", body.bodyId.index1);
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
                    auto physicsPose = b3Body_GetTransform(body[i].bodyId);
                    const auto physPos = Utils::Formats::b3ToGlmVec3(physicsPose.p);
                    const auto physRot = Utils::Formats::b3ToGlmQuat(physicsPose.q);

                    // Handle teleport
                    if (transform[i].localPosition != physPos || transform[i].localRotation != physRot)
                    {
                        spdlog::debug("Body {} teleported by {} units", body[i].bodyId.index1, glm::length(transform[i].localPosition - physPos));
                        b3Body_SetTransform(
                            body[i].bodyId,
                            Utils::Formats::glmToB3Vec3(transform[i].localPosition),
                            Utils::Formats::glmToB3Quat(transform[i].localRotation)
                        );
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
                        spdlog::warn("Entity {} is in physics but not alive!");
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