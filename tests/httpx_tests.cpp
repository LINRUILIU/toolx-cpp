#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>
#include <optional>
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
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(HTTPX_ENABLE_OPENSSL)
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#include "httpx.h"

namespace
{

namespace fs = std::filesystem;

#if defined(_WIN32)
using TestSocket = SOCKET;
constexpr TestSocket kInvalidTestSocket = INVALID_SOCKET;
#else
using TestSocket = int;
constexpr TestSocket kInvalidTestSocket = -1;
#endif

void CloseTestSocket(TestSocket socket)
{
    if (socket == kInvalidTestSocket)
    {
        return;
    }

#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool EnsureTestNetworkReady()
{
#if defined(_WIN32)
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once,
                   []
                   {
                       WSADATA data{};
                       ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
                   });
    return ok;
#else
    return true;
#endif
}

struct ScopedEnvVar
{
    std::string key;
    std::optional<std::string> original;

    ScopedEnvVar(std::string name, std::string value) : key(std::move(name))
    {
        if (const char* current = std::getenv(key.c_str()))
        {
            original = std::string(current);
        }

        Set(value);
    }

    ~ScopedEnvVar()
    {
        if (original.has_value())
        {
            Set(*original);
            return;
        }
        Clear();
    }

    void Set(const std::string& value)
    {
#if defined(_WIN32)
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }

    void Clear()
    {
#if defined(_WIN32)
        _putenv_s(key.c_str(), "");
#else
        unsetenv(key.c_str());
#endif
    }
};

struct LocalHttpServer
{
    std::uint16_t port{0};
    std::thread worker;
    std::shared_ptr<std::string> captured_request;

    LocalHttpServer() = default;
    LocalHttpServer(const LocalHttpServer&) = delete;
    LocalHttpServer& operator=(const LocalHttpServer&) = delete;
    LocalHttpServer(LocalHttpServer&&) noexcept = default;
    LocalHttpServer& operator=(LocalHttpServer&&) noexcept = default;

    ~LocalHttpServer()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
};

std::optional<LocalHttpServer> StartSingleResponseServer(std::string response, std::uint64_t response_delay_ms = 0,
                                                         std::shared_ptr<std::string> captured_request = nullptr)
{
    if (!EnsureTestNetworkReady())
    {
        return std::nullopt;
    }

    TestSocket listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == kInvalidTestSocket)
    {
        return std::nullopt;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    if (::listen(listen_socket, 1) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    sockaddr_in bound{};
    socklen_t bound_len = static_cast<socklen_t>(sizeof(bound));
    if (::getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    LocalHttpServer server;
    server.port = ntohs(bound.sin_port);
    server.captured_request = captured_request;
    server.worker = std::thread(
        [listen_socket, response = std::move(response), response_delay_ms, captured_request]()
        {
            sockaddr_in client_addr{};
            socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
            TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client != kInvalidTestSocket)
            {
                std::string req;
                req.reserve(4096);

                std::size_t expected_total = std::string::npos;
                for (;;)
                {
                    char req_buf[1024] = {0};
#if defined(_WIN32)
                    const int n = ::recv(client, req_buf, static_cast<int>(sizeof(req_buf)), 0);
#else
                    const int n = static_cast<int>(::recv(client, req_buf, sizeof(req_buf), 0));
#endif
                    if (n <= 0)
                    {
                        break;
                    }

                    req.append(req_buf, static_cast<std::size_t>(n));

                    if (expected_total == std::string::npos)
                    {
                        const auto header_end = req.find("\r\n\r\n");
                        if (header_end != std::string::npos)
                        {
                            expected_total = header_end + 4;

                            std::size_t cursor = req.find("\r\n");
                            if (cursor != std::string::npos)
                            {
                                cursor += 2;
                                while (cursor < header_end)
                                {
                                    const auto line_end = req.find("\r\n", cursor);
                                    if (line_end == std::string::npos || line_end > header_end)
                                    {
                                        break;
                                    }

                                    const auto line = req.substr(cursor, line_end - cursor);
                                    const auto colon = line.find(':');
                                    if (colon != std::string::npos)
                                    {
                                        std::string key = line.substr(0, colon);
                                        for (char& ch : key)
                                        {
                                            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                                        }
                                        if (key == "content-length")
                                        {
                                            std::string value = line.substr(colon + 1);
                                            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                                            {
                                                value.erase(value.begin());
                                            }
                                            const long parsed = std::strtol(value.c_str(), nullptr, 10);
                                            if (parsed > 0)
                                            {
                                                expected_total += static_cast<std::size_t>(parsed);
                                            }
                                        }
                                    }

                                    cursor = line_end + 2;
                                }
                            }
                        }
                    }

                    if (expected_total != std::string::npos && req.size() >= expected_total)
                    {
                        break;
                    }
                }

                if (captured_request)
                {
                    *captured_request = req;
                }

                if (response_delay_ms > 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms));
                }

                std::size_t sent = 0;
                while (sent < response.size())
                {
#if defined(_WIN32)
                    const int n = ::send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
#else
                    const int n = static_cast<int>(::send(client, response.data() + sent, response.size() - sent, 0));
#endif
                    if (n <= 0)
                    {
                        break;
                    }
                    sent += static_cast<std::size_t>(n);
                }

                CloseTestSocket(client);
            }

            CloseTestSocket(listen_socket);
        });

    return server;
}

std::string ReceiveHttpRequest(TestSocket client)
{
    std::string req;
    req.reserve(4096);

    std::size_t expected_total = std::string::npos;
    for (;;)
    {
        char req_buf[1024] = {0};
#if defined(_WIN32)
        const int n = ::recv(client, req_buf, static_cast<int>(sizeof(req_buf)), 0);
#else
        const int n = static_cast<int>(::recv(client, req_buf, sizeof(req_buf), 0));
#endif
        if (n <= 0)
        {
            break;
        }

        req.append(req_buf, static_cast<std::size_t>(n));

        if (expected_total == std::string::npos)
        {
            const auto header_end = req.find("\r\n\r\n");
            if (header_end != std::string::npos)
            {
                expected_total = header_end + 4;

                std::size_t cursor = req.find("\r\n");
                if (cursor != std::string::npos)
                {
                    cursor += 2;
                    while (cursor < header_end)
                    {
                        const auto line_end = req.find("\r\n", cursor);
                        if (line_end == std::string::npos || line_end > header_end)
                        {
                            break;
                        }

                        const auto line = req.substr(cursor, line_end - cursor);
                        const auto colon = line.find(':');
                        if (colon != std::string::npos)
                        {
                            std::string key = line.substr(0, colon);
                            for (char& ch : key)
                            {
                                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                            }
                            if (key == "content-length")
                            {
                                std::string value = line.substr(colon + 1);
                                while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                                {
                                    value.erase(value.begin());
                                }
                                const long parsed = std::strtol(value.c_str(), nullptr, 10);
                                if (parsed > 0)
                                {
                                    expected_total += static_cast<std::size_t>(parsed);
                                }
                            }
                        }

                        cursor = line_end + 2;
                    }
                }
            }
        }

        if (expected_total != std::string::npos && req.size() >= expected_total)
        {
            break;
        }
    }

    return req;
}

