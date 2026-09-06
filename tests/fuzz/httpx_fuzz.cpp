#include "../../src/detail/multipart_boundary.h"
#include "httpx.h"
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0 || size > 4096)
        return 0;
    const std::string text(reinterpret_cast<const char*>(data + 1), size - 1);
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.transport = [](const httpx::Request&, const httpx::ClientOptions&)
    {
        httpx::Result<httpx::Response> result;
        result.ok = true;
        result.value.status_code = 200;
        return result;
    };
    httpx::Request request;
    request.url = "http://localhost/";
    switch (data[0] % 6)
    {
    case 0:
        request.url += text;
        break;
    case 1:
        request.headers = {{text, "value"}};
        break;
    case 2:
        request.headers = {{"Content-Length", text}};
        break;
    case 3:
        request.headers = {{"X-Input", text}};
        break;
    case 4:
        request.multipart = {{text, text, text, "data"}};
        break;
    default:
    {
        request.multipart = {{"file", "input.bin", "application/octet-stream", text}};
        std::string boundary;
        // Exercise the production collision scan without requiring a network transport.
        int attempt = 0;
        (void)toolx_detail::SelectMultipartBoundary(
            request.multipart, [&]() { return (data[0] & 1u) ? text : "httpx-boundary-" + std::to_string(++attempt); },
            &boundary);
        break;
    }
    }
    (void)httpx::Client(options).Send(request);
    return 0;
}
