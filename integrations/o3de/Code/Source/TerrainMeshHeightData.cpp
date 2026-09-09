#include <TerrainCompositor/TerrainMeshHeightData.h>
#include <TerrainCompositor/TerrainMeshHeightMapping.h>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/limits.h>
#include <algorithm>
#include <cmath>

namespace TerrainCompositor
{
    namespace
    {
        double CoordinateTolerance(float minimum, float maximum)
        {
            return AZStd::max(1.0e-5, std::abs(double(maximum) - minimum) * 1.0e-6);
        }

        AZStd::vector<float> BuildAxis(AZStd::vector<float> values, double tolerance)
        {
            AZStd::sort(values.begin(), values.end());
            AZStd::vector<float> axis;
            axis.reserve(values.size());
            for (const float value : values)
            {
                if (axis.empty() || std::abs(double(value) - axis.back()) > tolerance)
                {
                    axis.push_back(value);
                }
            }
            return axis;
        }

        size_t FindAxisCoordinate(const AZStd::vector<float>& axis, float value, double tolerance)
        {
            const auto found = AZStd::lower_bound(axis.begin(), axis.end(), value);
            size_t best = axis.size();
            double distance = AZStd::numeric_limits<double>::max();
            if (found != axis.end())
            {
                best = static_cast<size_t>(found - axis.begin());
                distance = std::abs(double(*found) - value);
            }
            if (found != axis.begin())
            {
                const size_t prior = static_cast<size_t>((found - axis.begin()) - 1);
                const double priorDistance = std::abs(double(axis[prior]) - value);
                if (priorDistance < distance)
                {
                    best = prior;
                    distance = priorDistance;
                }
            }
            return distance <= tolerance ? best : axis.size();
        }

        bool HasRegularSpacing(const AZStd::vector<float>& axis, double tolerance, double& spacing)
        {
            if (axis.size() < 2)
            {
                return false;
            }
            spacing = double(axis[1]) - axis[0];
            if (!std::isfinite(spacing) || spacing <= tolerance)
            {
                return false;
            }
            for (size_t index = 2; index < axis.size(); ++index)
            {
                const double current = double(axis[index]) - axis[index - 1];
                if (std::abs(current - spacing) > tolerance)
                {
                    return false;
                }
            }
            return true;
        }

        void AddDiagnostic(
            TerrainMeshHeightBuildDiagnostics* diagnostics,
            TerrainMeshHeightValidation validation,
            AZ::u32 gridX = TerrainMeshHeightInvalidDiagnosticIndex,
            AZ::u32 gridY = TerrainMeshHeightInvalidDiagnosticIndex,
            AZ::u32 triangleIndex = TerrainMeshHeightInvalidDiagnosticIndex,
            AZ::u32 relatedTriangleIndex = TerrainMeshHeightInvalidDiagnosticIndex)
        {
            if (!diagnostics)
            {
                return;
            }
            ++diagnostics->m_totalOffenseCount;
            if (diagnostics->m_details.size() < TerrainMeshHeightMaximumDiagnosticDetails)
            {
                diagnostics->m_details.push_back({ validation, gridX, gridY, triangleIndex, relatedTriangleIndex });
            }
        }

        void SetCellBit(AZStd::vector<AZ::u32>& words, size_t cellIndex)
        {
            words[cellIndex / 32] |= AZ::u32(1) << (cellIndex % 32);
        }

        bool GetCellBit(const AZStd::vector<AZ::u32>& words, size_t cellIndex)
        {
            return (words[cellIndex / 32] & (AZ::u32(1) << (cellIndex % 32))) != 0;
        }

        unsigned int CountSetBits(AZ::u8 value)
        {
            unsigned int count = 0;
            while (value != 0)
            {
                count += value & 1;
                value >>= 1;
            }
            return count;
        }

        TerrainMeshHeightValidation ProjectedDegenerateReason(const AZ::Vector3& a, const AZ::Vector3& b, const AZ::Vector3& c)
        {
            const double areaSquared = double((b - a).Cross(c - a).GetLengthSq());
            return std::isfinite(areaSquared) && areaSquared > 1.0e-20 ? TerrainMeshHeightValidation::VerticalTriangle
                                                                       : TerrainMeshHeightValidation::DegenerateTriangle;
        }
    } // namespace