bool SendAllResponse(TestSocket client, const std::string& response)
{
    std::size_t sent = 0;
    while (sent < response.size())
    {
#if defined(_WIN32)
        const int n = ::send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
#else
        const int n = static_cast<int>(::send(client, response.data() + sent, response.size() - sent, 0));
#endif
        if (n <= 0)
        {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void SetRecvTimeoutMs(TestSocket socket, int timeout_ms)
{
#if defined(_WIN32)
    DWORD timeout = static_cast<DWORD>(timeout_ms);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

struct ScriptedServer
{
    std::uint16_t port{0};
    std::thread worker;
    std::shared_ptr<std::vector<std::string>> captured_requests;

    ScriptedServer() = default;
    ScriptedServer(const ScriptedServer&) = delete;
    ScriptedServer& operator=(const ScriptedServer&) = delete;
    ScriptedServer(ScriptedServer&&) noexcept = default;
    ScriptedServer& operator=(ScriptedServer&&) noexcept = default;

    ~ScriptedServer()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
};

std::optional<ScriptedServer> StartScriptedServer(std::vector<std::string> responses,
                                                  std::shared_ptr<std::vector<std::string>> captured_requests)
{
    if (!EnsureTestNetworkReady())
    {
        return std::nullopt;
    }

    TestSocket listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == kInvalidTestSocket)
    {
        return std::nullopt;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    if (::listen(listen_socket, static_cast<int>(responses.size())) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    sockaddr_in bound{};
    socklen_t bound_len = static_cast<socklen_t>(sizeof(bound));
    if (::getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    ScriptedServer server;
    server.port = ntohs(bound.sin_port);
    server.captured_requests = captured_requests;
    server.worker = std::thread(
        [listen_socket, responses = std::move(responses), captured_requests]()
        {
            for (const auto& response : responses)
            {
                sockaddr_in client_addr{};
                socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
                TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
                if (client == kInvalidTestSocket)
                {
                    continue;
                }

                const std::string req = ReceiveHttpRequest(client);
                if (captured_requests)
                {
                    captured_requests->push_back(req);
                }

                (void)SendAllResponse(client, response);
                CloseTestSocket(client);
            }

            CloseTestSocket(listen_socket);
        });

    return std::optional<ScriptedServer>(std::move(server));
}

std::optional<ScriptedServer>
StartKeepAliveTwoRequestServer(std::shared_ptr<std::vector<std::string>> captured_requests,
                               std::shared_ptr<std::atomic<int>> accept_count)
{
    if (!EnsureTestNetworkReady())
    {
        return std::nullopt;
    }

    TestSocket listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == kInvalidTestSocket)
    {
        return std::nullopt;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    if (::listen(listen_socket, 2) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    sockaddr_in bound{};
    socklen_t bound_len = static_cast<socklen_t>(sizeof(bound));
    if (::getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    ScriptedServer server;
    server.port = ntohs(bound.sin_port);
    server.captured_requests = captured_requests;
    server.worker = std::thread(
        [listen_socket, captured_requests, accept_count]()
        {
            sockaddr_in client_addr{};
            socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
            TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client == kInvalidTestSocket)
            {
                CloseTestSocket(listen_socket);
                return;
            }

            if (accept_count)
            {
                ++(*accept_count);
            }

            SetRecvTimeoutMs(client, 2000);

            const std::string req1 = ReceiveHttpRequest(client);
            if (captured_requests)
            {
                captured_requests->push_back(req1);
            }

            (void)SendAllResponse(client, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nOK");

            const std::string req2 = ReceiveHttpRequest(client);
            if (captured_requests)
            {
                captured_requests->push_back(req2);
            }

            (void)SendAllResponse(client, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");

            CloseTestSocket(client);
            CloseTestSocket(listen_socket);
        });

    return std::optional<ScriptedServer>(std::move(server));
}

struct LocalSocks5Proxy
{
    std::uint16_t port{0};
    std::thread worker;
    std::shared_ptr<std::string> captured_request;

    LocalSocks5Proxy() = default;
    LocalSocks5Proxy(const LocalSocks5Proxy&) = delete;
    LocalSocks5Proxy& operator=(const LocalSocks5Proxy&) = delete;
    LocalSocks5Proxy(LocalSocks5Proxy&&) noexcept = default;
    LocalSocks5Proxy& operator=(LocalSocks5Proxy&&) noexcept = default;

    ~LocalSocks5Proxy()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
};

std::optional<LocalSocks5Proxy> StartSocks5NoAuthProxy(std::string response,
                                                       std::shared_ptr<std::string> captured_request)
{
    if (!EnsureTestNetworkReady())
    {
        return std::nullopt;
    }

    TestSocket listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == kInvalidTestSocket)
    {
        return std::nullopt;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    if (::listen(listen_socket, 1) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    sockaddr_in bound{};
    socklen_t bound_len = static_cast<socklen_t>(sizeof(bound));
    if (::getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    LocalSocks5Proxy server;
    server.port = ntohs(bound.sin_port);
    server.captured_request = captured_request;
    server.worker = std::thread(
        [listen_socket, response = std::move(response), captured_request]()
        {
            auto recv_exact = [](TestSocket socket, std::size_t bytes, std::string* out) -> bool
            {
                if (out == nullptr)
                {
                    return false;
                }

                out->clear();
                out->reserve(bytes);
                while (out->size() < bytes)
                {
                    char buf[128] = {0};
                    const std::size_t want = std::min<std::size_t>(bytes - out->size(), sizeof(buf));
#if defined(_WIN32)
                    const int n = ::recv(socket, buf, static_cast<int>(want), 0);
#else
                    const int n = static_cast<int>(::recv(socket, buf, want, 0));
#endif
                    if (n <= 0)
                    {
                        return false;
                    }
                    out->append(buf, static_cast<std::size_t>(n));
                }
                return true;
            };

            auto send_exact = [](TestSocket socket, const char* data, std::size_t bytes) -> bool
            {
                std::size_t sent = 0;
                while (sent < bytes)
                {
#if defined(_WIN32)
                    const int n = ::send(socket, data + sent, static_cast<int>(bytes - sent), 0);
#else
                    const int n = static_cast<int>(::send(socket, data + sent, bytes - sent, 0));
#endif
                    if (n <= 0)
                    {
                        return false;
                    }
                    sent += static_cast<std::size_t>(n);
                }
                return true;
            };

            sockaddr_in client_addr{};
            socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
            TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client == kInvalidTestSocket)
            {
                CloseTestSocket(listen_socket);
                return;
            }

            SetRecvTimeoutMs(client, 3000);

            std::string greeting;
            if (!recv_exact(client, 2, &greeting))
            {
                CloseTestSocket(client);
                CloseTestSocket(listen_socket);
                return;
            }

            const std::size_t method_count = static_cast<unsigned char>(greeting[1]);
            std::string methods;
            if (!recv_exact(client, method_count, &methods))
            {
                CloseTestSocket(client);
                CloseTestSocket(listen_socket);
                return;
            }

            const char method_reply[2] = {static_cast<char>(0x05), static_cast<char>(0x00)};
            if (!send_exact(client, method_reply, sizeof(method_reply)))
            {
                CloseTestSocket(client);
                CloseTestSocket(listen_socket);
                return;
            }

            std::string connect_head;
            if (!recv_exact(client, 4, &connect_head))
            {
                CloseTestSocket(client);
                CloseTestSocket(listen_socket);
                return;
            }

            const unsigned char atyp = static_cast<unsigned char>(connect_head[3]);
            if (atyp == 0x01)
            {
                std::string discard;
                if (!recv_exact(client, 6, &discard))
                {
                    CloseTestSocket(client);
                    CloseTestSocket(listen_socket);
                    return;
                }
            }
            else if (atyp == 0x04)
            {
                std::string discard;
                if (!recv_exact(client, 18, &discard))
                {
                    CloseTestSocket(client);
                    CloseTestSocket(listen_socket);
                    return;
                }
            }
            else if (atyp == 0x03)
            {
                std::string len;
                if (!recv_exact(client, 1, &len))
                {
                    CloseTestSocket(client);
                    CloseTestSocket(listen_socket);
                    return;
                }
                const std::size_t host_len = static_cast<unsigned char>(len[0]);
                std::string discard;
                if (!recv_exact(client, host_len + 2, &discard))
                {
                    CloseTestSocket(client);
                    CloseTestSocket(listen_socket);
                    return;
                }
            }
            else
            {
                CloseTestSocket(client);
                CloseTestSocket(listen_socket);
                return;
            }

            const char connect_reply[10] = {static_cast<char>(0x05),
                                            static_cast<char>(0x00),
                                            static_cast<char>(0x00),
                                            static_cast<char>(0x01),
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0};
            if (!send_exact(client, connect_reply, sizeof(connect_reply)))
            {
                CloseTestSocket(client);
                CloseTestSocket(listen_socket);
                return;
            }

            const std::string request = ReceiveHttpRequest(client);
            if (captured_request)
            {
                *captured_request = request;
            }

            (void)SendAllResponse(client, response);
            CloseTestSocket(client);
            CloseTestSocket(listen_socket);
        });

    return std::optional<LocalSocks5Proxy>(std::move(server));
}

#if defined(HTTPX_ENABLE_OPENSSL)
struct TlsTestIdentity
{
    EVP_PKEY* private_key{nullptr};
    X509* certificate{nullptr};

    ~TlsTestIdentity()
    {
        if (certificate != nullptr)
        {
            X509_free(certificate);
        }
        if (private_key != nullptr)
        {
            EVP_PKEY_free(private_key);
        }
    }
};

bool AddCertificateExtension(X509* certificate, int nid, const char* value)
{
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, nullptr, nid, const_cast<char*>(value));
    if (extension == nullptr)
    {
        return false;
    }
    const bool ok = X509_add_ext(certificate, extension, -1) == 1;
    X509_EXTENSION_free(extension);
    return ok;
}

std::shared_ptr<TlsTestIdentity> MakeTlsTestIdentity()
{
    auto identity = std::make_shared<TlsTestIdentity>();
    EVP_PKEY_CTX* key_context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (key_context == nullptr)
    {
        return nullptr;
    }
    const bool key_ok = EVP_PKEY_keygen_init(key_context) == 1 &&
                        EVP_PKEY_CTX_set_rsa_keygen_bits(key_context, 2048) == 1 &&
                        EVP_PKEY_keygen(key_context, &identity->private_key) == 1;
    EVP_PKEY_CTX_free(key_context);
    if (!key_ok)
    {
        return nullptr;
    }

    identity->certificate = X509_new();
    if (identity->certificate == nullptr || X509_set_version(identity->certificate, 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(identity->certificate), 1) != 1 ||
        X509_gmtime_adj(X509_get_notBefore(identity->certificate), -60) == nullptr ||
        X509_gmtime_adj(X509_get_notAfter(identity->certificate), 24 * 60 * 60) == nullptr ||
        X509_set_pubkey(identity->certificate, identity->private_key) != 1)
    {
        return nullptr;
    }

    X509_NAME* subject = X509_get_subject_name(identity->certificate);
    const unsigned char localhost[] = "localhost";
    if (subject == nullptr || X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, localhost, -1, -1, 0) != 1 ||
        X509_set_issuer_name(identity->certificate, subject) != 1 ||
        !AddCertificateExtension(identity->certificate, NID_basic_constraints, "critical,CA:TRUE") ||
        !AddCertificateExtension(identity->certificate, NID_key_usage,
                                 "critical,keyCertSign,digitalSignature,keyEncipherment") ||
        !AddCertificateExtension(identity->certificate, NID_subject_alt_name, "DNS:localhost") ||
        X509_sign(identity->certificate, identity->private_key, EVP_sha256()) <= 0)
    {
        return nullptr;
    }
    return identity;
}

bool WritePublicCertificate(const fs::path& path, X509* certificate)
{
    BIO* output = BIO_new(BIO_s_mem());
    if (output == nullptr)
    {
        return false;
    }
    if (PEM_write_bio_X509(output, certificate) != 1)
    {
        BIO_free(output);
        return false;
    }

    BUF_MEM* contents = nullptr;
    BIO_get_mem_ptr(output, &contents);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const bool ok = contents != nullptr && file.is_open() &&
                    (contents->length == 0 ||
                     (file.write(contents->data, static_cast<std::streamsize>(contents->length)), file.good()));
    BIO_free(output);
    return ok;
}

struct LocalTlsServer
{
    std::uint16_t port{0};
    std::thread worker;

    LocalTlsServer() = default;
    LocalTlsServer(const LocalTlsServer&) = delete;
    LocalTlsServer& operator=(const LocalTlsServer&) = delete;
    LocalTlsServer(LocalTlsServer&&) noexcept = default;
    LocalTlsServer& operator=(LocalTlsServer&&) noexcept = default;

    ~LocalTlsServer()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
};

std::optional<LocalTlsServer> StartSingleResponseTlsServer(std::shared_ptr<TlsTestIdentity> identity,
                                                           std::string response)
{
    if (!EnsureTestNetworkReady() || identity == nullptr)
    {
        return std::nullopt;
    }

    TestSocket listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == kInvalidTestSocket)
    {
        return std::nullopt;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_socket, 1) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    sockaddr_in bound{};
    socklen_t bound_len = static_cast<socklen_t>(sizeof(bound));
    if (::getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    LocalTlsServer server;
    server.port = ntohs(bound.sin_port);
    server.worker = std::thread(
        [listen_socket, identity = std::move(identity), response = std::move(response)]()
        {
            SSL_CTX* context = SSL_CTX_new(TLS_server_method());
            if (context == nullptr || SSL_CTX_use_certificate(context, identity->certificate) != 1 ||
                SSL_CTX_use_PrivateKey(context, identity->private_key) != 1)
            {
                if (context != nullptr)
                {
                    SSL_CTX_free(context);
                }
                CloseTestSocket(listen_socket);
                return;
            }

            sockaddr_in client_addr{};
            socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
            TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client != kInvalidTestSocket)
            {
                SSL* ssl = SSL_new(context);
                if (ssl != nullptr)
                {
                    if (SSL_set_fd(ssl, static_cast<int>(client)) == 1 && SSL_accept(ssl) == 1)
                    {
                        SetRecvTimeoutMs(client, 1000);
                        char request_buffer[1024] = {0};
                        const int received = SSL_read(ssl, request_buffer, static_cast<int>(sizeof(request_buffer)));
                        if (received > 0)
                        {
                            std::size_t sent = 0;
                            while (sent < response.size())
                            {
                                const int written =
                                    SSL_write(ssl, response.data() + sent, static_cast<int>(response.size() - sent));
                                if (written <= 0)
                                {
                                    break;
                                }
                                sent += static_cast<std::size_t>(written);
                            }
                        }
                    }
                    SSL_free(ssl);
                }
                CloseTestSocket(client);
            }
            SSL_CTX_free(context);
            CloseTestSocket(listen_socket);
        });

    return server;
}
#endif

} // namespace

TEST(HttpxBasicsTests, ToStringMappingsAreStable)
{
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Get), "GET");
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Delete), "DELETE");
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Patch), "PATCH");
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Options), "OPTIONS");
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Connect), "CONNECT");
    EXPECT_STREQ(httpx::ToString(httpx::HttpMethod::Trace), "TRACE");
    EXPECT_STREQ(httpx::ToString(httpx::ErrorKind::Timeout), "timeout");
    EXPECT_STREQ(httpx::ToString(httpx::LogSeverity::Warning), "warning");
}

