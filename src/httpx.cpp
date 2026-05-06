#include "httpx.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(HTTPX_ENABLE_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#if defined(HTTPX_ENABLE_MBEDTLS)
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

#include "utils.h"

namespace
{

#if defined(_WIN32)
    bool ParseWindowsAddressLiteral(int family, const std::string &text, void *addr)
    {
        sockaddr_storage storage{};
        int storage_size = sizeof(storage);
        std::string mutable_text = text;
        if (WSAStringToAddressA(mutable_text.data(), family, nullptr, reinterpret_cast<sockaddr *>(&storage), &storage_size) != 0)
        {
            return false;
        }

        if (family == AF_INET)
        {
            *static_cast<in_addr *>(addr) = reinterpret_cast<sockaddr_in *>(&storage)->sin_addr;
            return true;
        }
        if (family == AF_INET6)
        {
            *static_cast<in6_addr *>(addr) = reinterpret_cast<sockaddr_in6 *>(&storage)->sin6_addr;
            return true;
        }
        return false;
    }
#endif

    struct ParsedUrl
    {
        std::string scheme;
        std::string host;
        std::uint16_t port{0};
        std::string target;
        bool host_is_ipv6_literal{false};
    };

    struct Endpoint
    {
        sockaddr_storage address{};
        socklen_t length{0};
        int family{AF_UNSPEC};
    };

    struct ParsedHeaderBlock
    {
        httpx::Response response;
        bool has_content_length{false};
        std::size_t content_length{0};
        bool chunked{false};
    };

#if defined(_WIN32)
    using SocketHandle = SOCKET;
    constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    using SocketHandle = int;
    constexpr SocketHandle kInvalidSocket = -1;
#endif

    enum class WaitState
    {
        Ready = 0,
        Timeout,
        Error,
    };

    class ScopedSocket
    {
    public:
        explicit ScopedSocket(SocketHandle handle)
            : handle_(handle)
        {
        }

        ScopedSocket(const ScopedSocket &) = delete;
        ScopedSocket &operator=(const ScopedSocket &) = delete;

        ~ScopedSocket()
        {
            Reset(kInvalidSocket);
        }

        SocketHandle Get() const noexcept
        {
            return handle_;
        }

        SocketHandle Release() noexcept
        {
            const SocketHandle out = handle_;
            handle_ = kInvalidSocket;
            return out;
        }

        void Reset(SocketHandle replacement) noexcept
        {
            if (handle_ == kInvalidSocket)
            {
                handle_ = replacement;
                return;
            }

#if defined(_WIN32)
            closesocket(handle_);
#else
            close(handle_);
#endif
            handle_ = replacement;
        }

    private:
        SocketHandle handle_{kInvalidSocket};
    };

    struct CookieEntry
    {
        std::string name;
        std::string value;
        std::string domain;
        std::string path;
        bool host_only{true};
        bool secure{false};
        bool http_only{false};
        bool has_expires{false};
        std::time_t expires_epoch{0};
    };

    struct PooledConnection
    {
        SocketHandle socket{kInvalidSocket};
        std::chrono::steady_clock::time_point last_used{};
    };

    std::mutex g_client_runtime_mu;
    std::unordered_map<const httpx::Client *, std::vector<CookieEntry>> g_client_cookies;
    std::unordered_map<const httpx::Client *, std::unordered_map<std::string, std::vector<PooledConnection>>> g_client_pools;

    constexpr std::chrono::seconds kPoolIdleTtl{30};

    httpx::Error MakeError(httpx::ErrorKind kind, std::string message, bool retryable = false, int status = 0)
    {
        httpx::Error error;
        error.kind = kind;
        error.message = std::move(message);
        error.retryable = retryable;
        error.http_status = status;
        return error;
    }

    bool IsSensitiveQueryKey(std::string_view key)
    {
        const auto lowered = utils::str::to_lower_ascii(key);
        return lowered == "token" ||
               lowered == "access_token" ||
               lowered == "auth" ||
               lowered == "password" ||
               lowered == "sig" ||
               lowered == "signature";
    }

    std::string ToLower(std::string_view value)
    {
        return utils::str::to_lower_ascii(value);
    }

    std::string Trim(std::string_view value)
    {
        return utils::str::trim(value);
    }

    std::string RequestPathFromTarget(std::string_view target)
    {
        const auto query = target.find('?');
        const std::string_view path = (query == std::string_view::npos) ? target : target.substr(0, query);
        if (path.empty())
        {
            return "/";
        }
        return std::string(path);
    }

    std::string DefaultCookiePath(std::string_view target)
    {
        const std::string path = RequestPathFromTarget(target);
        if (path.empty() || path[0] != '/')
        {
            return "/";
        }

        const auto slash = path.rfind('/');
        if (slash == std::string::npos || slash == 0)
        {
            return "/";
        }
        return path.substr(0, slash);
    }

    bool DomainMatches(std::string_view host, std::string_view cookie_domain, bool host_only)
    {
        if (host_only)
        {
            return ToLower(host) == ToLower(cookie_domain);
        }

        const std::string h = ToLower(host);
        const std::string d = ToLower(cookie_domain);
        if (h == d)
        {
            return true;
        }

        if (h.size() <= d.size())
        {
            return false;
        }

        return h.compare(h.size() - d.size(), d.size(), d) == 0 && h[h.size() - d.size() - 1] == '.';
    }

    bool PathMatches(std::string_view request_path, std::string_view cookie_path)
    {
        if (cookie_path.empty())
        {
            return true;
        }

        if (request_path.size() < cookie_path.size())
        {
            return false;
        }

        if (request_path.compare(0, cookie_path.size(), cookie_path) != 0)
        {
            return false;
        }

        if (request_path.size() == cookie_path.size())
        {
            return true;
        }

        if (cookie_path.back() == '/')
        {
            return true;
        }

        return request_path[cookie_path.size()] == '/';
    }

    bool ParseHttpDate(std::string_view value, std::time_t *out_epoch)
    {
        if (out_epoch == nullptr)
        {
            return false;
        }

        std::tm tm{};
        std::istringstream in{std::string(value)};
        in >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
        if (in.fail())
        {
            return false;
        }

#if defined(_WIN32)
        const std::time_t epoch = _mkgmtime(&tm);
#else
        const std::time_t epoch = timegm(&tm);
#endif
        if (epoch < 0)
        {
            return false;
        }

        *out_epoch = epoch;
        return true;
    }

    std::optional<std::string> FindHeaderValue(const httpx::HeaderList &headers, std::string_view key)
    {
        const std::string needle = ToLower(key);
        for (const auto &h : headers)
        {
            if (ToLower(h.first) == needle)
            {
                return h.second;
            }
        }
        return std::nullopt;
    }

    void RemoveHeader(httpx::HeaderList *headers, std::string_view key)
    {
        if (headers == nullptr)
        {
            return;
        }

        const std::string needle = ToLower(key);
        headers->erase(std::remove_if(headers->begin(),
                                      headers->end(),
                                      [&needle](const auto &kv)
                                      {
                                          return ToLower(kv.first) == needle;
                                      }),
                       headers->end());
    }

    bool IsRedirectStatus(int status_code)
    {
        return status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308;
    }

    std::string ResolveRedirectUrl(const ParsedUrl &base, std::string_view location)
    {
        if (location.find("://") != std::string_view::npos)
        {
            return std::string(location);
        }

        if (!location.empty() && location[0] == '/')
        {
            const bool default_port = (base.scheme == "http" && base.port == 80) ||
                                      (base.scheme == "https" && base.port == 443);
            std::string out = base.scheme + "://";
            if (base.host_is_ipv6_literal)
            {
                out.append("[").append(base.host).append("]");
            }
            else
            {
                out.append(base.host);
            }
            if (!default_port)
            {
                out.push_back(':');
                out.append(std::to_string(base.port));
            }
            out.append(location);
            return out;
        }

        std::string base_path = RequestPathFromTarget(base.target);
        const auto slash = base_path.rfind('/');
        const std::string dir = (slash == std::string::npos) ? "/" : base_path.substr(0, slash + 1);
        return ResolveRedirectUrl(base, dir + std::string(location));
    }

    void PruneExpiredCookies(std::vector<CookieEntry> *jar)
    {
        if (jar == nullptr)
        {
            return;
        }

        const std::time_t now = std::time(nullptr);
        jar->erase(std::remove_if(jar->begin(),
                                  jar->end(),
                                  [now](const CookieEntry &cookie)
                                  {
                                      return cookie.has_expires && cookie.expires_epoch <= now;
                                  }),
                   jar->end());
    }

    std::string BuildCookieHeaderForRequest(const httpx::Client *owner, const ParsedUrl &url)
    {
        std::scoped_lock lock(g_client_runtime_mu);
        auto it = g_client_cookies.find(owner);
        if (it == g_client_cookies.end())
        {
            return "";
        }

        PruneExpiredCookies(&it->second);

        const std::string host = ToLower(url.host);
        const std::string path = RequestPathFromTarget(url.target);
        const bool is_secure = (url.scheme == "https");

        std::string header;
        for (const auto &cookie : it->second)
        {
            if (cookie.secure && !is_secure)
            {
                continue;
            }
            if (!DomainMatches(host, cookie.domain, cookie.host_only))
            {
                continue;
            }
            if (!PathMatches(path, cookie.path))
            {
                continue;
            }

            if (!header.empty())
            {
                header.append("; ");
            }
            header.append(cookie.name).append("=").append(cookie.value);
        }

        return header;
    }

    void StoreSetCookieHeaders(const httpx::Client *owner, const ParsedUrl &url, const httpx::HeaderList &headers)
    {
        std::vector<CookieEntry> parsed;
        parsed.reserve(headers.size());

        for (const auto &h : headers)
        {
            if (ToLower(h.first) != "set-cookie")
            {
                continue;
            }

            const auto parts = utils::str::split(h.second, ';', false);
            if (parts.empty())
            {
                continue;
            }

            const auto nv = Trim(parts[0]);
            const auto eq = nv.find('=');
            if (eq == std::string::npos || eq == 0)
            {
                continue;
            }

            CookieEntry cookie;
            cookie.name = Trim(nv.substr(0, eq));
            cookie.value = std::string(nv.substr(eq + 1));
            cookie.domain = ToLower(url.host);
            cookie.path = DefaultCookiePath(url.target);
            cookie.host_only = true;

            for (std::size_t i = 1; i < parts.size(); ++i)
            {
                const std::string attr = Trim(parts[i]);
                if (attr.empty())
                {
                    continue;
                }

                const auto attr_eq = attr.find('=');
                const std::string key = ToLower(Trim(attr.substr(0, attr_eq)));
                const std::string value = (attr_eq == std::string::npos) ? "" : Trim(attr.substr(attr_eq + 1));

                if (key == "domain")
                {
                    std::string dom = ToLower(value);
                    while (!dom.empty() && dom.front() == '.')
                    {
                        dom.erase(dom.begin());
                    }
                    if (!dom.empty() && DomainMatches(url.host, dom, false))
                    {
                        cookie.domain = dom;
                        cookie.host_only = false;
                    }
                }
                else if (key == "path")
                {
                    cookie.path = value.empty() ? "/" : value;
                }
                else if (key == "secure")
                {
                    cookie.secure = true;
                }
                else if (key == "httponly")
                {
                    cookie.http_only = true;
                }
                else if (key == "max-age")
                {
                    const auto parsed_age = utils::parse::parse_int32(value);
                    if (parsed_age.ok)
                    {
                        cookie.has_expires = true;
                        cookie.expires_epoch = std::time(nullptr) + parsed_age.value;
                    }
                }
                else if (key == "expires")
                {
                    std::time_t expires_epoch = 0;
                    if (ParseHttpDate(value, &expires_epoch))
                    {
                        cookie.has_expires = true;
                        cookie.expires_epoch = expires_epoch;
                    }
                }
            }

            parsed.push_back(std::move(cookie));
        }

        if (parsed.empty())
        {
            return;
        }

        std::scoped_lock lock(g_client_runtime_mu);
        auto &jar = g_client_cookies[owner];
        PruneExpiredCookies(&jar);

        for (const auto &cookie : parsed)
        {
            jar.erase(std::remove_if(jar.begin(),
                                     jar.end(),
                                     [&cookie](const CookieEntry &existing)
                                     {
                                         return existing.name == cookie.name &&
                                                existing.domain == cookie.domain &&
                                                existing.path == cookie.path;
                                     }),
                      jar.end());

            if (cookie.has_expires && cookie.expires_epoch <= std::time(nullptr))
            {
                continue;
            }

            jar.push_back(cookie);
        }
    }

    std::string PoolKey(const ParsedUrl &url)
    {
        return url.scheme + "://" + ToLower(url.host) + ":" + std::to_string(url.port);
    }

    void CloseSocket(SocketHandle socket);

    void PrunePoolLocked(const httpx::Client *owner)
    {
        auto pit = g_client_pools.find(owner);
        if (pit == g_client_pools.end())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        for (auto key_it = pit->second.begin(); key_it != pit->second.end();)
        {
            auto &vec = key_it->second;
            vec.erase(std::remove_if(vec.begin(),
                                     vec.end(),
                                     [now](const PooledConnection &pc)
                                     {
                                         const bool expired = (now - pc.last_used) > kPoolIdleTtl;
                                         if (expired)
                                         {
                                             CloseSocket(pc.socket);
                                         }
                                         return expired;
                                     }),
                      vec.end());

            if (vec.empty())
            {
                key_it = pit->second.erase(key_it);
            }
            else
            {
                ++key_it;
            }
        }

        if (pit->second.empty())
        {
            g_client_pools.erase(pit);
        }
    }

    SocketHandle AcquirePooledSocket(const httpx::Client *owner, const std::string &key)
    {
        std::scoped_lock lock(g_client_runtime_mu);
        PrunePoolLocked(owner);

        auto pit = g_client_pools.find(owner);
        if (pit == g_client_pools.end())
        {
            return kInvalidSocket;
        }

        auto vit = pit->second.find(key);
        if (vit == pit->second.end() || vit->second.empty())
        {
            return kInvalidSocket;
        }

        const auto socket = vit->second.back().socket;
        vit->second.pop_back();
        if (vit->second.empty())
        {
            pit->second.erase(vit);
        }
        return socket;
    }

