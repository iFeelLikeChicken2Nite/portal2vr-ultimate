#pragma once
#include <cstdint>

constexpr bool IsUsableTrackedDeviceIndex(uint32_t index, uint32_t count, uint32_t invalid)
{
    return index != invalid && index < count;
}