    size_t GetTerrainMeshHeightCellCount(const TerrainMeshHeightData& data)
    {
        return data.m_width >= 2 && data.m_height >= 2 ? size_t(data.m_width - 1) * (data.m_height - 1) : 0;
    }

    size_t GetTerrainMeshHeightCellWordCount(const TerrainMeshHeightData& data)
    {
        const size_t cellCount = GetTerrainMeshHeightCellCount(data);
        return (cellCount + 31) / 32;
    }

    size_t CalculateTerrainMeshHeightDiagnosticStride(
        size_t cellWidth, size_t cellHeight, size_t maximumDisplayedCells)
    {
        if (cellWidth == 0 || cellHeight == 0 || maximumDisplayedCells == 0)
        {
            return 0;
        }
        size_t stride = 1;
        while (true)
        {
            const size_t displayedWidth = (cellWidth + stride - 1) / stride;
            const size_t displayedHeight = (cellHeight + stride - 1) / stride;
            if (displayedHeight <= maximumDisplayedCells / displayedWidth)
            {
                return stride;
            }
            ++stride;
        }
    }

    bool ResolveTerrainMeshHeightCell(
        const TerrainMeshHeightData& data, double localX, double localY, TerrainMeshHeightCellAddress& address)
    {
        address = {};
        if (data.m_width < 2 || data.m_height < 2 || data.m_gridSpacing.GetX() <= 0.0f || data.m_gridSpacing.GetY() <= 0.0f ||
            !data.m_localBounds.IsValid() ||
            !(localX >= data.m_localOrigin.GetX() && localY >= data.m_localOrigin.GetY() &&
              localX <= data.m_localBounds.GetMax().GetX() && localY <= data.m_localBounds.GetMax().GetY()))
        {
            return false;
        }
        const double gridX = TerrainMeshHeightGridCoordinate(float(localX), data.m_localOrigin.GetX(), 1.0f / data.m_gridSpacing.GetX());
        const double gridY = TerrainMeshHeightGridCoordinate(float(localY), data.m_localOrigin.GetY(), 1.0f / data.m_gridSpacing.GetY());
        const double maximumX = data.m_width - 1.0;
        const double maximumY = data.m_height - 1.0;
        if (!std::isfinite(gridX) || !std::isfinite(gridY))
        {
            return false;
        }

        // Check the authored domain before reciprocal multiplication. Rounding at
        // a non-binary maximum edge may put the grid coordinate just above its integer extent.
        const size_t x = static_cast<size_t>(std::clamp(gridX, 0.0, maximumX - 1.0));
        const size_t y = static_cast<size_t>(std::clamp(gridY, 0.0, maximumY - 1.0));
        address.m_x = aznumeric_cast<AZ::u32>(x);
        address.m_y = aznumeric_cast<AZ::u32>(y);
        address.m_index = y * size_t(data.m_width - 1) + x;
        address.m_fractionX = std::clamp(gridX - x, 0.0, 1.0);
        address.m_fractionY = std::clamp(gridY - y, 0.0, 1.0);
        return true;
    }

    bool IsTerrainMeshHeightCellUncovered(const TerrainMeshHeightData& data, size_t cellIndex)
    {
        return cellIndex < GetTerrainMeshHeightCellCount(data) &&
            data.m_uncoveredCellBits.size() == GetTerrainMeshHeightCellWordCount(data) && GetCellBit(data.m_uncoveredCellBits, cellIndex);
    }

    TerrainMeshHeightGapTileOccupancy GetTerrainMeshHeightGapTileOccupancy(const TerrainMeshHeightData& data, AZ::u32 cellX, AZ::u32 cellY)
    {
        if (data.m_width < 2 || data.m_height < 2 || cellX >= data.m_width - 1 || cellY >= data.m_height - 1 || data.m_gapTileWidth == 0 ||
            data.m_gapTileHeight == 0)
        {
            return TerrainMeshHeightGapTileOccupancy::Covered;
        }
        const AZ::u32 tileX = cellX / TerrainMeshHeightGapTileSize;
        const AZ::u32 tileY = cellY / TerrainMeshHeightGapTileSize;
        const size_t tileIndex = size_t(tileY) * data.m_gapTileWidth + tileX;
        return tileIndex < data.m_gapTileOccupancy.size() ? data.m_gapTileOccupancy[tileIndex] : TerrainMeshHeightGapTileOccupancy::Covered;
    }

