#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
    static bool initialized = false;
    static bool ready = false;
    if (!initialized)
    {
        WSADATA data{};
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        initialized = true;
    }
    return ready;
#else
    return true;
#endif
}

std::string ReceiveRequest(TestSocket client)
{
    std::string request;
    request.reserve(1024);
    while (request.find("\r\n\r\n") == std::string::npos)
    {
        char buffer[1024] = {0};
#if defined(_WIN32)
        const int count = ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
        const int count = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
#endif
        if (count <= 0)
        {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(count));
    }
    return request;
}

bool SendAll(TestSocket client, const std::string& response)
{
    std::size_t sent = 0;
    while (sent < response.size())
    {
#if defined(_WIN32)
        const int count = ::send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
#else
        const int count = static_cast<int>(::send(client, response.data() + sent, response.size() - sent, 0));
#endif
        if (count <= 0)
        {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

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

std::optional<LocalHttpServer> StartSingleResponseServer(std::string body,
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

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listen_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listen_socket, 1) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    sockaddr_in bound{};
    socklen_t bound_length = static_cast<socklen_t>(sizeof(bound));
    if (::getsockname(listen_socket, reinterpret_cast<sockaddr*>(&bound), &bound_length) != 0)
    {
        CloseTestSocket(listen_socket);
        return std::nullopt;
    }

    LocalHttpServer server;
    server.port = ntohs(bound.sin_port);
    server.captured_request = captured_request;
    server.worker = std::thread(
        [listen_socket, body = std::move(body), captured_request]()
        {
            sockaddr_in client_address{};
            socklen_t client_length = static_cast<socklen_t>(sizeof(client_address));
            TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_address), &client_length);
            if (client != kInvalidTestSocket)
            {
                if (captured_request)
                {
                    *captured_request = ReceiveRequest(client);
                }
                const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                             std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                (void)SendAll(client, response);
                CloseTestSocket(client);
            }
            CloseTestSocket(listen_socket);
        });
    return server;
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
        }
        else
        {
            Clear();
        }
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

std::string ShellQuote(const std::string& value)
{
#if defined(_WIN32)
    std::string out = "\"";
    for (const char ch : value)
    {
        if (ch == '\"')
        {
            out += "\\\"";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back('\"');
    return out;
#else
    std::string out = "'";
    for (const char ch : value)
    {
        if (ch == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
#endif
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

struct CommandResult
{
    int code{0};
    std::string output;
    std::string error;
};

CommandResult RunTool(const std::string& tool, const fs::path& root, const std::string& case_name,
                      const std::vector<std::string>& args)
{
    const fs::path output_path = root / (case_name + ".out");
    const fs::path error_path = root / (case_name + ".err");
    std::string command = ShellQuote(tool);
    for (const auto& arg : args)
    {
        command += " " + ShellQuote(arg);
    }
    command += " > " + ShellQuote(output_path.string()) + " 2> " + ShellQuote(error_path.string());
#if defined(_WIN32)
    command = "\"" + command + "\"";
#endif
    return {std::system(command.c_str()), ReadFile(output_path), ReadFile(error_path)};
}

[[noreturn]] void Fail(const std::string& message)
{
    std::cerr << message << "\n";
    std::exit(1);
}

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        Fail(message);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        Fail("usage: toolx_sync_proxy_contracts TOOLX_SYNC_EXE TEST_ROOT");
    }

    const std::string tool = argv[1];
    const fs::path root = argv[2];
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path base = root / "base.json";
    WriteFile(base, "{\"base\":true}");

    {
        const auto captured = std::make_shared<std::string>();
        const auto proxy = StartSingleResponseServer("{\"remote\":{\"through_proxy\":true}}", captured);
        Require(proxy.has_value(), "failed to start local proxy server");
        ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:" + std::to_string(proxy->port));
        ScopedEnvVar no_proxy("NO_PROXY", "not-used.invalid");

        const fs::path output = root / "proxied.json";
        const auto result = RunTool(tool, root, "remote-proxy",
                                    {"--base", base.string(), "--out", output.string(), "--remote-url",
                                     "http://nonexistent.invalid/remote.json", "--json"});
        Require(result.code == 0, "default remote proxy request failed: " + result.output + result.error);
        Require(result.output.find("\"proxy_from_environment\": true") != std::string::npos,
                "default remote proxy field was not true");
        Require(captured->find("GET http://nonexistent.invalid/remote.json HTTP/1.1") != std::string::npos,
                "remote URL did not reach the environment proxy");
        Require(ReadFile(output).find("\"through_proxy\": true") != std::string::npos,
                "proxied remote layer was not published");
    }

    {
        const auto captured = std::make_shared<std::string>();
        const auto server = StartSingleResponseServer("{\"remote\":{\"direct\":true}}", captured);
        Require(server.has_value(), "failed to start local remote server");
        ScopedEnvVar http_proxy("HTTP_PROXY", "http://127.0.0.1:9");
        ScopedEnvVar no_proxy("NO_PROXY", "not-used.invalid");

        const fs::path output = root / "direct.json";
        const std::string remote_url = "http://127.0.0.1:" + std::to_string(server->port) + "/remote.json";
        const auto result = RunTool(tool, root, "remote-no-proxy",
                                    {"--base", base.string(), "--out", output.string(), "--remote-url", remote_url,
                                     "--no-proxy-from-env", "--json"});
        Require(result.code == 0, "explicit no-proxy remote request failed: " + result.output + result.error);
        Require(result.output.find("\"proxy_from_environment\": false") != std::string::npos,
                "explicit no-proxy field was not false");
        Require(captured->find("GET /remote.json HTTP/1.1") != std::string::npos,
                "remote URL was not requested directly after --no-proxy-from-env");
        Require(ReadFile(output).find("\"direct\": true") != std::string::npos,
                "direct remote layer was not published");
    }

    fs::remove_all(root, ec);
    return 0;
}
