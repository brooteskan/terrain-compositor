#include "MeshCutoutTraversal.h"
#include <TerrainCompositor/TerrainMeshCutoutData.h>

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>
#include <AzCore/std/containers/unordered_map.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace TerrainCompositor
{
    namespace
    {
        constexpr size_t MaxTriangles = 1'000'000;
        constexpr size_t MaxVertices = 3'000'000;
        constexpr AZ::u32 LeafTriangleCount = 8;

        struct VertexBits
        {
            AZ::u32 m_x = 0;
            AZ::u32 m_y = 0;
            AZ::u32 m_z = 0;
            bool operator==(const VertexBits&) const = default;
        };

        struct VertexBitsHash
        {
            size_t operator()(const VertexBits& key) const
            {
                size_t value = key.m_x;
                value ^= size_t(key.m_y) + 0x9e3779b9 + (value << 6) + (value >> 2);
                value ^= size_t(key.m_z) + 0x9e3779b9 + (value << 6) + (value >> 2);
                return value;
            }
        };

        struct Edge
        {
            AZ::u32 m_a = 0;
            AZ::u32 m_b = 0;
            bool operator==(const Edge&) const = default;
        };

        struct EdgeHash
        {
            size_t operator()(const Edge& edge) const
            {
                return (size_t(edge.m_a) << 32) ^ edge.m_b;
            }
        };

        struct EdgeUse
        {
            AZ::u32 m_count = 0;
            int m_direction = 0;
        };

        VertexBits GetVertexBits(const AZ::Vector3& value)
        {
            VertexBits result;
            float components[3] = { value.GetX(), value.GetY(), value.GetZ() };
            // Canonicalize signed zero so exporter-created duplicates weld deterministically.
            for (float& component : components)
            {
                if (component == 0.0f)
                {
                    component = 0.0f;
                }
            }
            std::memcpy(&result.m_x, &components[0], sizeof(AZ::u32));
            std::memcpy(&result.m_y, &components[1], sizeof(AZ::u32));
            std::memcpy(&result.m_z, &components[2], sizeof(AZ::u32));
            return result;
        }

        AZ::Aabb TriangleBounds(const TerrainMeshCutoutTriangle& triangle)
        {
            AZ::Aabb bounds = AZ::Aabb::CreateFromPoint(triangle.m_a);
            bounds.AddPoint(triangle.m_b);
            bounds.AddPoint(triangle.m_c);
            return bounds;
        }

        bool SharesVertex(const TerrainMeshCutoutTriangle& a, const TerrainMeshCutoutTriangle& b)
        {
            return a.m_a == b.m_a || a.m_a == b.m_b || a.m_a == b.m_c ||
                a.m_b == b.m_a || a.m_b == b.m_b || a.m_b == b.m_c ||
                a.m_c == b.m_a || a.m_c == b.m_b || a.m_c == b.m_c;
        }

        bool SegmentIntersectsTriangleInterior(
            const AZ::Vector3& start, const AZ::Vector3& end, const TerrainMeshCutoutTriangle& triangle)
        {
            const AZ::Vector3 direction = end - start;
            const AZ::Vector3 edge1 = triangle.m_b - triangle.m_a;
            const AZ::Vector3 edge2 = triangle.m_c - triangle.m_a;
            const AZ::Vector3 p = direction.Cross(edge2);
            const double determinant = double(edge1.Dot(p));
            const double scale = double(direction.GetLength()) * double(edge1.GetLength()) * double(edge2.GetLength());
            const double epsilon = AZStd::max(1.0e-12, scale * 1.0e-10);
            if (std::abs(determinant) <= epsilon)
            {
                return false;
            }
            const double inverse = 1.0 / determinant;
            const AZ::Vector3 offset = start - triangle.m_a;
            const double u = double(offset.Dot(p)) * inverse;
            const AZ::Vector3 q = offset.Cross(edge1);
            const double v = double(direction.Dot(q)) * inverse;
            const double t = double(edge2.Dot(q)) * inverse;
            constexpr double EdgeEpsilon = 1.0e-7;
            return u >= -EdgeEpsilon && v >= -EdgeEpsilon && u + v <= 1.0 + EdgeEpsilon &&
                t > EdgeEpsilon && t < 1.0 - EdgeEpsilon;
        }

        struct ProjectedPoint
        {
            double m_x = 0.0;
            double m_y = 0.0;
        };

        ProjectedPoint ProjectPoint(const AZ::Vector3& point, int droppedAxis)
        {
            if (droppedAxis == 0) return { point.GetY(), point.GetZ() };
            if (droppedAxis == 1) return { point.GetX(), point.GetZ() };
            return { point.GetX(), point.GetY() };
        }

        double Orient2d(const ProjectedPoint& a, const ProjectedPoint& b, const ProjectedPoint& c)
        {
            return (b.m_x - a.m_x) * (c.m_y - a.m_y) - (b.m_y - a.m_y) * (c.m_x - a.m_x);
        }

        bool PointInProjectedTriangle(
            const ProjectedPoint& point, const ProjectedPoint& a,
            const ProjectedPoint& b, const ProjectedPoint& c, double epsilon)
        {
            const double ab = Orient2d(a, b, point);
            const double bc = Orient2d(b, c, point);
            const double ca = Orient2d(c, a, point);
            return (ab >= -epsilon && bc >= -epsilon && ca >= -epsilon) ||
                (ab <= epsilon && bc <= epsilon && ca <= epsilon);
        }

        bool ProjectedSegmentsIntersect(
            const ProjectedPoint& a, const ProjectedPoint& b,
            const ProjectedPoint& c, const ProjectedPoint& d, double epsilon)
        {
            const double abC = Orient2d(a, b, c);
            const double abD = Orient2d(a, b, d);
            const double cdA = Orient2d(c, d, a);
            const double cdB = Orient2d(c, d, b);
            return ((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
                ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon));
        }

        bool CoplanarTrianglesOverlap(
            const TerrainMeshCutoutTriangle& a, const TerrainMeshCutoutTriangle& b)
        {
            const AZ::Vector3 normalA = (a.m_b - a.m_a).Cross(a.m_c - a.m_a);
            const AZ::Vector3 normalB = (b.m_b - b.m_a).Cross(b.m_c - b.m_a);
            const double normalScale = double(normalA.GetLengthSq()) * double(normalB.GetLengthSq());
            if (normalScale <= 0.0 || double(normalA.Cross(normalB).GetLengthSq()) > normalScale * 1.0e-12)
            {
                return false;
            }
            const double planeScale = double(normalA.GetLength()) *
                AZStd::max(1.0, double(a.m_bounds.GetExtents().GetLength() + b.m_bounds.GetExtents().GetLength()));
            if (std::abs(double(normalA.Dot(b.m_a - a.m_a))) > planeScale * 1.0e-7)
            {
                return false;
            }
            const AZ::Vector3 absoluteNormal = normalA.GetAbs();
            int droppedAxis = absoluteNormal.GetY() > absoluteNormal.GetX() ? 1 : 0;
            if (absoluteNormal.GetZ() > absoluteNormal.GetElement(droppedAxis)) droppedAxis = 2;
            const ProjectedPoint projectedA[]{ ProjectPoint(a.m_a, droppedAxis), ProjectPoint(a.m_b, droppedAxis),
                ProjectPoint(a.m_c, droppedAxis) };
            const ProjectedPoint projectedB[]{ ProjectPoint(b.m_a, droppedAxis), ProjectPoint(b.m_b, droppedAxis),
                ProjectPoint(b.m_c, droppedAxis) };
            const double epsilon = AZStd::max(1.0e-12, planeScale * 1.0e-9);
            for (int edgeA = 0; edgeA < 3; ++edgeA)
            {
                for (int edgeB = 0; edgeB < 3; ++edgeB)
                {
                    if (ProjectedSegmentsIntersect(projectedA[edgeA], projectedA[(edgeA + 1) % 3],
                        projectedB[edgeB], projectedB[(edgeB + 1) % 3], epsilon))
                    {
                        return true;
                    }
                }
            }
            return PointInProjectedTriangle(projectedA[0], projectedB[0], projectedB[1], projectedB[2], epsilon) ||
                PointInProjectedTriangle(projectedB[0], projectedA[0], projectedA[1], projectedA[2], epsilon);
        }

        bool TrianglesIntersect(
            const TerrainMeshCutoutTriangle& a, const TerrainMeshCutoutTriangle& b)
        {
            if (!a.m_bounds.Overlaps(b.m_bounds) || SharesVertex(a, b))
            {
                return false;
            }
            return SegmentIntersectsTriangleInterior(a.m_a, a.m_b, b) ||
                SegmentIntersectsTriangleInterior(a.m_b, a.m_c, b) ||
                SegmentIntersectsTriangleInterior(a.m_c, a.m_a, b) ||
                SegmentIntersectsTriangleInterior(b.m_a, b.m_b, a) ||
                SegmentIntersectsTriangleInterior(b.m_b, b.m_c, a) ||
                SegmentIntersectsTriangleInterior(b.m_c, b.m_a, a) || CoplanarTrianglesOverlap(a, b);
        }

        bool HasSelfIntersection(const TerrainMeshCutoutData& data)
        {
            for (AZ::u32 triangleIndex = 0; triangleIndex < data.m_triangles.size(); ++triangleIndex)
            {
                const auto& triangle = data.m_triangles[triangleIndex];
                if (Internal::VisitMeshCutoutTriangles(data,
                    [&](const AZ::Aabb& bounds) { return bounds.Overlaps(triangle.m_bounds); },
                    [&](AZ::u32 candidate) { return candidate > triangleIndex && TrianglesIntersect(triangle, data.m_triangles[candidate]); }))
                    return true;
            }
            return false;
        }

        class BvhBuilder
        {
        public:
            explicit BvhBuilder(TerrainMeshCutoutData& data)
                : m_data(data)
            {
            }

            void Build()
            {
                m_order.resize(m_data.m_triangles.size());
                for (AZ::u32 index = 0; index < m_order.size(); ++index)
                {
                    m_order[index] = index;
                }
                m_data.m_bvh.reserve(m_data.m_triangles.size() * 2);
                BuildNode(0, aznumeric_cast<AZ::u32>(m_order.size()));
                AZStd::vector<TerrainMeshCutoutTriangle> reordered;
                reordered.reserve(m_data.m_triangles.size());
                for (const AZ::u32 index : m_order)
                {
                    reordered.push_back(m_data.m_triangles[index]);
                }
                m_data.m_triangles = AZStd::move(reordered);
            }

        private:
            AZ::u32 BuildNode(AZ::u32 first, AZ::u32 count)
            {
                const AZ::u32 nodeIndex = aznumeric_cast<AZ::u32>(m_data.m_bvh.size());
                m_data.m_bvh.emplace_back();
                AZ::Aabb bounds = AZ::Aabb::CreateNull();
                AZ::Aabb centroids = AZ::Aabb::CreateNull();
                for (AZ::u32 offset = 0; offset < count; ++offset)
                {
                    const auto& triangle = m_data.m_triangles[m_order[first + offset]];
                    bounds.AddAabb(triangle.m_bounds);
                    centroids.AddPoint((triangle.m_a + triangle.m_b + triangle.m_c) / 3.0f);
                }
                auto& node = m_data.m_bvh[nodeIndex];
                node.m_bounds = bounds;
                if (count <= LeafTriangleCount)
                {
                    node.m_firstTriangle = first;
                    node.m_triangleCount = count;
                    node.m_escapeIndex = nodeIndex + 1;
                    return nodeIndex;
                }

                const AZ::Vector3 extents = centroids.GetExtents();
                int axis = extents.GetY() > extents.GetX() ? 1 : 0;
                if (extents.GetZ() > extents.GetElement(axis))
                {
                    axis = 2;
                }
                AZStd::sort(m_order.begin() + first, m_order.begin() + first + count,
                    [this, axis](AZ::u32 left, AZ::u32 right)
                    {
                        const auto centroid = [this, axis](AZ::u32 index)
                        {
                            const auto& triangle = m_data.m_triangles[index];
                            return ((triangle.m_a + triangle.m_b + triangle.m_c) / 3.0f).GetElement(axis);
                        };
                        const float a = centroid(left);
                        const float b = centroid(right);
                        return a != b ? a < b : left < right;
                    });
                const AZ::u32 leftCount = count / 2;
                BuildNode(first, leftCount);
                BuildNode(first + leftCount, count - leftCount);
                m_data.m_bvh[nodeIndex].m_escapeIndex = aznumeric_cast<AZ::u32>(m_data.m_bvh.size());
                return nodeIndex;
            }

            TerrainMeshCutoutData& m_data;
            AZStd::vector<AZ::u32> m_order;
        };
    } // namespace

    TerrainMeshCutoutValidation BuildTerrainMeshCutoutData(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<const AZ::u32> indices,
        TerrainMeshCutoutData& result)
    {
        result.m_localBounds = AZ::Aabb::CreateNull();
        result.m_vertices.clear();
        result.m_indices.clear();
        result.m_triangles.clear();
        result.m_bvh.clear();
        if (positions.empty() || indices.empty())
        {
            return TerrainMeshCutoutValidation::Empty;
        }
        if (positions.size() > MaxVertices)
        {
            return TerrainMeshCutoutValidation::ResourceLimit;
        }
        if (indices.size() % 3 != 0)
        {
            return TerrainMeshCutoutValidation::IndexCount;
        }
        if (indices.size() / 3 > MaxTriangles)
        {
            return TerrainMeshCutoutValidation::ResourceLimit;
        }

        AZStd::unordered_map<VertexBits, AZ::u32, VertexBitsHash> welded;
        AZStd::vector<AZ::u32> remap(positions.size());
        AZStd::vector<AZ::Vector3> canonical;
        canonical.reserve(positions.size());
        for (size_t index = 0; index < positions.size(); ++index)
        {
            if (!positions[index].IsFinite())
            {
                return TerrainMeshCutoutValidation::NonFinitePosition;
            }
            const VertexBits key = GetVertexBits(positions[index]);
            const auto [entry, inserted] = welded.emplace(key, aznumeric_cast<AZ::u32>(canonical.size()));
            if (inserted)
            {
                canonical.push_back(positions[index]);
                result.m_localBounds.AddPoint(positions[index]);
            }
            remap[index] = entry->second;
        }
        if (!result.m_localBounds.IsValid() || !result.m_localBounds.GetMin().IsFinite() ||
            !result.m_localBounds.GetMax().IsFinite())
        {
            return TerrainMeshCutoutValidation::Empty;
        }

        const double diagonal = result.m_localBounds.GetExtents().GetLength();
        const double areaSquaredTolerance = std::max(1.0e-24, diagonal * diagonal * diagonal * diagonal * 1.0e-16);
        AZStd::unordered_map<Edge, EdgeUse, EdgeHash> edges;
        result.m_indices.reserve(indices.size());
        result.m_triangles.reserve(indices.size() / 3);
        double signedVolumeTimesSix = 0.0;
        for (size_t index = 0; index < indices.size(); index += 3)
        {
            const AZ::u32 sourceA = indices[index];
            const AZ::u32 sourceB = indices[index + 1];
            const AZ::u32 sourceC = indices[index + 2];
            if (sourceA >= positions.size() || sourceB >= positions.size() || sourceC >= positions.size())
            {
                return TerrainMeshCutoutValidation::IndexOutOfRange;
            }
            const AZ::u32 a = remap[sourceA];
            const AZ::u32 b = remap[sourceB];
            const AZ::u32 c = remap[sourceC];
            if (a == b || b == c || c == a)
            {
                return TerrainMeshCutoutValidation::DegenerateTriangle;
            }
            TerrainMeshCutoutTriangle triangle{ canonical[a], canonical[b], canonical[c] };
            const AZ::Vector3 cross = (triangle.m_b - triangle.m_a).Cross(triangle.m_c - triangle.m_a);
            if (!cross.IsFinite() || double(cross.GetLengthSq()) <= areaSquaredTolerance)
            {
                return TerrainMeshCutoutValidation::DegenerateTriangle;
            }
            triangle.m_bounds = TriangleBounds(triangle);
            result.m_triangles.push_back(triangle);
            result.m_indices.insert(result.m_indices.end(), { a, b, c });
            signedVolumeTimesSix += double(triangle.m_a.Dot(triangle.m_b.Cross(triangle.m_c)));

            const auto addEdge = [&edges](AZ::u32 from, AZ::u32 to)
            {
                const Edge edge{ AZStd::min(from, to), AZStd::max(from, to) };
                auto& use = edges[edge];
                ++use.m_count;
                use.m_direction += from < to ? 1 : -1;
            };
            addEdge(a, b);
            addEdge(b, c);
            addEdge(c, a);
        }
        for (const auto& [edge, use] : edges)
        {
            (void)edge;
            if (use.m_count != 2)
            {
                return TerrainMeshCutoutValidation::OpenOrNonManifold;
            }
            if (use.m_direction != 0)
            {
                return TerrainMeshCutoutValidation::InconsistentWinding;
            }
        }
        const double volumeTolerance = std::max(1.0e-18, diagonal * diagonal * diagonal * 1.0e-12);
        if (!std::isfinite(signedVolumeTimesSix) || std::abs(signedVolumeTimesSix) <= volumeTolerance)
        {
            return TerrainMeshCutoutValidation::ZeroVolume;
        }

        result.m_vertices = AZStd::move(canonical);
        if (signedVolumeTimesSix < 0.0)
        {
            // Winding-number rendering relies on a single outward convention. Imported closed meshes
            // are permitted to be globally reversed, so normalize once in the immutable asset data.
            for (auto& triangle : result.m_triangles)
            {
                AZStd::swap(triangle.m_b, triangle.m_c);
                triangle.m_bounds = TriangleBounds(triangle);
            }
            for (size_t index = 0; index < result.m_indices.size(); index += 3)
            {
                AZStd::swap(result.m_indices[index + 1], result.m_indices[index + 2]);
            }
        }

        BvhBuilder(result).Build();
        if (HasSelfIntersection(result))
        {
            return TerrainMeshCutoutValidation::SelfIntersection;
        }
        return TerrainMeshCutoutValidation::Valid;
    }

    const char* GetTerrainMeshCutoutValidationMessage(TerrainMeshCutoutValidation validation)
    {
        switch (validation)
        {
        case TerrainMeshCutoutValidation::Valid: return "Ready: closed cutter geometry is valid.";
        case TerrainMeshCutoutValidation::Empty: return "Cutter has no indexed triangles.";
        case TerrainMeshCutoutValidation::IndexCount: return "Cutter index count is not divisible by three.";
        case TerrainMeshCutoutValidation::NonFinitePosition: return "Cutter contains a non-finite vertex position.";
        case TerrainMeshCutoutValidation::IndexOutOfRange: return "Cutter contains an out-of-range vertex index.";
        case TerrainMeshCutoutValidation::DegenerateTriangle: return "Cutter contains a zero-area or collapsed triangle.";
        case TerrainMeshCutoutValidation::OpenOrNonManifold: return "Cutter is open or non-manifold; every edge must have exactly two faces.";
        case TerrainMeshCutoutValidation::InconsistentWinding: return "Cutter face winding is inconsistent across a shared edge.";
        case TerrainMeshCutoutValidation::ZeroVolume: return "Cutter does not enclose a finite non-zero volume.";
        case TerrainMeshCutoutValidation::SelfIntersection: return "Cutter contains self-intersecting non-adjacent triangles.";
        case TerrainMeshCutoutValidation::ResourceLimit: return "Cutter exceeds the supported triangle or memory limit.";
        }
        return "Cutter geometry is unsupported.";
    }
} // namespace TerrainCompositor