    TerrainMeshHeightValidation BuildTerrainMeshHeightData(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<const AZ::u32> indices,
        TerrainMeshHeightData& result,
        TerrainMeshHeightBuildDiagnostics* diagnostics)
    {
        const AZ::Data::AssetId assetId = result.m_assetId;
        result = {};
        result.m_assetId = assetId;
        if (diagnostics)
        {
            *diagnostics = {};
        }
        if (positions.empty() || indices.empty())
        {
            AddDiagnostic(diagnostics, TerrainMeshHeightValidation::Empty);
            return TerrainMeshHeightValidation::Empty;
        }
        if (positions.size() > TerrainModelGeometryMaximumVertices || indices.size() > TerrainModelGeometryMaximumIndices)
        {
            AddDiagnostic(diagnostics, TerrainMeshHeightValidation::ResourceLimit);
            return TerrainMeshHeightValidation::ResourceLimit;
        }
        if (indices.size() % 3 != 0)
        {
            AddDiagnostic(diagnostics, TerrainMeshHeightValidation::IndexCount);
            return TerrainMeshHeightValidation::IndexCount;
        }
        for (size_t index = 0; index < indices.size(); ++index)
        {
            if (indices[index] >= positions.size())
            {
                AddDiagnostic(
                    diagnostics,
                    TerrainMeshHeightValidation::IndexOutOfRange,
                    TerrainMeshHeightInvalidDiagnosticIndex,
                    TerrainMeshHeightInvalidDiagnosticIndex,
                    aznumeric_cast<AZ::u32>(index / 3));
                return TerrainMeshHeightValidation::IndexOutOfRange;
            }
        }

        TerrainMeshHeightData candidate;
        candidate.m_assetId = assetId;
        float minimumX = AZStd::numeric_limits<float>::max();
        float maximumX = -AZStd::numeric_limits<float>::max();
        float minimumY = AZStd::numeric_limits<float>::max();
        float maximumY = -AZStd::numeric_limits<float>::max();
        float minimumZ = AZStd::numeric_limits<float>::max();
        float maximumZ = -AZStd::numeric_limits<float>::max();
        AZStd::vector<float> xValues;
        AZStd::vector<float> yValues;
        xValues.reserve(positions.size());
        yValues.reserve(positions.size());
        for (const AZ::Vector3& position : positions)
        {
            if (!position.IsFinite())
            {
                AddDiagnostic(diagnostics, TerrainMeshHeightValidation::NonFinitePosition);
                return TerrainMeshHeightValidation::NonFinitePosition;
            }
            candidate.m_localBounds.AddPoint(position);
            minimumX = AZStd::min(minimumX, position.GetX());
            maximumX = AZStd::max(maximumX, position.GetX());
            minimumY = AZStd::min(minimumY, position.GetY());
            maximumY = AZStd::max(maximumY, position.GetY());
            minimumZ = AZStd::min(minimumZ, position.GetZ());
            maximumZ = AZStd::max(maximumZ, position.GetZ());
            xValues.push_back(position.GetX());
            yValues.push_back(position.GetY());
        }
        const double xTolerance = CoordinateTolerance(minimumX, maximumX);
        const double yTolerance = CoordinateTolerance(minimumY, maximumY);
        const double zTolerance = CoordinateTolerance(minimumZ, maximumZ);
        const auto xAxis = BuildAxis(AZStd::move(xValues), xTolerance);
        const auto yAxis = BuildAxis(AZStd::move(yValues), yTolerance);
        if (xAxis.size() < 2 || yAxis.size() < 2 || xAxis.size() > AZStd::numeric_limits<AZ::u32>::max() ||
            yAxis.size() > AZStd::numeric_limits<AZ::u32>::max())
        {
            AddDiagnostic(diagnostics, TerrainMeshHeightValidation::GridDimensions);
            return TerrainMeshHeightValidation::GridDimensions;
        }
        if (yAxis.size() > TerrainMeshHeightMaximumGridSamples / xAxis.size())
        {
            AddDiagnostic(diagnostics, TerrainMeshHeightValidation::ResourceLimit);
            return TerrainMeshHeightValidation::ResourceLimit;
        }
        double spacingX = 0.0;
        double spacingY = 0.0;
        if (!HasRegularSpacing(xAxis, xTolerance, spacingX) || !HasRegularSpacing(yAxis, yTolerance, spacingY))
        {
            AddDiagnostic(diagnostics, TerrainMeshHeightValidation::InconsistentGridSpacing);
            return TerrainMeshHeightValidation::InconsistentGridSpacing;
        }

        candidate.m_width = aznumeric_cast<AZ::u32>(xAxis.size());
        candidate.m_height = aznumeric_cast<AZ::u32>(yAxis.size());
        candidate.m_localOrigin = AZ::Vector2(xAxis.front(), yAxis.front());
        candidate.m_gridSpacing = AZ::Vector2(aznumeric_cast<float>(spacingX), aznumeric_cast<float>(spacingY));
        if (diagnostics)
        {
            diagnostics->m_localBounds = candidate.m_localBounds;
            diagnostics->m_localOrigin = candidate.m_localOrigin;
            diagnostics->m_gridSpacing = candidate.m_gridSpacing;
            diagnostics->m_gridWidth = candidate.m_width;
            diagnostics->m_gridHeight = candidate.m_height;
        }
        const size_t sampleCount = xAxis.size() * yAxis.size();
        candidate.m_localHeights.resize(sampleCount, 0.0f);
        AZStd::vector<AZ::u8> vertexPresence(sampleCount, 0);
        AZStd::vector<AZ::u32> vertexGridIndices(positions.size());
        for (size_t vertex = 0; vertex < positions.size(); ++vertex)
        {
            const size_t x = FindAxisCoordinate(xAxis, positions[vertex].GetX(), xTolerance);
            const size_t y = FindAxisCoordinate(yAxis, positions[vertex].GetY(), yTolerance);
            if (x == xAxis.size() || y == yAxis.size())
            {
                AddDiagnostic(diagnostics, TerrainMeshHeightValidation::InconsistentGridSpacing);
                return TerrainMeshHeightValidation::InconsistentGridSpacing;
            }
            const size_t sample = y * xAxis.size() + x;
            if (vertexPresence[sample] && std::abs(double(candidate.m_localHeights[sample]) - positions[vertex].GetZ()) > zTolerance)
            {
                AddDiagnostic(
                    diagnostics, TerrainMeshHeightValidation::ConflictingHeight, aznumeric_cast<AZ::u32>(x), aznumeric_cast<AZ::u32>(y));
                return TerrainMeshHeightValidation::ConflictingHeight;
            }
            if (!vertexPresence[sample])
            {
                candidate.m_localHeights[sample] = positions[vertex].GetZ();
                vertexPresence[sample] = 1;
            }
            vertexGridIndices[vertex] = aznumeric_cast<AZ::u32>(sample);
        }
        bool missingGridPoint = false;
        for (size_t sample = 0; sample < sampleCount; ++sample)
        {
            if (!vertexPresence[sample])
            {
                missingGridPoint = true;
                AddDiagnostic(
                    diagnostics,
                    TerrainMeshHeightValidation::MissingGridPoint,
                    aznumeric_cast<AZ::u32>(sample % xAxis.size()),
                    aznumeric_cast<AZ::u32>(sample / xAxis.size()));
            }
        }
        if (missingGridPoint)
        {
            return TerrainMeshHeightValidation::MissingGridPoint;
        }

        const size_t cellWidth = xAxis.size() - 1;
        const size_t cellCount = cellWidth * (yAxis.size() - 1);
        AZStd::vector<AZ::u8> cellTriangleSets(cellCount, 0);
        AZStd::vector<AZ::u32> firstCellTriangle(cellCount, TerrainMeshHeightInvalidDiagnosticIndex);
        for (size_t triangleOffset = 0; triangleOffset < indices.size(); triangleOffset += 3)
        {
            const AZ::u32 triangleIndex = aznumeric_cast<AZ::u32>(triangleOffset / 3);
            const AZ::u32 ia = indices[triangleOffset];
            const AZ::u32 ib = indices[triangleOffset + 1];
            const AZ::u32 ic = indices[triangleOffset + 2];
            const AZ::u32 samples[3] = { vertexGridIndices[ia], vertexGridIndices[ib], vertexGridIndices[ic] };
            const size_t xs[3] = { samples[0] % xAxis.size(), samples[1] % xAxis.size(), samples[2] % xAxis.size() };
            const size_t ys[3] = { samples[0] / xAxis.size(), samples[1] / xAxis.size(), samples[2] / xAxis.size() };
            const size_t minX = AZStd::min(xs[0], AZStd::min(xs[1], xs[2]));
            const size_t maxX = AZStd::max(xs[0], AZStd::max(xs[1], xs[2]));
            const size_t minY = AZStd::min(ys[0], AZStd::min(ys[1], ys[2]));
            const size_t maxY = AZStd::max(ys[0], AZStd::max(ys[1], ys[2]));
            if (minX == maxX || minY == maxY)
            {
                const auto reason = ProjectedDegenerateReason(positions[ia], positions[ib], positions[ic]);
                AddDiagnostic(diagnostics, reason, aznumeric_cast<AZ::u32>(minX), aznumeric_cast<AZ::u32>(minY), triangleIndex);
                return reason;
            }
            if (maxX - minX != 1 || maxY - minY != 1)
            {
                AddDiagnostic(
                    diagnostics,
                    TerrainMeshHeightValidation::NonAdjacentTriangle,
                    aznumeric_cast<AZ::u32>(minX),
                    aznumeric_cast<AZ::u32>(minY),
                    triangleIndex);
                return TerrainMeshHeightValidation::NonAdjacentTriangle;
            }
            AZ::u8 cornerMask = 0;
            for (size_t vertex = 0; vertex < 3; ++vertex)
            {
                const size_t corner = (xs[vertex] - minX) + 2 * (ys[vertex] - minY);
                cornerMask |= AZ::u8(1u << corner);
            }
            AZ::u8 setBit = 0;
            switch (cornerMask)
            {
            case 0x7:
                setBit = 0x1;
                break;
            case 0xB:
                setBit = 0x2;
                break;
            case 0xD:
                setBit = 0x4;
                break;
            case 0xE:
                setBit = 0x8;
                break;
            default:
                AddDiagnostic(
                    diagnostics,
                    TerrainMeshHeightValidation::DegenerateTriangle,
                    aznumeric_cast<AZ::u32>(minX),
                    aznumeric_cast<AZ::u32>(minY),
                    triangleIndex);
                return TerrainMeshHeightValidation::DegenerateTriangle;
            }
            const size_t cell = minY * cellWidth + minX;
            AZ::u8& sets = cellTriangleSets[cell];
            if ((sets & setBit) != 0)
            {
                AddDiagnostic(
                    diagnostics,
                    TerrainMeshHeightValidation::OverlappingTriangles,
                    aznumeric_cast<AZ::u32>(minX),
                    aznumeric_cast<AZ::u32>(minY),
                    triangleIndex,
                    firstCellTriangle[cell]);
                return TerrainMeshHeightValidation::OverlappingTriangles;
            }
            if (firstCellTriangle[cell] == TerrainMeshHeightInvalidDiagnosticIndex)
            {
                firstCellTriangle[cell] = triangleIndex;
            }
            sets |= setBit;
        }

        const size_t wordCount = (cellCount + 31) / 32;
        candidate.m_uncoveredCellBits.resize(wordCount, 0);
        candidate.m_cellDiagonalBits.resize(wordCount, 0);
        TerrainMeshHeightValidation topologyValidation = TerrainMeshHeightValidation::Valid;
        for (size_t cell = 0; cell < cellCount; ++cell)
        {
            const AZ::u8 sets = cellTriangleSets[cell];
            if (sets == 0)
            {
                SetCellBit(candidate.m_uncoveredCellBits, cell);
                ++candidate.m_uncoveredCellCount;
            }
            else if (sets == (0x2 | 0x4))
            {
                ++candidate.m_coveredCellCount;
            }
            else if (sets == (0x1 | 0x8))
            {
                SetCellBit(candidate.m_cellDiagonalBits, cell);
                ++candidate.m_coveredCellCount;
            }
            else
            {
                const auto reason =
                    CountSetBits(sets) == 1 ? TerrainMeshHeightValidation::HalfCell : TerrainMeshHeightValidation::OverlappingTriangles;
                if (topologyValidation == TerrainMeshHeightValidation::Valid)
                {
                    topologyValidation = reason;
                }
                AddDiagnostic(
                    diagnostics,
                    reason,
                    aznumeric_cast<AZ::u32>(cell % cellWidth),
                    aznumeric_cast<AZ::u32>(cell / cellWidth),
                    firstCellTriangle[cell]);
            }
        }
        if (topologyValidation != TerrainMeshHeightValidation::Valid)
        {
            return topologyValidation;
        }

        const size_t cellHeight = yAxis.size() - 1;
        candidate.m_gapTileWidth = aznumeric_cast<AZ::u32>((cellWidth + TerrainMeshHeightGapTileSize - 1) / TerrainMeshHeightGapTileSize);
        candidate.m_gapTileHeight = aznumeric_cast<AZ::u32>((cellHeight + TerrainMeshHeightGapTileSize - 1) / TerrainMeshHeightGapTileSize);
        const size_t tileCount = size_t(candidate.m_gapTileWidth) * candidate.m_gapTileHeight;
        candidate.m_gapTileOccupancy.resize(tileCount, TerrainMeshHeightGapTileOccupancy::Covered);
        for (AZ::u32 tileY = 0; tileY < candidate.m_gapTileHeight; ++tileY)
        {
            for (AZ::u32 tileX = 0; tileX < candidate.m_gapTileWidth; ++tileX)
            {
                const size_t beginX = size_t(tileX) * TerrainMeshHeightGapTileSize;
                const size_t beginY = size_t(tileY) * TerrainMeshHeightGapTileSize;
                const size_t endX = AZStd::min(beginX + TerrainMeshHeightGapTileSize, cellWidth);
                const size_t endY = AZStd::min(beginY + TerrainMeshHeightGapTileSize, cellHeight);
                size_t uncovered = 0;
                const size_t total = (endX - beginX) * (endY - beginY);
                for (size_t y = beginY; y < endY; ++y)
                {
                    for (size_t x = beginX; x < endX; ++x)
                    {
                        const size_t cell = y * cellWidth + x;
                        if (IsTerrainMeshHeightCellUncovered(candidate, cell))
                        {
                            ++uncovered;
                            // The regular grid is allowed import-rounding tolerance. Rejection bounds
                            // must contain both its reconstructed and actual authored coordinates.
                            const float minimumCellX = AZStd::min(xAxis[x], aznumeric_cast<float>(double(candidate.m_localOrigin.GetX()) + x * spacingX));
                            const float minimumCellY = AZStd::min(yAxis[y], aznumeric_cast<float>(double(candidate.m_localOrigin.GetY()) + y * spacingY));
                            const float maximumCellX = AZStd::max(xAxis[x + 1], aznumeric_cast<float>(double(candidate.m_localOrigin.GetX()) + (x + 1) * spacingX));
                            const float maximumCellY = AZStd::max(yAxis[y + 1], aznumeric_cast<float>(double(candidate.m_localOrigin.GetY()) + (y + 1) * spacingY));
                            candidate.m_localUncoveredBounds.AddPoint(AZ::Vector3(minimumCellX, minimumCellY, 0.0f));
                            candidate.m_localUncoveredBounds.AddPoint(AZ::Vector3(maximumCellX, maximumCellY, 0.0f));
                        }
                    }
                }
                candidate.m_gapTileOccupancy[size_t(tileY) * candidate.m_gapTileWidth + tileX] = uncovered == 0
                    ? TerrainMeshHeightGapTileOccupancy::Covered
                    : uncovered == total ? TerrainMeshHeightGapTileOccupancy::Uncovered
                                         : TerrainMeshHeightGapTileOccupancy::Mixed;
            }
        }

        result = AZStd::move(candidate);
        return TerrainMeshHeightValidation::Valid;
    }