TEST(HttpxClientTests, ConvenienceMethodsCoverExtendedStandardVerbs)
{
    std::vector<httpx::HttpMethod> seen_methods;
    std::vector<std::string> seen_bodies;

    httpx::ClientOptions options;
    options.transport = [&seen_methods, &seen_bodies](const httpx::Request& request,
                                                      const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        seen_methods.push_back(request.method);
        seen_bodies.push_back(request.body);

        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.reason = "OK";
        out.value.body = "ok";
        return out;
    };

    httpx::Client client(options);

    ASSERT_TRUE(client.Options("http://localhost/opts").ok);
    ASSERT_TRUE(client.Connect("http://localhost/connect").ok);
    ASSERT_TRUE(client.Trace("http://localhost/trace").ok);
    ASSERT_TRUE(client.Patch("http://localhost/patch", "p-body").ok);

    ASSERT_EQ(seen_methods.size(), 4u);
    EXPECT_EQ(seen_methods[0], httpx::HttpMethod::Options);
    EXPECT_EQ(seen_methods[1], httpx::HttpMethod::Connect);
    EXPECT_EQ(seen_methods[2], httpx::HttpMethod::Trace);
    EXPECT_EQ(seen_methods[3], httpx::HttpMethod::Patch);

    ASSERT_EQ(seen_bodies.size(), 4u);
    EXPECT_TRUE(seen_bodies[0].empty());
    EXPECT_TRUE(seen_bodies[1].empty());
    EXPECT_TRUE(seen_bodies[2].empty());
    EXPECT_EQ(seen_bodies[3], "p-body");
}