    void ReleasePooledSocket(const httpx::Client *owner,
                             const std::string &key,
                             SocketHandle socket,
                             const httpx::ClientOptions &options)
    {
        if (socket == kInvalidSocket)
        {
            return;
        }

        std::scoped_lock lock(g_client_runtime_mu);
        PrunePoolLocked(owner);

        auto &pool = g_client_pools[owner];
        auto &bucket = pool[key];
        bucket.push_back({socket, std::chrono::steady_clock::now()});

        while (bucket.size() > options.pool.per_host_limit)
        {
            CloseSocket(bucket.front().socket);
            bucket.erase(bucket.begin());
        }

        std::size_t total = 0;
        for (const auto &kv : pool)
        {
            total += kv.second.size();
        }

        if (total > options.pool.total_limit)
        {
            for (auto it = pool.begin(); it != pool.end() && total > options.pool.total_limit; ++it)
            {
                auto &vec = it->second;
                while (!vec.empty() && total > options.pool.total_limit)
                {
                    CloseSocket(vec.front().socket);
                    vec.erase(vec.begin());
                    --total;
                }
            }
        }
    }

    void ClearClientRuntime(const httpx::Client *owner)
    {
        std::scoped_lock lock(g_client_runtime_mu);

        auto pit = g_client_pools.find(owner);
        if (pit != g_client_pools.end())
        {
            for (auto &kv : pit->second)
            {
                for (const auto &conn : kv.second)
                {
                    CloseSocket(conn.socket);
                }
            }
            g_client_pools.erase(pit);
        }

        g_client_cookies.erase(owner);
    }

    std::string ReadEnv(const char *name)
    {
        if (name == nullptr)
        {
            return "";
        }

        const char *raw = std::getenv(name);
        if (raw == nullptr)
        {
            return "";
        }

        return std::string(raw);
    }

    std::size_t HeaderBytes(const httpx::HeaderList &headers)
    {
        std::size_t total = 0;
        for (const auto &kv : headers)
        {
            total += kv.first.size();
            total += kv.second.size();
            total += 4; // ": " + CRLF
        }
        return total;
    }

    bool ContainsHeader(const httpx::HeaderList &headers, std::string_view key);
    bool IsTlsBackendEnabled();
    const char *ActiveTlsBackendName();
    bool ValidateTlsOptionsForHttps(const httpx::ClientOptions &options, httpx::Error *error);

    void CloseSocket(SocketHandle socket)
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

    int LastSocketError()
    {
#if defined(_WIN32)
        return static_cast<int>(WSAGetLastError());
#else
        return errno;
#endif
    }

    std::string SocketErrorText(int code)
    {
        return "socket_error=" + std::to_string(code);
    }

    bool IsWouldBlockError(int code)
    {
#if defined(_WIN32)
        return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEALREADY;
#else
        return code == EWOULDBLOCK || code == EAGAIN || code == EINPROGRESS || code == EALREADY;
#endif
    }

    bool SetBlockingMode(SocketHandle socket, bool blocking)
    {
#if defined(_WIN32)
        u_long mode = blocking ? 0UL : 1UL;
        return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0)
        {
            return false;
        }

        if (blocking)
        {
            flags &= ~O_NONBLOCK;
        }
        else
        {
            flags |= O_NONBLOCK;
        }
        return fcntl(socket, F_SETFL, flags) == 0;
#endif
    }

    bool EnsureNetworkStackReady(std::string *error_message)
    {
#if defined(_WIN32)
        static std::once_flag once;
        static bool ok = false;
        static std::string message;
        std::call_once(once, []
                       {
                           WSADATA data{};
                           const int rc = WSAStartup(MAKEWORD(2, 2), &data);
                           if (rc != 0)
                           {
                               ok = false;
                               message = "WSAStartup failed: " + std::to_string(rc);
                               return;
                           }
                           ok = true; });

        if (!ok && error_message != nullptr)
        {
            *error_message = message;
        }
        return ok;
#else
        (void)error_message;
        return true;
#endif
    }

    std::uint64_t RemainingMs(std::chrono::steady_clock::time_point deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return 0;
        }

        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    }

    std::uint64_t EffectiveTimeoutMs(std::uint64_t preferred, std::chrono::steady_clock::time_point deadline)
    {
        const auto remaining = RemainingMs(deadline);
        if (remaining == 0)
        {
            return 0;
        }

        if (preferred == 0)
        {
            return remaining;
        }

        return std::min(preferred, remaining);
    }

    WaitState WaitSocketReady(SocketHandle socket,
                              bool want_read,
                              std::uint64_t timeout_ms,
                              int *error_code)
    {
        if (error_code != nullptr)
        {
            *error_code = 0;
        }

        fd_set read_set;
        fd_set write_set;
        fd_set error_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);

        if (want_read)
        {
            FD_SET(socket, &read_set);
        }
        else
        {
            FD_SET(socket, &write_set);
        }
        FD_SET(socket, &error_set);

        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout_ms / 1000u);
        tv.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);

#if defined(_WIN32)
        const int rc = select(0, &read_set, &write_set, &error_set, &tv);
#else
        const int rc = select(socket + 1, &read_set, &write_set, &error_set, &tv);
#endif
        if (rc == 0)
        {
            return WaitState::Timeout;
        }

        if (rc < 0)
        {
            if (error_code != nullptr)
            {
                *error_code = LastSocketError();
            }
            return WaitState::Error;
        }

        if (FD_ISSET(socket, &error_set))
        {
            int so_error = 0;
            socklen_t so_len = static_cast<socklen_t>(sizeof(so_error));
            getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&so_error), &so_len);
            if (error_code != nullptr)
            {
                *error_code = so_error;
            }
            return WaitState::Error;
        }

        return WaitState::Ready;
    }

    httpx::Result<ParsedUrl> ParseUrlInternal(std::string_view url)
    {
        httpx::Result<ParsedUrl> out;
        const auto delimiter = url.find("://");
        if (delimiter == std::string_view::npos)
        {
            out.error = MakeError(httpx::ErrorKind::InvalidUrl, "url must contain scheme delimiter '://'");
            return out;
        }

        ParsedUrl parsed;
        parsed.scheme = ToLower(url.substr(0, delimiter));
        if (parsed.scheme != "http" && parsed.scheme != "https")
        {
            out.error = MakeError(httpx::ErrorKind::UnsupportedScheme, "only http/https schemes are supported");
            return out;
        }

        const std::string_view rest = url.substr(delimiter + 3);
        const auto authority_end = rest.find_first_of("/?#");
        const std::string_view authority = authority_end == std::string_view::npos ? rest : rest.substr(0, authority_end);
        if (authority_end == std::string_view::npos)
        {
            parsed.target = "/";
        }
        else
        {
            parsed.target = std::string(rest.substr(authority_end));
            if (!parsed.target.empty() && parsed.target[0] != '/')
            {
                parsed.target.insert(parsed.target.begin(), '/');
            }
        }

        if (authority.empty())
        {
            out.error = MakeError(httpx::ErrorKind::InvalidUrl, "url authority is empty");
            return out;
        }

        std::string_view host_view;
        std::string_view port_view;

        if (!authority.empty() && authority.front() == '[')
        {
            const auto closing = authority.find(']');
            if (closing == std::string_view::npos)
            {
                out.error = MakeError(httpx::ErrorKind::InvalidUrl, "ipv6 literal host must end with ']'");
                return out;
            }

            parsed.host_is_ipv6_literal = true;
            host_view = authority.substr(1, closing - 1);

            if (closing + 1 < authority.size())
            {
                if (authority[closing + 1] != ':')
                {
                    out.error = MakeError(httpx::ErrorKind::InvalidUrl, "invalid characters after ipv6 host literal");
                    return out;
                }
                port_view = authority.substr(closing + 2);
            }
        }
        else
        {
            const auto colon_pos = authority.rfind(':');
            if (colon_pos != std::string_view::npos)
            {
                if (authority.find(':') != colon_pos)
                {
                    out.error = MakeError(httpx::ErrorKind::InvalidUrl, "ipv6 hosts must use bracket notation");
                    return out;
                }

                host_view = authority.substr(0, colon_pos);
                port_view = authority.substr(colon_pos + 1);
            }
            else
            {
                host_view = authority;
            }
        }

        parsed.host = std::string(host_view);
        if (!port_view.empty())
        {
            const auto parsed_port = utils::parse::parse_int32(port_view);
            if (!parsed_port.ok || parsed_port.value <= 0 || parsed_port.value > 65535)
            {
                out.error = MakeError(httpx::ErrorKind::InvalidUrl, "url port is invalid");
                return out;
            }
            parsed.port = static_cast<std::uint16_t>(parsed_port.value);
        }
        else
        {
            parsed.port = parsed.scheme == "https" ? 443 : 80;
        }

        if (parsed.host.empty())
        {
            out.error = MakeError(httpx::ErrorKind::InvalidUrl, "url host is empty");
            return out;
        }

        out.ok = true;
        out.value = std::move(parsed);
        return out;
    }

    httpx::Result<std::vector<Endpoint>> ResolveEndpoints(const ParsedUrl &url,
                                                          httpx::DnsStrategy dns_strategy)
    {
        httpx::Result<std::vector<Endpoint>> out;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo *head = nullptr;
        const auto service = std::to_string(url.port);
        const int rc = getaddrinfo(url.host.c_str(), service.c_str(), &hints, &head);
        if (rc != 0)
        {
#if defined(_WIN32)
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "getaddrinfo failed: " + std::to_string(rc),
                                  true);
#else
            out.error = MakeError(httpx::ErrorKind::Network,
                                  std::string("getaddrinfo failed: ") + gai_strerror(rc),
                                  true);
#endif
            return out;
        }

        std::vector<Endpoint> original;
        for (addrinfo *it = head; it != nullptr; it = it->ai_next)
        {
            if (it->ai_addr == nullptr || it->ai_addrlen <= 0)
            {
                continue;
            }

            Endpoint endpoint;
            endpoint.family = it->ai_family;
            endpoint.length = static_cast<socklen_t>(it->ai_addrlen);
            std::memcpy(&endpoint.address, it->ai_addr, static_cast<std::size_t>(it->ai_addrlen));
            original.push_back(endpoint);
        }

        freeaddrinfo(head);

        if (original.empty())
        {
            out.error = MakeError(httpx::ErrorKind::Network, "dns resolution returned no endpoints", true);
            return out;
        }

        if (dns_strategy == httpx::DnsStrategy::SystemOrder)
        {
            out.ok = true;
            out.value = std::move(original);
            return out;
        }

        std::vector<Endpoint> ipv6;
        std::vector<Endpoint> ipv4;
        std::vector<Endpoint> others;
        ipv6.reserve(original.size());
        ipv4.reserve(original.size());
        others.reserve(original.size());

        for (auto &ep : original)
        {
            if (ep.family == AF_INET6)
            {
                ipv6.push_back(ep);
            }
            else if (ep.family == AF_INET)
            {
                ipv4.push_back(ep);
            }
            else
            {
                others.push_back(ep);
            }
        }

        std::vector<Endpoint> interleaved;
        interleaved.reserve(original.size());

        const std::size_t max_count = std::max(ipv6.size(), ipv4.size());
        for (std::size_t i = 0; i < max_count; ++i)
        {
            if (i < ipv6.size())
            {
                interleaved.push_back(ipv6[i]);
            }
            if (i < ipv4.size())
            {
                interleaved.push_back(ipv4[i]);
            }
        }

        for (const auto &ep : others)
        {
            interleaved.push_back(ep);
        }

        out.ok = true;
        out.value = std::move(interleaved);
        return out;
    }

    httpx::Result<SocketHandle> ConnectEndpoint(const Endpoint &endpoint,
                                                std::uint64_t connect_timeout_ms)
    {
        httpx::Result<SocketHandle> out;

        SocketHandle socket = ::socket(endpoint.family, SOCK_STREAM, IPPROTO_TCP);
        if (socket == kInvalidSocket)
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "socket create failed: " + SocketErrorText(LastSocketError()),
                                  true);
            return out;
        }

        ScopedSocket guard(socket);

        if (!SetBlockingMode(socket, false))
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "set non-blocking failed: " + SocketErrorText(LastSocketError()),
                                  true);
            return out;
        }

        const int rc = ::connect(socket,
                                 reinterpret_cast<const sockaddr *>(&endpoint.address),
                                 endpoint.length);
        if (rc != 0)
        {
            const int connect_error = LastSocketError();
            if (!IsWouldBlockError(connect_error))
            {
                out.error = MakeError(httpx::ErrorKind::Network,
                                      "connect failed: " + SocketErrorText(connect_error),
                                      true);
                return out;
            }

            int wait_error = 0;
            const auto wait_state = WaitSocketReady(socket, false, connect_timeout_ms, &wait_error);
            if (wait_state == WaitState::Timeout)
            {
                out.error = MakeError(httpx::ErrorKind::Timeout,
                                      "connect timed out",
                                      true);
                return out;
            }
            if (wait_state == WaitState::Error)
            {
                out.error = MakeError(httpx::ErrorKind::Network,
                                      "connect wait failed: " + SocketErrorText(wait_error),
                                      true);
                return out;
            }

            int so_error = 0;
            socklen_t so_len = static_cast<socklen_t>(sizeof(so_error));
            getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&so_error), &so_len);
            if (so_error != 0)
            {
                out.error = MakeError(httpx::ErrorKind::Network,
                                      "connect failed after wait: " + SocketErrorText(so_error),
                                      true);
                return out;
            }
        }

        if (!SetBlockingMode(socket, true))
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "restore blocking mode failed: " + SocketErrorText(LastSocketError()),
                                  true);
            return out;
        }

        out.ok = true;
        out.value = guard.Release();
        return out;
    }

    httpx::Result<SocketHandle> ConnectWithResolvedEndpoints(const ParsedUrl &route,
                                                             const httpx::ClientOptions &options,
                                                             std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<SocketHandle> out;

        const auto endpoints = ResolveEndpoints(route, options.dns_strategy);
        if (!endpoints.ok)
        {
            out.error = endpoints.error;
            return out;
        }

        httpx::Error last_connect_error = MakeError(httpx::ErrorKind::Network, "connect failed", true);
        for (std::size_t i = 0; i < endpoints.value.size(); ++i)
        {
            std::uint64_t per_attempt_timeout = EffectiveTimeoutMs(options.timeout.connect_ms, deadline);
            if (per_attempt_timeout == 0)
            {
                out.error = MakeError(httpx::ErrorKind::Timeout, "connect timed out", true);
                return out;
            }

            if (options.dns_strategy == httpx::DnsStrategy::HappyEyeballs && i + 1 < endpoints.value.size())
            {
                per_attempt_timeout = std::min<std::uint64_t>(per_attempt_timeout, 250u);
            }

            const auto attempt = ConnectEndpoint(endpoints.value[i], per_attempt_timeout);
            if (attempt.ok)
            {
                out.ok = true;
                out.value = attempt.value;
                return out;
            }

            last_connect_error = attempt.error;
        }

        out.error = last_connect_error;
        return out;
    }

    std::string BuildHostHeaderValue(const ParsedUrl &url)
    {
        const bool default_port = (url.scheme == "http" && url.port == 80) ||
                                  (url.scheme == "https" && url.port == 443);
        if (default_port)
        {
            return url.host;
        }

        if (url.host_is_ipv6_literal)
        {
            return "[" + url.host + "]:" + std::to_string(url.port);
        }

        return url.host + ":" + std::to_string(url.port);
    }

    std::string BuildAbsoluteUriForProxyTarget(const ParsedUrl &url)
    {
        std::string out;
        out.reserve(url.target.size() + url.host.size() + 32);
        out.append(url.scheme);
        out.append("://");

        if (url.host_is_ipv6_literal)
        {
            out.push_back('[');
            out.append(url.host);
            out.push_back(']');
        }
        else
        {
            out.append(url.host);
        }

        const bool default_port = (url.scheme == "http" && url.port == 80) ||
                                  (url.scheme == "https" && url.port == 443);
        if (!default_port)
        {
            out.push_back(':');
            out.append(std::to_string(url.port));
        }

        out.append(url.target.empty() ? "/" : url.target);
        return out;
    }

    httpx::Result<std::size_t> RecvSome(SocketHandle socket,
                                        char *buffer,
                                        std::size_t buffer_size,
                                        const httpx::ClientOptions &options,
                                        std::chrono::steady_clock::time_point deadline);

    httpx::Status SendRawBytes(SocketHandle socket,
                               std::string_view payload,
                               const httpx::ClientOptions &options,
                               std::chrono::steady_clock::time_point deadline)
    {
        httpx::Status st;

        std::size_t sent = 0;
        while (sent < payload.size())
        {
            const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
            if (timeout_ms == 0)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Timeout, "write timed out", true);
                return st;
            }

            int wait_error = 0;
            const auto wait_state = WaitSocketReady(socket, false, timeout_ms, &wait_error);
            if (wait_state == WaitState::Timeout)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Timeout, "write timed out", true);
                return st;
            }
            if (wait_state == WaitState::Error)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Network,
                                     "write wait failed: " + SocketErrorText(wait_error),
                                     true);
                return st;
            }

