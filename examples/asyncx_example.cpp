#include <chrono>
#include <iostream>
#include <atomic>
#include <thread>
#include <vector>

#include "asyncx.h"

int main()
{
    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 16;

    asyncx::ThreadPool pool(options);

    std::vector<std::future<int>> futures;
    futures.reserve(8);

    for (int i = 1; i <= 8; ++i)
    {
        auto submitted = pool.Submit([i]()
                                     { return i * i; });
        if (!submitted.ok)
        {
            std::cerr << "submit failed: " << submitted.error.message << '\n';
            return 2;
        }
        futures.push_back(std::move(submitted.value));
    }

    auto first_ready = asyncx::WaitAnyFor(futures, std::chrono::milliseconds(300));
    if (!first_ready.ok)
    {
        std::cerr << "wait any failed: " << first_ready.error.message << '\n';
        return 2;
    }

    auto all_ready = asyncx::WaitAllFor(futures, std::chrono::milliseconds(500));
    if (!all_ready.ok)
    {
        std::cerr << "wait all failed: " << all_ready.error.message << '\n';
        return 2;
    }

    int total = 0;
    for (auto &future : futures)
    {
        total += future.get();
    }

    std::cout << "sum_of_squares=" << total << '\n';

    asyncx::PoolOptions small_options;
    small_options.worker_count = 1;
    small_options.queue_capacity = 1;
    asyncx::ThreadPool small_pool(small_options);

    std::promise<void> gate;
    std::shared_future<void> hold = gate.get_future().share();
    (void)small_pool.Post([hold]()
                          { hold.wait(); });
    (void)small_pool.Post([]() {});

    auto immediate = small_pool.TryPost([]() {});
    if (!immediate.ok)
    {
        std::cout << "TryPost rejected kind=" << asyncx::ToString(immediate.error.kind)
                  << " msg=" << immediate.error.message << '\n';
    }

    gate.set_value();
    auto idle = small_pool.WaitForIdleFor(std::chrono::milliseconds(300));
    if (!idle.ok)
    {
        std::cerr << "wait idle failed: " << idle.error.message << '\n';
        return 2;
    }

    small_pool.Stop(asyncx::StopMode::Drain);
    small_pool.Join();

    auto status = pool.PostFor(std::chrono::milliseconds(50), []()
                               { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    if (!status.ok)
    {
        std::cerr << "post failed: " << status.error.message << '\n';
    }

    std::atomic<int> periodic_hits{0};
    auto periodic = pool.ScheduleEvery(std::chrono::milliseconds(30), [&periodic_hits]()
                                       { periodic_hits.fetch_add(1, std::memory_order_relaxed); }, true);
    if (!periodic.ok)
    {
        std::cerr << "schedule periodic failed: " << periodic.error.message << '\n';
        return 2;
    }

    auto delayed = pool.PostDelayedFor(std::chrono::milliseconds(80), []()
                                       { std::cout << "delayed task fired" << '\n'; });
    if (!delayed.ok)
    {
        std::cerr << "post delayed failed: " << delayed.error.message << '\n';
        return 2;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    (void)pool.CancelScheduled(periodic.value);
    pool.WaitForIdleFor(std::chrono::milliseconds(300));
    std::cout << "periodic_hits=" << periodic_hits.load(std::memory_order_relaxed) << '\n';

    pool.WaitForIdle();
    const auto snapshot = pool.GetMetricsSnapshot();
    std::cout << "metrics scheduler(created/fired/cancelled)="
              << snapshot.scheduler.created << '/'
              << snapshot.scheduler.fired << '/'
              << snapshot.scheduler.cancelled << '\n';

    const auto stats = pool.ResetStats();
    std::cout << "stats_before_reset submitted=" << stats.submitted
              << " completed=" << stats.completed
              << " backpressure_rejected=" << stats.backpressure_rejected << '\n';

    pool.StopAndJoin(asyncx::StopMode::Drain);
    return 0;
}