TEST(HttpxConfigTests, ParseOptionsFromFlatConfigWorks)
{
    std::vector<httpx::FlatConfigEntry> entries = {
        {"httpx.timeout.connect_ms", "1200"},
        {"httpx.timeout.total_ms", "9000"},
        {"httpx.redirects.follow", "true"},
        {"httpx.dns.strategy", "system_order"},
        {"httpx.io_buffer_bytes", "4096"},
        {"httpx.pool.per_host_limit", "16"},
        {"httpx.proxy.enabled", "true"},
        {"httpx.proxy.scheme", "http"},
        {"httpx.proxy.host", "127.0.0.1"},
        {"httpx.proxy.port", "8080"},
        {"httpx.tls.verify_peer", "true"},
        {"httpx.tls.verify_host", "false"},
        {"httpx.tls.ca_file", "certs/test-root.pem"},
        {"httpx.retry.max_attempts", "2"},
    };

    const auto parsed = httpx::ParseOptionsFromFlatConfig(entries);
    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    EXPECT_EQ(parsed.value.timeout.connect_ms, 1200u);
    EXPECT_EQ(parsed.value.timeout.total_ms, 9000u);
    EXPECT_TRUE(parsed.value.redirects.follow);
    EXPECT_EQ(parsed.value.dns_strategy, httpx::DnsStrategy::SystemOrder);
    EXPECT_EQ(parsed.value.io_buffer_bytes, 4096u);
    EXPECT_EQ(parsed.value.pool.per_host_limit, 16u);
    EXPECT_TRUE(parsed.value.proxy.enabled);
    EXPECT_EQ(parsed.value.proxy.host, "127.0.0.1");
    EXPECT_EQ(parsed.value.proxy.port, 8080u);
    EXPECT_TRUE(parsed.value.tls.verify_peer);
    EXPECT_FALSE(parsed.value.tls.verify_host);
    EXPECT_EQ(parsed.value.tls.ca_file, "certs/test-root.pem");
    EXPECT_EQ(parsed.value.retry_policy.max_attempts, 2u);
    EXPECT_EQ(parsed.value.max_retry_attempts, 0u);
}

TEST(HttpxConfigTests, ParseOptionsRejectsUnknownKey)
{
    std::vector<httpx::FlatConfigEntry> entries = {
        {"httpx.unknown.value", "1"},
    };

    const auto parsed = httpx::ParseOptionsFromFlatConfig(entries);
    ASSERT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.error.kind, httpx::ErrorKind::InvalidArgument);
}

TEST(HttpxRedactionTests, RedactUrlMasksSensitiveKeys)
{
    const auto redacted = httpx::RedactUrl("https://example.com/a?token=abc&name=demo&password=123");
    EXPECT_NE(redacted.find("token=***"), std::string::npos);
    EXPECT_NE(redacted.find("password=***"), std::string::npos);
    EXPECT_NE(redacted.find("name=demo"), std::string::npos);
}

TEST(HttpxClientTests, SendUsesTransportAndAppliesHeadSemantics)
{
    httpx::ClientOptions options;
    options.transport = [](const httpx::Request& request, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.reason = "OK";
        out.value.body = "payload";

        bool has_user_agent = false;
        for (const auto& h : request.headers)
        {
            if (h.first == "User-Agent")
            {
                has_user_agent = true;
                break;
            }
        }
        if (!has_user_agent)
        {
            out.ok = false;
            out.error.kind = httpx::ErrorKind::Internal;
            out.error.message = "missing user-agent header";
        }

        return out;
    };

    httpx::Client client(options);
    auto head_res = client.Head("http://localhost/test");
    ASSERT_TRUE(head_res.ok) << head_res.error.message;
    EXPECT_TRUE(head_res.value.body.empty());
}

TEST(HttpxClientTests, RetryHookCanRecoverTransientFailure)
{
    std::atomic<int> calls{0};

    httpx::ClientOptions options;
    options.max_retry_attempts = 2;
    options.should_retry = [](const httpx::Error& error, std::size_t) { return error.retryable; };
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        const int current = ++calls;
        if (current == 1)
        {
            httpx::Result<httpx::Response> fail;
            fail.ok = false;
            fail.error.kind = httpx::ErrorKind::Network;
            fail.error.retryable = true;
            fail.error.message = "transient";
            return fail;
        }

        httpx::Result<httpx::Response> ok;
        ok.ok = true;
        ok.value.status_code = 200;
        ok.value.body = "ok";
        return ok;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://localhost/retry");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(calls.load(), 2);
}

TEST(HttpxClientTests, RetryPolicyMaxAttemptsCountsInitialAttempt)
{
    std::atomic<int> calls{0};

    httpx::ClientOptions options;
    options.retry_policy.max_attempts = 1;
    options.retry_policy.retry_transient_errors = true;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        ++calls;
        httpx::Result<httpx::Response> out;
        out.ok = false;
        out.error.kind = httpx::ErrorKind::Timeout;
        out.error.retryable = true;
        out.error.message = "transient";
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://example.test/retry-budget");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(calls.load(), 1);
}

TEST(HttpxClientTests, LegacyMaxRetryAttemptsStillCountsRetries)
{
    std::atomic<int> calls{0};

    httpx::ClientOptions options;
    options.max_retry_attempts = 1;
    options.retry_policy.max_attempts = 0;
    options.retry_policy.retry_transient_errors = true;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        ++calls;
        httpx::Result<httpx::Response> out;
        out.ok = false;
        out.error.kind = httpx::ErrorKind::Timeout;
        out.error.retryable = true;
        out.error.message = "transient";
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://example.test/legacy-retry-budget");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(calls.load(), 2);
}

TEST(HttpxClientTests, RetryPolicyMaxAttemptsOverridesLegacyRetryBudget)
{
    std::atomic<int> calls{0};

    httpx::ClientOptions options;
    options.max_retry_attempts = 3;
    options.retry_policy.max_attempts = 1;
    options.retry_policy.retry_transient_errors = true;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        ++calls;
        httpx::Result<httpx::Response> out;
        out.ok = false;
        out.error.kind = httpx::ErrorKind::Timeout;
        out.error.retryable = true;
        out.error.message = "transient";
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://example.test/retry-conflict");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(calls.load(), 1);
}

TEST(HttpxClientTests, FailureStatsAreExposed)
{
    httpx::ClientOptions options;
    options.transport = [](const httpx::Request&, const httpx::ClientOptions&) -> httpx::Result<httpx::Response>
    {
        httpx::Result<httpx::Response> out;
        out.ok = false;
        out.error.kind = httpx::ErrorKind::TransportUnavailable;
        out.error.message = "down";
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://localhost/down");
    ASSERT_FALSE(result.ok);

    const auto stats = client.GetFailureStats();
    EXPECT_EQ(stats.total_requests, 1u);
    EXPECT_EQ(stats.total_failures, 1u);
    EXPECT_EQ(stats.consecutive_failures, 1u);
}

TEST(HttpxClientTests, HttpsWithoutBackendReturnsTlsError)
{
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.total_ms = 500;
    options.timeout.connect_ms = 200;
    options.timeout.read_write_ms = 200;

    httpx::Client client(options);
    const auto result = client.Get("https://localhost/no-transport");
    ASSERT_FALSE(result.ok);
#if defined(HTTPX_ENABLE_OPENSSL) || defined(HTTPX_ENABLE_MBEDTLS)
    EXPECT_NE(result.error.kind, httpx::ErrorKind::InvalidArgument);
#else
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Tls);
#endif
}

