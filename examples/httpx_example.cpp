#include <iostream>

#include "httpx.h"

int main()
{
    httpx::ClientOptions options;
    options.logger = [](const httpx::LogEvent &event)
    {
        std::cout << "[" << httpx::ToString(event.severity) << "]"
                  << " method=" << event.method
                  << " url=" << event.url
                  << " status=" << event.status_code
                  << " kind=" << httpx::ToString(event.error_kind)
                  << " duration_ms=" << event.duration_ms
                  << " message=" << event.message
                  << "\n";
    };

    options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.reason = "OK";
        out.value.headers.push_back({"Content-Type", "application/json"});
        out.value.body = std::string("{\"echo_method\":\"") + httpx::ToString(request.method) + "\"}";
        return out;
    };

    httpx::Client client(options);

    httpx::Request req;
    req.method = httpx::HttpMethod::Get;
    req.url = "https://example.com/api?token=secret&name=demo";

    const auto res = client.Send(req);
    if (!res.ok)
    {
        std::cerr << "request failed: " << res.error.message << "\n";
        return 1;
    }

    std::cout << "status=" << res.value.status_code << " body=" << res.value.body << "\n";

    const auto stats = client.GetFailureStats();
    std::cout << "total_requests=" << stats.total_requests
              << " total_failures=" << stats.total_failures
              << " consecutive_failures=" << stats.consecutive_failures
              << "\n";

    return 0;
}
