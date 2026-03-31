#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "asyncx.h"

TEST(AsyncxTests, SubmitCollectsResults)
{
    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 16;

    asyncx::ThreadPool pool(options);

    std::vector<std::future<int>> futures;
    futures.reserve(10);

    for (int i = 1; i <= 10; ++i)
    {
        auto submitted = pool.Submit([i]()
                                     { return i * 2; });
        ASSERT_TRUE(submitted.ok) << submitted.error.message;
        futures.push_back(std::move(submitted.value));
    }

    int total = 0;
    for (auto &future : futures)
    {
        total += future.get();
    }

    EXPECT_EQ(total, 110);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, PostForTimesOutWhenQueueFull)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 1;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();

    ASSERT_TRUE(pool.Post([hold]()
                          { hold.wait(); })
                    .ok);
    ASSERT_TRUE(pool.Post([]() {}).ok);

    const auto status = pool.PostFor(std::chrono::milliseconds(25), []() {});
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, asyncx::ErrorKind::Timeout);

    gate.set_value();

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, StopRejectsNewTasks)
{
    asyncx::ThreadPool pool;

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);

    const auto status = pool.Post([]() {});
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, asyncx::ErrorKind::QueueClosed);

    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, TaskExceptionDoesNotBreakPool)
{
    asyncx::ThreadPool pool;

    auto bad = pool.Submit([]() -> int
                           { throw std::runtime_error("boom"); });
    ASSERT_TRUE(bad.ok) << bad.error.message;
    EXPECT_THROW(bad.value.get(), std::runtime_error);

    auto good = pool.Submit([]()
                            { return 7; });
    ASSERT_TRUE(good.ok) << good.error.message;
    EXPECT_EQ(good.value.get(), 7);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, ManualStartAndQueueCapacityFallback)
{
    asyncx::PoolOptions options;
    options.start_immediately = false;
    options.worker_count = 1;
    options.queue_capacity = 0;

    asyncx::ThreadPool pool(options);

    auto before_start = pool.Post([]() {});
    EXPECT_FALSE(before_start.ok);
    EXPECT_EQ(before_start.error.kind, asyncx::ErrorKind::NotRunning);

    EXPECT_TRUE(pool.Start().ok);
    EXPECT_EQ(pool.QueueCapacity(), 1U);

    auto submitted = pool.Submit([]()
                                 { return 3; });
    ASSERT_TRUE(submitted.ok) << submitted.error.message;
    EXPECT_EQ(submitted.value.get(), 3);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, TryPostReturnsQueueFullWhenNoSlot)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 1;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();

    ASSERT_TRUE(pool.Post([hold]()
                          { hold.wait(); })
                    .ok);
    ASSERT_TRUE(pool.Post([]() {}).ok);

    const auto status = pool.TryPost([]() {});
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, asyncx::ErrorKind::QueueFull);

    const auto high_status = pool.TryPostWithPriority(asyncx::TaskPriority::High, []() {});
    EXPECT_FALSE(high_status.ok);
    EXPECT_EQ(high_status.error.kind, asyncx::ErrorKind::QueueFull);

    auto submit_status = pool.TrySubmit([]()
                                        { return 1; });
    EXPECT_FALSE(submit_status.ok);
    EXPECT_EQ(submit_status.error.kind, asyncx::ErrorKind::QueueFull);

    gate.set_value();
    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(300)).ok);
    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, PriorityTasksRunHighBeforeLow)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 8;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();
    std::vector<int> order;
    std::mutex order_mu;

    ASSERT_TRUE(pool.Post([hold]()
                          { hold.wait(); })
                    .ok);

    ASSERT_TRUE(pool.PostWithPriority(asyncx::TaskPriority::Low,
                                      [&order, &order_mu]()
                                      {
                                          std::lock_guard<std::mutex> lock(order_mu);
                                          order.push_back(1);
                                      })
                    .ok);

    ASSERT_TRUE(pool.PostWithPriority(asyncx::TaskPriority::High,
                                      [&order, &order_mu]()
                                      {
                                          std::lock_guard<std::mutex> lock(order_mu);
                                          order.push_back(2);
                                      })
                    .ok);

    gate.set_value();
    ASSERT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(500)).ok);

    ASSERT_EQ(order.size(), 2U);
    EXPECT_EQ(order[0], 2);
    EXPECT_EQ(order[1], 1);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, SubmitWithPriorityWorks)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 4;

    asyncx::ThreadPool pool(options);

    auto f = pool.SubmitWithPriority(asyncx::TaskPriority::High, []()
                                     { return 42; });
    ASSERT_TRUE(f.ok) << f.error.message;
    EXPECT_EQ(f.value.get(), 42);
    EXPECT_STREQ(asyncx::ToString(asyncx::TaskPriority::High), "high");

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, PrioritySchedulingPreventsLowStarvation)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 64;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();
    ASSERT_TRUE(pool.Post([hold]()
                          { hold.wait(); })
                    .ok);

    std::vector<int> order;
    std::mutex order_mu;

    ASSERT_TRUE(pool.PostWithPriority(asyncx::TaskPriority::Low,
                                      [&order, &order_mu]()
                                      {
                                          std::lock_guard<std::mutex> lock(order_mu);
                                          order.push_back(1000);
                                      })
                    .ok);

    for (int i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(pool.PostWithPriority(asyncx::TaskPriority::High,
                                          [&order, &order_mu, i]()
                                          {
                                              std::lock_guard<std::mutex> lock(order_mu);
                                              order.push_back(i);
                                          })
                        .ok);
    }

    gate.set_value();
    ASSERT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(1000)).ok);

    ASSERT_EQ(order.size(), 11u);
    auto low_it = std::find(order.begin(), order.end(), 1000);
    ASSERT_NE(low_it, order.end());
    const std::size_t low_index = static_cast<std::size_t>(std::distance(order.begin(), low_it));
    EXPECT_LE(low_index, 6u);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, SubmitUntilTimesOutWhenQueueFull)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 1;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();

    ASSERT_TRUE(pool.Post([hold]()
                          { hold.wait(); })
                    .ok);
    ASSERT_TRUE(pool.Post([]() {}).ok);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
    auto submitted = pool.SubmitUntil(deadline, []()
                                      { return 5; });
    EXPECT_FALSE(submitted.ok);
    EXPECT_EQ(submitted.error.kind, asyncx::ErrorKind::Timeout);

    gate.set_value();
    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(300)).ok);
    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, WaitForIdleTracksRunningTasks)
{
    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 8;

    asyncx::ThreadPool pool(options);

    std::atomic<int> done{0};
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_TRUE(pool.Post([&done]()
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                  done.fetch_add(1, std::memory_order_relaxed); })
                        .ok);
    }

    EXPECT_FALSE(pool.IsIdle());
    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(500)).ok);
    EXPECT_EQ(done.load(std::memory_order_relaxed), 6);
    EXPECT_EQ(pool.ActiveWorkerCount(), 0U);
    EXPECT_TRUE(pool.IsIdle());

    const auto stats = pool.GetStats();
    EXPECT_EQ(stats.submitted, 6U);
    EXPECT_EQ(stats.completed, 6U);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);
}