TEST(HttpxClientTests, HttpsRejectsInvalidTlsVerifyCombination)
{
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.tls.verify_peer = false;
    options.tls.verify_host = true;

    httpx::Client client(options);
    const auto result = client.Get("https://localhost/invalid-tls-policy");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::InvalidArgument);
}

TEST(HttpxClientTests, HttpsRejectsMissingCustomCaFile)
{
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.tls.ca_file = "this-file-should-not-exist-httpx.pem";

    httpx::Client client(options);
    const auto result = client.Get("https://localhost/missing-ca");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Tls);
    EXPECT_NE(result.error.message.find("tls.ca_file cannot be opened"), std::string::npos);
}

#if defined(HTTPX_ENABLE_OPENSSL)
TEST(HttpxClientTests, OpenSslLoopbackTlsVerifiesLocalhostAndRejectsHostnameMismatch)
{
    const auto identity = MakeTlsTestIdentity();
    ASSERT_NE(identity, nullptr) << "failed to generate OpenSSL test identity";

    const fs::path root = fs::current_path() / "toolx_test_tmp" / "httpx_openssl_tls";
    const fs::path ca_file = root / "localhost-ca.pem";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    ASSERT_TRUE(WritePublicCertificate(ca_file, identity->certificate));

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 1000;
    options.timeout.read_write_ms = 1000;
    options.timeout.total_ms = 3000;
    options.tls.ca_file = ca_file.string();

    {
        const auto server = StartSingleResponseTlsServer(
            identity, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
        ASSERT_TRUE(server.has_value()) << "failed to start TLS loopback server";

        httpx::Client client(options);
        const auto trusted = client.Get("https://localhost:" + std::to_string(server->port) + "/tls");
        ASSERT_TRUE(trusted.ok) << trusted.error.message;
        EXPECT_EQ(trusted.value.status_code, 200);
        EXPECT_EQ(trusted.value.body, "ok");
    }

    {
        const auto server = StartSingleResponseTlsServer(
            identity, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
        ASSERT_TRUE(server.has_value()) << "failed to start TLS loopback server";

        httpx::Client client(options);
        const auto mismatched = client.Get("https://127.0.0.1:" + std::to_string(server->port) + "/tls");
        ASSERT_FALSE(mismatched.ok);
        EXPECT_EQ(mismatched.error.kind, httpx::ErrorKind::Tls);
    }

    fs::remove_all(root, ec);
}
#endif

TEST(HttpxClientTests, DefaultTransportCanRoundTripLocalHttp)
{
    const auto server =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start local loopback server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 1000;
    options.timeout.read_write_ms = 1000;
    options.timeout.total_ms = 3000;

    httpx::Client client(options);
    const auto result = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/ping");

    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);
    EXPECT_EQ(result.value.body, "hello");
}

TEST(HttpxClientTests, DefaultTransportReadTimeoutWorks)
{
    const auto server =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 150);
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start local loopback server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 1000;
    options.timeout.read_write_ms = 30;
    options.timeout.total_ms = 500;

    httpx::Client client(options);
    const auto result = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/slow");

    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Timeout);
}

TEST(HttpxClientTests, DefaultTransportDecodesChunkedResponse)
{
    const auto server =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                                  "5\r\nhello\r\n"
                                  "6\r\n world\r\n"
                                  "0\r\nX-Debug: ok\r\n\r\n");
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start local loopback server";
    }

    std::vector<std::string> streamed;

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 1000;
    options.timeout.read_write_ms = 1000;
    options.timeout.total_ms = 3000;

    httpx::Client client(options);
    httpx::Request request;
    request.method = httpx::HttpMethod::Get;
    request.url = "http://127.0.0.1:" + std::to_string(server->port) + "/chunked";
    request.on_response_chunk = [&streamed](std::string_view chunk)
    {
        streamed.emplace_back(chunk);
        return true;
    };

    const auto result = client.Send(request);
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);
    EXPECT_EQ(result.value.body, "hello world");
    ASSERT_EQ(streamed.size(), 2u);
    EXPECT_EQ(streamed[0], "hello");
    EXPECT_EQ(streamed[1], " world");
}

TEST(HttpxClientTests, ChunkedCallbackAbortReturnsInternalError)
{
    const auto server =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                                  "5\r\nhello\r\n"
                                  "0\r\n\r\n");
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start local loopback server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 1000;
    options.timeout.read_write_ms = 1000;
    options.timeout.total_ms = 3000;

    httpx::Client client(options);
    httpx::Request request;
    request.method = httpx::HttpMethod::Get;
    request.url = "http://127.0.0.1:" + std::to_string(server->port) + "/chunked-abort";
    request.on_response_chunk = [](std::string_view) { return false; };

    const auto result = client.Send(request);
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Internal);
}

