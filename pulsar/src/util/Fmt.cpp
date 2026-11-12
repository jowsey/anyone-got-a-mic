#include "Fmt.h"

#include <array>
#include <cmath>
#include <format>

std::string pulsar::Utils::Fmt::prettyPrint(double value)
{
    constexpr std::array suffixes = {"", "K", "M", "B", "T", "Qa"};

    std::size_t suffixIndex = 0;
    while (std::abs(value) >= 1000.0 && suffixIndex < suffixes.size() - 1)
    {
        value /= 1000.0;
        ++suffixIndex;
    }

    return std::format("{:.3f}{}", value, suffixes[suffixIndex]);
}