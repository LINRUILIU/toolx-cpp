#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "asyncx.h"

namespace
{

    std::uint64_t ParseU64OrDefault(const char *text, std::uint64_t fallback)
    {
        if (text == nullptr)
        {
            return fallback;
        }

        try
        {
            return static_cast<std::uint64_t>(std::stoull(text));
        }
        catch (...)
        {
            return fallback;
        }
    }

    void RunCase(const std::string &name,
                 asyncx::ThreadPool &pool,
                 std::uint64_t task_count,
                 asyncx::TaskPriority priority,
                 std::uint64_t work)
    {
        std::atomic<std::uint64_t> sink{0};

        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0; i < task_count; ++i)
        {
            const auto st = pool.PostWithPriority(priority,
                                                  [&sink, i, work]()
                                                  {
                                                      std::uint64_t local = i;
                                                      for (std::uint64_t k = 0; k < work; ++k)
                                                      {
                                                          local = (local * 1664525u) + 1013904223u;
                                                      }
                                                      sink.fetch_add(local, std::memory_order_relaxed);
                                                  });
            if (!st.ok)
            {
                std::cerr << "enqueue failed in " << name << ": " << st.error.message << "\n";
                return;
            }
        }

        const auto wait = pool.WaitForIdleFor(std::chrono::seconds(30));
        if (!wait.ok)
        {
            std::cerr << "wait failed in " << name << ": " << wait.error.message << "\n";
            return;
        }

        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        const double seconds = std::max<double>(0.001, static_cast<double>(elapsed_ms) / 1000.0);
        const double throughput = static_cast<double>(task_count) / seconds;

        std::cout << "[" << name << "] tasks=" << task_count
                  << " elapsed_ms=" << elapsed_ms
                  << " throughput=" << static_cast<std::uint64_t>(throughput)
                  << " task/s"
                  << " sink=" << sink.load(std::memory_order_relaxed)
                  << "\n";
    }

} // namespace

int main(int argc, char **argv)
{
    const std::uint64_t task_count = (argc > 1) ? ParseU64OrDefault(argv[1], 120000) : 120000;
    const std::uint64_t work = (argc > 2) ? ParseU64OrDefault(argv[2], 64) : 64;

    asyncx::PoolOptions options;
    options.worker_count = std::max<std::size_t>(2u, std::thread::hardware_concurrency());
    options.queue_capacity = 4096;
    options.backpressure_policy = asyncx::BackpressurePolicy::Block;

    asyncx::ThreadPool pool(options);

    std::cout << "asyncx_benchmark: workers=" << options.worker_count
              << " tasks=" << task_count
              << " work=" << work
              << "\n";

    RunCase("high", pool, task_count, asyncx::TaskPriority::High, work);
    RunCase("normal", pool, task_count, asyncx::TaskPriority::Normal, work);
    RunCase("low", pool, task_count, asyncx::TaskPriority::Low, work);

    if (!pool.StopAndJoin(asyncx::StopMode::Drain).ok)
    {
        return 2;
    }

    return 0;
}
