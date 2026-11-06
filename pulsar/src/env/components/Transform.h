#pragma once

#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

namespace pulsar::Components
{
    struct Transform
    {
        glm::mat4 localMatrix;
        glm::mat4 worldMatrix;

        bool isLocalDirty;

        glm::vec3 localPosition{0};
        glm::quat localRotation{glm::identity<glm::quat>()};
        glm::vec3 localScale{1};

        void recomputeLocalMatrix()
        {
            assert(!isLocalDirty && "Attempted to recompute local matrix for a non-dirty Transform!");

            localMatrix = glm::translate(glm::mat4(1.0f), localPosition) *
                          glm::mat4(localRotation) *
                          glm::scale(glm::mat4(1.0f), localScale);

            isLocalDirty = false;
        }

        void setLocalPosition(const glm::vec3& newPosition)
        {
            localPosition = newPosition;
            isLocalDirty = true;
        }

        void setLocalRotation(const glm::quat& newRotation)
        {
            localRotation = newRotation;
            isLocalDirty = true;
        }

        void setLocalScale(const glm::vec3& newScale)
        {
            localScale = newScale;
            isLocalDirty = true;
        }
    };
}