#if defined(_WIN32)
            const int n = ::send(socket,
                                 payload.data() + sent,
                                 static_cast<int>(payload.size() - sent),
                                 0);
#else
            const int n = static_cast<int>(::send(socket,
                                                  payload.data() + sent,
                                                  payload.size() - sent,
                                                  0));
#endif
            if (n <= 0)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Network,
                                     "send failed: " + SocketErrorText(LastSocketError()),
                                     true);
                return st;
            }
            sent += static_cast<std::size_t>(n);
        }

        st.ok = true;
        return st;
    }

    httpx::Result<std::string> RecvExactBytes(SocketHandle socket,
                                              std::size_t bytes,
                                              const httpx::ClientOptions &options,
                                              std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<std::string> out;
        out.value.reserve(bytes);

        while (out.value.size() < bytes)
        {
            char chunk[256] = {0};
            const std::size_t want = std::min<std::size_t>(bytes - out.value.size(), sizeof(chunk));
            const auto received = RecvSome(socket, chunk, want, options, deadline);
            if (!received.ok)
            {
                out.error = received.error;
                return out;
            }
            if (received.value == 0)
            {
                out.error = MakeError(httpx::ErrorKind::Network,
                                      "connection closed during proxy handshake",
                                      true);
                return out;
            }

            out.value.append(chunk, received.value);
        }

        out.ok = true;
        return out;
    }

    bool ParseIpv4Literal(std::string_view host, std::array<std::uint8_t, 4> *out)
    {
        if (out == nullptr)
        {
            return false;
        }

        in_addr addr4{};
        const std::string text(host);
#if defined(_WIN32)
        if (!ParseWindowsAddressLiteral(AF_INET, text, &addr4))
#else
        if (inet_pton(AF_INET, text.c_str(), &addr4) != 1)
#endif
        {
            return false;
        }

        const auto *raw = reinterpret_cast<const std::uint8_t *>(&addr4);
        for (std::size_t i = 0; i < out->size(); ++i)
        {
            (*out)[i] = raw[i];
        }
        return true;
    }

    bool ParseIpv6Literal(std::string_view host, std::array<std::uint8_t, 16> *out)
    {
        if (out == nullptr)
        {
            return false;
        }

        in6_addr addr6{};
        const std::string text(host);
#if defined(_WIN32)
        if (!ParseWindowsAddressLiteral(AF_INET6, text, &addr6))
#else
        if (inet_pton(AF_INET6, text.c_str(), &addr6) != 1)
#endif
        {
            return false;
        }

        const auto *raw = reinterpret_cast<const std::uint8_t *>(&addr6);
        for (std::size_t i = 0; i < out->size(); ++i)
        {
            (*out)[i] = raw[i];
        }
        return true;
    }

    const char *Socks5ReplyText(std::uint8_t code)
    {
        switch (code)
        {
        case 0x00:
            return "succeeded";
        case 0x01:
            return "general failure";
        case 0x02:
            return "connection not allowed by ruleset";
        case 0x03:
            return "network unreachable";
        case 0x04:
            return "host unreachable";
        case 0x05:
            return "connection refused";
        case 0x06:
            return "ttl expired";
        case 0x07:
            return "command not supported";
        case 0x08:
            return "address type not supported";
        default:
            return "unknown error";
        }
    }

    httpx::Status EstablishSocks5NoAuthTunnel(SocketHandle socket,
                                              const ParsedUrl &destination,
                                              const httpx::ClientOptions &options,
                                              std::chrono::steady_clock::time_point deadline)
    {
        auto ProxyStatus = [](std::string message, bool retryable) -> httpx::Status
        {
            httpx::Status st;
            st.ok = false;
            st.error = MakeError(httpx::ErrorKind::Proxy, std::move(message), retryable);
            return st;
        };

        const std::string greeting("\x05\x01\x00", 3);
        const auto greeting_status = SendRawBytes(socket, greeting, options, deadline);
        if (!greeting_status.ok)
        {
            return greeting_status;
        }

        const auto method_reply = RecvExactBytes(socket, 2, options, deadline);
        if (!method_reply.ok)
        {
            return httpx::Status{false, method_reply.error};
        }
        if (static_cast<std::uint8_t>(method_reply.value[0]) != 0x05)
        {
            return ProxyStatus("socks5 proxy sent invalid greeting version", false);
        }
        if (static_cast<std::uint8_t>(method_reply.value[1]) != 0x00)
        {
            return ProxyStatus("socks5 proxy does not allow no-auth method", false);
        }

        std::string connect_request;
        connect_request.reserve(destination.host.size() + 24);
        connect_request.push_back(static_cast<char>(0x05));
        connect_request.push_back(static_cast<char>(0x01));
        connect_request.push_back(static_cast<char>(0x00));

        std::array<std::uint8_t, 4> ip4{};
        std::array<std::uint8_t, 16> ip6{};
        if (ParseIpv4Literal(destination.host, &ip4))
        {
            connect_request.push_back(static_cast<char>(0x01));
            connect_request.append(reinterpret_cast<const char *>(ip4.data()), ip4.size());
        }
        else if (ParseIpv6Literal(destination.host, &ip6))
        {
            connect_request.push_back(static_cast<char>(0x04));
            connect_request.append(reinterpret_cast<const char *>(ip6.data()), ip6.size());
        }
        else
        {
            if (destination.host.empty() || destination.host.size() > 255)
            {
                return ProxyStatus("socks5 destination host length must be in 1..255", false);
            }

            connect_request.push_back(static_cast<char>(0x03));
            connect_request.push_back(static_cast<char>(destination.host.size()));
            connect_request.append(destination.host);
        }

        connect_request.push_back(static_cast<char>((destination.port >> 8) & 0xff));
        connect_request.push_back(static_cast<char>(destination.port & 0xff));

        const auto connect_status = SendRawBytes(socket, connect_request, options, deadline);
        if (!connect_status.ok)
        {
            return connect_status;
        }

        const auto connect_reply = RecvExactBytes(socket, 4, options, deadline);
        if (!connect_reply.ok)
        {
            return httpx::Status{false, connect_reply.error};
        }

        const std::uint8_t version = static_cast<std::uint8_t>(connect_reply.value[0]);
        const std::uint8_t reply = static_cast<std::uint8_t>(connect_reply.value[1]);
        const std::uint8_t atyp = static_cast<std::uint8_t>(connect_reply.value[3]);
        if (version != 0x05)
        {
            return ProxyStatus("socks5 proxy sent invalid connect reply version", false);
        }
        if (reply != 0x00)
        {
            return ProxyStatus(std::string("socks5 connect failed: ") + Socks5ReplyText(reply), true);
        }

        std::size_t address_bytes = 0;
        if (atyp == 0x01)
        {
            address_bytes = 4;
        }
        else if (atyp == 0x04)
        {
            address_bytes = 16;
        }
        else if (atyp == 0x03)
        {
            const auto len_reply = RecvExactBytes(socket, 1, options, deadline);
            if (!len_reply.ok)
            {
                return httpx::Status{false, len_reply.error};
            }
            address_bytes = static_cast<std::uint8_t>(len_reply.value[0]);
        }
        else
        {
            return ProxyStatus("socks5 proxy replied with unsupported address type", false);
        }

        const auto tail_reply = RecvExactBytes(socket, address_bytes + 2, options, deadline);
        if (!tail_reply.ok)
        {
            return httpx::Status{false, tail_reply.error};
        }

        httpx::Status st;
        st.ok = true;
        return st;
    }

    std::string Base64Encode(std::string_view plain)
    {
        static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve(((plain.size() + 2u) / 3u) * 4u);

        std::size_t i = 0;
        while (i + 3u <= plain.size())
        {
            const std::uint32_t block =
                (static_cast<std::uint32_t>(static_cast<unsigned char>(plain[i])) << 16u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(plain[i + 1])) << 8u) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(plain[i + 2]));
            out.push_back(kAlphabet[(block >> 18u) & 0x3fu]);
            out.push_back(kAlphabet[(block >> 12u) & 0x3fu]);
            out.push_back(kAlphabet[(block >> 6u) & 0x3fu]);
            out.push_back(kAlphabet[block & 0x3fu]);
            i += 3u;
        }

        const std::size_t remain = plain.size() - i;
        if (remain == 1u)
        {
            const std::uint32_t block =
                static_cast<std::uint32_t>(static_cast<unsigned char>(plain[i])) << 16u;
            out.push_back(kAlphabet[(block >> 18u) & 0x3fu]);
            out.push_back(kAlphabet[(block >> 12u) & 0x3fu]);
            out.push_back('=');
            out.push_back('=');
        }
        else if (remain == 2u)
        {
            const std::uint32_t block =
                (static_cast<std::uint32_t>(static_cast<unsigned char>(plain[i])) << 16u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(plain[i + 1])) << 8u);
            out.push_back(kAlphabet[(block >> 18u) & 0x3fu]);
            out.push_back(kAlphabet[(block >> 12u) & 0x3fu]);
            out.push_back(kAlphabet[(block >> 6u) & 0x3fu]);
            out.push_back('=');
        }

        return out;
    }

    void MaybeInjectProxyAuthorization(httpx::Request *request, const httpx::ProxyOptions &proxy)
    {
        if (request == nullptr)
        {
            return;
        }

        if ((proxy.username.empty() && proxy.password.empty()) ||
            ContainsHeader(request->headers, "proxy-authorization"))
        {
            return;
        }

        const std::string token = proxy.username + ":" + proxy.password;
        request->headers.push_back({"Proxy-Authorization", "Basic " + Base64Encode(token)});
    }

    std::string EscapeQuoted(std::string_view input)
    {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input)
        {
            if (ch == '\\' || ch == '"')
            {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        return out;
    }

    std::string BuildMultipartBoundary()
    {
        static std::atomic<std::uint64_t> next_id{1};
        const auto seed = next_id.fetch_add(1, std::memory_order_relaxed);
        return "httpx-boundary-" + std::to_string(seed);
    }

    httpx::Result<std::string> BuildMultipartBody(const httpx::Request &request,
                                                  std::string *boundary_out)
    {
        httpx::Result<std::string> out;
        if (boundary_out == nullptr)
        {
            out.error = MakeError(httpx::ErrorKind::Internal, "multipart boundary output is null");
            return out;
        }

        if (request.multipart.empty())
        {
            out.error = MakeError(httpx::ErrorKind::InvalidArgument, "multipart parts are empty");
            return out;
        }

        if (!request.body.empty())
        {
            out.error = MakeError(httpx::ErrorKind::InvalidArgument,
                                  "request.body and request.multipart cannot be used together");
            return out;
        }

        const std::string boundary = BuildMultipartBoundary();
        std::string body;

        for (const auto &part : request.multipart)
        {
            if (part.name.empty())
            {
                out.error = MakeError(httpx::ErrorKind::InvalidArgument,
                                      "multipart part name cannot be empty");
                return out;
            }

            body.append("--");
            body.append(boundary);
            body.append("\r\n");
            body.append("Content-Disposition: form-data; name=\"");
            body.append(EscapeQuoted(part.name));
            body.append("\"");
            if (!part.filename.empty())
            {
                body.append("; filename=\"");
                body.append(EscapeQuoted(part.filename));
                body.append("\"");
            }
            body.append("\r\n");

            const bool is_file = !part.filename.empty();
            const std::string content_type = part.content_type.empty()
                                                 ? (is_file ? "application/octet-stream" : "text/plain; charset=utf-8")
                                                 : part.content_type;
            body.append("Content-Type: ");
            body.append(content_type);
            body.append("\r\n\r\n");

            body.append(part.data);
            body.append("\r\n");
        }

        body.append("--");
        body.append(boundary);
        body.append("--\r\n");

        *boundary_out = boundary;
        out.ok = true;
        out.value = std::move(body);
        return out;
    }

    bool ParseChunkSize(std::string_view line, std::size_t *size_out)
    {
        if (size_out == nullptr)
        {
            return false;
        }

        const auto semicolon = line.find(';');
        const auto size_token = Trim(semicolon == std::string_view::npos ? line : line.substr(0, semicolon));
        if (size_token.empty())
        {
            return false;
        }

        for (const char ch : size_token)
        {
            if (!std::isxdigit(static_cast<unsigned char>(ch)))
            {
                return false;
            }
        }

        char *end = nullptr;
        const auto parsed = std::strtoull(size_token.c_str(), &end, 16);
        if (end == nullptr || *end != '\0')
        {
            return false;
        }

        if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        {
            return false;
        }

        *size_out = static_cast<std::size_t>(parsed);
        return true;
    }

    httpx::Result<std::string> BuildRequestPayload(const httpx::Request &request,
                                                   const ParsedUrl &url,
                                                   const httpx::ClientOptions &options,
                                                   bool use_proxy_absolute_form)
    {
        httpx::Result<std::string> out;

        std::string request_body = request.body;
        std::string multipart_boundary;
        if (!request.multipart.empty())
        {
            const auto multipart = BuildMultipartBody(request, &multipart_boundary);
            if (!multipart.ok)
            {
                out.error = multipart.error;
                return out;
            }
            request_body = multipart.value;
        }

        std::string payload;
        payload.reserve(request_body.size() + 512);

        const std::string request_target = use_proxy_absolute_form
                                               ? BuildAbsoluteUriForProxyTarget(url)
                                               : (url.target.empty() ? "/" : url.target);

        payload.append(httpx::ToString(request.method));
        payload.push_back(' ');
        payload.append(request_target);
        payload.append(" HTTP/1.1\r\n");

        if (!ContainsHeader(request.headers, "host"))
        {
            payload.append("Host: ");
            payload.append(BuildHostHeaderValue(url));
            payload.append("\r\n");
        }

        if (!ContainsHeader(request.headers, "connection"))
        {
            payload.append("Connection: keep-alive\r\n");
        }

        if (!ContainsHeader(request.headers, "accept"))
        {
            payload.append("Accept: */*\r\n");
        }

        if (!multipart_boundary.empty() && !ContainsHeader(request.headers, "content-type"))
        {
            payload.append("Content-Type: multipart/form-data; boundary=");
            payload.append(multipart_boundary);
            payload.append("\r\n");
        }

        if (!ContainsHeader(request.headers, "user-agent") && !options.user_agent.empty())
        {
            payload.append("User-Agent: ");
            payload.append(options.user_agent);
            payload.append("\r\n");
        }

        for (const auto &kv : request.headers)
        {
            payload.append(kv.first);
            payload.append(": ");
            payload.append(kv.second);
            payload.append("\r\n");
        }

        if (!ContainsHeader(request.headers, "content-length"))
        {
            payload.append("Content-Length: ");
            payload.append(std::to_string(request_body.size()));
            payload.append("\r\n");
        }

        payload.append("\r\n");
        payload.append(request_body);

        if (payload.size() > options.limits.max_body_bytes + options.limits.max_header_bytes)
        {
            out.error = MakeError(httpx::ErrorKind::InvalidArgument,
                                  "request payload exceeds configured limit",
                                  false);
            return out;
        }

        out.ok = true;
        out.value = std::move(payload);
        return out;
    }

    httpx::Result<std::size_t> RecvSome(SocketHandle socket,
                                        char *buffer,
                                        std::size_t buffer_size,
                                        const httpx::ClientOptions &options,
                                        std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<std::size_t> out;

        const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
        if (timeout_ms == 0)
        {
            out.error = MakeError(httpx::ErrorKind::Timeout, "read timed out", true);
            return out;
        }

        int wait_error = 0;
        const auto wait_state = WaitSocketReady(socket, true, timeout_ms, &wait_error);
        if (wait_state == WaitState::Timeout)
        {
            out.error = MakeError(httpx::ErrorKind::Timeout, "read timed out", true);
            return out;
        }
        if (wait_state == WaitState::Error)
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "read wait failed: " + SocketErrorText(wait_error),
                                  true);
            return out;
        }

