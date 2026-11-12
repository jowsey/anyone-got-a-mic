#include "Transform.h"
#include "../components/Transform.h"

#include "glm/gtc/quaternion.hpp"

void pulsar::Systems::Transform::Register(const flecs::world& world)
{
    world.system<Components::Transform, const flecs::Parent>("TransformSystem")
        .kind(flecs::PostUpdate)
        .group_by(flecs::ParentDepth)
        .each([world](Components::Transform& tr, const flecs::Parent& parent) {
            // note that transforms with no parent entity (should just be env roots) won't be process at all.
            // below check is for transforms *with* parents but *without* parent _transforms_ (in-env top-level entities)
            const auto parentTransform = world.entity(parent.value).try_get<Components::Transform>();
            const bool isWorldStale = tr.isLocalStale || (parentTransform && parentTransform->worldChangedThisFrame);

            if (tr.isLocalStale) tr.recomputeLocalMatrix();
            if (isWorldStale)
            {
                tr.worldMatrix = (parentTransform ? parentTransform->worldMatrix : glm::mat4(1.0f)) * tr.localMatrix;
            }

            tr.worldChangedThisFrame = isWorldStale;
        });
}
