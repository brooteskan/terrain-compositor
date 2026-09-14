#pragma once

#include <AzCore/base.h>
#include <algorithm>
#include <cstddef>
#include <limits>

namespace TerrainCompositor::Internal
{
    inline bool HasCompleteImageBuffer(AZ::u32 width, AZ::u32 height, size_t count)
    {
        return width > 0 && height > 0 && size_t(height) <= std::numeric_limits<size_t>::max() / width &&
            count == size_t(width) * height;
    }

    struct BilinearCoordinates
    {
        size_t m_topLeft, m_topRight, m_bottomLeft, m_bottomRight;
        double m_tx, m_ty;
    };

    // Callers validate nonzero dimensions and the complete row-major buffer.
    // A one-pixel dimension has identical neighbors and needs no division.
    inline BilinearCoordinates GetBilinearCoordinates(AZ::u32 width, AZ::u32 height, double u, double v)
    {
        const double pixelX = std::clamp(u, 0.0, 1.0) * (width - 1);
        const double pixelY = std::clamp(1.0 - v, 0.0, 1.0) * (height - 1);
        const size_t x0 = static_cast<size_t>(pixelX);
        const size_t y0 = static_cast<size_t>(pixelY);
        const size_t x1 = std::min(x0 + 1, size_t(width - 1));
        const size_t y1 = std::min(y0 + 1, size_t(height - 1));
        const size_t row0 = y0 * size_t(width);
        const size_t row1 = y1 * size_t(width);
        return { row0 + x0, row0 + x1, row1 + x0, row1 + x1, pixelX - double(x0), pixelY - double(y0) };
    }

    inline double SampleBilinear(const float* samples, AZ::u32 width, AZ::u32 height, double u, double v)
    {
        const auto c = GetBilinearCoordinates(width, height, u, v);
        // Keep the two row interpolations: a four-weight sum changes rounding.
        const double top = samples[c.m_topLeft] * (1.0 - c.m_tx) + samples[c.m_topRight] * c.m_tx;
        const double bottom = samples[c.m_bottomLeft] * (1.0 - c.m_tx) + samples[c.m_bottomRight] * c.m_tx;
        return top * (1.0 - c.m_ty) + bottom * c.m_ty;
    }
}