TEST(HttpxClientTests, MultipartEncodingAndUploadProgressWork)
{
    auto captured = std::make_shared<std::string>();

    {
        const auto server = StartSingleResponseServer(
            "HTTP/1.1 201 Created\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 0, captured);
        if (!server.has_value())
        {
            GTEST_SKIP() << "failed to start local loopback server";
        }

        std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;

        httpx::ClientOptions options;
        options.use_proxy_from_environment = false;
        options.timeout.connect_ms = 1000;
        options.timeout.read_write_ms = 1000;
        options.timeout.total_ms = 3000;

        httpx::Client client(options);
        httpx::Request request;
        request.method = httpx::HttpMethod::Post;
        request.url = "http://127.0.0.1:" + std::to_string(server->port) + "/upload";
        request.multipart.push_back({"meta", "", "text/plain", "demo"});
        request.multipart.push_back({"file", "blob.bin", "application/octet-stream", std::string(4096, 'x')});
        request.on_upload_progress = [&progress](std::uint64_t sent, std::uint64_t total)
        { progress.push_back({sent, total}); };

        const auto result = client.Send(request);
        ASSERT_TRUE(result.ok) << result.error.message;
        EXPECT_EQ(result.value.status_code, 201);

        ASSERT_FALSE(progress.empty());
        EXPECT_EQ(progress.front().first, 0u);
        EXPECT_GT(progress.front().second, 0u);
        EXPECT_EQ(progress.back().first, progress.back().second);
    }

    ASSERT_FALSE(captured->empty());
    EXPECT_NE(captured->find("Content-Type: multipart/form-data; boundary="), std::string::npos);
    EXPECT_NE(captured->find("name=\"meta\""), std::string::npos);
    EXPECT_NE(captured->find("filename=\"blob.bin\""), std::string::npos);
    EXPECT_NE(captured->find("demo"), std::string::npos);
}

TEST(HttpxClientTests, RedirectFollowStripsSensitiveHeadersOnCrossOrigin)
{
    auto redirected_capture = std::make_shared<std::string>();
    auto target_capture = std::make_shared<std::string>();

    const auto target = StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
                                                  0, target_capture);
    if (!target.has_value())
    {
        GTEST_SKIP() << "failed to start target server";
    }

    const auto redirect =
        StartSingleResponseServer("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(target->port) +
                                      "/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                                  0, redirected_capture);
    if (!redirect.has_value())
    {
        GTEST_SKIP() << "failed to start redirect server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.redirects.follow = true;
    options.redirects.max_hops = 3;

    httpx::Client client(options);
    httpx::Request request;
    request.method = httpx::HttpMethod::Get;
    request.url = "http://127.0.0.1:" + std::to_string(redirect->port) + "/jump";
    request.headers.push_back({"Authorization", "Bearer top-secret"});
    request.headers.push_back({"Cookie", "sid=manual"});

    const auto result = client.Send(request);
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);

    EXPECT_NE(redirected_capture->find("Authorization:"), std::string::npos);
    EXPECT_NE(redirected_capture->find("Cookie:"), std::string::npos);
    EXPECT_EQ(target_capture->find("Authorization:"), std::string::npos);
    EXPECT_EQ(target_capture->find("Cookie:"), std::string::npos);
}

TEST(HttpxClientTests, Redirect303ConvertsPostToGetAndDropsBodyHeaders)
{
    auto redirected_capture = std::make_shared<std::string>();
    auto target_capture = std::make_shared<std::string>();

    const auto target = StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
                                                  0, target_capture);
    if (!target.has_value())
    {
        GTEST_SKIP() << "failed to start target server";
    }

    const auto redirect = StartSingleResponseServer(
        "HTTP/1.1 303 See Other\r\nLocation: http://127.0.0.1:" + std::to_string(target->port) +
            "/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        0, redirected_capture);
    if (!redirect.has_value())
    {
        GTEST_SKIP() << "failed to start redirect server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.redirects.follow = true;
    options.redirects.max_hops = 3;

    httpx::Client client(options);
    httpx::Request request;
    request.method = httpx::HttpMethod::Post;
    request.url = "http://127.0.0.1:" + std::to_string(redirect->port) + "/jump";
    request.body = "payload";
    request.headers.push_back({"Content-Type", "text/plain"});

    const auto result = client.Send(request);
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);

    EXPECT_NE(redirected_capture->find("POST /jump HTTP/1.1"), std::string::npos);
    EXPECT_NE(redirected_capture->find("Content-Type: text/plain"), std::string::npos);
    EXPECT_NE(target_capture->find("GET /final HTTP/1.1"), std::string::npos);
    EXPECT_EQ(target_capture->find("POST /final HTTP/1.1"), std::string::npos);
    EXPECT_EQ(target_capture->find("Content-Type: text/plain"), std::string::npos);
    EXPECT_EQ(target_capture->find("Content-Length: 7"), std::string::npos);
}

TEST(HttpxClientTests, CookieJarSendsStoredCookieOnNextRequest)
{
    auto captured = std::make_shared<std::vector<std::string>>();
    const auto server = StartScriptedServer(
        {
            "HTTP/1.1 200 OK\r\nSet-Cookie: sid=abc123; Path=/; HttpOnly\r\nContent-Length: 2\r\nConnection: "
            "close\r\n\r\nok",
            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
        },
        captured);
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start scripted cookie server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;

    httpx::Client client(options);
    const auto r1 = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/cookie/step1");
    ASSERT_TRUE(r1.ok) << r1.error.message;

    const auto r2 = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/cookie/step2");
    ASSERT_TRUE(r2.ok) << r2.error.message;

    ASSERT_EQ(captured->size(), 2u);
    EXPECT_EQ((*captured)[0].find("Cookie: sid=abc123"), std::string::npos);
    EXPECT_NE((*captured)[1].find("Cookie: sid=abc123"), std::string::npos);
}

TEST(HttpxClientTests, CookieJarRespectsCookiePathScope)
{
    auto captured = std::make_shared<std::vector<std::string>>();
    const auto server = StartScriptedServer(
        {
            "HTTP/1.1 200 OK\r\nSet-Cookie: scoped=yes; Path=/cookie; HttpOnly\r\nContent-Length: 2\r\nConnection: "
            "close\r\n\r\nok",
            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
        },
        captured);
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start scripted cookie server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;

    httpx::Client client(options);
    ASSERT_TRUE(client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/cookie/seed").ok);
    ASSERT_TRUE(client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/other/path").ok);
    ASSERT_TRUE(client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/cookie/again").ok);

    ASSERT_EQ(captured->size(), 3u);
    EXPECT_EQ((*captured)[0].find("Cookie: scoped=yes"), std::string::npos);
    EXPECT_EQ((*captured)[1].find("Cookie: scoped=yes"), std::string::npos);
    EXPECT_NE((*captured)[2].find("Cookie: scoped=yes"), std::string::npos);
}

TEST(HttpxClientTests, ConnectionPoolReusesKeepAliveSocket)
{
    auto captured = std::make_shared<std::vector<std::string>>();
    auto accepts = std::make_shared<std::atomic<int>>(0);
    const auto server = StartKeepAliveTwoRequestServer(captured, accepts);
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start keep-alive server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.pool.per_host_limit = 8;
    options.pool.total_limit = 32;

    httpx::Client client(options);
    const auto r1 = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/pool/one");
    ASSERT_TRUE(r1.ok) << r1.error.message;

    const auto r2 = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/pool/two");
    ASSERT_TRUE(r2.ok) << r2.error.message;

    EXPECT_EQ(accepts->load(), 1);
    ASSERT_EQ(captured->size(), 2u);
    EXPECT_NE((*captured)[0].find("GET /pool/one HTTP/1.1"), std::string::npos);
    EXPECT_NE((*captured)[1].find("GET /pool/two HTTP/1.1"), std::string::npos);
}

TEST(HttpxClientTests, ProxyUsesAbsoluteFormTarget)
{
    auto captured = std::make_shared<std::string>();
    const auto proxy =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 0, captured);
    if (!proxy.has_value())
    {
        GTEST_SKIP() << "failed to start local proxy server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.proxy.enabled = true;
    options.proxy.scheme = "http";
    options.proxy.host = "127.0.0.1";
    options.proxy.port = proxy->port;

    httpx::Client client(options);
    const auto result = client.Get("http://nonexistent.invalid/proxy/abs?q=1");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);

    EXPECT_NE(captured->find("GET http://nonexistent.invalid/proxy/abs?q=1 HTTP/1.1"), std::string::npos);
    EXPECT_NE(captured->find("Host: nonexistent.invalid"), std::string::npos);
}

TEST(HttpxClientTests, ProxyInjectsBasicAuthorizationHeader)
{
    auto captured = std::make_shared<std::string>();
    const auto proxy =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 0, captured);
    if (!proxy.has_value())
    {
        GTEST_SKIP() << "failed to start local proxy server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.proxy.enabled = true;
    options.proxy.scheme = "http";
    options.proxy.host = "127.0.0.1";
    options.proxy.port = proxy->port;
    options.proxy.username = "alice";
    options.proxy.password = "secret";

    httpx::Client client(options);
    const auto result = client.Get("http://nonexistent.invalid/proxy/auth");
    ASSERT_TRUE(result.ok) << result.error.message;

    EXPECT_NE(captured->find("Proxy-Authorization: Basic YWxpY2U6c2VjcmV0"), std::string::npos);
}

TEST(HttpxClientTests, ProxyCanBeLoadedFromEnvironment)
{
    auto captured = std::make_shared<std::string>();
    const auto proxy =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 0, captured);
    if (!proxy.has_value())
    {
        GTEST_SKIP() << "failed to start local proxy server";
    }

    ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:" + std::to_string(proxy->port));
    ScopedEnvVar no_proxy("NO_PROXY", "not-used.invalid");

    httpx::ClientOptions options;
    options.use_proxy_from_environment = true;
    options.proxy.enabled = false;

    httpx::Client client(options);
    const auto result = client.Get("http://nonexistent.invalid/proxy/env");
    ASSERT_TRUE(result.ok) << result.error.message;

    EXPECT_NE(captured->find("GET http://nonexistent.invalid/proxy/env HTTP/1.1"), std::string::npos);
}

TEST(HttpxClientTests, ExplicitlyDisabledEnvironmentProxyIsIgnored)
{
    ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:9");
    ScopedEnvVar no_proxy("NO_PROXY", "not-used.invalid");

    bool saw_transport = false;
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.transport = [&saw_transport](const httpx::Request&,
                                         const httpx::ClientOptions& effective) -> httpx::Result<httpx::Response>
    {
        saw_transport = true;
        EXPECT_FALSE(effective.proxy.enabled);
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://disabled-proxy.invalid/path");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_TRUE(saw_transport);
}

TEST(HttpxClientTests, NoProxyBypassesEnvironmentProxyRules)
{
    struct Case
    {
        std::string rule;
        std::string url;
    };

    const std::vector<Case> cases = {
        {"service.internal", "http://service.internal:8080/path"},
        {".example.com", "http://api.example.com/path"},
        {"api.example.com:8080", "http://api.example.com:8080/path"},
        {"[::1]:8080", "http://[::1]:8080/path"},
        {"*", "http://anything.invalid/path"},
    };

    for (const auto& c : cases)
    {
        ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:9");
        ScopedEnvVar no_proxy("NO_PROXY", c.rule);

        bool saw_transport = false;
        httpx::ClientOptions options;
        options.use_proxy_from_environment = true;
        options.transport = [&saw_transport](const httpx::Request&,
                                             const httpx::ClientOptions& effective) -> httpx::Result<httpx::Response>
        {
            saw_transport = true;
            EXPECT_FALSE(effective.proxy.enabled);
            httpx::Result<httpx::Response> out;
            out.ok = true;
            out.value.status_code = 200;
            return out;
        };

        httpx::Client client(options);
        const auto result = client.Get(c.url);
        ASSERT_TRUE(result.ok) << c.rule << ": " << result.error.message;
        EXPECT_TRUE(saw_transport);
    }
}

TEST(HttpxClientTests, NoProxyPortMismatchKeepsEnvironmentProxy)
{
    ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:9");
    ScopedEnvVar no_proxy("NO_PROXY", "api.example.com:9090");

    bool saw_transport = false;
    httpx::ClientOptions options;
    options.use_proxy_from_environment = true;
    options.transport = [&saw_transport](const httpx::Request&,
                                         const httpx::ClientOptions& effective) -> httpx::Result<httpx::Response>
    {
        saw_transport = true;
        EXPECT_TRUE(effective.proxy.enabled);
        EXPECT_EQ(effective.proxy.host, "127.0.0.1");
        EXPECT_EQ(effective.proxy.port, 9u);
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://api.example.com:8080/path");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_TRUE(saw_transport);
}

TEST(HttpxClientTests, LowercaseNoProxyBypassesEnvironmentProxy)
{
    ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:9");
    ScopedEnvVar upper_no_proxy("NO_PROXY", "");
    ScopedEnvVar no_proxy("no_proxy", "api.lowercase.test");

    bool saw_transport = false;
    httpx::ClientOptions options;
    options.use_proxy_from_environment = true;
    options.transport = [&saw_transport](const httpx::Request&,
                                         const httpx::ClientOptions& effective) -> httpx::Result<httpx::Response>
    {
        saw_transport = true;
        EXPECT_FALSE(effective.proxy.enabled);
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };

    httpx::Client client(options);
    const auto result = client.Get("http://api.lowercase.test/path");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_TRUE(saw_transport);
}

TEST(HttpxClientTests, Socks5NoAuthProxyUsesOriginFormTarget)
{
    auto captured = std::make_shared<std::string>();
    const auto proxy =
        StartSocks5NoAuthProxy("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", captured);
    if (!proxy.has_value())
    {
        GTEST_SKIP() << "failed to start local socks5 proxy server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.proxy.enabled = true;
    options.proxy.scheme = "socks5";
    options.proxy.host = "127.0.0.1";
    options.proxy.port = proxy->port;

    httpx::Client client(options);
    const auto result = client.Get("http://nonexistent.invalid/socks/path?q=1");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);

    EXPECT_NE(captured->find("GET /socks/path?q=1 HTTP/1.1"), std::string::npos);
    EXPECT_EQ(captured->find("GET http://nonexistent.invalid/socks/path?q=1 HTTP/1.1"), std::string::npos);
    EXPECT_EQ(captured->find("Proxy-Authorization:"), std::string::npos);
}

TEST(HttpxClientTests, Socks5NoAuthRejectsCredentialConfiguration)
{
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.proxy.enabled = true;
    options.proxy.scheme = "socks5";
    options.proxy.host = "127.0.0.1";
    options.proxy.port = 1080;
    options.proxy.username = "alice";
    options.proxy.password = "secret";

    httpx::Client client(options);
    const auto result = client.Get("http://example.invalid/socks/noauth");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Proxy);
    EXPECT_NE(result.error.message.find("no-auth"), std::string::npos);
}

TEST(HttpxClientTests, Socks5ProxyCanBeLoadedFromEnvironment)
{
    auto captured = std::make_shared<std::string>();
    const auto proxy =
        StartSocks5NoAuthProxy("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", captured);
    if (!proxy.has_value())
    {
        GTEST_SKIP() << "failed to start local socks5 proxy server";
    }

    ScopedEnvVar http_proxy("HTTP_PROXY", "socks5://127.0.0.1:" + std::to_string(proxy->port));
    ScopedEnvVar no_proxy("NO_PROXY", "not-used.invalid");

    httpx::ClientOptions options;
    options.use_proxy_from_environment = true;
    options.proxy.enabled = false;

    httpx::Client client(options);
    const auto result = client.Get("http://nonexistent.invalid/socks/env");
    ASSERT_TRUE(result.ok) << result.error.message;
    EXPECT_EQ(result.value.status_code, 200);

    EXPECT_NE(captured->find("GET /socks/env HTTP/1.1"), std::string::npos);
}

TEST(HttpxClientTests, Socks5ProxyWithCredentialsFromEnvironmentIsRejected)
{
    ScopedEnvVar http_proxy("HTTP_PROXY", "socks5://alice:secret@127.0.0.1:1080");
    ScopedEnvVar no_proxy("NO_PROXY", "not-used.invalid");

    httpx::ClientOptions options;
    options.use_proxy_from_environment = true;
    options.proxy.enabled = false;

    httpx::Client client(options);
    const auto result = client.Get("http://example.invalid/socks/env-noauth");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Proxy);
    EXPECT_NE(result.error.message.find("no-auth"), std::string::npos);
}

TEST(HttpxClientTests, RedirectLoopIsDetected)
{
    const auto server = StartSingleResponseServer(
        "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start local loopback server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.redirects.follow = true;
    options.redirects.max_hops = 8;

    httpx::Client client(options);
    const auto result = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/loop");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Redirect);
}

TEST(HttpxClientTests, MalformedChunkedResponseReturnsProtocolError)
{
    const auto server =
        StartSingleResponseServer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                                  "X\r\nhello\r\n"
                                  "0\r\n\r\n");
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start local loopback server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;

    httpx::Client client(options);
    const auto result = client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/bad-chunk");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Protocol);
}

TEST(HttpxClientTests, ProxyConnectFailureIsClassifiedAsProxyError)
{
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 200;
    options.timeout.read_write_ms = 200;
    options.timeout.total_ms = 600;
    options.proxy.enabled = true;
    options.proxy.scheme = "http";
    options.proxy.host = "127.0.0.1";
    options.proxy.port = 1;

    httpx::Client client(options);
    const auto result = client.Get("http://example.invalid/proxy-fail");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, httpx::ErrorKind::Proxy);
}

TEST(HttpxClientTests, SoakSequentialRequestsRemainStable)
{
    constexpr int kRounds = 40;
    std::vector<std::string> scripted;
    scripted.reserve(kRounds);
    for (int i = 0; i < kRounds; ++i)
    {
        scripted.push_back("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
    }

    const auto captured = std::make_shared<std::vector<std::string>>();
    const auto server = StartScriptedServer(std::move(scripted), captured);
    if (!server.has_value())
    {
        GTEST_SKIP() << "failed to start scripted server";
    }

    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.timeout.connect_ms = 1000;
    options.timeout.read_write_ms = 1000;
    options.timeout.total_ms = 3000;

    httpx::Client client(options);
    for (int i = 0; i < kRounds; ++i)
    {
        const auto result =
            client.Get("http://127.0.0.1:" + std::to_string(server->port) + "/soak/" + std::to_string(i));
        ASSERT_TRUE(result.ok) << "round=" << i << " err=" << result.error.message;
        EXPECT_EQ(result.value.status_code, 200);
    }

    ASSERT_EQ(static_cast<int>(captured->size()), kRounds);
}

TEST(HttpxClientTests, DownloadFileWritesThroughTemporaryFile)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "httpx_download";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    httpx::ClientOptions options;
    options.transport = [](const httpx::Request& request, const httpx::ClientOptions&)
    {
        httpx::Result<httpx::Response> out;
        if (request.on_response_chunk)
        {
            EXPECT_TRUE(request.on_response_chunk("he"));
            EXPECT_TRUE(request.on_response_chunk("llo"));
        }
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };
    httpx::Client client(options);

    std::uint64_t progress = 0;
    httpx::DownloadOptions download;
    download.on_progress = [&progress](std::uint64_t value) { progress = value; };
    const auto status = client.DownloadFile("http://example.test/file", (root / "file.txt").string(), download);
    ASSERT_TRUE(status.ok) << status.error.message;

    std::ifstream in(root / "file.txt", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "hello");
    EXPECT_EQ(progress, 5u);
}

TEST(HttpxClientTests, DownloadFileRetriesWithFreshTemporaryFile)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "httpx_download_retry";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    std::atomic<int> calls{0};
    httpx::ClientOptions options;
    options.retry_policy.max_attempts = 2;
    options.transport = [&calls](const httpx::Request& request, const httpx::ClientOptions&)
    {
        const int attempt = ++calls;
        httpx::Result<httpx::Response> out;
        EXPECT_TRUE(static_cast<bool>(request.on_response_chunk));
        if (attempt == 1)
        {
            EXPECT_TRUE(request.on_response_chunk("partial"));
            out.ok = false;
            out.error.kind = httpx::ErrorKind::Network;
            out.error.message = "transient failure after partial response";
            out.error.retryable = true;
            return out;
        }

        EXPECT_TRUE(request.on_response_chunk("complete"));
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };
    httpx::Client client(options);

    const auto target = root / "file.txt";
    const auto status = client.DownloadFile("http://example.test/file", target.string());
    ASSERT_TRUE(status.ok) << status.error.message;
    EXPECT_EQ(calls.load(), 2);

    std::ifstream in(target, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "complete");

    std::filesystem::remove_all(root, ec);
}

