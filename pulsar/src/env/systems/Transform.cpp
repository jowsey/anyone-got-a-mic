#include "Transform.h"
#include "../components/Transform.h"

#include "glm/gtc/quaternion.hpp"

void pulsar::Systems::Transform::Register(const flecs::world& world)
{
    world.system<Components::Transform, const flecs::Parent>("TransformSystem")
        .kind(flecs::PostUpdate)
        .group_by(flecs::ParentDepth)
        .each([world](Components::Transform& tr, const flecs::Parent& parent) {
            // all envs have a top-level transform-less (transparent) root entity. this means
            // transforms will always have a parent entity (the env), but may not have a parent *transform*
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
