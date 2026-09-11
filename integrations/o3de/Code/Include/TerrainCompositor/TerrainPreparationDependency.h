#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace TerrainCompositor
{
    //! A lifetime-safe invalidation authority, not a lease on a live bus handler.
    //! Notifications advance the revision immediately, before deferred terrain refresh.
    class TerrainPreparationDependency
    {
    public:
        AZ::u64 Capture() const
        {
            std::lock_guard lock(m_mutex);
            return m_revision;
        }
        void Invalidate()
        {
            std::lock_guard lock(m_mutex);
            ++m_revision;
        }
        void Retire()
        {
            std::lock_guard lock(m_mutex);
            m_active = false;
            ++m_revision;
        }

    private:
        friend class TerrainPreparationAdmission;
        mutable std::mutex m_mutex;
        AZ::u64 m_revision = 0;
        bool m_active = true;
    };

    struct TerrainPreparationDependencyTicket
    {
        std::shared_ptr<TerrainPreparationDependency> m_dependency;
        AZ::u64 m_revision = 0;
    };

    //! Hold through GPU commit. No source callbacks or terrain buses may be invoked
    //! while admitted. Lock order is publication, then unique dependencies by address.
    class TerrainPreparationAdmission
    {
    public:
        explicit TerrainPreparationAdmission(const AZStd::vector<TerrainPreparationDependencyTicket>& tickets)
        {
            for (const auto& ticket : tickets)
                if (ticket.m_dependency) m_dependencies.push_back(ticket.m_dependency);
            std::sort(m_dependencies.begin(), m_dependencies.end(), [](const auto& left, const auto& right)
            {
                return std::less<TerrainPreparationDependency*>{}(left.get(), right.get());
            });
            m_dependencies.erase(std::unique(m_dependencies.begin(), m_dependencies.end()), m_dependencies.end());
            for (const auto& dependency : m_dependencies) m_locks.emplace_back(dependency->m_mutex);
            for (const auto& ticket : tickets)
                m_valid = m_valid && ticket.m_dependency && ticket.m_dependency->m_active &&
                    ticket.m_dependency->m_revision == ticket.m_revision;
        }
        bool IsValid() const { return m_valid; }

    private:
        // Keep the mutexes alive even if the caller passes temporary tickets.
        std::vector<std::shared_ptr<TerrainPreparationDependency>> m_dependencies;
        std::vector<std::unique_lock<std::mutex>> m_locks;
        bool m_valid = true;
    };
}
