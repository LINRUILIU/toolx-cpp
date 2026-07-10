#include <chrono>
#include <cctype>
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

struct CommandResult
{
    int code{0};
    std::string out;
    std::string err;
    std::string command;
};

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
    static bool ok = false;
    if (!initialized)
    {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        initialized = true;
    }
    return ok;
#else
    return true;
#endif
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

struct LocalHttpServer
{
    std::uint16_t port{0};
    TestSocket listen_socket{kInvalidTestSocket};
    std::thread worker;
    std::shared_ptr<std::string> captured_request;

    LocalHttpServer() = default;
    LocalHttpServer(const LocalHttpServer&) = delete;
    LocalHttpServer& operator=(const LocalHttpServer&) = delete;
    LocalHttpServer(LocalHttpServer&& other) noexcept
        : port(other.port), listen_socket(other.listen_socket), worker(std::move(other.worker)),
          captured_request(std::move(other.captured_request))
    {
        other.listen_socket = kInvalidTestSocket;
    }
    LocalHttpServer& operator=(LocalHttpServer&& other) noexcept
    {
        if (this != &other)
        {
            CloseTestSocket(listen_socket);
            if (worker.joinable())
            {
                worker.join();
            }
            port = other.port;
            listen_socket = other.listen_socket;
            worker = std::move(other.worker);
            captured_request = std::move(other.captured_request);
            other.listen_socket = kInvalidTestSocket;
        }
        return *this;
    }

