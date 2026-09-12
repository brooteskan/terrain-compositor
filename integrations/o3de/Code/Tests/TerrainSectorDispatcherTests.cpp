#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainSectorDispatcher.h>
#include <thread>

namespace TerrainCompositor
{
    namespace
    {
        using Dispatcher = TerrainSectorDispatcher<int>;
        Dispatcher::BatchPtr Submit(Dispatcher& dispatcher, size_t count, size_t bytes = 100, bool critical = false)
        {
            std::vector<std::shared_ptr<int>> values;
            std::vector<std::shared_ptr<std::atomic_bool>> flags;
            for (size_t i = 0; i < count; ++i)
            {
                values.push_back(std::make_shared<int>(int(i)));
                flags.push_back(std::make_shared<std::atomic_bool>(false));
            }
            return dispatcher.Submit(std::move(values), std::move(flags), bytes, critical);
        }
    }
    TEST(TerrainSectorDispatcherTests, OutOfOrderResultsHandOffOnlyAsOneCompleteGroup)
    {
        Dispatcher dispatcher({ 3, 10, 1000, 2 });
        const auto batch = Submit(dispatcher, 3);
        std::vector<Dispatcher::Work> jobs;
        const auto pump = [&] { dispatcher.Pump([](int& value) { value += 10; }, [&](auto job) { jobs.push_back(std::move(job)); }); };
        pump();
        ASSERT_EQ(jobs.size(), 2);
        jobs[1]();
        EXPECT_TRUE(dispatcher.TakeCompleted().empty());
        pump();
        ASSERT_EQ(jobs.size(), 3);
        jobs[2]();
        EXPECT_TRUE(dispatcher.TakeCompleted().empty());
        jobs[0]();
        ASSERT_EQ(dispatcher.TakeCompleted().size(), 1);
        for (size_t i = 0; i < 3; ++i) EXPECT_EQ(*batch->m_values[i], int(i + 10));
        EXPECT_EQ(dispatcher.GetAccounting().m_reservedBytes, 100); // Awaiting atomic commit still owns memory.
        dispatcher.Release(batch);
        EXPECT_EQ(dispatcher.GetAccounting().m_reservedBytes, 0);
    }
    TEST(TerrainSectorDispatcherTests, ReservationsBoundGroupsRequestsBytesAndCompletionStorage)
    {
        Dispatcher dispatcher({ 2, 4, 250, 1 });
        const auto a = Submit(dispatcher, 2);
        EXPECT_FALSE(Submit(dispatcher, 3));
        EXPECT_FALSE(Submit(dispatcher, 1, 151));
        const auto b = Submit(dispatcher, 2, 150);
        EXPECT_FALSE(Submit(dispatcher, 1, 0));
        EXPECT_EQ(dispatcher.GetAccounting().m_peakBytes, 250);
        dispatcher.Cancel(a);
        EXPECT_FALSE(Submit(dispatcher, 1)); // Cancel is not reclamation.
        dispatcher.Release(a);
        EXPECT_TRUE(Submit(dispatcher, 2));
        dispatcher.Cancel(b);
        dispatcher.Drain();
        EXPECT_EQ(dispatcher.GetAccounting().m_groups, 0);
    }
    TEST(TerrainSectorDispatcherTests, CancellationRetainsRunningStorageUntilAcknowledgement)
    {
        Dispatcher dispatcher({ 1, 4, 100, 1 });
        auto batch = Submit(dispatcher, 4);
        std::vector<Dispatcher::Work> jobs;
        dispatcher.Pump([](int& value) { value = 42; }, [&](auto job) { jobs.push_back(std::move(job)); });
        dispatcher.Cancel(batch);
        for (const auto& flag : batch->m_cancellations) EXPECT_TRUE(flag->load());
        dispatcher.Release(batch);
        EXPECT_EQ(dispatcher.GetAccounting().m_reservedBytes, 100);
        EXPECT_TRUE(dispatcher.TakeCompleted().empty());
        jobs.front()();
        ASSERT_EQ(dispatcher.TakeCompleted().size(), 1);
        dispatcher.Release(batch);
        EXPECT_EQ(dispatcher.GetAccounting().m_reservedBytes, 0);
        EXPECT_TRUE(Submit(dispatcher, 1));
    }
    TEST(TerrainSectorDispatcherTests, CriticalReversalDoesNotStarveAnOlderCoarsePrerequisite)
    {
        Dispatcher dispatcher({ 3, 20, 1000, 1 });
        auto older = Submit(dispatcher, 2);
        auto reversal = Submit(dispatcher, 8, 100, true);
        std::vector<Dispatcher::Work> jobs;
        for (size_t turn = 0; turn < 4; ++turn)
        {
            dispatcher.Pump([](int& value) { value += 100; }, [&](auto job) { jobs.push_back(std::move(job)); });
            jobs.back()();
        }
        EXPECT_EQ(older->m_finished, 1); // The fourth dispatch must serve the older group.
        // Inspect payloads only after finishing the complete older group.
        dispatcher.Cancel(reversal);
        for (size_t turn = 0; turn < 2; ++turn)
        {
            jobs.clear();
            dispatcher.Pump([](int& value) { value += 100; }, [&](auto job) { jobs.push_back(std::move(job)); });
            for (auto& job : jobs) job();
        }
        EXPECT_EQ(older->m_finished, 2);
        EXPECT_EQ(*older->m_values[0], 100);
        EXPECT_EQ(*older->m_values[1], 101);
    }
    TEST(TerrainSectorDispatcherTests, ResetAndShutdownJoinWorkersAndReleaseAllRetainedValues)
    {
        Dispatcher dispatcher({ 1, 4, 100, 2 });
        auto batch = Submit(dispatcher, 4);
        std::weak_ptr<int> value = batch->m_values.front();
        std::vector<std::thread> workers;
        dispatcher.Pump([](int& item) { item += 1; }, [&](auto job) { workers.emplace_back(std::move(job)); });
        batch.reset();
        dispatcher.Drain();
        for (auto& worker : workers) worker.join();
        EXPECT_TRUE(value.expired());
        EXPECT_EQ(dispatcher.GetAccounting().m_running, 0);
        EXPECT_EQ(dispatcher.GetAccounting().m_reservedBytes, 0);
        EXPECT_TRUE(Submit(dispatcher, 1)); // Reset permits a fresh generation.
    }
}