#if defined(_WIN32)
        const int read_size = ::recv(socket, buffer, static_cast<int>(buffer_size), 0);
#else
        const int read_size = static_cast<int>(::recv(socket, buffer, buffer_size, 0));
#endif
        if (read_size < 0)
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "recv failed: " + SocketErrorText(LastSocketError()),
                                  true);
            return out;
        }

        out.ok = true;
        out.value = static_cast<std::size_t>(read_size);
        return out;
    }

    httpx::Status SendAll(SocketHandle socket,
                          std::string_view payload,
                          const httpx::Request &request,
                          const httpx::ClientOptions &options,
                          std::chrono::steady_clock::time_point deadline)
    {
        httpx::Status st;

        std::size_t sent = 0;
        if (request.on_upload_progress)
        {
            request.on_upload_progress(0, static_cast<std::uint64_t>(payload.size()));
        }

        while (sent < payload.size())
        {
            const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
            if (timeout_ms == 0)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Timeout, "write timed out", true);
                return st;
            }

            int wait_error = 0;
            const auto wait_state = WaitSocketReady(socket, false, timeout_ms, &wait_error);
            if (wait_state == WaitState::Timeout)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Timeout, "write timed out", true);
                return st;
            }
            if (wait_state == WaitState::Error)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Network,
                                     "write wait failed: " + SocketErrorText(wait_error),
                                     true);
                return st;
            }

            const std::size_t remain = payload.size() - sent;
#if defined(_WIN32)
            const int n = ::send(socket, payload.data() + sent, static_cast<int>(remain), 0);
#else
            const int n = static_cast<int>(::send(socket, payload.data() + sent, remain, 0));
#endif
            if (n <= 0)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Network,
                                     "send failed: " + SocketErrorText(LastSocketError()),
                                     true);
                return st;
            }

            sent += static_cast<std::size_t>(n);
            if (request.on_upload_progress)
            {
                request.on_upload_progress(static_cast<std::uint64_t>(sent),
                                           static_cast<std::uint64_t>(payload.size()));
            }
        }

        st.ok = true;
        return st;
    }

    httpx::Result<ParsedHeaderBlock> ParseHeaderBlock(std::string_view raw)
    {
        httpx::Result<ParsedHeaderBlock> out;

        const auto first_line_end = raw.find("\r\n");
        if (first_line_end == std::string_view::npos)
        {
            out.error = MakeError(httpx::ErrorKind::Protocol, "response status line is missing");
            return out;
        }

        const std::string status_line(raw.substr(0, first_line_end));
        std::istringstream ss(status_line);
        std::string version;
        ss >> version;
        ss >> out.value.response.status_code;
        std::string reason;
        std::getline(ss, reason);
        out.value.response.reason = Trim(reason);

        if (version.rfind("HTTP/", 0) != 0 || out.value.response.status_code <= 0)
        {
            out.error = MakeError(httpx::ErrorKind::Protocol, "invalid HTTP status line");
            return out;
        }

        std::size_t cursor = first_line_end + 2;
        while (cursor < raw.size())
        {
            const auto line_end = raw.find("\r\n", cursor);
            const auto span_end = (line_end == std::string_view::npos) ? raw.size() : line_end;
            const std::string_view line = raw.substr(cursor, span_end - cursor);
            if (!line.empty())
            {
                const auto colon = line.find(':');
                if (colon == std::string_view::npos)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol, "malformed response header line");
                    return out;
                }

                const std::string key = Trim(line.substr(0, colon));
                const std::string value = Trim(line.substr(colon + 1));
                out.value.response.headers.push_back({key, value});

                const auto lower_key = ToLower(key);
                if (lower_key == "content-length")
                {
                    const auto parsed_length = utils::parse::parse_int32(value);
                    if (!parsed_length.ok || parsed_length.value < 0)
                    {
                        out.error = MakeError(httpx::ErrorKind::Protocol, "invalid content-length value");
                        return out;
                    }
                    out.value.has_content_length = true;
                    out.value.content_length = static_cast<std::size_t>(parsed_length.value);
                }
                else if (lower_key == "transfer-encoding")
                {
                    const auto lower_value = ToLower(value);
                    if (lower_value.find("chunked") != std::string::npos)
                    {
                        out.value.chunked = true;
                    }
                }
            }

            if (line_end == std::string_view::npos)
            {
                break;
            }
            cursor = line_end + 2;
        }

        out.ok = true;
        return out;
    }

    bool AppendBodyChunk(httpx::Response *response,
                         std::string_view chunk,
                         const httpx::Request &request,
                         const httpx::ClientOptions &options,
                         httpx::Error *error)
    {
        if (response == nullptr || error == nullptr)
        {
            return false;
        }

        if (chunk.empty())
        {
            return true;
        }

        if (response->body.size() + chunk.size() > options.limits.max_body_bytes)
        {
            *error = MakeError(httpx::ErrorKind::Protocol, "response body exceeds configured limit");
            return false;
        }

        if (request.on_response_chunk)
        {
            const bool accepted = request.on_response_chunk(chunk);
            if (!accepted)
            {
                *error = MakeError(httpx::ErrorKind::Internal, "response chunk callback aborted stream");
                return false;
            }
        }

        response->body.append(chunk);
        return true;
    }

    httpx::Result<httpx::Response> ReadHttpResponse(SocketHandle socket,
                                                    const httpx::Request &request,
                                                    const httpx::ClientOptions &options,
                                                    std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<httpx::Response> out;

        const std::size_t io_buffer_bytes = std::max<std::size_t>(1024u, options.io_buffer_bytes);
        std::vector<char> io_buffer(io_buffer_bytes, '\0');
        std::string wire;
        wire.reserve(io_buffer_bytes);

        std::size_t header_end = std::string::npos;
        while (header_end == std::string::npos)
        {
            const auto chunk = RecvSome(socket, io_buffer.data(), io_buffer.size(), options, deadline);
            if (!chunk.ok)
            {
                out.error = chunk.error;
                return out;
            }

            if (chunk.value == 0)
            {
                out.error = MakeError(httpx::ErrorKind::Protocol, "connection closed before response headers");
                return out;
            }

            wire.append(io_buffer.data(), chunk.value);
            if (wire.size() > options.limits.max_header_bytes)
            {
                out.error = MakeError(httpx::ErrorKind::Protocol, "response headers exceed configured limit");
                return out;
            }

            header_end = wire.find("\r\n\r\n");
        }

        const auto parsed_headers = ParseHeaderBlock(std::string_view(wire).substr(0, header_end));
        if (!parsed_headers.ok)
        {
            out.error = parsed_headers.error;
            return out;
        }

        out.value = parsed_headers.value.response;

        std::string remainder = wire.substr(header_end + 4);

        if (request.method == httpx::HttpMethod::Head)
        {
            out.ok = true;
            out.value.body.clear();
            return out;
        }

        if (parsed_headers.value.chunked)
        {
            std::string chunk_wire = std::move(remainder);
            std::size_t cursor = 0;

            auto fill_chunk_wire = [&]() -> bool
            {
                const auto chunk = RecvSome(socket, io_buffer.data(), io_buffer.size(), options, deadline);
                if (!chunk.ok)
                {
                    out.error = chunk.error;
                    return false;
                }
                if (chunk.value == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol,
                                          "connection closed during chunked response decode");
                    return false;
                }

                chunk_wire.append(io_buffer.data(), chunk.value);
                if (chunk_wire.size() > options.limits.max_header_bytes + options.limits.max_body_bytes)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol,
                                          "chunked response exceeded configured limits");
                    return false;
                }
                return true;
            };

            for (;;)
            {
                std::size_t line_end = std::string::npos;
                while (line_end == std::string::npos)
                {
                    line_end = chunk_wire.find("\r\n", cursor);
                    if (line_end == std::string::npos)
                    {
                        if (!fill_chunk_wire())
                        {
                            return out;
                        }
                    }
                }

                const auto line = std::string_view(chunk_wire).substr(cursor, line_end - cursor);
                std::size_t chunk_size = 0;
                if (!ParseChunkSize(line, &chunk_size))
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol, "invalid chunk size line");
                    return out;
                }
                cursor = line_end + 2;

                if (chunk_size == 0)
                {
                    for (;;)
                    {
                        std::size_t trailer_end = std::string::npos;
                        while (trailer_end == std::string::npos)
                        {
                            trailer_end = chunk_wire.find("\r\n", cursor);
                            if (trailer_end == std::string::npos)
                            {
                                if (!fill_chunk_wire())
                                {
                                    return out;
                                }
                            }
                        }

                        if (trailer_end == cursor)
                        {
                            out.ok = true;
                            return out;
                        }
                        cursor = trailer_end + 2;
                    }
                }

                const std::size_t need = chunk_size + 2;
                while ((chunk_wire.size() - cursor) < need)
                {
                    if (!fill_chunk_wire())
                    {
                        return out;
                    }
                }

                httpx::Error append_error;
                if (!AppendBodyChunk(&out.value,
                                     std::string_view(chunk_wire).substr(cursor, chunk_size),
                                     request,
                                     options,
                                     &append_error))
                {
                    out.error = append_error;
                    return out;
                }

                if (chunk_wire[cursor + chunk_size] != '\r' || chunk_wire[cursor + chunk_size + 1] != '\n')
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol, "chunk data must end with CRLF");
                    return out;
                }
                cursor += need;

                if (cursor > 64u * 1024u)
                {
                    chunk_wire.erase(0, cursor);
                    cursor = 0;
                }
            }
        }

        if (parsed_headers.value.has_content_length)
        {
            if (parsed_headers.value.content_length > options.limits.max_body_bytes)
            {
                out.error = MakeError(httpx::ErrorKind::Protocol, "response body exceeds configured limit");
                return out;
            }

            const std::size_t take = std::min(parsed_headers.value.content_length, remainder.size());
            httpx::Error append_error;
            if (!AppendBodyChunk(&out.value,
                                 std::string_view(remainder).substr(0, take),
                                 request,
                                 options,
                                 &append_error))
            {
                out.error = append_error;
                return out;
            }

            while (out.value.body.size() < parsed_headers.value.content_length)
            {
                const auto chunk = RecvSome(socket, io_buffer.data(), io_buffer.size(), options, deadline);
                if (!chunk.ok)
                {
                    out.error = chunk.error;
                    return out;
                }
                if (chunk.value == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol,
                                          "connection closed before expected content-length body was complete");
                    return out;
                }

                const auto need = std::min(parsed_headers.value.content_length - out.value.body.size(),
                                           chunk.value);
                httpx::Error append_error_loop;
                if (!AppendBodyChunk(&out.value,
                                     std::string_view(io_buffer.data(), need),
                                     request,
                                     options,
                                     &append_error_loop))
                {
                    out.error = append_error_loop;
                    return out;
                }
            }

            out.ok = true;
            return out;
        }

        if (!remainder.empty())
        {
            httpx::Error append_error;
            if (!AppendBodyChunk(&out.value,
                                 std::string_view(remainder),
                                 request,
                                 options,
                                 &append_error))
            {
                out.error = append_error;
                return out;
            }
        }

        for (;;)
        {
            const auto chunk = RecvSome(socket, io_buffer.data(), io_buffer.size(), options, deadline);
            if (!chunk.ok)
            {
                out.error = chunk.error;
                return out;
            }
            if (chunk.value == 0)
            {
                break;
            }

            httpx::Error append_error_loop;
            if (!AppendBodyChunk(&out.value,
                                 std::string_view(io_buffer.data(), chunk.value),
                                 request,
                                 options,
                                 &append_error_loop))
            {
                out.error = append_error_loop;
                return out;
            }
        }

        out.ok = true;
        return out;
    }

    template <typename RecvFn>
    httpx::Result<httpx::Response> ReadHttpResponseWithCustomRecv(const httpx::Request &request,
                                                                  const httpx::ClientOptions &options,
                                                                  RecvFn recv_fn)
    {
        httpx::Result<httpx::Response> out;

        const std::size_t io_buffer_bytes = std::max<std::size_t>(1024u, options.io_buffer_bytes);
        std::vector<char> io_buffer(io_buffer_bytes, '\0');
        std::string wire;
        wire.reserve(io_buffer_bytes);

        std::size_t header_end = std::string::npos;
        while (header_end == std::string::npos)
        {
            const auto chunk = recv_fn(io_buffer.data(), io_buffer.size());
            if (!chunk.ok)
            {
                out.error = chunk.error;
                return out;
            }

            if (chunk.value == 0)
            {
                out.error = MakeError(httpx::ErrorKind::Protocol, "connection closed before response headers");
                return out;
            }

            wire.append(io_buffer.data(), chunk.value);
            if (wire.size() > options.limits.max_header_bytes)
            {
                out.error = MakeError(httpx::ErrorKind::Protocol, "response headers exceed configured limit");
                return out;
            }

            header_end = wire.find("\r\n\r\n");
        }

        const auto parsed_headers = ParseHeaderBlock(std::string_view(wire).substr(0, header_end));
        if (!parsed_headers.ok)
        {
            out.error = parsed_headers.error;
            return out;
        }

        out.value = parsed_headers.value.response;
        std::string remainder = wire.substr(header_end + 4);

        if (request.method == httpx::HttpMethod::Head)
        {
            out.ok = true;
            out.value.body.clear();
            return out;
        }

        if (parsed_headers.value.chunked)
        {
            std::string chunk_wire = std::move(remainder);
            std::size_t cursor = 0;

            auto fill_chunk_wire = [&]() -> bool
            {
                const auto chunk = recv_fn(io_buffer.data(), io_buffer.size());
                if (!chunk.ok)
                {
                    out.error = chunk.error;
                    return false;
                }
                if (chunk.value == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol,
                                          "connection closed during chunked response decode");
                    return false;
                }

                chunk_wire.append(io_buffer.data(), chunk.value);
                if (chunk_wire.size() > options.limits.max_header_bytes + options.limits.max_body_bytes)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol,
                                          "chunked response exceeded configured limits");
                    return false;
                }
                return true;
            };

            for (;;)
            {
                std::size_t line_end = std::string::npos;
                while (line_end == std::string::npos)
                {
                    line_end = chunk_wire.find("\r\n", cursor);
                    if (line_end == std::string::npos)
                    {
                        if (!fill_chunk_wire())
                        {
                            return out;
                        }
                    }
                }

                const auto line = std::string_view(chunk_wire).substr(cursor, line_end - cursor);
                std::size_t chunk_size = 0;
                if (!ParseChunkSize(line, &chunk_size))
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol, "invalid chunk size line");
                    return out;
                }
                cursor = line_end + 2;

                if (chunk_size == 0)
                {
                    for (;;)
                    {
                        std::size_t trailer_end = std::string::npos;
                        while (trailer_end == std::string::npos)
                        {
                            trailer_end = chunk_wire.find("\r\n", cursor);
                            if (trailer_end == std::string::npos)
                            {
                                if (!fill_chunk_wire())
                                {
                                    return out;
                                }
                            }
                        }

                        if (trailer_end == cursor)
                        {
                            out.ok = true;
                            return out;
                        }
                        cursor = trailer_end + 2;
                    }
                }

                const std::size_t need = chunk_size + 2;
                while ((chunk_wire.size() - cursor) < need)
                {
                    if (!fill_chunk_wire())
                    {
                        return out;
                    }
                }

                httpx::Error append_error;
                if (!AppendBodyChunk(&out.value,
                                     std::string_view(chunk_wire).substr(cursor, chunk_size),
                                     request,
                                     options,
                                     &append_error))
                {
                    out.error = append_error;
                    return out;
                }

                if (chunk_wire[cursor + chunk_size] != '\r' || chunk_wire[cursor + chunk_size + 1] != '\n')
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol, "chunk data must end with CRLF");
                    return out;
                }
                cursor += need;

                if (cursor > 64u * 1024u)
                {
                    chunk_wire.erase(0, cursor);
                    cursor = 0;
                }
            }
        }

        if (parsed_headers.value.has_content_length)
        {
            if (parsed_headers.value.content_length > options.limits.max_body_bytes)
            {
                out.error = MakeError(httpx::ErrorKind::Protocol, "response body exceeds configured limit");
                return out;
            }

            const std::size_t take = std::min(parsed_headers.value.content_length, remainder.size());
            httpx::Error append_error;
            if (!AppendBodyChunk(&out.value,
                                 std::string_view(remainder).substr(0, take),
                                 request,
                                 options,
                                 &append_error))
            {
                out.error = append_error;
                return out;
            }

            while (out.value.body.size() < parsed_headers.value.content_length)
            {
                const auto chunk = recv_fn(io_buffer.data(), io_buffer.size());
                if (!chunk.ok)
                {
                    out.error = chunk.error;
                    return out;
                }
                if (chunk.value == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Protocol,
                                          "connection closed before expected content-length body was complete");
                    return out;
                }

                const auto need = std::min(parsed_headers.value.content_length - out.value.body.size(),
                                           chunk.value);
                httpx::Error append_error_loop;
                if (!AppendBodyChunk(&out.value,
                                     std::string_view(io_buffer.data(), need),
                                     request,
                                     options,
                                     &append_error_loop))
                {
                    out.error = append_error_loop;
                    return out;
                }
            }

            out.ok = true;
            return out;
        }

        if (!remainder.empty())
        {
            httpx::Error append_error;
            if (!AppendBodyChunk(&out.value,
                                 std::string_view(remainder),
                                 request,
                                 options,
                                 &append_error))
            {
                out.error = append_error;
                return out;
            }
        }

        for (;;)
        {
            const auto chunk = recv_fn(io_buffer.data(), io_buffer.size());
            if (!chunk.ok)
            {
                out.error = chunk.error;
                return out;
            }
            if (chunk.value == 0)
            {
                break;
            }

            httpx::Error append_error_loop;
            if (!AppendBodyChunk(&out.value,
                                 std::string_view(io_buffer.data(), chunk.value),
                                 request,
                                 options,
                                 &append_error_loop))
            {
                out.error = append_error_loop;
                return out;
            }
        }

        out.ok = true;
        return out;
    }

    template <typename WriteFn>
    httpx::Status SendAllWithCustomWriter(std::string_view payload,
                                          const httpx::Request &request,
                                          WriteFn write_fn)
    {
        httpx::Status st;

        std::size_t sent = 0;
        if (request.on_upload_progress)
        {
            request.on_upload_progress(0, static_cast<std::uint64_t>(payload.size()));
        }

        while (sent < payload.size())
        {
            const auto wrote = write_fn(payload.data() + sent, payload.size() - sent);
            if (!wrote.ok)
            {
                st.ok = false;
                st.error = wrote.error;
                return st;
            }
            if (wrote.value == 0)
            {
                st.ok = false;
                st.error = MakeError(httpx::ErrorKind::Network, "write returned 0 bytes", true);
                return st;
            }

            sent += wrote.value;
            if (request.on_upload_progress)
            {
                request.on_upload_progress(static_cast<std::uint64_t>(sent),
                                           static_cast<std::uint64_t>(payload.size()));
            }
        }

        st.ok = true;
        return st;
    }

    httpx::Error RewriteProxyError(httpx::Error error, bool using_proxy)
    {
        if (!using_proxy)
        {
            return error;
        }

        if (error.kind == httpx::ErrorKind::Network ||
            error.kind == httpx::ErrorKind::Timeout ||
            error.kind == httpx::ErrorKind::Protocol)
        {
            error.kind = httpx::ErrorKind::Proxy;
            if (error.message.rfind("proxy ", 0) != 0)
            {
                error.message = "proxy " + error.message;
            }
        }

        return error;
    }