    bool SampleTerrainMeshLocalHeight(const TerrainMeshHeightData& data, double localX, double localY, double& height)
    {
        height = 0.0;
        const size_t cellCount = GetTerrainMeshHeightCellCount(data);
        const size_t wordCount = GetTerrainMeshHeightCellWordCount(data);
        if (cellCount == 0 || data.m_localHeights.size() != size_t(data.m_width) * data.m_height ||
            data.m_uncoveredCellBits.size() != wordCount || data.m_cellDiagonalBits.size() != wordCount ||
            data.m_gapTileOccupancy.size() != size_t(data.m_gapTileWidth) * data.m_gapTileHeight ||
            size_t(data.m_coveredCellCount) + data.m_uncoveredCellCount != cellCount)
        {
            return false;
        }
        TerrainMeshHeightCellAddress address;
        if (!ResolveTerrainMeshHeightCell(data, localX, localY, address) || IsTerrainMeshHeightCellUncovered(data, address.m_index))
        {
            return false;
        }

        const size_t row = size_t(address.m_y) * data.m_width;
        const size_t x = address.m_x;
        const double z00 = data.m_localHeights[row + x];
        const double z10 = data.m_localHeights[row + x + 1];
        const double z01 = data.m_localHeights[row + data.m_width + x];
        const double z11 = data.m_localHeights[row + data.m_width + x + 1];
        const double fx = address.m_fractionX;
        const double fy = address.m_fractionY;
        if (!GetCellBit(data.m_cellDiagonalBits, address.m_index))
        {
            height = fy <= fx ? z00 + fx * (z10 - z00) + fy * (z11 - z10) : z00 + fx * (z11 - z01) + fy * (z01 - z00);
        }
        else
        {
            height = fx + fy <= 1.0 ? z00 + fx * (z10 - z00) + fy * (z01 - z00) : z11 + (1.0 - fy) * (z10 - z11) + (1.0 - fx) * (z01 - z11);
        }
        return std::isfinite(height);
    }

