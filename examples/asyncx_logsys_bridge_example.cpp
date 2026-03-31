#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <vector>

#include "asyncx.h"
#include "logsys.h"

int main()
{
    namespace fs = std::filesystem;
    fs::create_directories("temp/asyncx_bridge");

    logsys::DefaultLoggerOptions log_options;
    log_options.level = logsys::LogLevel::Info;
    log_options.record_level = logsys::LogLevel::Debug;
    log_options.enable_console = true;
    log_options.enable_file = true;
    log_options.file_path = "temp/asyncx_bridge/async_logsys.log";

    auto &logger = logsys::Logger::Instance();
    logger.ConfigureDefaultLogger(log_options);
    logger.SetDefaultOrigin(logsys::ErrorSource::Business,
                            logsys::ModuleId::BusinessCommon,
                            logsys::ErrorCategory::Business);

    asyncx::PoolOptions options;
    options.worker_count = 2;
    options.queue_capacity = 64;
    options.backpressure_policy = asyncx::BackpressurePolicy::Block;

    asyncx::ThreadPool pool(options);

    std::vector<std::future<int>> futures;
    futures.reserve(24);

    for (int i = 0; i < 24; ++i)
    {
        auto submitted = pool.Submit([i]()
                                     {
                                         LOGI("asyncx-logsys task=%d", i);
                                         return i; });
        if (!submitted.ok)
        {
            std::cerr << "submit failed: " << submitted.error.message << '\n';
            return 2;
        }
        futures.push_back(std::move(submitted.value));
    }

    const auto wait_status = asyncx::WaitAllFor(futures, std::chrono::seconds(2));
    if (!wait_status.ok)
    {
        std::cerr << "wait all failed: " << wait_status.error.message << '\n';
        return 2;
    }

    int sum = 0;
    for (auto &future : futures)
    {
        sum += future.get();
    }

    auto metrics = pool.GetMetricsSnapshot();
    std::cout << "logsys bridge sum=" << sum
              << " submitted=" << metrics.execution.submitted
              << " completed=" << metrics.execution.completed
              << '\n';

    pool.StopAndJoin(asyncx::StopMode::Drain);
    logger.Flush();
    return 0;
}