TEST(AsyncxTests, WaitForIdleUntilTimesOut)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 2;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();
    ASSERT_TRUE(pool.Post([hold]()
                          { hold.wait(); })
                    .ok);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    const auto status = pool.WaitForIdleUntil(deadline);
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, asyncx::ErrorKind::Timeout);

    gate.set_value();
    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(300)).ok);
    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, ResetStatsReturnsPreviousAndClears)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 8;

    asyncx::ThreadPool pool(options);

    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(pool.Post([]()
                              { std::this_thread::sleep_for(std::chrono::milliseconds(5)); })
                        .ok);
    }

    ASSERT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(500)).ok);

    const auto before_reset = pool.GetStats();
    EXPECT_EQ(before_reset.submitted, 3U);
    EXPECT_EQ(before_reset.completed, 3U);

    const auto previous = pool.ResetStats();
    EXPECT_EQ(previous.submitted, 3U);
    EXPECT_EQ(previous.completed, 3U);

    const auto after_reset = pool.GetStats();
    EXPECT_EQ(after_reset.submitted, 0U);
    EXPECT_EQ(after_reset.completed, 0U);
    EXPECT_EQ(after_reset.rejected, 0U);
    EXPECT_EQ(after_reset.timed_out, 0U);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, StopAndJoinRejectsNewTasks)
{
    asyncx::ThreadPool pool;

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);

    const auto status = pool.Post([]() {});
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, asyncx::ErrorKind::NotRunning);
}

