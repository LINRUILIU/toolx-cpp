#include "httpx.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

int main()
{
    // Scenario 1: a custom transport makes client code testable without network access.
    httpx::ClientOptions options;
    options.transport = [](const httpx::Request& request, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.reason = "OK";
        out.value.body = "method=" + std::string(httpx::ToString(request.method));
        return out;
    };
    httpx::Client client(options);
    std::cout << "get-body=" << client.Get("http://example.test").value.body << "\n";

    // Scenario 2: retry policy retries transient errors before surfacing failure.
    int attempts = 0;
    options.retry_policy.max_attempts = 2;
    options.retry_policy.delay_ms = 0;
    options.transport = [&attempts](const httpx::Request&,
                                    const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        ++attempts;
        if (attempts == 1)
        {
            return {false, {}, {httpx::ErrorKind::Network, 0, true, "temporary"}};
        }
        return {true, {200, "OK", {}, "retry-ok"}, {}};
    };
    httpx::Client retry_client(options);
    std::cout << "retry=" << retry_client.Get("http://example.test").value.body << " attempts=" << attempts << "\n";

    // Scenario 3: circuit breaker fails fast after repeated transport failures.
    options.circuit_breaker.enabled = true;
    options.circuit_breaker.failure_threshold = 1;
    options.transport = [](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    { return {false, {}, {httpx::ErrorKind::Network, 0, true, "down"}}; };
    httpx::Client circuit_client(options);
    (void)circuit_client.Get("http://example.test");
    std::cout << "circuit-open=" << circuit_client.CircuitSnapshot().open << "\n";

    // Scenario 4: downloads write through a temp file and then replace the target.
    const auto root = std::filesystem::current_path() / "temp" / "toolx_httpx_cookbook";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    options.circuit_breaker.enabled = false;
    options.transport = [](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    { return {true, {200, "OK", {}, "download-body"}, {}}; };
    httpx::Client download_client(options);
    download_client.DownloadFile("http://example.test/file", (root / "file.txt").string());

    // Scenario 5: UploadFile creates a multipart request from a file path.
    WriteText(root / "upload.txt", "payload");
    options.transport = [](const httpx::Request& request, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    { return {true, {200, "OK", {}, std::to_string(request.multipart.size())}, {}}; };
    httpx::Client upload_client(options);
    std::cout
        << "upload-parts="
        << upload_client.UploadFile("http://example.test/upload", "file", (root / "upload.txt").string()).value.body
        << "\n";
    std::filesystem::remove_all(root, ec);
    return 0;
}
