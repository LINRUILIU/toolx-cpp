#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "asyncx.h"
#include "fsx.h"

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = "temp/asyncx_fsx_bridge";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    asyncx::PoolOptions options;
    options.worker_count = 3;
    options.queue_capacity = 16;

    asyncx::ThreadPool pool(options);

    std::vector<std::future<fsx::RunResult>> futures;
    futures.reserve(3);

    for (int i = 0; i < 3; ++i)
    {
        fsx::BatchPlan plan;
        const auto from = (root / ("job" + std::to_string(i) + "_from.txt")).string();
        const auto to = (root / ("job" + std::to_string(i) + "_to.txt")).string();
        plan.AddAtomicWrite(from, "payload-" + std::to_string(i) + "\n")
            .AddRename(from, to);

        fsx::RunOptions run_options;
        run_options.rollback_mode = fsx::RollbackMode::BestEffort;
        run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
        run_options.journal_path = (root / ("job" + std::to_string(i) + ".journal")).string();

        auto submitted = pool.Submit([plan, run_options]()
                                     { return fsx::Run(plan, run_options); });
        if (!submitted.ok)
        {
            std::cerr << "submit failed: " << submitted.error.message << '\n';
            return 2;
        }
        futures.push_back(std::move(submitted.value));
    }

    auto first = asyncx::WaitAnyFor(futures, std::chrono::milliseconds(800));
    if (first.ok)
    {
        std::cout << "first fsx job ready index=" << first.value << '\n';
    }

    const auto all_status = asyncx::WaitAllFor(futures, std::chrono::seconds(2));
    if (!all_status.ok)
    {
        std::cerr << "wait all failed: " << all_status.error.message << '\n';
        return 2;
    }

    int ok_count = 0;
    for (auto &future : futures)
    {
        const auto result = future.get();
        if (result.ok)
        {
            ++ok_count;
        }
        else
        {
            std::cerr << "fsx run failed: " << result.error << '\n';
        }
    }

    auto metrics = pool.GetMetricsSnapshot();
    std::cout << "fsx bridge ok=" << ok_count
              << " submitted=" << metrics.execution.submitted
              << " completed=" << metrics.execution.completed
              << '\n';

    pool.StopAndJoin(asyncx::StopMode::Drain);
    return (ok_count == 3) ? 0 : 2;
}
