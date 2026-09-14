#pragma once

namespace TerrainCompositor::Internal
{
    // Publication invalidation compares identity and validation, not the detailed
    // diagnostic payload used separately to detect conflicting registration ties.
    template<class Snapshot>
    bool AssetSnapshotsEqual(const Snapshot& left, const Snapshot& right)
    {
        if (left.m_status != right.m_status) return false;
        if constexpr (requires { left.m_modelValidation; })
            if (left.m_modelValidation != right.m_modelValidation) return false;
        if constexpr (requires { left.m_validation; })
            if (left.m_validation != right.m_validation) return false;
        return left.m_revision == right.m_revision && left.m_assetId == right.m_assetId && left.m_data == right.m_data;
    }
}