    const char* GetTerrainMeshHeightValidationMessage(TerrainMeshHeightValidation validation)
    {
        switch (validation)
        {
        case TerrainMeshHeightValidation::Valid:
            return "Valid";
        case TerrainMeshHeightValidation::Empty:
            return "The terrain model contains no indexed triangles or vertices.";
        case TerrainMeshHeightValidation::IndexCount:
            return "Terrain indices must be a nonempty triangle list.";
        case TerrainMeshHeightValidation::IndexOutOfRange:
            return "A terrain index is outside the vertex array.";
        case TerrainMeshHeightValidation::NonFinitePosition:
            return "Terrain positions must all be finite.";
        case TerrainMeshHeightValidation::GridDimensions:
            return "The terrain must contain at least a 2 by 2 XY grid.";
        case TerrainMeshHeightValidation::InconsistentGridSpacing:
            return "Terrain X and Y grid coordinates must have consistent spacing.";
        case TerrainMeshHeightValidation::MissingGridPoint:
            return "The terrain must contain one height at every point of its "
                   "rectangular grid.";
        case TerrainMeshHeightValidation::ConflictingHeight:
            return "Multiple vertices at one terrain XY coordinate have conflicting "
                   "heights.";
        case TerrainMeshHeightValidation::DegenerateTriangle:
            return "A terrain triangle is degenerate.";
        case TerrainMeshHeightValidation::VerticalTriangle:
            return "A terrain triangle is vertical and has zero projected XY area.";
        case TerrainMeshHeightValidation::NonAdjacentTriangle:
            return "Every terrain triangle must use three corners of one adjacent grid "
                   "cell.";
        case TerrainMeshHeightValidation::OverlappingTriangles:
            return "The terrain contains duplicate or overlapping projected triangles.";
        case TerrainMeshHeightValidation::HalfCell:
            return "A terrain grid cell contains only one triangle; use zero triangles "
                   "or one complementary pair.";
        case TerrainMeshHeightValidation::UnexpectedCellTopology:
            return "Every terrain grid cell must contain zero triangles or exactly two "
                   "triangles sharing one supported diagonal.";
        case TerrainMeshHeightValidation::ResourceLimit:
            return "The terrain height grid exceeds the resource limit.";
        }
        return "Unsupported terrain mesh geometry.";
    }
} // namespace TerrainCompositor