#if defined(HTTPX_ENABLE_OPENSSL)
    class OpenSslSession
    {
    public:
        OpenSslSession() = default;
        ~OpenSslSession()
        {
            if (ssl != nullptr)
            {
                SSL_free(ssl);
            }
            if (ctx != nullptr)
            {
                SSL_CTX_free(ctx);
            }
        }

        SSL_CTX *ctx{nullptr};
        SSL *ssl{nullptr};
    };

    bool EnsureOpenSslReady(std::string *error_message)
    {
        static std::once_flag once;
        static bool ready = false;
        static std::string message;

        std::call_once(once, []()
                       {
                           const int rc = OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                                                           nullptr);
                           if (rc != 1)
                           {
                               ready = false;
                               message = "OPENSSL_init_ssl failed";
                               return;
                           }
                           ready = true; });

        if (!ready && error_message != nullptr)
        {
            *error_message = message;
        }
        return ready;
    }

    std::string OpenSslLastErrorText()
    {
        unsigned long code = ERR_get_error();
        if (code == 0)
        {
            return "openssl_error";
        }

        char buffer[256] = {0};
        ERR_error_string_n(code, buffer, sizeof(buffer));
        return std::string(buffer);
    }

    httpx::Result<std::size_t> OpenSslReadSome(SSL *ssl,
                                               SocketHandle socket,
                                               char *buffer,
                                               std::size_t buffer_size,
                                               const httpx::ClientOptions &options,
                                               std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<std::size_t> out;

        for (;;)
        {
            const int rc = SSL_read(ssl, buffer, static_cast<int>(buffer_size));
            if (rc > 0)
            {
                out.ok = true;
                out.value = static_cast<std::size_t>(rc);
                return out;
            }

            const int err = SSL_get_error(ssl, rc);
            if (err == SSL_ERROR_ZERO_RETURN)
            {
                out.ok = true;
                out.value = 0;
                return out;
            }

            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
                if (timeout_ms == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls read timed out", true);
                    return out;
                }

                int wait_error = 0;
                const bool want_read = err == SSL_ERROR_WANT_READ;
                const auto wait_state = WaitSocketReady(socket, want_read, timeout_ms, &wait_error);
                if (wait_state == WaitState::Timeout)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls read timed out", true);
                    return out;
                }
                if (wait_state == WaitState::Error)
                {
                    out.error = MakeError(httpx::ErrorKind::Network,
                                          "tls read wait failed: " + SocketErrorText(wait_error),
                                          true);
                    return out;
                }

                continue;
            }

            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "tls read failed: " + OpenSslLastErrorText(),
                                  true);
            return out;
        }
    }

    httpx::Result<std::size_t> OpenSslWriteSome(SSL *ssl,
                                                SocketHandle socket,
                                                const char *buffer,
                                                std::size_t buffer_size,
                                                const httpx::ClientOptions &options,
                                                std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<std::size_t> out;

        for (;;)
        {
            const int rc = SSL_write(ssl, buffer, static_cast<int>(buffer_size));
            if (rc > 0)
            {
                out.ok = true;
                out.value = static_cast<std::size_t>(rc);
                return out;
            }

            const int err = SSL_get_error(ssl, rc);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
                if (timeout_ms == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls write timed out", true);
                    return out;
                }

                int wait_error = 0;
                const bool want_read = err == SSL_ERROR_WANT_READ;
                const auto wait_state = WaitSocketReady(socket, want_read, timeout_ms, &wait_error);
                if (wait_state == WaitState::Timeout)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls write timed out", true);
                    return out;
                }
                if (wait_state == WaitState::Error)
                {
                    out.error = MakeError(httpx::ErrorKind::Network,
                                          "tls write wait failed: " + SocketErrorText(wait_error),
                                          true);
                    return out;
                }

                continue;
            }

            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "tls write failed: " + OpenSslLastErrorText(),
                                  true);
            return out;
        }
    }

    httpx::Result<httpx::Response> SendByOpenSslTransport(const httpx::Request &request,
                                                          const ParsedUrl &parsed_url,
                                                          const httpx::ClientOptions &options,
                                                          std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<httpx::Response> out;

        std::string init_error;
        if (!EnsureOpenSslReady(&init_error))
        {
            out.error = MakeError(httpx::ErrorKind::Tls, "openssl init failed: " + init_error);
            return out;
        }

        const auto connected = ConnectWithResolvedEndpoints(parsed_url, options, deadline);
        if (!connected.ok)
        {
            out.error = connected.error;
            return out;
        }
        ScopedSocket guard(connected.value);

        if (!SetBlockingMode(guard.Get(), false))
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "set non-blocking failed: " + SocketErrorText(LastSocketError()),
                                  true);
            return out;
        }

        OpenSslSession session;
        session.ctx = SSL_CTX_new(TLS_client_method());
        if (session.ctx == nullptr)
        {
            out.error = MakeError(httpx::ErrorKind::Tls, "SSL_CTX_new failed: " + OpenSslLastErrorText());
            return out;
        }

        const int verify_mode = options.tls.verify_peer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
        SSL_CTX_set_verify(session.ctx, verify_mode, nullptr);

        if (options.tls.verify_peer)
        {
            if (options.tls.ca_file.empty())
            {
                if (SSL_CTX_set_default_verify_paths(session.ctx) != 1)
                {
                    out.error = MakeError(httpx::ErrorKind::Tls,
                                          "load default trust store failed: " + OpenSslLastErrorText());
                    return out;
                }
            }
            else if (SSL_CTX_load_verify_locations(session.ctx, options.tls.ca_file.c_str(), nullptr) != 1)
            {
                out.error = MakeError(httpx::ErrorKind::Tls,
                                      "load tls.ca_file failed: " + OpenSslLastErrorText());
                return out;
            }
        }

        session.ssl = SSL_new(session.ctx);
        if (session.ssl == nullptr)
        {
            out.error = MakeError(httpx::ErrorKind::Tls, "SSL_new failed: " + OpenSslLastErrorText());
            return out;
        }

        if (SSL_set_fd(session.ssl, static_cast<int>(guard.Get())) != 1)
        {
            out.error = MakeError(httpx::ErrorKind::Tls, "SSL_set_fd failed: " + OpenSslLastErrorText());
            return out;
        }

        if (SSL_set_tlsext_host_name(session.ssl, parsed_url.host.c_str()) != 1)
        {
            out.error = MakeError(httpx::ErrorKind::Tls, "set SNI failed: " + OpenSslLastErrorText());
            return out;
        }

        if (options.tls.verify_host)
        {
            X509_VERIFY_PARAM *param = SSL_get0_param(session.ssl);
            if (param == nullptr || X509_VERIFY_PARAM_set1_host(param, parsed_url.host.c_str(), 0) != 1)
            {
                out.error = MakeError(httpx::ErrorKind::Tls, "set hostname verification failed");
                return out;
            }
        }

        for (;;)
        {
            const int rc = SSL_connect(session.ssl);
            if (rc == 1)
            {
                break;
            }

            const int err = SSL_get_error(session.ssl, rc);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                const auto timeout_ms = EffectiveTimeoutMs(options.timeout.connect_ms, deadline);
                if (timeout_ms == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls handshake timed out", true);
                    return out;
                }

                int wait_error = 0;
                const bool want_read = err == SSL_ERROR_WANT_READ;
                const auto wait_state = WaitSocketReady(guard.Get(), want_read, timeout_ms, &wait_error);
                if (wait_state == WaitState::Timeout)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls handshake timed out", true);
                    return out;
                }
                if (wait_state == WaitState::Error)
                {
                    out.error = MakeError(httpx::ErrorKind::Network,
                                          "tls handshake wait failed: " + SocketErrorText(wait_error),
                                          true);
                    return out;
                }

                continue;
            }

            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "tls handshake failed: " + OpenSslLastErrorText(),
                                  true);
            return out;
        }

        if (options.tls.verify_peer)
        {
            const long vr = SSL_get_verify_result(session.ssl);
            if (vr != X509_V_OK)
            {
                out.error = MakeError(httpx::ErrorKind::Tls,
                                      "tls peer verification failed: " + std::to_string(vr));
                return out;
            }
        }

        const auto payload = BuildRequestPayload(request, parsed_url, options, false);
        if (!payload.ok)
        {
            out.error = payload.error;
            return out;
        }

        const auto sent = SendAllWithCustomWriter(payload.value,
                                                  request,
                                                  [&](const char *buf, std::size_t len) -> httpx::Result<std::size_t>
                                                  {
                                                      return OpenSslWriteSome(session.ssl,
                                                                              guard.Get(),
                                                                              buf,
                                                                              len,
                                                                              options,
                                                                              deadline);
                                                  });
        if (!sent.ok)
        {
            out.error = sent.error;
            return out;
        }

        return ReadHttpResponseWithCustomRecv(request,
                                              options,
                                              [&](char *buf, std::size_t len) -> httpx::Result<std::size_t>
                                              {
                                                  return OpenSslReadSome(session.ssl,
                                                                         guard.Get(),
                                                                         buf,
                                                                         len,
                                                                         options,
                                                                         deadline);
                                              });
    }
