#include "Physics.h"

#include "../../util/FormatConversion.h"
#include "../components/RigidBody.h"
#include "../components/Transform.h"
#include "box3d/box3d.h"

void pulsar::Systems::Physics::Register(const flecs::world& world, const std::vector<b3WorldId>& worldIds, const float deltaTime)
{
    world.system<Components::Transform, Components::RigidBody>("PhysicsSystem")
        .kind(flecs::PostUpdate)
        .run([&, deltaTime](flecs::iter& it) {
            while (it.next())
            {
                const auto transform = it.field<Components::Transform>(0);
                const auto body = it.field<Components::RigidBody>(1);

                if (body->isTeleportDirty)
                {
                    const glm::vec3& pos = transform->localPosition;
                    const glm::quat& rot = transform->localRotation;
                    b3Body_SetTransform(body->bodyId, Utils::Formats::glmToB3Vec3(pos), Utils::Formats::glmToB3Quat(rot));
                    body->isTeleportDirty = false;
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

                    const auto* entityId = static_cast<flecs::entity_t*>(event.userData);
                    flecs::entity entity = world.entity(*entityId);

                    auto& transform = entity.get_mut<Components::Transform>();
                    transform.setLocalPose(
                        Utils::Formats::b3ToGlmVec3(newPos),
                        Utils::Formats::b3ToGlmQuat(newRot)
                    );
                }
            }
        });
}