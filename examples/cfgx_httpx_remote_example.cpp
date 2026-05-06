#include <iostream>

#include "cfgx.h"
#include "httpx.h"

int main()
{
    httpx::ClientOptions options;
    options.transport = [](const httpx::Request &request, const httpx::ClientOptions &) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.body = std::string("{\"remote_url\":\"") + request.url + "\",\"svc\":{\"port\":8443}}";
        return out;
    };

    httpx::Client client(options);
    cfgx::SetRemoteFetcher([&client](const cfgx::RemoteFetchRequest &request)
                           {
                               cfgx::Result<cfgx::RemoteFetchResponse> out;
                               const auto response = client.Get(request.url, request.headers);
                               if (!response.ok)
                               {
                                   out.ok = false;
                                   out.error = response.error.message;
                                   return out;
                               }

                               out.ok = true;
                               out.value.body = response.value.body;
                               out.value.headers = response.value.headers;
                               out.value.status_code = response.value.status_code;
                               return out;
                           });

    const auto loaded = cfgx::LoadFromRemote("https://config.toolx.local/runtime.json");
    if (!loaded.ok)
    {
        std::cerr << "load failed: " << loaded.error << '\n';
        return 2;
    }

    const auto url = cfgx::GetNode(loaded.value, "remote_url");
    const auto port = cfgx::GetNode(loaded.value, "svc.port");
    if (!url.ok || !port.ok)
    {
        std::cerr << "missing expected nodes\n";
        return 2;
    }

    std::cout << "remote_url=" << url.value->AsString() << '\n';
    std::cout << "svc.port=" << port.value->AsInt(-1) << '\n';
    return 0;
}