#endif

#if defined(HTTPX_ENABLE_MBEDTLS)
    class MbedTlsSession
    {
    public:
        MbedTlsSession()
        {
            mbedtls_ssl_init(&ssl);
            mbedtls_ssl_config_init(&conf);
            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&drbg);
            mbedtls_x509_crt_init(&ca);
        }

        ~MbedTlsSession()
        {
            mbedtls_x509_crt_free(&ca);
            mbedtls_ctr_drbg_free(&drbg);
            mbedtls_entropy_free(&entropy);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ssl_free(&ssl);
        }

        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context drbg;
        mbedtls_x509_crt ca;
    };

    std::string MbedTlsErrorText(int code)
    {
        char buffer[256] = {0};
        mbedtls_strerror(code, buffer, sizeof(buffer));
        return std::string(buffer);
    }

    int MbedTlsBioSend(void *ctx, const unsigned char *buf, std::size_t len)
    {
        if (ctx == nullptr)
        {
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }

        auto *socket = reinterpret_cast<SocketHandle *>(ctx);
#if defined(_WIN32)
        const int n = ::send(*socket, reinterpret_cast<const char *>(buf), static_cast<int>(len), 0);
        if (n == SOCKET_ERROR)
        {
#else
        const int n = static_cast<int>(::send(*socket, buf, len, 0));
        if (n < 0)
        {
#endif
            const int err = LastSocketError();
            if (IsWouldBlockError(err))
            {
                return MBEDTLS_ERR_SSL_WANT_WRITE;
            }
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }

        return n;
    }

    int MbedTlsBioRecv(void *ctx, unsigned char *buf, std::size_t len)
    {
        if (ctx == nullptr)
        {
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }

        auto *socket = reinterpret_cast<SocketHandle *>(ctx);
#if defined(_WIN32)
        const int n = ::recv(*socket, reinterpret_cast<char *>(buf), static_cast<int>(len), 0);
        if (n == SOCKET_ERROR)
        {
#else
        const int n = static_cast<int>(::recv(*socket, buf, len, 0));
        if (n < 0)
        {
#endif
            const int err = LastSocketError();
            if (IsWouldBlockError(err))
            {
                return MBEDTLS_ERR_SSL_WANT_READ;
            }
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }

        return n;
    }

    httpx::Result<std::size_t> MbedTlsReadSome(mbedtls_ssl_context *ssl,
                                               SocketHandle socket,
                                               char *buffer,
                                               std::size_t buffer_size,
                                               const httpx::ClientOptions &options,
                                               std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<std::size_t> out;

        for (;;)
        {
            const int rc = mbedtls_ssl_read(ssl,
                                            reinterpret_cast<unsigned char *>(buffer),
                                            buffer_size);
            if (rc > 0)
            {
                out.ok = true;
                out.value = static_cast<std::size_t>(rc);
                return out;
            }

            if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            {
                out.ok = true;
                out.value = 0;
                return out;
            }

            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
                if (timeout_ms == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls read timed out", true);
                    return out;
                }

                int wait_error = 0;
                const bool want_read = rc == MBEDTLS_ERR_SSL_WANT_READ;
                const auto wait_state = WaitSocketReady(socket, want_read, timeout_ms, &wait_error);
                if (wait_state == WaitState::Timeout)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls read timed out", true);
                    return out;
                }
                if (wait_state == WaitState::Error)
                {
                    out.error = MakeError(httpx::ErrorKind::Network,
                                          "tls read wait failed: " + SocketErrorText(wait_error),
                                          true);
                    return out;
                }

                continue;
            }

            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "tls read failed: " + MbedTlsErrorText(rc),
                                  true);
            return out;
        }
    }

    httpx::Result<std::size_t> MbedTlsWriteSome(mbedtls_ssl_context *ssl,
                                                SocketHandle socket,
                                                const char *buffer,
                                                std::size_t buffer_size,
                                                const httpx::ClientOptions &options,
                                                std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<std::size_t> out;

        for (;;)
        {
            const int rc = mbedtls_ssl_write(ssl,
                                             reinterpret_cast<const unsigned char *>(buffer),
                                             buffer_size);
            if (rc > 0)
            {
                out.ok = true;
                out.value = static_cast<std::size_t>(rc);
                return out;
            }

            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                const auto timeout_ms = EffectiveTimeoutMs(options.timeout.read_write_ms, deadline);
                if (timeout_ms == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls write timed out", true);
                    return out;
                }

                int wait_error = 0;
                const bool want_read = rc == MBEDTLS_ERR_SSL_WANT_READ;
                const auto wait_state = WaitSocketReady(socket, want_read, timeout_ms, &wait_error);
                if (wait_state == WaitState::Timeout)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls write timed out", true);
                    return out;
                }
                if (wait_state == WaitState::Error)
                {
                    out.error = MakeError(httpx::ErrorKind::Network,
                                          "tls write wait failed: " + SocketErrorText(wait_error),
                                          true);
                    return out;
                }

                continue;
            }

            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "tls write failed: " + MbedTlsErrorText(rc),
                                  true);
            return out;
        }
    }

    httpx::Result<httpx::Response> SendByMbedTlsTransport(const httpx::Request &request,
                                                          const ParsedUrl &parsed_url,
                                                          const httpx::ClientOptions &options,
                                                          std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<httpx::Response> out;

        const auto connected = ConnectWithResolvedEndpoints(parsed_url, options, deadline);
        if (!connected.ok)
        {
            out.error = connected.error;
            return out;
        }
        ScopedSocket guard(connected.value);

        if (!SetBlockingMode(guard.Get(), false))
        {
            out.error = MakeError(httpx::ErrorKind::Network,
                                  "set non-blocking failed: " + SocketErrorText(LastSocketError()),
                                  true);
            return out;
        }

        MbedTlsSession session;
        const char *personal = "httpx";
        int rc = mbedtls_ctr_drbg_seed(&session.drbg,
                                       mbedtls_entropy_func,
                                       &session.entropy,
                                       reinterpret_cast<const unsigned char *>(personal),
                                       std::strlen(personal));
        if (rc != 0)
        {
            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "mbedtls rng seed failed: " + MbedTlsErrorText(rc));
            return out;
        }

        rc = mbedtls_ssl_config_defaults(&session.conf,
                                         MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc != 0)
        {
            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "mbedtls config defaults failed: " + MbedTlsErrorText(rc));
            return out;
        }

        mbedtls_ssl_conf_rng(&session.conf, mbedtls_ctr_drbg_random, &session.drbg);
        mbedtls_ssl_conf_authmode(&session.conf,
                                  options.tls.verify_peer ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);

        if (options.tls.verify_peer)
        {
            if (options.tls.ca_file.empty())
            {
                out.error = MakeError(httpx::ErrorKind::Tls,
                                      "mbedtls requires tls.ca_file when verify_peer=true");
                return out;
            }

            rc = mbedtls_x509_crt_parse_file(&session.ca, options.tls.ca_file.c_str());
            if (rc < 0)
            {
                out.error = MakeError(httpx::ErrorKind::Tls,
                                      "load tls.ca_file failed: " + MbedTlsErrorText(rc));
                return out;
            }

            mbedtls_ssl_conf_ca_chain(&session.conf, &session.ca, nullptr);
        }

        rc = mbedtls_ssl_setup(&session.ssl, &session.conf);
        if (rc != 0)
        {
            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "mbedtls ssl setup failed: " + MbedTlsErrorText(rc));
            return out;
        }

        if (options.tls.verify_host)
        {
            rc = mbedtls_ssl_set_hostname(&session.ssl, parsed_url.host.c_str());
            if (rc != 0)
            {
                out.error = MakeError(httpx::ErrorKind::Tls,
                                      "mbedtls set hostname failed: " + MbedTlsErrorText(rc));
                return out;
            }
        }

        SocketHandle bio_socket = guard.Get();
        mbedtls_ssl_set_bio(&session.ssl,
                            &bio_socket,
                            MbedTlsBioSend,
                            MbedTlsBioRecv,
                            nullptr);

        for (;;)
        {
            rc = mbedtls_ssl_handshake(&session.ssl);
            if (rc == 0)
            {
                break;
            }

            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                const auto timeout_ms = EffectiveTimeoutMs(options.timeout.connect_ms, deadline);
                if (timeout_ms == 0)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls handshake timed out", true);
                    return out;
                }

                int wait_error = 0;
                const bool want_read = rc == MBEDTLS_ERR_SSL_WANT_READ;
                const auto wait_state = WaitSocketReady(guard.Get(), want_read, timeout_ms, &wait_error);
                if (wait_state == WaitState::Timeout)
                {
                    out.error = MakeError(httpx::ErrorKind::Timeout, "tls handshake timed out", true);
                    return out;
                }
                if (wait_state == WaitState::Error)
                {
                    out.error = MakeError(httpx::ErrorKind::Network,
                                          "tls handshake wait failed: " + SocketErrorText(wait_error),
                                          true);
                    return out;
                }
                continue;
            }

            out.error = MakeError(httpx::ErrorKind::Tls,
                                  "tls handshake failed: " + MbedTlsErrorText(rc),
                                  true);
            return out;
        }

        if (options.tls.verify_peer)
        {
            const std::uint32_t flags = mbedtls_ssl_get_verify_result(&session.ssl);
            if (flags != 0)
            {
                out.error = MakeError(httpx::ErrorKind::Tls,
                                      "tls peer verification failed: " + std::to_string(flags));
                return out;
            }
        }

        const auto payload = BuildRequestPayload(request, parsed_url, options, false);
        if (!payload.ok)
        {
            out.error = payload.error;
            return out;
        }

        const auto sent = SendAllWithCustomWriter(payload.value,
                                                  request,
                                                  [&](const char *buf, std::size_t len) -> httpx::Result<std::size_t>
                                                  {
                                                      return MbedTlsWriteSome(&session.ssl,
                                                                              guard.Get(),
                                                                              buf,
                                                                              len,
                                                                              options,
                                                                              deadline);
                                                  });
        if (!sent.ok)
        {
            out.error = sent.error;
            return out;
        }

        return ReadHttpResponseWithCustomRecv(request,
                                              options,
                                              [&](char *buf, std::size_t len) -> httpx::Result<std::size_t>
                                              {
                                                  return MbedTlsReadSome(&session.ssl,
                                                                         guard.Get(),
                                                                         buf,
                                                                         len,
                                                                         options,
                                                                         deadline);
                                              });
    }
