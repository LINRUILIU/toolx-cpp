#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "httpx.h"

namespace
{

#if defined(_WIN32)
    using BenchSocket = SOCKET;
    constexpr BenchSocket kInvalidBenchSocket = INVALID_SOCKET;
#else
    using BenchSocket = int;
    constexpr BenchSocket kInvalidBenchSocket = -1;
#endif

    void CloseBenchSocket(BenchSocket socket)
    {
        if (socket == kInvalidBenchSocket)
        {
            return;
        }

#if defined(_WIN32)
        closesocket(socket);
#else
        close(socket);
#endif
    }

    bool EnsureBenchNetworkReady()
    {
#if defined(_WIN32)
        static std::once_flag once;
        static bool ok = false;
        std::call_once(once, []()
                       {
                       WSADATA data{};
                       ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; });
        return ok;
#else
        return true;
#endif
    }

    std::uint64_t ParseU64Or(std::string_view text, std::uint64_t fallback)
    {
        try
        {
            return static_cast<std::uint64_t>(std::stoull(std::string(text)));
        }
        catch (...)
        {
            return fallback;
        }
    }

    double Percentile(std::vector<double> values, double p)
    {
        if (values.empty())
        {
            return 0.0;
        }

        std::sort(values.begin(), values.end());
        const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
        return values[idx];
    }

    std::string ReceiveRequestHeaders(BenchSocket client)
    {
        std::string request;
        request.reserve(1024);

        for (;;)
        {
            char buf[512] = {0};
#if defined(_WIN32)
            const int n = ::recv(client, buf, static_cast<int>(sizeof(buf)), 0);
#else
            const int n = static_cast<int>(::recv(client, buf, sizeof(buf), 0));
#endif
            if (n <= 0)
            {
                break;
            }

            request.append(buf, static_cast<std::size_t>(n));
            if (request.find("\r\n\r\n") != std::string::npos)
            {
                break;
            }
        }

        return request;
    }

