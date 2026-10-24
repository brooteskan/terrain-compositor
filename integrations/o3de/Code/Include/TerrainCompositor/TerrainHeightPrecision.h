#pragma once
#include <cstdint>
#include <cstring>

namespace TerrainCompositor
{
    // IEEE binary16, round to nearest with ties to even. CPU authoring/physics
    // data stays intact; this conversion is only for selected GPU publications.
    inline uint16_t PackTerrainHeight16(float value)
    {
        uint32_t bits; std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t sign = (bits >> 16) & 0x8000u;
        const uint32_t exponent = (bits >> 23) & 255u;
        uint32_t mantissa = bits & 0x7fffffu;
        if (exponent == 255u) return uint16_t(sign | 0x7c00u | (mantissa ? 0x200u : 0u));
        const int e = int(exponent) - 127 + 15;
        if (e >= 31) return uint16_t(sign | 0x7c00u);
        if (e < -10) return uint16_t(sign);
        uint32_t shift;
        if (e <= 0) { mantissa |= 0x800000u; shift = uint32_t(14 - e); }
        else shift = 13;
        uint32_t rounded = mantissa >> shift;
        const uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (rounded & 1u))) ++rounded;
        return uint16_t(sign | (e > 0 ? (uint32_t(e) << 10) + rounded : rounded));
    }
}
