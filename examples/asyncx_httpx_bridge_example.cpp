#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "asyncx.h"
#include "httpx.h"

int main()
{
    httpx::ClientOptions client_options;
    client_options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;

        std::chrono::milliseconds simulated{40};
        if (request.url.find("slow") != std::string::npos)
        {
            simulated = std::chrono::milliseconds(140);
        }
        else if (request.url.find("fast") != std::string::npos)
        {
            simulated = std::chrono::milliseconds(20);
        }
        std::this_thread::sleep_for(simulated);

        out.ok = true;
        out.value.status_code = 200;
        out.value.reason = "OK";
        out.value.body = std::string("{\"url\":\"") + request.url + "\"}";
        return out;
    };

    httpx::Client client(client_options);

    asyncx::PoolOptions options;
    options.worker_count = 3;
    options.queue_capacity = 16;

    asyncx::ThreadPool pool(options);

    const std::vector<std::string> urls = {
        "https://demo.local/fast/a",
        "https://demo.local/slow/b",
        "https://demo.local/normal/c"};

    std::vector<std::future<httpx::Result<httpx::Response>>> futures;
    futures.reserve(urls.size());

    for (const auto &url : urls)
    {
        auto submitted = pool.Submit([&client, url]()
                                     {
                                         httpx::Request req;
                                         req.method = httpx::HttpMethod::Get;
                                         req.url = url;
                                         return client.Send(req); });
        if (!submitted.ok)
        {
            std::cerr << "submit failed: " << submitted.error.message << '\n';
            return 2;
        }
        futures.push_back(std::move(submitted.value));
    }

    auto first = asyncx::WaitAnyFor(futures, std::chrono::milliseconds(500));
    if (!first.ok)
    {
        std::cerr << "wait any failed: " << first.error.message << '\n';
        return 2;
    }
    std::cout << "first response index=" << first.value << '\n';

    auto all_status = asyncx::WaitAllFor(futures, std::chrono::seconds(2));
    if (!all_status.ok)
    {
        std::cerr << "wait all failed: " << all_status.error.message << '\n';
        return 2;
    }

    std::size_t ok_count = 0;
    for (auto &future : futures)
    {
        const auto result = future.get();
        if (result.ok)
        {
            ++ok_count;
            std::cout << "http ok status=" << result.value.status_code
                      << " body=" << result.value.body << '\n';
        }
        else
        {
            std::cerr << "http failed: " << result.error.message << '\n';
        }
    }

    const auto stats = client.GetFailureStats();
    auto metrics = pool.GetMetricsSnapshot();
    std::cout << "httpx bridge ok=" << ok_count
              << " failures=" << stats.total_failures
              << " submitted=" << metrics.execution.submitted
              << " completed=" << metrics.execution.completed
              << '\n';

    pool.StopAndJoin(asyncx::StopMode::Drain);
    return (ok_count == urls.size()) ? 0 : 2;
}