    bool SendAll(BenchSocket client, const std::string &payload)
    {
        std::size_t sent = 0;
        while (sent < payload.size())
        {
#if defined(_WIN32)
            const int n = ::send(client,
                                 payload.data() + sent,
                                 static_cast<int>(payload.size() - sent),
                                 0);
#else
            const int n = static_cast<int>(::send(client,
                                                  payload.data() + sent,
                                                  payload.size() - sent,
                                                  0));
#endif
            if (n <= 0)
            {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    class LoopbackHttpBenchServer
    {
    public:
        bool Start()
        {
            if (!EnsureBenchNetworkReady())
            {
                return false;
            }

            listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listen_socket_ == kInvalidBenchSocket)
            {
                return false;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;

            if (::bind(listen_socket_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
            {
                CloseBenchSocket(listen_socket_);
                listen_socket_ = kInvalidBenchSocket;
                return false;
            }

            if (::listen(listen_socket_, 256) != 0)
            {
                CloseBenchSocket(listen_socket_);
                listen_socket_ = kInvalidBenchSocket;
                return false;
            }

            sockaddr_in bound{};
            socklen_t bound_len = static_cast<socklen_t>(sizeof(bound));
            if (::getsockname(listen_socket_, reinterpret_cast<sockaddr *>(&bound), &bound_len) != 0)
            {
                CloseBenchSocket(listen_socket_);
                listen_socket_ = kInvalidBenchSocket;
                return false;
            }

            port_ = ntohs(bound.sin_port);
            worker_ = std::thread([this]()
                                  { WorkerLoop(); });
            return true;
        }

        void Stop()
        {
            stop_.store(true, std::memory_order_relaxed);
            WakeAccept();

            if (worker_.joinable())
            {
                worker_.join();
            }

            CloseBenchSocket(listen_socket_);
            listen_socket_ = kInvalidBenchSocket;
        }

        ~LoopbackHttpBenchServer()
        {
            Stop();
        }

        std::uint16_t Port() const
        {
            return port_;
        }

        std::uint64_t Handled() const
        {
            return handled_.load(std::memory_order_relaxed);
        }

    private:
        void WakeAccept()
        {
            if (listen_socket_ == kInvalidBenchSocket || port_ == 0)
            {
                return;
            }

            BenchSocket wake = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (wake == kInvalidBenchSocket)
            {
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port_);
            (void)::connect(wake, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
            CloseBenchSocket(wake);
        }

        void WorkerLoop()
        {
            const std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "ok";

            for (;;)
            {
                sockaddr_in client_addr{};
                socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
                BenchSocket client = ::accept(listen_socket_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
                if (client == kInvalidBenchSocket)
                {
                    if (stop_.load(std::memory_order_relaxed))
                    {
                        break;
                    }
                    continue;
                }

                if (stop_.load(std::memory_order_relaxed))
                {
                    CloseBenchSocket(client);
                    break;
                }

                (void)ReceiveRequestHeaders(client);
                (void)SendAll(client, response);
                handled_.fetch_add(1, std::memory_order_relaxed);
                CloseBenchSocket(client);
            }
        }

        BenchSocket listen_socket_{kInvalidBenchSocket};
        std::thread worker_{};
        std::atomic<bool> stop_{false};
        std::atomic<std::uint64_t> handled_{0};
        std::uint16_t port_{0};
    };

    struct WorkerStats
    {
        std::uint64_t ok{0};
        std::uint64_t fail{0};
        std::string first_error;
        std::vector<double> latencies_ms;
    };

} // namespace

int main(int argc, char **argv)
{
    const std::uint64_t total_requests = (argc > 1) ? ParseU64Or(argv[1], 10000) : 10000;
    std::size_t concurrency = (argc > 2) ? static_cast<std::size_t>(ParseU64Or(argv[2], 8)) : 8u;
    if (concurrency == 0)
    {
        concurrency = 1;
    }

    LoopbackHttpBenchServer server;
    if (!server.Start())
    {
        std::cerr << "failed to start loopback benchmark server\n";
        return 2;
    }

    const std::string url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/bench";

    std::cout << "httpx_benchmark start"
              << " total_requests=" << total_requests
              << " concurrency=" << concurrency
              << " url=" << url
              << "\n";

    std::vector<WorkerStats> stats(concurrency);
    std::vector<std::thread> workers;
    workers.reserve(concurrency);

    const std::uint64_t base = total_requests / static_cast<std::uint64_t>(concurrency);
    const std::uint64_t remain = total_requests % static_cast<std::uint64_t>(concurrency);

    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < concurrency; ++i)
    {
        const std::uint64_t count = base + ((i < remain) ? 1u : 0u);
        workers.emplace_back([&, i, count]()
                             {
                                 WorkerStats &st = stats[i];
                                 st.latencies_ms.reserve(static_cast<std::size_t>(count));

                                 httpx::ClientOptions options;
                                 options.use_proxy_from_environment = false;
                                 options.timeout.connect_ms = 2000;
                                 options.timeout.read_write_ms = 3000;
                                 options.timeout.total_ms = 5000;

                                 httpx::Client client(options);
                                 for (std::uint64_t n = 0; n < count; ++n)
                                 {
                                     const auto t0 = std::chrono::steady_clock::now();
                                     const auto result = client.Get(url);
                                     const auto t1 = std::chrono::steady_clock::now();

                                     const double ms = static_cast<double>(
                                         std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) /
                                                       1000.0;
                                     st.latencies_ms.push_back(ms);

                                     if (result.ok && result.value.status_code == 200 && result.value.body == "ok")
                                     {
                                         ++st.ok;
                                     }
                                     else
                                     {
                                         ++st.fail;
                                         if (st.first_error.empty())
                                         {
                                             st.first_error = result.ok ?
                                                                 ("unexpected status: " + std::to_string(result.value.status_code)) :
                                                                 result.error.message;
                                         }
                                     }
                                 } });
    }

    for (auto &w : workers)
    {
        if (w.joinable())
        {
            w.join();
        }
    }
    const auto end = std::chrono::steady_clock::now();

    server.Stop();

    std::uint64_t ok = 0;
    std::uint64_t fail = 0;
    std::vector<double> all_latencies;
    all_latencies.reserve(static_cast<std::size_t>(total_requests));

    std::string first_error;
    for (const auto &st : stats)
    {
        ok += st.ok;
        fail += st.fail;
        all_latencies.insert(all_latencies.end(), st.latencies_ms.begin(), st.latencies_ms.end());

        if (first_error.empty() && !st.first_error.empty())
        {
            first_error = st.first_error;
        }
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
    const double elapsed_s = std::max(0.001, static_cast<double>(elapsed_ms) / 1000.0);
    const double qps = static_cast<double>(ok + fail) / elapsed_s;

    std::cout << "httpx_benchmark done"
              << " total=" << (ok + fail)
              << " ok=" << ok
              << " fail=" << fail
              << " handled_by_server=" << server.Handled()
              << " elapsed_ms=" << elapsed_ms
              << " qps=" << static_cast<std::uint64_t>(qps)
              << " p50_ms=" << Percentile(all_latencies, 0.50)
              << " p95_ms=" << Percentile(all_latencies, 0.95)
              << " p99_ms=" << Percentile(all_latencies, 0.99)
              << "\n";

    if (!first_error.empty())
    {
        std::cout << "first_error=" << first_error << "\n";
    }

    return (fail == 0) ? 0 : 1;
}
