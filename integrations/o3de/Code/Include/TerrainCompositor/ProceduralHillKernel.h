#pragma once

// Value-only kernel, deliberately independent of the engine for differential
// experiments with the identical optimized compiler and floating-point mode.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace TerrainCompositor
{
    enum class ProceduralHillPolicy : uint8_t { Reference, CachedExact, IntegerPowers, PrunedReference };

    struct ProceduralHillCounters
    {
        size_t m_samples = 0, m_cells = 0, m_cellHits = 0, m_sqrt = 0, m_pow = 0, m_rejected = 0;
    };

    class ProceduralHillKernel
    {
    public:
        static constexpr float Radius = 0.85f;
        static constexpr float HeightRange = 2048.0f;
        struct Cell { float m_x, m_y; bool m_depression; };
        struct Scratch
        {
            struct Entry { int64_t m_x = INT64_MIN, m_y = INT64_MIN; Cell m_cell{}; };
            std::array<Entry, 64> m_cells;
            std::array<Cell, 9> m_neighborhood;
            int64_t m_x = INT64_MIN, m_y = INT64_MIN;
        };

        ProceduralHillKernel(float density, float amplitude, float frequency,
            ProceduralHillPolicy policy = ProceduralHillPolicy::CachedExact)
            : m_density(std::clamp(density, 0.0f, 1.0f)),
              m_amplitude(std::clamp(amplitude, 0.0f, HeightRange * 0.5f) / HeightRange),
              m_frequency(std::clamp(frequency, 0.1f, 16.0f)),
              m_policy(policy <= ProceduralHillPolicy::PrunedReference ? policy : ProceduralHillPolicy::Reference)
        {
            if (policy == ProceduralHillPolicy::IntegerPowers)
            {
                if (m_frequency == 1) m_power = Power<1>;
                else if (m_frequency == 2) m_power = Power<2>;
                else if (m_frequency == 3) m_power = Power<3>;
                else if (m_frequency == 4) m_power = Power<4>;
            }
        }

        static uint64_t MixBits(uint64_t value)
        {
            value ^= value >> 30; value *= 0xBF58476D1CE4E5B9ULL;
            value ^= value >> 27; value *= 0x94D049BB133111EBULL;
            return value ^ (value >> 31);
        }
        static float CoordinateValue(int64_t x, int64_t y, uint64_t salt)
        {
            const auto hash = MixBits(0x9E3779B97F4A7C15ULL ^ salt ^ MixBits(uint64_t(x)) ^ (MixBits(uint64_t(y)) << 1));
            return float(hash & 0x00FFFFFFu) * (1.0f / float(0x00FFFFFFu));
        }
        static Cell MakeCell(int64_t x, int64_t y)
        {
            return { float(x) + (0.15f + CoordinateValue(x, y, 0xA24BAED4963EE407ULL) * 0.7f),
                float(y) + (0.15f + CoordinateValue(x, y, 0x9FB21C651E98DF25ULL) * 0.7f),
                CoordinateValue(x, y, 0xD1B54A32D192ED03ULL) < 0.5f };
        }
        static float SmoothCurve(float value)
        {
            const float p = std::clamp(value, 0.0f, 1.0f);
            return p * p * (3.0f - (2.0f * p));
        }
        float Frequency() const { return m_frequency; }
        ProceduralHillPolicy Policy() const { return m_policy; }

        // Reference preserves the original 9-cell order, float operations and pow.
        // Each batch owns its scratch; cache misses and batch boundaries cannot
        // alter coordinates, accumulation order or the captured math policy.
        template<bool Measure = false>
        float Sample(float x, float y, Scratch& scratch, ProceduralHillCounters* counters = nullptr) const
        {
            if constexpr (Measure) ++counters->m_samples;
            if (m_density <= 0 || m_amplitude <= 0) return 0.5f;
            // Avoid undefined float-to-integer conversion even through ordinary
            // source entrypoints. Retained contracts reject these before writing.
            if (!std::isfinite(x) || !std::isfinite(y) ||
                !std::isfinite(m_density) || !std::isfinite(m_amplitude) || !std::isfinite(m_frequency)) return 0.5f;
            const float cx = x * m_density, cy = y * m_density;
            // Ordinary queries can exceed the retained snapshot's 1e12-world
            // contract. Keep their original values whenever scaled cell indices
            // and +/-1 neighbors fit int64. The adjacent float below 2^63 leaves
            // ample integer headroom; equality at either endpoint is rejected.
            if (!std::isfinite(cx) || !std::isfinite(cy) || std::abs(cx) >= 0x1p63f || std::abs(cy) >= 0x1p63f) return 0.5f;
            const auto bx = int64_t(std::floor(cx)), by = int64_t(std::floor(cy));
            const bool reference = m_policy == ProceduralHillPolicy::Reference || m_policy == ProceduralHillPolicy::PrunedReference;
            if (!reference && (scratch.m_x != bx || scratch.m_y != by))
            {
                size_t i = 0;
                for (int64_t oy = -1; oy <= 1; ++oy)
                    for (int64_t ox = -1; ox <= 1; ++ox, ++i)
                    {
                        const auto hx = bx + ox, hy = by + oy;
                        auto& entry = scratch.m_cells[(uint64_t(hx) * 17u + uint64_t(hy) * 31u) & 63u];
                        if (entry.m_x != hx || entry.m_y != hy)
                        {
                            entry = { hx, hy, MakeCell(hx, hy) };
                            if constexpr (Measure) ++counters->m_cells;
                        }
                        else if constexpr (Measure) ++counters->m_cellHits;
                        scratch.m_neighborhood[i] = entry.m_cell;
                    }
                scratch.m_x = bx; scratch.m_y = by;
            }
            else if (!reference) { if constexpr (Measure) counters->m_cellHits += 9; }
            float bumps = 0, depressions = 0;
            size_t i = 0;
            for (int64_t oy = -1; oy <= 1; ++oy)
                for (int64_t ox = -1; ox <= 1; ++ox, ++i)
                {
                    const auto hx = bx + ox, hy = by + oy;
                    const auto cell = reference ? Cell{
                        float(hx) + (0.15f + CoordinateValue(hx, hy, 0xA24BAED4963EE407ULL) * 0.7f),
                        float(hy) + (0.15f + CoordinateValue(hx, hy, 0x9FB21C651E98DF25ULL) * 0.7f), false }
                        : scratch.m_neighborhood[i];
                    if constexpr (Measure) { if (reference) ++counters->m_cells; }
                    const float dx = cx - cell.m_x, dy = cy - cell.m_y;
                    const float d2 = (dx * dx) + (dy * dy);
                    // Deliberately loose: sqrt(d2) >= 1 is safely beyond 0.85.
                    // The entire floating-point support boundary uses the original
                    // sqrt/division/profile, never d2 >= rounded radius*radius.
                    if (!reference && d2 >= 1.0f) { if constexpr (Measure) ++counters->m_rejected; continue; }
                    if constexpr (Measure) ++counters->m_sqrt;
                    const float p = SmoothCurve(1.0f - std::sqrt(d2) / Radius);
                    if (m_policy != ProceduralHillPolicy::Reference && p == 0) { if constexpr (Measure) ++counters->m_rejected; continue; }
                    const float h = m_power(p, m_frequency);
                    if constexpr (Measure) { if (m_power == GeneralPower) ++counters->m_pow; }
                    const bool depression = reference ? CoordinateValue(hx, hy, 0xD1B54A32D192ED03ULL) < 0.5f : cell.m_depression;
                    float& blend = depression ? depressions : bumps;
                    blend += (1.0f - blend) * h;
                }
            return std::clamp(0.5f + ((bumps - depressions) * m_amplitude), 0.0f, 1.0f);
        }

        template<class Positions, class Values>
        void SampleBatch(const Positions& positions, Values values, ProceduralHillCounters* counters = nullptr) const
        {
            Scratch scratch;
            if (counters)
                for (size_t i = 0; i < positions.size(); ++i)
                    values[i] = Sample<true>(positions[i].GetX(), positions[i].GetY(), scratch, counters);
            else
                for (size_t i = 0; i < positions.size(); ++i)
                    values[i] = Sample(positions[i].GetX(), positions[i].GetY(), scratch);
        }

    private:
        static float GeneralPower(float p, float f) { return std::pow(p, f); }
        template<int N> static float Power(float p, float)
        {
            if constexpr (N == 1) return p;
            else if constexpr (N == 2) return p * p;
            else if constexpr (N == 3) return (p * p) * p;
            else return ((p * p) * p) * p;
        }
        float m_density, m_amplitude, m_frequency;
        ProceduralHillPolicy m_policy;
        float (*m_power)(float, float) = GeneralPower;
    };
}
