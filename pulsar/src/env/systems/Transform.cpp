#include "Transform.h"
#include "../components/EnvironmentId.h"
#include "../components/Transform.h"

#include "glm/gtc/quaternion.hpp"

void pulsar::Systems::Transform::processChildTransforms(const flecs::entity& parent, const glm::mat4& parentWorld, bool parentDirty = false)
{
    parent.children([parentWorld, parentDirty](const flecs::entity child) {
        Components::Transform& tr = child.get_mut<Components::Transform>();

        const bool isWorldDirty = tr.isLocalDirty || parentDirty;

        if (tr.isLocalDirty)
        {
            tr.recomputeLocalMatrix();
        }

        if (isWorldDirty)
        {
            tr.worldMatrix = parentWorld * tr.localMatrix;
        }

        processChildTransforms(child, tr.worldMatrix, isWorldDirty);
    });
}

void pulsar::Systems::Transform::Register(const flecs::world& world)
{
    world.system<Components::EnvironmentId>("TransformSystem")
        .without<flecs::Parent>()
        .kind(flecs::PostUpdate)
        .multi_threaded()
        .each([](const flecs::entity envRoot, const Components::EnvironmentId& /*envId*/) {
            processChildTransforms(envRoot, glm::mat4(1.0f));
        });
}