TEST(HttpxClientTests, DownloadFileRejectsEmptyTempSuffixWithoutTouchingExistingFile)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "httpx_download_empty_suffix";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto target = root / "file.txt";
    {
        std::ofstream out(target, std::ios::binary);
        out << "original";
    }

    std::atomic<int> calls{0};
    httpx::ClientOptions options;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&)
    {
        calls.fetch_add(1);
        httpx::Result<httpx::Response> out;
        out.error.kind = httpx::ErrorKind::Network;
        out.error.message = "network unavailable";
        return out;
    };
    httpx::Client client(options);

    httpx::DownloadOptions download;
    download.temp_suffix.clear();
    const auto status = client.DownloadFile("http://example.test/file", target.string(), download);
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, httpx::ErrorKind::InvalidArgument);
    EXPECT_EQ(calls.load(), 0);

    std::ifstream in(target, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "original");
}

TEST(HttpxClientTests, DownloadFileOverwriteFalseDoesNotCreateTempFile)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "httpx_download_no_overwrite";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto target = root / "file.txt";
    {
        std::ofstream out(target, std::ios::binary);
        out << "original";
    }

    std::atomic<int> calls{0};
    httpx::ClientOptions options;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&)
    {
        ++calls;
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        out.value.body = "replacement";
        return out;
    };
    httpx::Client client(options);

    httpx::DownloadOptions download;
    download.overwrite = false;
    const auto status = client.DownloadFile("http://example.test/file", target.string(), download);
    EXPECT_FALSE(status.ok);
    EXPECT_EQ(status.error.kind, httpx::ErrorKind::InvalidArgument);
    EXPECT_EQ(calls.load(), 0);
    EXPECT_FALSE(std::filesystem::exists(target.string() + download.temp_suffix));

    std::ifstream in(target, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "original");
}

