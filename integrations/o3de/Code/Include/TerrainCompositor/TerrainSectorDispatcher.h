#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace TerrainCompositor
{
    //! Bounded control-thread dispatcher. Payloads are inaccessible to the
    //! consumer until an entire batch is returned through TakeCompleted(). The
    //! mutex is the completion handoff, not timestamps or polling worker vectors.
    //! Launch must enqueue asynchronously; prepare never captures a manager.
    template<class Payload>
    class TerrainSectorDispatcher
    {
    public:
        struct Limits
        {
            size_t m_groups = 3, m_requests = 96, m_bytes = 256 * 1024 * 1024, m_workers = 2;
        };
        struct Batch
        {
            size_t m_id = 0, m_bytes = 0, m_next = 0, m_running = 0, m_finished = 0;
            bool m_critical = false, m_cancelled = false;
            std::vector<std::shared_ptr<Payload>> m_values;
            std::vector<std::shared_ptr<std::atomic_bool>> m_cancellations;
        };
        struct Accounting { size_t m_groups, m_requests, m_running, m_reservedBytes, m_peakBytes; };
        using BatchPtr = std::shared_ptr<Batch>;
        using Work = std::function<void()>;

        explicit TerrainSectorDispatcher(Limits limits = {}) : m_state(std::make_shared<State>(limits)) {}
        ~TerrainSectorDispatcher() { Drain(); }
        TerrainSectorDispatcher(const TerrainSectorDispatcher&) = delete;
        TerrainSectorDispatcher& operator=(const TerrainSectorDispatcher&) = delete;

        bool CanReserve(size_t requests, size_t bytes) const
        {
            std::lock_guard lock(m_state->m_mutex);
            return Fits(*m_state, requests, bytes);
        }
        BatchPtr Submit(std::vector<std::shared_ptr<Payload>> values,
            std::vector<std::shared_ptr<std::atomic_bool>> cancellations, size_t bytes, bool critical)
        {
            std::lock_guard lock(m_state->m_mutex);
            if (values.empty() || values.size() != cancellations.size() || !Fits(*m_state, values.size(), bytes)) return {};
            auto batch = std::make_shared<Batch>();
            batch->m_id = ++m_state->m_nextId;
            batch->m_bytes = bytes;
            batch->m_critical = critical;
            batch->m_values = std::move(values);
            batch->m_cancellations = std::move(cancellations);
            m_state->m_requests += batch->m_values.size();
            m_state->m_bytes += bytes;
            m_state->m_peakBytes = std::max(m_state->m_peakBytes, m_state->m_bytes);
            m_state->m_batches.push_back(batch);
            return batch;
        }
        void Prioritize(const BatchPtr& batch)
        {
            std::lock_guard lock(m_state->m_mutex);
            batch->m_critical = true;
        }
        void Cancel(const BatchPtr& batch)
        {
            std::lock_guard lock(m_state->m_mutex);
            CancelLocked(*batch);
        }
        template<class Prepare, class Launch>
        void Pump(Prepare prepare, Launch launch)
        {
            for (;;)
            {
                BatchPtr batch;
                size_t index = 0;
                {
                    std::lock_guard lock(m_state->m_mutex);
                    if (m_state->m_running >= m_state->m_limits.m_workers) return;
                    // Every fourth dispatch serves the oldest eligible group.
                    // Reversals/critical work cannot starve older prerequisites.
                    const bool oldest = (++m_state->m_dispatches % 4) == 0;
                    for (const auto& candidate : m_state->m_batches)
                        if (!candidate->m_cancelled && candidate->m_next < candidate->m_values.size() &&
                            (!batch || (!oldest && candidate->m_critical && !batch->m_critical))) batch = candidate;
                    if (!batch) return;
                    index = batch->m_next++;
                    ++batch->m_running;
                    ++m_state->m_running;
                }
                auto state = m_state;
                launch(Work([state, batch, index, prepare]() mutable
                {
                    prepare(*batch->m_values[index]);
                    std::lock_guard lock(state->m_mutex);
                    --batch->m_running;
                    ++batch->m_finished;
                    batch.reset(); // Release the worker's payload ownership before the handoff.
                    --state->m_running;
                    state->m_done.notify_all();
                }));
            }
        }
        // The returned batches are wholly owned by the control thread. Taking
        // completion does NOT release their reservation until Release is called.
        std::vector<BatchPtr> TakeCompleted()
        {
            std::lock_guard lock(m_state->m_mutex);
            std::vector<BatchPtr> completed;
            for (const auto& batch : m_state->m_batches)
                if (!batch->m_running && batch->m_finished == batch->m_values.size()) completed.push_back(batch);
            return completed;
        }
        void Release(const BatchPtr& batch)
        {
            std::lock_guard lock(m_state->m_mutex);
            const auto found = std::find(m_state->m_batches.begin(), m_state->m_batches.end(), batch);
            if (found == m_state->m_batches.end() || batch->m_running || batch->m_finished != batch->m_values.size()) return;
            m_state->m_requests -= batch->m_values.size();
            m_state->m_bytes -= batch->m_bytes;
            m_state->m_batches.erase(found);
        }
        Accounting GetAccounting() const
        {
            std::lock_guard lock(m_state->m_mutex);
            return { m_state->m_batches.size(), m_state->m_requests, m_state->m_running, m_state->m_bytes, m_state->m_peakBytes };
        }
        void Drain()
        {
            std::unique_lock lock(m_state->m_mutex);
            for (auto& batch : m_state->m_batches) CancelLocked(*batch);
            m_state->m_done.wait(lock, [&] { return !m_state->m_running; });
            m_state->m_batches.clear();
            m_state->m_requests = m_state->m_bytes = 0;
        }
    private:
        struct State
        {
            explicit State(Limits limits) : m_limits(limits) { m_batches.reserve(limits.m_groups); }
            Limits m_limits;
            std::mutex m_mutex;
            std::condition_variable m_done;
            std::vector<BatchPtr> m_batches;
            size_t m_requests = 0, m_bytes = 0, m_running = 0, m_peakBytes = 0, m_nextId = 0, m_dispatches = 0;
        };
        static bool Fits(const State& state, size_t requests, size_t bytes)
        {
            return state.m_limits.m_workers && state.m_batches.size() < state.m_limits.m_groups &&
                requests <= state.m_limits.m_requests - state.m_requests && bytes <= state.m_limits.m_bytes - state.m_bytes;
        }
        static void CancelLocked(Batch& batch)
        {
            if (batch.m_cancelled) return;
            batch.m_cancelled = true;
            for (const auto& flag : batch.m_cancellations) flag->store(true, std::memory_order_release);
            batch.m_finished += batch.m_values.size() - batch.m_next;
            batch.m_next = batch.m_values.size();
        }
        std::shared_ptr<State> m_state;
    };
}
