#include "FormatConversion.h"

#include "glm/detail/type_quat.hpp"
#include <bit>

b3Vec3 pulsar::Utils::Formats::glmToB3Vec3(const glm::vec3& vec)
{
    return std::bit_cast<b3Vec3>(vec);
}

glm::vec3 pulsar::Utils::Formats::b3ToGlmVec3(const b3Vec3& vec)
{
    return std::bit_cast<glm::vec3>(vec);
}

b3Quat pulsar::Utils::Formats::glmToB3Quat(const glm::quat& quat)
{
    return {.v = {.x = quat.x, .y = quat.y, .z = quat.z}, .s = quat.w};
}

glm::quat pulsar::Utils::Formats::b3ToGlmQuat(const b3Quat& quat)
{
    return glm::quat{quat.s, quat.v.x, quat.v.y, quat.v.z};
}