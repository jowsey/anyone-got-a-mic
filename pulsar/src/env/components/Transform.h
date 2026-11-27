#pragma once

#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"

namespace pulsar::Components
{
    struct Transform
    {
        glm::mat4 localMatrix{1.0f};
        glm::mat4 worldMatrix{1.0f};

        bool isLocalStale{true};
        bool worldChangedThisFrame{true};

        glm::vec3 localPosition{0};
        glm::quat localRotation{glm::identity<glm::quat>()};
        glm::vec3 localScale{1};

        void recomputeLocalMatrix()
        {
            assert(isLocalStale && "Attempted to recompute local matrix for a non-stale Transform!");

            localMatrix = glm::translate(glm::mat4(1.0f), localPosition) *
                          glm::mat4(localRotation) *
                          glm::scale(glm::mat4(1.0f), localScale);

            isLocalStale = false;
        }

        void setLocalPosition(const glm::vec3& newPosition)
        {
            localPosition = newPosition;
            isLocalStale = true;
        }

        void setLocalRotation(const glm::quat& newRotation)
        {
            localRotation = newRotation;
            isLocalStale = true;
        }

        void setLocalScale(const glm::vec3& newScale)
        {
            localScale = newScale;
            isLocalStale = true;
        }

        void setLocalPose(const glm::vec3& newPosition, const glm::quat& newRotation)
        {
            localPosition = newPosition;
            localRotation = newRotation;

            isLocalStale = true;
        }

        [[nodiscard]] glm::vec3 getWorldPosition() const
        {
            return {worldMatrix[3]};
        }

        [[nodiscard]] glm::quat getWorldRotation() const
        {
            // todo cache these with world matrix changes
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation{};
            glm::decompose(worldMatrix, scale, rotation, translation, skew, perspective);
            return rotation;
        }
    };
}
