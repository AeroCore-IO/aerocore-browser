#pragma once

#include <cstdint>

namespace GamepadTrigger {

constexpr std::int16_t kPressThreshold = 16000;
constexpr std::int16_t kReleaseThreshold = 10000;

inline bool isHeld(std::int16_t axisValue, bool wasHeld)
{
    if (wasHeld) {
        return axisValue > kReleaseThreshold;
    }
    return axisValue > kPressThreshold;
}

} // namespace GamepadTrigger
