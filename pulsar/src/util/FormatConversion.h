#pragma once
#include "box3d/math_functions.h"
#include "glm/fwd.hpp"
#include "glm/vec3.hpp"

namespace pulsar::Utils::Formats
{
    b3Vec3 glmToB3Vec3(const glm::vec3& vec);
    glm::vec3 b3ToGlmVec3(const b3Vec3& vec);

    b3Quat glmToB3Quat(const glm::quat& quat);
    glm::quat b3ToGlmQuat(const b3Quat& quat);
}
