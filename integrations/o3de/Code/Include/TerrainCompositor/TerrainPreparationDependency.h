#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace TerrainCompositor
{
    // Each domain retains its own process-local sequence and original atomic ordering.
    template<class Domain>
    AZ::u64 NextTerrainIdentity()
    {
        static std::atomic<AZ::u64> next{ 0 };
        return ++next;
    }

    //! A lifetime-safe invalidation authority, not a lease on a live bus handler.
    //! Notifications advance the revision immediately, before deferred terrain refresh.
    class TerrainPreparationDependency
    {
    public:
        AZ::u64 Capture() const
        {
            std::lock_guard lock(m_mutex);
            return m_revision.load(std::memory_order_relaxed);
        }
        //! Visibility observation only, never a commit lease. Invalidation is
        //! monotonic and retirement permanent, so unchanged tickets stay valid.
        bool IsCurrent(AZ::u64 revision) const
        {
            return m_active.load(std::memory_order_acquire) && m_revision.load(std::memory_order_acquire) == revision;
        }
        void Invalidate()
        {
            std::lock_guard lock(m_mutex);
            m_revision.fetch_add(1, std::memory_order_release);
        }
        void Retire()
        {
            std::lock_guard lock(m_mutex);
            m_active.store(false, std::memory_order_release);
            m_revision.fetch_add(1, std::memory_order_release);
        }

    private:
        friend class TerrainPreparationAdmission;
        mutable std::mutex m_mutex;
        std::atomic<AZ::u64> m_revision{ 0 };
        std::atomic_bool m_active{ true };
    };

    struct TerrainPreparationDependencyTicket
    {
        std::shared_ptr<TerrainPreparationDependency> m_dependency;
        AZ::u64 m_revision = 0;
    };

    inline std::vector<std::shared_ptr<TerrainPreparationDependency>> CollectTerrainPreparationDependencies(
        const AZStd::vector<TerrainPreparationDependencyTicket>& tickets)
    {
        std::vector<std::shared_ptr<TerrainPreparationDependency>> dependencies;
        for (const auto& ticket : tickets)
            if (ticket.m_dependency) dependencies.push_back(ticket.m_dependency);
        std::sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right)
        {
            return std::less<TerrainPreparationDependency*>{}(left.get(), right.get());
        });
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        return dependencies;
    }

    //! Immutable lock order and exact tickets. Preparing metadata never refreshes a
    //! revision or grants admission. Equal sets may be shared by committed sectors.
    class TerrainPreparationDependencySet
    {
    public:
        explicit TerrainPreparationDependencySet(const AZStd::vector<TerrainPreparationDependencyTicket>& tickets)
            : m_tickets(tickets)
        {
            m_dependencies = CollectTerrainPreparationDependencies(tickets);
        }
        const AZStd::vector<TerrainPreparationDependencyTicket>& GetTickets() const { return m_tickets; }
        //! A fresh observation of every exact ticket, including conflicting
        //! duplicates. This does not hold invalidation off through GPU publication.
        bool IsCurrent() const
        {
            for (const auto& ticket : m_tickets)
                if (!ticket.m_dependency || !ticket.m_dependency->IsCurrent(ticket.m_revision)) return false;
            return true;
        }
        size_t GetHeapBytes() const
        {
            return m_tickets.capacity() * sizeof(TerrainPreparationDependencyTicket) +
                m_dependencies.capacity() * sizeof(std::shared_ptr<TerrainPreparationDependency>);
        }
        bool Matches(const AZStd::vector<TerrainPreparationDependencyTicket>& tickets) const
        {
            return tickets.size() == m_tickets.size() && std::equal(tickets.begin(), tickets.end(), m_tickets.begin(),
                [](const auto& a, const auto& b) { return a.m_dependency == b.m_dependency && a.m_revision == b.m_revision; });
        }

    private:
        friend class TerrainPreparationAdmission;
        AZStd::vector<TerrainPreparationDependencyTicket> m_tickets;
        std::vector<std::shared_ptr<TerrainPreparationDependency>> m_dependencies;
    };

    //! One control owner, never concurrent admissions. Capacity survives;
    //! no locks, source references or validation results survive an admission.
    class TerrainPreparationAdmissionScratch
    {
    private:
        friend class TerrainPreparationAdmission;
        std::vector<std::unique_lock<std::mutex>> m_locks;
    };

    //! Hold through GPU commit. No source callbacks or terrain buses may be invoked
    //! while admitted. Lock order is publication, then unique dependencies by address.
    class TerrainPreparationAdmission
    {
    public:
        explicit TerrainPreparationAdmission(const AZStd::vector<TerrainPreparationDependencyTicket>& tickets)
        {
            m_dependencies = CollectTerrainPreparationDependencies(tickets);
            for (const auto& dependency : m_dependencies) m_locks.emplace_back(dependency->m_mutex);
            for (const auto& ticket : tickets)
                m_valid = m_valid && ticket.m_dependency && ticket.m_dependency->m_active &&
                    ticket.m_dependency->m_revision == ticket.m_revision;
        }
        TerrainPreparationAdmission(std::shared_ptr<const TerrainPreparationDependencySet> dependencies,
            TerrainPreparationAdmissionScratch& scratch)
            : m_prepared(AZStd::move(dependencies)), m_reusedLocks(&scratch.m_locks)
        {
            // A nested admission gets private storage; it cannot release the outer locks.
            if (!m_reusedLocks->empty()) m_reusedLocks = &m_locks;
            if (!m_prepared) { m_valid = false; return; }
            m_reusedLocks->reserve(m_prepared->m_dependencies.size());
            for (const auto& dependency : m_prepared->m_dependencies) m_reusedLocks->emplace_back(dependency->m_mutex);
            for (const auto& ticket : m_prepared->m_tickets)
                m_valid = m_valid && ticket.m_dependency && ticket.m_dependency->m_active &&
                    ticket.m_dependency->m_revision == ticket.m_revision;
        }
        ~TerrainPreparationAdmission()
        {
            // Release locks before the retained set (and therefore its mutexes).
            if (m_reusedLocks) m_reusedLocks->clear();
        }
        TerrainPreparationAdmission(const TerrainPreparationAdmission&) = delete;
        TerrainPreparationAdmission& operator=(const TerrainPreparationAdmission&) = delete;
        bool IsValid() const { return m_valid; }

    private:
        // Keep the mutexes alive even if the caller passes temporary tickets.
        std::vector<std::shared_ptr<TerrainPreparationDependency>> m_dependencies;
        std::vector<std::unique_lock<std::mutex>> m_locks;
        std::shared_ptr<const TerrainPreparationDependencySet> m_prepared;
        std::vector<std::unique_lock<std::mutex>>* m_reusedLocks = nullptr;
        bool m_valid = true;
    };
}
