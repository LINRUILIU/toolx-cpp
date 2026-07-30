#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

namespace
{
#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

void CloseSocket(Socket socket)
{
    if (socket == kInvalidSocket)
    {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool SendAll(Socket socket, const std::string& response)
{
    std::size_t sent = 0;
    while (sent < response.size())
    {
#if defined(_WIN32)
        const int count = ::send(socket, response.data() + sent, static_cast<int>(response.size() - sent), 0);
#else
        const int count = static_cast<int>(::send(socket, response.data() + sent, response.size() - sent, 0));
#endif
        if (count <= 0)
        {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

std::string ParsePortFile(int argc, const char* const argv[])
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--port-file" && index + 1 < argc)
        {
            return argv[index + 1];
        }
    }
    return {};
}
} // namespace

int main(int argc, const char* const argv[])
{
    const std::string port_file = ParsePortFile(argc, argv);
    if (port_file.empty())
    {
        std::cerr << "usage: toolx_showcase_server --port-file FILE\n";
        return 2;
    }

#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        std::cerr << "failed to initialize Winsock\n";
        return 1;
    }
#endif

    Socket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket)
    {
        std::cerr << "failed to create loopback socket\n";
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(listener, 1) != 0)
    {
        std::cerr << "failed to bind loopback socket\n";
        CloseSocket(listener);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int bound_length = static_cast<int>(sizeof(bound));
#else
    socklen_t bound_length = static_cast<socklen_t>(sizeof(bound));
#endif
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_length) != 0)
    {
        std::cerr << "failed to inspect loopback port\n";
        CloseSocket(listener);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    const auto port_path = std::filesystem::path(port_file);
    std::error_code ec;
    if (port_path.has_parent_path())
    {
        std::filesystem::create_directories(port_path.parent_path(), ec);
    }
    std::ofstream port_out(port_path, std::ios::binary | std::ios::trunc);
    if (!port_out)
    {
        std::cerr << "failed to write port file\n";
        CloseSocket(listener);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }
    port_out << ntohs(bound.sin_port) << '\n';
    port_out.close();

    sockaddr_in client_address{};
#if defined(_WIN32)
    int client_length = static_cast<int>(sizeof(client_address));
#else
    socklen_t client_length = static_cast<socklen_t>(sizeof(client_address));
#endif
    Socket client = ::accept(listener, reinterpret_cast<sockaddr*>(&client_address), &client_length);
    CloseSocket(listener);
    if (client == kInvalidSocket)
    {
        std::cerr << "failed to accept loopback request\n";
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    char request_buffer[4096] = {0};
#if defined(_WIN32)
    (void)::recv(client, request_buffer, static_cast<int>(sizeof(request_buffer)), 0);
#else
    (void)::recv(client, request_buffer, sizeof(request_buffer), 0);
#endif

    const std::string body = R"({"status":"ready","service":"toolx-showcase"})";
    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    const bool sent = SendAll(client, response);
    CloseSocket(client);

#if defined(_WIN32)
    WSACleanup();
#endif
    return sent ? 0 : 1;
}