TEST(AsyncxTests, PostDelayedForRunsAndBecomesIdle)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 8;

    asyncx::ThreadPool pool(options);

    std::promise<void> done;
    auto done_future = done.get_future();

    auto scheduled = pool.PostDelayedFor(std::chrono::milliseconds(40), [&done]()
                                         { done.set_value(); });
    ASSERT_TRUE(scheduled.ok) << scheduled.error.message;
    EXPECT_EQ(pool.ScheduledCount(), 1U);

    EXPECT_FALSE(pool.IsIdle());
    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(600)).ok);
    EXPECT_EQ(done_future.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, CancelScheduledPreventsDelayedExecution)
{
    asyncx::ThreadPool pool;

    std::atomic<int> hits{0};
    auto scheduled = pool.PostDelayedFor(std::chrono::milliseconds(120), [&hits]()
                                         { hits.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(scheduled.ok) << scheduled.error.message;

    const auto cancel_status = pool.CancelScheduled(scheduled.value);
    EXPECT_TRUE(cancel_status.ok) << cancel_status.error.message;
    EXPECT_EQ(pool.ScheduledCount(), 0U);

    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    EXPECT_EQ(hits.load(std::memory_order_relaxed), 0);

    const auto missing = pool.CancelScheduled(scheduled.value);
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(missing.error.kind, asyncx::ErrorKind::NotFound);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, ScheduleEveryCanRunAndCancel)
{
    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 16;

    asyncx::ThreadPool pool(options);

    std::atomic<int> ticks{0};
    auto periodic = pool.ScheduleEvery(std::chrono::milliseconds(20), [&ticks]()
                                       { ticks.fetch_add(1, std::memory_order_relaxed); }, true);
    ASSERT_TRUE(periodic.ok) << periodic.error.message;

    const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < wait_deadline && ticks.load(std::memory_order_relaxed) < 3)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(ticks.load(std::memory_order_relaxed), 3);

    ASSERT_TRUE(pool.CancelScheduled(periodic.value).ok);
    const int captured = ticks.load(std::memory_order_relaxed);

    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(500)).ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_LE(ticks.load(std::memory_order_relaxed), captured + 1);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, WaitForIdleIncludesScheduledTasks)
{
    asyncx::ThreadPool pool;

    auto scheduled = pool.PostDelayedFor(std::chrono::milliseconds(100), []() {});
    ASSERT_TRUE(scheduled.ok) << scheduled.error.message;

    const auto early = pool.WaitForIdleFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(early.ok);
    EXPECT_EQ(early.error.kind, asyncx::ErrorKind::Timeout);

    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(500)).ok);
    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, WaitAllAndWaitAnyHelpersWork)
{
    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 16;

    asyncx::ThreadPool pool(options);

    std::vector<std::future<int>> futures;
    auto f0 = pool.Submit([]()
                          {
                              std::this_thread::sleep_for(std::chrono::milliseconds(80));
                              return 10; });
    auto f1 = pool.Submit([]()
                          {
                              std::this_thread::sleep_for(std::chrono::milliseconds(20));
                              return 20; });
    ASSERT_TRUE(f0.ok);
    ASSERT_TRUE(f1.ok);
    futures.push_back(std::move(f0.value));
    futures.push_back(std::move(f1.value));

    auto any = asyncx::WaitAnyFor(futures, std::chrono::milliseconds(300));
    ASSERT_TRUE(any.ok) << any.error.message;
    EXPECT_EQ(any.value, 1U);

    auto all_status = asyncx::WaitAllFor(futures, std::chrono::milliseconds(400));
    ASSERT_TRUE(all_status.ok) << all_status.error.message;
    EXPECT_EQ(futures[0].get(), 10);
    EXPECT_EQ(futures[1].get(), 20);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, WaitAllForCanTimeout)
{
    asyncx::ThreadPool pool;

    std::vector<std::future<int>> futures;
    auto f = pool.Submit([]()
                         {
                             std::this_thread::sleep_for(std::chrono::milliseconds(120));
                             return 1; });
    ASSERT_TRUE(f.ok);
    futures.push_back(std::move(f.value));

    const auto timeout_status = asyncx::WaitAllFor(futures, std::chrono::milliseconds(20));
    EXPECT_FALSE(timeout_status.ok);
    EXPECT_EQ(timeout_status.error.kind, asyncx::ErrorKind::Timeout);

    const auto ok_status = asyncx::WaitAllFor(futures, std::chrono::milliseconds(300));
    EXPECT_TRUE(ok_status.ok);
    EXPECT_EQ(futures[0].get(), 1);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, BackpressureRejectPolicyAndMetricsSnapshot)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 1;
    options.backpressure_policy = asyncx::BackpressurePolicy::Reject;

    asyncx::ThreadPool pool(options);
    EXPECT_EQ(pool.GetBackpressurePolicy(), asyncx::BackpressurePolicy::Reject);

    std::promise<void> gate;
    std::promise<void> started;
    std::shared_future<void> hold = gate.get_future().share();
    auto started_future = started.get_future();
    ASSERT_TRUE(pool.Post([&started, hold]()
                          {
                              started.set_value();
                              hold.wait(); })
                    .ok);
    ASSERT_EQ(started_future.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);

    ASSERT_TRUE(pool.Post([]() {}).ok);

    const auto blocked = pool.Post([]() {});
    EXPECT_FALSE(blocked.ok);
    EXPECT_EQ(blocked.error.kind, asyncx::ErrorKind::QueueFull);

    auto snapshot = pool.GetMetricsSnapshot();
    EXPECT_EQ(snapshot.backpressure_policy, asyncx::BackpressurePolicy::Reject);
    EXPECT_GE(snapshot.execution.backpressure_rejected, 1U);
    EXPECT_GE(snapshot.execution.rejected, 1U);

    gate.set_value();
    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(300)).ok);

    const auto reset_stats = pool.ResetStats();
    EXPECT_GE(reset_stats.backpressure_rejected, 1U);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, SchedulerMetricsTracksCreateFireCancel)
{
    asyncx::ThreadPool pool;

    auto delayed = pool.PostDelayedFor(std::chrono::milliseconds(20), []() {});
    ASSERT_TRUE(delayed.ok);

    auto periodic = pool.ScheduleEvery(std::chrono::milliseconds(15), []() {}, true);
    ASSERT_TRUE(periodic.ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    EXPECT_TRUE(pool.CancelScheduled(periodic.value).ok);

    EXPECT_TRUE(pool.WaitForIdleFor(std::chrono::milliseconds(400)).ok);
    const auto snapshot = pool.GetMetricsSnapshot();
    EXPECT_GE(snapshot.scheduler.created, 2U);
    EXPECT_GE(snapshot.scheduler.fired, 2U);
    EXPECT_GE(snapshot.scheduler.cancelled, 1U);
    EXPECT_GE(snapshot.scheduler.periodic_rescheduled, 1U);

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
}

TEST(AsyncxTests, StopCancelPendingDropsQueuedTasks)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 8;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();
    std::promise<void> started;
    auto started_future = started.get_future();
    std::atomic<int> ran{0};

    ASSERT_TRUE(pool.Post([hold, &ran, &started]()
                          {
                              ran.fetch_add(1, std::memory_order_relaxed);
                              started.set_value();
                              hold.wait(); })
                    .ok);
    ASSERT_EQ(started_future.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);

    ASSERT_TRUE(pool.Post([&ran]()
                          { ran.fetch_add(1, std::memory_order_relaxed); })
                    .ok);
    ASSERT_TRUE(pool.Post([&ran]()
                          { ran.fetch_add(1, std::memory_order_relaxed); })
                    .ok);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::CancelPending).ok);
    gate.set_value();
    EXPECT_TRUE(pool.Join().ok);

    EXPECT_EQ(ran.load(std::memory_order_relaxed), 1);
    EXPECT_TRUE(pool.IsIdle());

    const auto stats = pool.GetStats();
    EXPECT_GE(stats.rejected, 2U);
}