#endif

    httpx::Result<httpx::Response> SendByTlsBackend(const httpx::Request &request,
                                                    const ParsedUrl &parsed_url,
                                                    const httpx::ClientOptions &options,
                                                    std::chrono::steady_clock::time_point deadline)
    {
#if defined(HTTPX_ENABLE_OPENSSL)
        return SendByOpenSslTransport(request, parsed_url, options, deadline);
#elif defined(HTTPX_ENABLE_MBEDTLS)
        return SendByMbedTlsTransport(request, parsed_url, options, deadline);
#else
        (void)request;
        (void)parsed_url;
        (void)options;
        (void)deadline;
        httpx::Result<httpx::Response> out;
        out.error = MakeError(httpx::ErrorKind::Tls,
                              "https requires TLS backend; enable HTTPX_ENABLE_OPENSSL or HTTPX_ENABLE_MBEDTLS");
        return out;
#endif
    }

    httpx::Result<httpx::Response> SendByDefaultTransport(const httpx::Client *owner,
                                                          const httpx::Request &request,
                                                          const httpx::ClientOptions &options,
                                                          std::chrono::steady_clock::time_point deadline)
    {
        httpx::Result<httpx::Response> out;

        std::string startup_error;
        if (!EnsureNetworkStackReady(&startup_error))
        {
            out.error = MakeError(httpx::ErrorKind::Network, startup_error, true);
            return out;
        }

        const auto parsed_url = ParseUrlInternal(request.url);
        if (!parsed_url.ok)
        {
            out.error = parsed_url.error;
            return out;
        }

        if (parsed_url.value.scheme == "https")
        {
            if (!ValidateTlsOptionsForHttps(options, &out.error))
            {
                return out;
            }

            return SendByTlsBackend(request, parsed_url.value, options, deadline);
        }

        bool using_proxy = false;
        bool using_socks5_proxy = false;
        ParsedUrl route = parsed_url.value;
        std::string pool_key = PoolKey(parsed_url.value);
        httpx::Request wire_request = request;

        if (options.proxy.enabled)
        {
            if (options.proxy.host.empty() || options.proxy.port == 0)
            {
                out.error = MakeError(httpx::ErrorKind::Proxy,
                                      "proxy is enabled but host/port is not configured");
                return out;
            }

            const std::string proxy_scheme = options.proxy.scheme.empty() ? "http" : ToLower(options.proxy.scheme);
            if (proxy_scheme != "http" && proxy_scheme != "socks5")
            {
                out.error = MakeError(httpx::ErrorKind::Proxy,
                                      "supported proxy schemes are http and socks5");
                return out;
            }

            if (proxy_scheme == "socks5" &&
                (!options.proxy.username.empty() || !options.proxy.password.empty()))
            {
                out.error = MakeError(httpx::ErrorKind::Proxy,
                                      "socks5 no-auth mode does not accept username/password");
                return out;
            }

            using_proxy = true;
            using_socks5_proxy = (proxy_scheme == "socks5");
            route.scheme = proxy_scheme;
            route.host = options.proxy.host;
            route.port = options.proxy.port;
            route.target = "/";
            route.host_is_ipv6_literal = false;

            if (!route.host.empty() && route.host.front() == '[' && route.host.back() == ']')
            {
                route.host = route.host.substr(1, route.host.size() - 2);
                route.host_is_ipv6_literal = true;
            }
            else if (route.host.find(':') != std::string::npos)
            {
                route.host_is_ipv6_literal = true;
            }

            if (using_socks5_proxy)
            {
                pool_key = "socks5://" + ToLower(route.host) + ":" + std::to_string(route.port) +
                           "->" + ToLower(parsed_url.value.host) + ":" + std::to_string(parsed_url.value.port);
            }
            else
            {
                pool_key = "proxy://" + ToLower(route.host) + ":" + std::to_string(route.port);
                MaybeInjectProxyAuthorization(&wire_request, options.proxy);
            }
        }

        SocketHandle socket = AcquirePooledSocket(owner, pool_key);
        bool fresh_connection = false;
        if (socket == kInvalidSocket)
        {
            const auto connected = ConnectWithResolvedEndpoints(route, options, deadline);
            if (!connected.ok)
            {
                out.error = RewriteProxyError(connected.error, using_proxy);
                return out;
            }
            socket = connected.value;
            fresh_connection = true;
        }

        if (using_socks5_proxy && fresh_connection)
        {
            const auto tunnel_status = EstablishSocks5NoAuthTunnel(socket,
                                                                   parsed_url.value,
                                                                   options,
                                                                   deadline);
            if (!tunnel_status.ok)
            {
                out.ok = false;
                out.error = RewriteProxyError(tunnel_status.error, true);
                return out;
            }
        }

        ScopedSocket guard(socket);

        const bool use_proxy_absolute_form = using_proxy && !using_socks5_proxy;
        const auto payload = BuildRequestPayload(wire_request, parsed_url.value, options, use_proxy_absolute_form);
        if (!payload.ok)
        {
            out.error = payload.error;
            return out;
        }

        const auto sent = SendAll(socket, payload.value, wire_request, options, deadline);
        if (!sent.ok)
        {
            out.error = RewriteProxyError(sent.error, using_proxy);
            return out;
        }

        const auto response = ReadHttpResponse(socket, wire_request, options, deadline);
        if (!response.ok)
        {
            out = response;
            out.error = RewriteProxyError(out.error, using_proxy);
            return out;
        }

        bool keep_alive = true;
        const auto connection_header = FindHeaderValue(response.value.headers, "connection");
        if (connection_header.has_value() && ToLower(*connection_header).find("close") != std::string::npos)
        {
            keep_alive = false;
        }

        const bool has_content_length = FindHeaderValue(response.value.headers, "content-length").has_value();
        const bool has_chunked = [&]()
        {
            const auto te = FindHeaderValue(response.value.headers, "transfer-encoding");
            return te.has_value() && ToLower(*te).find("chunked") != std::string::npos;
        }();

        if (!has_content_length && !has_chunked)
        {
            keep_alive = false;
        }

        if (keep_alive)
        {
            ReleasePooledSocket(owner, pool_key, guard.Release(), options);
        }

        return response;
    }

    bool ParseBoolValue(std::string_view text, bool *out)
    {
        if (out == nullptr)
        {
            return false;
        }

        const auto parsed = utils::parse::parse_bool(text);
        if (!parsed.ok)
        {
            return false;
        }

        *out = parsed.value;
        return true;
    }

    bool ParseSizeValue(std::string_view text, std::size_t *out)
    {
        if (out == nullptr)
        {
            return false;
        }

        const auto parsed = utils::parse::parse_int32(text);
        if (!parsed.ok || parsed.value < 0)
        {
            return false;
        }

        *out = static_cast<std::size_t>(parsed.value);
        return true;
    }

    bool ParseU16Value(std::string_view text, std::uint16_t *out)
    {
        if (out == nullptr)
        {
            return false;
        }

        const auto parsed = utils::parse::parse_int32(text);
        if (!parsed.ok || parsed.value <= 0 || parsed.value > 65535)
        {
            return false;
        }

        *out = static_cast<std::uint16_t>(parsed.value);
        return true;
    }

    bool ParseU64Value(std::string_view text, std::uint64_t *out)
    {
        if (out == nullptr)
        {
            return false;
        }

        const auto parsed = utils::parse::parse_int32(text);
        if (!parsed.ok || parsed.value < 0)
        {
            return false;
        }

        *out = static_cast<std::uint64_t>(parsed.value);
        return true;
    }

    std::optional<httpx::ProxyOptions> ProxyFromEnvironment(std::string_view scheme)
    {
        std::string candidate;
        if (scheme == "https")
        {
            candidate = ReadEnv("HTTPS_PROXY");
            if (candidate.empty())
            {
                candidate = ReadEnv("https_proxy");
            }
        }
        else
        {
            candidate = ReadEnv("HTTP_PROXY");
            if (candidate.empty())
            {
                candidate = ReadEnv("http_proxy");
            }
        }

        if (candidate.empty())
        {
            return std::nullopt;
        }

        candidate = Trim(candidate);
        if (candidate.find("://") == std::string::npos)
        {
            candidate = "http://" + candidate;
        }

        auto parse_proxy_endpoint = [](std::string_view text, httpx::ProxyOptions *out) -> bool
        {
            if (out == nullptr)
            {
                return false;
            }

            const auto delimiter = text.find("://");
            if (delimiter == std::string_view::npos)
            {
                return false;
            }

            const std::string proxy_scheme = ToLower(text.substr(0, delimiter));
            if (proxy_scheme != "http" && proxy_scheme != "socks5")
            {
                return false;
            }

            const std::string_view rest = text.substr(delimiter + 3);
            const auto authority_end = rest.find_first_of("/?#");
            const std::string_view authority = authority_end == std::string_view::npos ? rest : rest.substr(0, authority_end);
            if (authority.empty())
            {
                return false;
            }

            std::string_view host_view;
            std::string_view port_view;
            if (authority.front() == '[')
            {
                const auto closing = authority.find(']');
                if (closing == std::string_view::npos)
                {
                    return false;
                }
                host_view = authority.substr(1, closing - 1);
                if (closing + 1 < authority.size())
                {
                    if (authority[closing + 1] != ':')
                    {
                        return false;
                    }
                    port_view = authority.substr(closing + 2);
                }
            }
            else
            {
                const auto colon_pos = authority.rfind(':');
                if (colon_pos != std::string_view::npos)
                {
                    if (authority.find(':') != colon_pos)
                    {
                        return false;
                    }
                    host_view = authority.substr(0, colon_pos);
                    port_view = authority.substr(colon_pos + 1);
                }
                else
                {
                    host_view = authority;
                }
            }

            if (host_view.empty())
            {
                return false;
            }

            std::uint16_t port = 0;
            if (!port_view.empty())
            {
                const auto parsed_port = utils::parse::parse_int32(port_view);
                if (!parsed_port.ok || parsed_port.value <= 0 || parsed_port.value > 65535)
                {
                    return false;
                }
                port = static_cast<std::uint16_t>(parsed_port.value);
            }
            else
            {
                port = (proxy_scheme == "socks5") ? 1080 : 80;
            }

            out->enabled = true;
            out->scheme = proxy_scheme;
            out->host = std::string(host_view);
            out->port = port;
            return true;
        };

        std::string userinfo;
        const auto scheme_delim = candidate.find("://");
        if (scheme_delim != std::string::npos)
        {
            const std::size_t authority_start = scheme_delim + 3u;
            const auto authority_end = candidate.find_first_of("/?#", authority_start);
            const auto at = candidate.find('@', authority_start);
            if (at != std::string::npos && (authority_end == std::string::npos || at < authority_end))
            {
                userinfo = candidate.substr(authority_start, at - authority_start);
                candidate.erase(authority_start, (at - authority_start) + 1u);
            }
        }

        httpx::ProxyOptions proxy;
        if (!parse_proxy_endpoint(candidate, &proxy))
        {
            return std::nullopt;
        }

        if (!userinfo.empty())
        {
            const auto colon = userinfo.find(':');
            if (colon == std::string::npos)
            {
                proxy.username = userinfo;
            }
            else
            {
                proxy.username = userinfo.substr(0, colon);
                proxy.password = userinfo.substr(colon + 1);
            }
        }

        return proxy;
    }

    bool ContainsHeader(const httpx::HeaderList &headers, std::string_view key)
    {
        const std::string target = ToLower(key);
        for (const auto &kv : headers)
        {
            if (ToLower(kv.first) == target)
            {
                return true;
            }
        }
        return false;
    }

    bool IsTlsBackendEnabled()
    {
#if defined(HTTPX_ENABLE_OPENSSL) || defined(HTTPX_ENABLE_MBEDTLS)
        return true;
#else
        return false;
#endif
    }

    const char *ActiveTlsBackendName()
    {
#if defined(HTTPX_ENABLE_OPENSSL) && defined(HTTPX_ENABLE_MBEDTLS)
        return "openssl+mbedtls";
#elif defined(HTTPX_ENABLE_OPENSSL)
        return "openssl";
#elif defined(HTTPX_ENABLE_MBEDTLS)
        return "mbedtls";
#else
        return "none";
#endif
    }

    bool ValidateTlsOptionsForHttps(const httpx::ClientOptions &options, httpx::Error *error)
    {
        if (error == nullptr)
        {
            return false;
        }

        if (options.tls.verify_host && !options.tls.verify_peer)
        {
            *error = MakeError(httpx::ErrorKind::InvalidArgument,
                               "tls.verify_host=true requires tls.verify_peer=true");
            return false;
        }

        if (!options.tls.ca_file.empty())
        {
            std::ifstream ca_file(options.tls.ca_file, std::ios::binary);
            if (!ca_file.good())
            {
                *error = MakeError(httpx::ErrorKind::Tls,
                                   "tls.ca_file cannot be opened: " + options.tls.ca_file);
                return false;
            }
        }

        return true;
    }

} // namespace

namespace httpx
{

    const char *ToString(HttpMethod method) noexcept
    {
        switch (method)
        {
        case HttpMethod::Get:
            return "GET";
        case HttpMethod::Post:
            return "POST";
        case HttpMethod::Put:
            return "PUT";
        case HttpMethod::Patch:
            return "PATCH";
        case HttpMethod::Delete:
            return "DELETE";
        case HttpMethod::Head:
            return "HEAD";
        case HttpMethod::Options:
            return "OPTIONS";
        case HttpMethod::Connect:
            return "CONNECT";
        case HttpMethod::Trace:
            return "TRACE";
        default:
            return "UNKNOWN";
        }
    }

    const char *ToString(ErrorKind kind) noexcept
    {
        switch (kind)
        {
        case ErrorKind::None:
            return "none";
        case ErrorKind::InvalidArgument:
            return "invalid_argument";
        case ErrorKind::InvalidUrl:
            return "invalid_url";
        case ErrorKind::UnsupportedScheme:
            return "unsupported_scheme";
        case ErrorKind::Timeout:
            return "timeout";
        case ErrorKind::Network:
            return "network";
        case ErrorKind::Proxy:
            return "proxy";
        case ErrorKind::Tls:
            return "tls";
        case ErrorKind::Protocol:
            return "protocol";
        case ErrorKind::Redirect:
            return "redirect";
        case ErrorKind::TransportUnavailable:
            return "transport_unavailable";
        case ErrorKind::Internal:
            return "internal";
        default:
            return "unknown";
        }
    }

    const char *ToString(LogSeverity severity) noexcept
    {
        switch (severity)
        {
        case LogSeverity::Debug:
            return "debug";
        case LogSeverity::Info:
            return "info";
        case LogSeverity::Warning:
            return "warning";
        case LogSeverity::Error:
            return "error";
        default:
            return "unknown";
        }
    }

    std::string RedactUrl(std::string_view url)
    {
        const auto query_pos = url.find('?');
        if (query_pos == std::string_view::npos)
        {
            return std::string(url);
        }

        std::string output(url.substr(0, query_pos));
        output.push_back('?');

        const auto query = url.substr(query_pos + 1);
        const auto parts = utils::str::split(query, '&', false);

        bool first = true;
        for (const auto &part : parts)
        {
            if (!first)
            {
                output.push_back('&');
            }
            first = false;

            const auto eq = part.find('=');
            if (eq == std::string::npos)
            {
                output.append(part);
                continue;
            }

            const std::string key = part.substr(0, eq);
            output.append(key);
            output.push_back('=');
            if (IsSensitiveQueryKey(key))
            {
                output.append("***");
            }
            else
            {
                output.append(part.substr(eq + 1));
            }
        }

        return output;
    }

    std::string RedactHeaderValue(std::string_view key, std::string_view value)
    {
        const auto lower_key = ToLower(key);
        if (lower_key == "authorization" || lower_key == "cookie" || lower_key == "set-cookie")
        {
            return "***";
        }
        return std::string(value);
    }