    ~LocalHttpServer()
    {
        CloseTestSocket(listen_socket);
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
    server.listen_socket = listen_socket;
    server.captured_request = captured_request;
    server.worker = std::thread(
        [listen_socket, response = std::move(response), response_delay_ms, captured_request]()
        {
            sockaddr_in client_addr{};
            socklen_t client_len = static_cast<socklen_t>(sizeof(client_addr));
            TestSocket client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client == kInvalidTestSocket)
            {
                return;
            }

            const std::string request = ReceiveHttpRequest(client);
            if (captured_request)
            {
                *captured_request = request;
            }
            if (response_delay_ms > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms));
            }
            (void)SendAllResponse(client, response);
            CloseTestSocket(client);
        });

    return server;
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string ShellQuote(const std::string& value)
{
#if defined(_WIN32)
    std::string out = "\"";
    for (char ch : value)
    {
        if (ch == '"')
        {
            out += "\\\"";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    for (char ch : value)
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

int DecodeSystemExitCode(int code)
{
#if defined(_WIN32)
    return code;
#else
    if (WIFEXITED(code))
    {
        return WEXITSTATUS(code);
    }
    return code;
#endif
}

CommandResult RunTool(const std::string& exe, const fs::path& root, const std::string& case_name,
                      const std::vector<std::string>& args)
{
    const fs::path out_path = root / (case_name + ".out");
    const fs::path err_path = root / (case_name + ".err");
    std::string command = ShellQuote(exe);
    for (const auto& arg : args)
    {
        command.push_back(' ');
        command += ShellQuote(arg);
    }
    command += " > ";
    command += ShellQuote(out_path.string());
    command += " 2> ";
    command += ShellQuote(err_path.string());

    std::string shell_command = command;
#if defined(_WIN32)
    shell_command = "\"" + command + "\"";
#endif
    const int code = DecodeSystemExitCode(std::system(shell_command.c_str()));
    return CommandResult{code, ReadFile(out_path), ReadFile(err_path), shell_command};
}

[[noreturn]] void Fail(const std::string& message)
{
    std::cerr << message << "\n";
    std::exit(1);
}

void AssertCode(const std::string& case_name, const CommandResult& result, int expected)
{
    if (result.code != expected)
    {
        Fail("[" + case_name + "] expected exit " + std::to_string(expected) + ", got " + std::to_string(result.code) +
             "\ncommand:\n" + result.command + "\nstdout:\n" + result.out + "\nstderr:\n" + result.err);
    }
}

void AssertContains(const std::string& case_name, const std::string& text, const std::string& needle)
{
    if (text.find(needle) == std::string::npos)
    {
        Fail("[" + case_name + "] expected output to contain: " + needle + "\nactual:\n" + text);
    }
}

void AssertNotContains(const std::string& case_name, const std::string& text, const std::string& needle)
{
    if (text.find(needle) != std::string::npos)
    {
        Fail("[" + case_name + "] expected output not to contain: " + needle + "\nactual:\n" + text);
    }
}

void AssertJsonEnvelope(const std::string& case_name, const std::string& text, const std::string& schema, bool ok,
                        int code)
{
    AssertContains(case_name, text, "\"schema\": \"" + schema + "\"");
    AssertContains(case_name, text, "\"schema_version\": 1");
    AssertContains(case_name, text, std::string("\"ok\": ") + (ok ? "true" : "false"));
    AssertContains(case_name, text, "\"code\": " + std::to_string(code));
    AssertContains(case_name, text, "\"issues\":");
    AssertContains(case_name, text, "\"data\":");
}

void AssertHttpDataFields(const std::string& case_name, const std::string& text)
{
    for (const char* field :
         {"command", "manifest", "checked", "passed", "failed", "duration_ms", "checks", "warnings"})
    {
        AssertContains(case_name, text, std::string("\"") + field + "\":");
    }
}

void AssertHttpCheckFields(const std::string& case_name, const std::string& text)
{
    for (const char* field : {"name", "url", "method", "ok", "status", "duration_ms", "error_kind", "message",
                              "expect_status", "body_matched"})
    {
        AssertContains(case_name, text, std::string("\"") + field + "\":");
    }
}

std::string UrlFor(const LocalHttpServer& server, const std::string& path = "/")
{
    return "http://127.0.0.1:" + std::to_string(server.port) + path;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        Fail("usage: toolx_http_cli_contracts TOOLX_HTTP_EXE TEST_ROOT");
    }

    const std::string tool = argv[1];
    const fs::path root = argv[2];
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    auto help = RunTool(tool, root, "help", {"--help"});
    AssertCode("help", help, 0);
    AssertContains("help", help.out, "toolx-http - preflight runtime HTTP endpoints");

    auto missing = RunTool(tool, root, "missing", {"check", "--json"});
    AssertCode("missing", missing, 2);
    AssertContains("missing", missing.out, "\"code\": 2");
    AssertContains("missing", missing.out, "missing required option --url or --manifest");
    AssertJsonEnvelope("missing", missing.out, "toolx.http.result", false, 2);

    auto missing_manifest =
        RunTool(tool, root, "missing-manifest", {"check", "--manifest", (root / "missing.json").string(), "--json"});
    AssertCode("missing-manifest", missing_manifest, 3);
    AssertContains("missing-manifest", missing_manifest.out, "\"code\": 3");
    AssertJsonEnvelope("missing-manifest", missing_manifest.out, "toolx.http.result", false, 3);

    WriteFile(root / "bad-manifest.json", R"({"checks":[{"url":"http://127.0.0.1","unexpected":true}]})");
    auto bad_manifest =
        RunTool(tool, root, "bad-manifest", {"check", "--manifest", (root / "bad-manifest.json").string(), "--json"});
    AssertCode("bad-manifest", bad_manifest, 4);
    AssertContains("bad-manifest", bad_manifest.out, "manifest validation failed");
    AssertJsonEnvelope("bad-manifest", bad_manifest.out, "toolx.http.result", false, 4);

    auto missing_body = RunTool(
        tool, root, "missing-body",
        {"check", "--url", "http://127.0.0.1:1", "--body-file", (root / "missing-body.txt").string(), "--json"});
    AssertCode("missing-body", missing_body, 3);
    AssertContains("missing-body", missing_body.out, "body file not found");
    AssertJsonEnvelope("missing-body", missing_body.out, "toolx.http.result", false, 3);

    {
        auto server =
            StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
        if (!server)
        {
            Fail("failed to start local server");
        }
        auto ok = RunTool(tool, root, "ok", {"check", "--url", UrlFor(*server), "--json"});
        AssertCode("ok", ok, 0);
        AssertContains("ok", ok.out, "\"schema\": \"toolx.http.result\"");
        AssertContains("ok", ok.out, "\"status\": 200");
        AssertContains("ok", ok.out, "\"body_matched\": true");
        AssertJsonEnvelope("ok", ok.out, "toolx.http.result", true, 0);
        AssertHttpDataFields("ok", ok.out);
        AssertHttpCheckFields("ok", ok.out);
    }

    {
        auto server = StartSingleResponseServer(
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 5\r\nConnection: close\r\n\r\nerror");
        if (!server)
        {
            Fail("failed to start local server");
        }
        auto mismatch =
            RunTool(tool, root, "mismatch", {"check", "--url", UrlFor(*server), "--expect-status", "200", "--json"});
        AssertCode("mismatch", mismatch, 4);
        AssertContains("mismatch", mismatch.out, "\"code\": 4");
        AssertContains("mismatch", mismatch.out, "preflight validation failed");
        AssertJsonEnvelope("mismatch", mismatch.out, "toolx.http.result", false, 4);
        AssertHttpDataFields("mismatch", mismatch.out);
        AssertHttpCheckFields("mismatch", mismatch.out);
    }

    {
        auto server =
            StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\nhello ready");
        if (!server)
        {
            Fail("failed to start local server");
        }
        auto body_pass =
            RunTool(tool, root, "body-pass", {"check", "--url", UrlFor(*server), "--expect-body-contains", "ready"});
        AssertCode("body-pass", body_pass, 0);
        AssertContains("body-pass", body_pass.out, "checked=1");
    }

    {
        auto server =
            StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
        if (!server)
        {
            Fail("failed to start local server");
        }
        auto body_fail = RunTool(tool, root, "body-fail",
                                 {"check", "--url", UrlFor(*server), "--expect-body-contains", "missing", "--json"});
        AssertCode("body-fail", body_fail, 4);
        AssertContains("body-fail", body_fail.out, "response body did not contain expected text");
        AssertJsonEnvelope("body-fail", body_fail.out, "toolx.http.result", false, 4);
    }

    {
        auto captured = std::make_shared<std::string>();
        auto server = StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok",
                                                0, captured);
        if (!server)
        {
            Fail("failed to start local server");
        }
        auto post = RunTool(tool, root, "post",
                            {"check", "--url", UrlFor(*server, "/submit"), "--method", "POST", "--header",
                             "X-Test: yes", "--body", "payload", "--json"});
        AssertCode("post", post, 0);
        AssertJsonEnvelope("post", post.out, "toolx.http.result", true, 0);
        AssertHttpDataFields("post", post.out);
        AssertHttpCheckFields("post", post.out);
        AssertContains("post-captured", *captured, "POST /submit");
        AssertContains("post-captured", *captured, "X-Test: yes");
        AssertContains("post-captured", *captured, "payload");
    }

    {
        auto one =
            StartSingleResponseServer("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        auto two = StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nready");
        if (!one || !two)
        {
            Fail("failed to start local server");
        }
        WriteFile(root / "manifest.json", "{\n"
                                          "  \"timeout_ms\": 1000,\n"
                                          "  \"checks\": [\n"
                                          "    {\"name\":\"one\",\"url\":\"" +
                                              UrlFor(*one) +
                                              "\",\"expect_status\":\"204\"},\n"
                                              "    {\"name\":\"two\",\"url\":\"" +
                                              UrlFor(*two) +
                                              "\",\"expect_body_contains\":\"ready\"}\n"
                                              "  ]\n"
                                              "}\n");
        auto manifest =
            RunTool(tool, root, "manifest", {"check", "--manifest", (root / "manifest.json").string(), "--json"});
        AssertCode("manifest", manifest, 0);
        AssertContains("manifest", manifest.out, "\"checked\": 2");
        AssertContains("manifest", manifest.out, "\"passed\": 2");
        AssertJsonEnvelope("manifest", manifest.out, "toolx.http.result", true, 0);
        AssertHttpDataFields("manifest", manifest.out);
        AssertHttpCheckFields("manifest", manifest.out);
    }

    {
        auto captured = std::make_shared<std::string>();
        auto server = StartSingleResponseServer(
            "HTTP/1.1 201 Created\r\nContent-Length: 7\r\nConnection: close\r\n\r\ncreated", 0, captured);
        if (!server)
        {
            Fail("failed to start local server");
        }
        WriteFile(root / "manifest-override.json",
                  "{\n"
                  "  \"headers\": [\"X-Manifest: should-not-be-sent\"],\n"
                  "  \"checks\": [\n"
                  "    {\"name\":\"override\",\"url\":\"" +
                      UrlFor(*server, "/override?token=secret&name=demo") +
                      "\",\"method\":\"GET\",\"headers\":[\"X-Check: yes\"],\"body\":\"manifest-body\","
                      "\"expect_status\":\"500\"}\n"
                      "  ]\n"
                      "}\n");
        auto manifest_override =
            RunTool(tool, root, "manifest-override",
                    {"check", "--manifest", (root / "manifest-override.json").string(), "--method", "POST", "--header",
                     "X-Override: yes", "--expect-status", "201", "--body", "cli-body", "--json"});
        AssertCode("manifest-override", manifest_override, 0);
        AssertJsonEnvelope("manifest-override", manifest_override.out, "toolx.http.result", true, 0);
        AssertHttpDataFields("manifest-override", manifest_override.out);
        AssertHttpCheckFields("manifest-override", manifest_override.out);
        AssertContains("manifest-override", manifest_override.out, "\"method\": \"POST\"");
        AssertContains("manifest-override", manifest_override.out, "\"expect_status\": \"201\"");
        AssertContains("manifest-override", manifest_override.out, "token=***&name=demo");
        AssertNotContains("manifest-override", manifest_override.out, "token=secret");
        AssertContains("manifest-override-captured", *captured, "POST /override?token=secret&name=demo");
        AssertContains("manifest-override-captured", *captured, "X-Override: yes");
        AssertContains("manifest-override-captured", *captured, "X-Check: yes");
        AssertNotContains("manifest-override-captured", *captured, "X-Manifest: should-not-be-sent");
        AssertContains("manifest-override-captured", *captured, "cli-body");
        AssertNotContains("manifest-override-captured", *captured, "manifest-body");
    }

    {
        auto server =
            StartSingleResponseServer("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 300);
        if (!server)
        {
            Fail("failed to start local server");
        }
        auto timeout =
            RunTool(tool, root, "timeout",
                    {"check", "--url", UrlFor(*server), "--timeout-ms", "50", "--connect-timeout-ms", "50", "--json"});
        AssertCode("timeout", timeout, 1);
        AssertContains("timeout", timeout.out, "\"code\": 1");
        AssertContains("timeout", timeout.out, "runtime failed");
        AssertJsonEnvelope("timeout", timeout.out, "toolx.http.result", false, 1);
        AssertHttpDataFields("timeout", timeout.out);
        AssertHttpCheckFields("timeout", timeout.out);
    }

    return 0;
}