TEST(AsyncxTests, JoinIsIdempotentAfterStopAndJoin)
{
    asyncx::ThreadPool pool;

    EXPECT_TRUE(pool.StopAndJoin(asyncx::StopMode::Drain).ok);
    EXPECT_TRUE(pool.Join().ok);

    const auto status = pool.Post([]() {});
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, asyncx::ErrorKind::NotRunning);
}

TEST(AsyncxTests, StopDrainRunsQueuedTasksBeforeExit)
{
    asyncx::PoolOptions options;
    options.worker_count = 1;
    options.queue_capacity = 8;

    asyncx::ThreadPool pool(options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();
    std::atomic<int> ran{0};

    ASSERT_TRUE(pool.Post([hold, &ran]()
                          {
                              ran.fetch_add(1, std::memory_order_relaxed);
                              hold.wait(); })
                    .ok);
    ASSERT_TRUE(pool.Post([&ran]()
                          { ran.fetch_add(1, std::memory_order_relaxed); })
                    .ok);
    ASSERT_TRUE(pool.Post([&ran]()
                          { ran.fetch_add(1, std::memory_order_relaxed); })
                    .ok);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::Drain).ok);
    gate.set_value();
    EXPECT_TRUE(pool.Join().ok);

    EXPECT_EQ(ran.load(std::memory_order_relaxed), 3);
}

TEST(AsyncxTests, StopCancelPendingClearsScheduledTasks)
{
    asyncx::ThreadPool pool;

    std::atomic<int> fired{0};
    auto delayed = pool.PostDelayedFor(std::chrono::milliseconds(500), [&fired]()
                                       { fired.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(delayed.ok);

    auto periodic = pool.ScheduleEvery(std::chrono::milliseconds(500), [&fired]()
                                       { fired.fetch_add(1, std::memory_order_relaxed); }, false);
    ASSERT_TRUE(periodic.ok);
    EXPECT_EQ(pool.ScheduledCount(), 2U);

    EXPECT_TRUE(pool.Stop(asyncx::StopMode::CancelPending).ok);
    EXPECT_TRUE(pool.Join().ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(fired.load(std::memory_order_relaxed), 0);

    const auto snapshot = pool.GetMetricsSnapshot();
    EXPECT_EQ(snapshot.scheduled_count, 0U);
    EXPECT_GE(snapshot.scheduler.cancelled, 2U);
}
