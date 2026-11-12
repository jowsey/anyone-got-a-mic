#include "Physics.h"

#include "../components/RigidBody.h"
#include "../components/Transform.h"

void pulsar::Systems::Physics::Register(const flecs::world& world)
{
    world.system<Components::Transform, Components::RigidBody>("PhysicsSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::entity e, Components::Transform& transform, Components::RigidBody& body) {
            
        });
    // system.add(flecs::DependsOn, )
}