    Result<ClientOptions> ParseOptionsFromFlatConfig(const std::vector<FlatConfigEntry> &entries)
    {
        Result<ClientOptions> out;
        out.ok = true;

        for (const auto &entry : entries)
        {
            const std::string key = ToLower(Trim(entry.key));
            const std::string value = Trim(entry.value);

            if (key.empty())
            {
                out.ok = false;
                out.error = MakeError(ErrorKind::InvalidArgument, "config key cannot be empty");
                return out;
            }

            if (key == "httpx.timeout.connect_ms")
            {
                if (!ParseU64Value(value, &out.value.timeout.connect_ms))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid connect timeout value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.timeout.read_write_ms")
            {
                if (!ParseU64Value(value, &out.value.timeout.read_write_ms))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid read/write timeout value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.timeout.total_ms")
            {
                if (!ParseU64Value(value, &out.value.timeout.total_ms))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid total timeout value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.redirects.follow")
            {
                if (!ParseBoolValue(value, &out.value.redirects.follow))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid redirects.follow value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.redirects.max_hops")
            {
                if (!ParseSizeValue(value, &out.value.redirects.max_hops))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid redirects.max_hops value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.proxy.enabled")
            {
                if (!ParseBoolValue(value, &out.value.proxy.enabled))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid proxy.enabled value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.proxy.scheme")
            {
                out.value.proxy.scheme = ToLower(value);
                continue;
            }

            if (key == "httpx.proxy.host")
            {
                out.value.proxy.host = value;
                continue;
            }

            if (key == "httpx.proxy.port")
            {
                if (!ParseU16Value(value, &out.value.proxy.port))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid proxy.port value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.proxy.username")
            {
                out.value.proxy.username = value;
                continue;
            }

            if (key == "httpx.proxy.password")
            {
                out.value.proxy.password = value;
                continue;
            }

            if (key == "httpx.tls.verify_peer")
            {
                if (!ParseBoolValue(value, &out.value.tls.verify_peer))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid tls.verify_peer value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.tls.verify_host")
            {
                if (!ParseBoolValue(value, &out.value.tls.verify_host))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid tls.verify_host value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.tls.ca_file")
            {
                out.value.tls.ca_file = value;
                continue;
            }

            if (key == "httpx.pool.per_host_limit")
            {
                if (!ParseSizeValue(value, &out.value.pool.per_host_limit))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid pool.per_host_limit value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.pool.total_limit")
            {
                if (!ParseSizeValue(value, &out.value.pool.total_limit))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid pool.total_limit value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.dns.strategy")
            {
                const auto lowered = ToLower(value);
                if (lowered == "system" || lowered == "system_order")
                {
                    out.value.dns_strategy = DnsStrategy::SystemOrder;
                }
                else if (lowered == "happy" || lowered == "happy-eyeballs" || lowered == "happy_eyeballs")
                {
                    out.value.dns_strategy = DnsStrategy::HappyEyeballs;
                }
                else
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid dns.strategy value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.io_buffer_bytes")
            {
                if (!ParseSizeValue(value, &out.value.io_buffer_bytes) || out.value.io_buffer_bytes == 0)
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid io_buffer_bytes value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.limits.max_header_bytes")
            {
                if (!ParseSizeValue(value, &out.value.limits.max_header_bytes))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid limits.max_header_bytes value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.limits.max_body_bytes")
            {
                if (!ParseSizeValue(value, &out.value.limits.max_body_bytes))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid limits.max_body_bytes value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.user_agent")
            {
                out.value.user_agent = value;
                continue;
            }

            if (key == "httpx.redact_sensitive_data")
            {
                if (!ParseBoolValue(value, &out.value.redact_sensitive_data))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid redact_sensitive_data value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.use_proxy_from_environment")
            {
                if (!ParseBoolValue(value, &out.value.use_proxy_from_environment))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid use_proxy_from_environment value");
                    return out;
                }
                continue;
            }

            if (key == "httpx.retry.max_attempts")
            {
                if (!ParseSizeValue(value, &out.value.max_retry_attempts))
                {
                    out.ok = false;
                    out.error = MakeError(ErrorKind::InvalidArgument, "invalid retry.max_attempts value");
                    return out;
                }
                continue;
            }

            out.ok = false;
            out.error = MakeError(ErrorKind::InvalidArgument, "unknown config key: " + key);
            return out;
        }

        return out;
    }

    Client::Client(ClientOptions options)
        : options_(std::move(options))
    {
    }

    Client::~Client()
    {
        ClearClientRuntime(this);
    }

    Result<Response> Client::Send(const Request &request)
    {
        const auto started_at = std::chrono::steady_clock::now();
        ClientOptions effective;
        {
            std::scoped_lock lock(mu_);
            effective = options_;
        }
        const auto deadline = started_at + std::chrono::milliseconds(effective.timeout.total_ms);

        if (request.url.empty())
        {
            Result<Response> out;
            out.error = MakeError(ErrorKind::InvalidArgument, "request url cannot be empty");
            EmitLog(request, out, 0, effective, LogSeverity::Error);

            std::scoped_lock lock(mu_);
            ++stats_.total_requests;
            ++stats_.total_failures;
            ++stats_.consecutive_failures;
            return out;
        }

        const auto parsed_url = ParseUrlInternal(request.url);
        if (!parsed_url.ok)
        {
            Result<Response> out;
            out.error = parsed_url.error;
            EmitLog(request, out, 0, effective, LogSeverity::Error);

            std::scoped_lock lock(mu_);
            ++stats_.total_requests;
            ++stats_.total_failures;
            ++stats_.consecutive_failures;
            return out;
        }

        if (request.body.size() > effective.limits.max_body_bytes)
        {
            Result<Response> out;
            out.error = MakeError(ErrorKind::InvalidArgument, "request body exceeds configured limit");
            EmitLog(request, out, 0, effective, LogSeverity::Error);

            std::scoped_lock lock(mu_);
            ++stats_.total_requests;
            ++stats_.total_failures;
            ++stats_.consecutive_failures;
            return out;
        }

        if (HeaderBytes(request.headers) > effective.limits.max_header_bytes)
        {
            Result<Response> out;
            out.error = MakeError(ErrorKind::InvalidArgument, "request headers exceed configured limit");
            EmitLog(request, out, 0, effective, LogSeverity::Error);

            std::scoped_lock lock(mu_);
            ++stats_.total_requests;
            ++stats_.total_failures;
            ++stats_.consecutive_failures;
            return out;
        }

        if (!effective.proxy.enabled && effective.use_proxy_from_environment)
        {
            const auto env_proxy = ProxyFromEnvironment(parsed_url.value.scheme);
            if (env_proxy.has_value())
            {
                effective.proxy = *env_proxy;
            }
        }

        Result<Response> last;
        std::size_t attempt = 0;
        const std::size_t max_attempt = effective.max_retry_attempts;
        for (;;)
        {
            if (RemainingMs(deadline) == 0)
            {
                last.ok = false;
                last.error = MakeError(ErrorKind::Timeout, "request exceeded total timeout budget", true);
                break;
            }

            last = SendOnce(request, effective, deadline);
            if (last.ok)
            {
                break;
            }

            const bool can_retry = attempt < max_attempt;
            const bool should_retry = can_retry && effective.should_retry && effective.should_retry(last.error, attempt + 1);
            if (!should_retry)
            {
                break;
            }
            ++attempt;
        }

        const auto duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count());

        if (request.method == HttpMethod::Head && last.ok)
        {
            last.value.body.clear();
        }

        {
            std::scoped_lock lock(mu_);
            ++stats_.total_requests;
            if (last.ok)
            {
                stats_.consecutive_failures = 0;
            }
            else
            {
                ++stats_.total_failures;
                ++stats_.consecutive_failures;
            }
        }

        EmitLog(request,
                last,
                duration_ms,
                effective,
                last.ok ? LogSeverity::Info : LogSeverity::Error);

        return last;
    }

    Result<Response> Client::Get(std::string url, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Get;
        req.url = std::move(url);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Delete(std::string url, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Delete;
        req.url = std::move(url);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Head(std::string url, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Head;
        req.url = std::move(url);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Options(std::string url, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Options;
        req.url = std::move(url);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Connect(std::string url, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Connect;
        req.url = std::move(url);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Trace(std::string url, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Trace;
        req.url = std::move(url);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Post(std::string url, std::string body, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Post;
        req.url = std::move(url);
        req.body = std::move(body);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Put(std::string url, std::string body, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Put;
        req.url = std::move(url);
        req.body = std::move(body);
        req.headers = std::move(headers);
        return Send(req);
    }

    Result<Response> Client::Patch(std::string url, std::string body, HeaderList headers)
    {
        Request req;
        req.method = HttpMethod::Patch;
        req.url = std::move(url);
        req.body = std::move(body);
        req.headers = std::move(headers);
        return Send(req);
    }

    Status Client::ApplyFlatConfig(const std::vector<FlatConfigEntry> &entries)
    {
        const auto parsed = ParseOptionsFromFlatConfig(entries);

        Status st;
        if (!parsed.ok)
        {
            st.ok = false;
            st.error = parsed.error;
            return st;
        }

        std::scoped_lock lock(mu_);
        options_.timeout = parsed.value.timeout;
        options_.redirects = parsed.value.redirects;
        options_.proxy = parsed.value.proxy;
        options_.pool = parsed.value.pool;
        options_.tls = parsed.value.tls;
        options_.limits = parsed.value.limits;
        options_.dns_strategy = parsed.value.dns_strategy;
        options_.io_buffer_bytes = parsed.value.io_buffer_bytes;
        options_.user_agent = std::move(parsed.value.user_agent);
        options_.redact_sensitive_data = parsed.value.redact_sensitive_data;
        options_.use_proxy_from_environment = parsed.value.use_proxy_from_environment;
        options_.max_retry_attempts = parsed.value.max_retry_attempts;

        st.ok = true;
        return st;
    }

    const ClientOptions &Client::Options() const noexcept
    {
        return options_;
    }

    FailureStats Client::GetFailureStats() const
    {
        std::scoped_lock lock(mu_);
        return stats_;
    }

    void Client::ResetFailureStats()
    {
        std::scoped_lock lock(mu_);
        stats_ = {};
    }

    Result<Response> Client::SendOnce(const Request &request,
                                      const ClientOptions &effective_options,
                                      std::chrono::steady_clock::time_point deadline)
    {
        Request current = request;
        const bool follow_redirect = current.follow_redirect.value_or(effective_options.redirects.follow);
        const std::size_t max_redirects = current.max_redirects.value_or(effective_options.redirects.max_hops);

        std::unordered_set<std::string> visited_redirects;
        visited_redirects.insert(current.url);

        for (std::size_t redirect_hops = 0;; ++redirect_hops)
        {
            const auto parsed_current = ParseUrlInternal(current.url);
            if (!parsed_current.ok)
            {
                Result<Response> bad;
                bad.error = parsed_current.error;
                return bad;
            }

            Request normalized = current;
            if (!ContainsHeader(normalized.headers, "user-agent") && !effective_options.user_agent.empty())
            {
                normalized.headers.push_back({"User-Agent", effective_options.user_agent});
            }

            if (!ContainsHeader(normalized.headers, "cookie"))
            {
                const std::string cookie_header = BuildCookieHeaderForRequest(this, parsed_current.value);
                if (!cookie_header.empty())
                {
                    normalized.headers.push_back({"Cookie", cookie_header});
                }
            }

            Result<Response> out;
            if (effective_options.transport)
            {
                out = effective_options.transport(normalized, effective_options);
            }
            else
            {
                out = SendByDefaultTransport(this, normalized, effective_options, deadline);
            }

            if (!out.ok)
            {
                return out;
            }

            StoreSetCookieHeaders(this, parsed_current.value, out.value.headers);

            if (HeaderBytes(out.value.headers) > effective_options.limits.max_header_bytes)
            {
                Result<Response> rejected;
                rejected.error = MakeError(ErrorKind::Protocol, "response headers exceed configured limit");
                return rejected;
            }

            if (out.value.body.size() > effective_options.limits.max_body_bytes)
            {
                Result<Response> rejected;
                rejected.error = MakeError(ErrorKind::Protocol, "response body exceeds configured limit");
                return rejected;
            }

            if (!follow_redirect || !IsRedirectStatus(out.value.status_code))
            {
                return out;
            }

            if (redirect_hops >= max_redirects)
            {
                Result<Response> too_many;
                too_many.error = MakeError(ErrorKind::Redirect, "redirect hops exceeded configured limit");
                return too_many;
            }

            const auto location = FindHeaderValue(out.value.headers, "location");
            if (!location.has_value() || location->empty())
            {
                Result<Response> missing;
                missing.error = MakeError(ErrorKind::Redirect, "redirect response missing Location header");
                return missing;
            }

            const std::string next_url = ResolveRedirectUrl(parsed_current.value, *location);
            if (next_url.empty())
            {
                Result<Response> invalid;
                invalid.error = MakeError(ErrorKind::Redirect, "cannot resolve redirect location");
                return invalid;
            }

            if (!visited_redirects.insert(next_url).second)
            {
                Result<Response> loop;
                loop.error = MakeError(ErrorKind::Redirect, "redirect loop detected");
                return loop;
            }

            const auto parsed_next = ParseUrlInternal(next_url);
            if (!parsed_next.ok)
            {
                Result<Response> invalid;
                invalid.error = MakeError(ErrorKind::Redirect,
                                          "redirect target is invalid: " + parsed_next.error.message);
                return invalid;
            }

            const bool cross_origin = ToLower(parsed_current.value.scheme) != ToLower(parsed_next.value.scheme) ||
                                      ToLower(parsed_current.value.host) != ToLower(parsed_next.value.host) ||
                                      parsed_current.value.port != parsed_next.value.port;
            if (cross_origin)
            {
                RemoveHeader(&current.headers, "authorization");
                RemoveHeader(&current.headers, "cookie");
            }

            if (out.value.status_code == 303 && current.method != HttpMethod::Head)
            {
                current.method = HttpMethod::Get;
                current.body.clear();
                current.multipart.clear();
                RemoveHeader(&current.headers, "content-length");
                RemoveHeader(&current.headers, "content-type");
            }

            current.url = next_url;
            current.follow_redirect.reset();
            current.max_redirects.reset();
        }
    }

    void Client::EmitLog(const Request &request,
                         const Result<Response> &result,
                         std::uint64_t duration_ms,
                         const ClientOptions &effective_options,
                         LogSeverity severity) const
    {
        if (!effective_options.logger)
        {
            return;
        }

        LogEvent event;
        event.severity = severity;
        event.method = ToString(request.method);
        event.url = effective_options.redact_sensitive_data ? RedactUrl(request.url) : request.url;
        event.status_code = result.ok ? result.value.status_code : result.error.http_status;
        event.error_kind = result.ok ? ErrorKind::None : result.error.kind;
        event.retryable = result.ok ? false : result.error.retryable;
        event.duration_ms = duration_ms;
        event.message = result.ok ? "request completed" : result.error.message;

        effective_options.logger(event);
    }

} // namespace httpx