TEST(HttpxClientTests, UploadFileBuildsMultipartRequest)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "httpx_upload";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    {
        std::ofstream out(root / "blob.bin", std::ios::binary);
        out << "payload";
    }

    bool saw_file = false;
    httpx::ClientOptions options;
    options.transport = [&saw_file](const httpx::Request& request, const httpx::ClientOptions&)
    {
        saw_file = request.multipart.size() == 1 && request.multipart[0].name == "file" &&
                   request.multipart[0].filename == "blob.bin" && request.multipart[0].data == "payload";
        httpx::Result<httpx::Response> out;
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };

    httpx::Client client(options);
    const auto response = client.UploadFile("http://example.test/upload", "file", (root / "blob.bin").string());
    ASSERT_TRUE(response.ok) << response.error.message;
    EXPECT_TRUE(saw_file);
}

TEST(HttpxClientTests, UploadFileMissingInputFailsBeforeTransport)
{
    std::atomic<int> calls{0};
    httpx::ClientOptions options;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&)
    {
        ++calls;
        httpx::Result<httpx::Response> out;
        out.ok = true;
        return out;
    };

    httpx::Client client(options);
    const auto response = client.UploadFile("http://example.test/upload", "file", "missing-input.bin");
    EXPECT_FALSE(response.ok);
    EXPECT_EQ(response.error.kind, httpx::ErrorKind::InvalidArgument);
    EXPECT_EQ(calls.load(), 0);
}

TEST(HttpxClientTests, UploadFilePayloadLimitFailsBeforeTransport)
{
    const auto root = std::filesystem::current_path() / "toolx_test_tmp" / "httpx_upload_limit";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto input = root / "blob.bin";
    {
        std::ofstream out(input, std::ios::binary);
        out << "payload";
    }

    std::atomic<int> calls{0};
    httpx::ClientOptions options;
    options.limits.max_body_bytes = 1;
    options.limits.max_header_bytes = 1;
    options.transport = [&calls](const httpx::Request&, const httpx::ClientOptions&)
    {
        ++calls;
        httpx::Result<httpx::Response> out;
        out.ok = true;
        return out;
    };

    httpx::Client client(options);
    const auto response = client.UploadFile("http://example.test/upload", "file", input.string());
    EXPECT_FALSE(response.ok);
    EXPECT_EQ(response.error.kind, httpx::ErrorKind::InvalidArgument);
    EXPECT_EQ(calls.load(), 0);
}

TEST(HttpxClientTests, RetryPolicyAndCircuitBreakerWork)
{
    std::atomic<int> attempts{0};
    httpx::ClientOptions options;
    options.retry_policy.max_attempts = 2;
    options.retry_policy.retry_transient_errors = true;
    options.circuit_breaker.enabled = true;
    options.circuit_breaker.failure_threshold = 1;
    options.circuit_breaker.reset_timeout_ms = 10000;
    options.transport = [&attempts](const httpx::Request&, const httpx::ClientOptions&)
    {
        httpx::Result<httpx::Response> out;
        if (attempts.fetch_add(1) == 0)
        {
            out.ok = false;
            out.error.kind = httpx::ErrorKind::Timeout;
            out.error.retryable = true;
            out.error.message = "transient";
            return out;
        }
        out.ok = true;
        out.value.status_code = 200;
        return out;
    };

    httpx::Client client(options);
    ASSERT_TRUE(client.Get("http://example.test/retry").ok);
    EXPECT_EQ(attempts.load(), 2);

    options.retry_policy.max_attempts = 0;
    options.transport = [](const httpx::Request&, const httpx::ClientOptions&)
    {
        httpx::Result<httpx::Response> out;
        out.ok = false;
        out.error.kind = httpx::ErrorKind::Network;
        out.error.retryable = true;
        out.error.message = "down";
        return out;
    };
    httpx::Client circuit_client(options);
    EXPECT_FALSE(circuit_client.Get("http://example.test/fail").ok);
    const auto blocked = circuit_client.Get("http://example.test/blocked");
    EXPECT_FALSE(blocked.ok);
    EXPECT_EQ(blocked.error.kind, httpx::ErrorKind::CircuitOpen);
    EXPECT_TRUE(circuit_client.CircuitSnapshot().open);
}
