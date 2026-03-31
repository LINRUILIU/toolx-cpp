#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace httpx
{

    enum class HttpMethod
    {
        Get = 0,
        Post,
        Put,
        Patch,
        Delete,
        Head,
        Options,
        Connect,
        Trace,
    };

    enum class ErrorKind
    {
        None = 0,
        InvalidArgument,
        InvalidUrl,
        UnsupportedScheme,
        Timeout,
        Network,
        Proxy,
        Tls,
        Protocol,
        Redirect,
        TransportUnavailable,
        Internal,
    };

    enum class LogSeverity
    {
        Debug = 0,
        Info,
        Warning,
        Error,
    };

    enum class DnsStrategy
    {
        SystemOrder = 0,
        HappyEyeballs,
    };

    struct Error
    {
        ErrorKind kind{ErrorKind::None};
        int http_status{0};
        bool retryable{false};
        std::string message;
    };

    struct Status
    {
        bool ok{false};
        Error error{};
    };

    template <typename T>
    struct Result
    {
        bool ok{false};
        T value{};
        Error error{};
    };

    using HeaderList = std::vector<std::pair<std::string, std::string>>;

    struct TimeoutOptions
    {
        std::uint64_t connect_ms{5000};
        std::uint64_t read_write_ms{30000};
        std::uint64_t total_ms{60000};
    };

    struct RedirectOptions
    {
        bool follow{false};
        std::size_t max_hops{5};
    };

    struct ProxyOptions
    {
        bool enabled{false};
        std::string scheme;
        std::string host;
        std::uint16_t port{0};
        std::string username;
        std::string password;
    };

    struct PoolOptions
    {
        std::size_t per_host_limit{8};
        std::size_t total_limit{128};
    };

    struct TlsOptions
    {
        bool verify_peer{true};
        bool verify_host{true};
        std::string ca_file;
    };

    struct Limits
    {
        std::size_t max_header_bytes{64u * 1024u};
        std::size_t max_body_bytes{32u * 1024u * 1024u};
    };

    struct MultipartPart
    {
        std::string name;
        std::string filename;
        std::string content_type;
        std::string data;
    };

    struct Request
    {
        HttpMethod method{HttpMethod::Get};
        std::string url;
        HeaderList headers;
        std::string body;
        std::vector<MultipartPart> multipart;
        std::optional<bool> follow_redirect;
        std::optional<std::size_t> max_redirects;
        std::function<bool(std::string_view chunk)> on_response_chunk;
        std::function<void(std::uint64_t sent_bytes, std::uint64_t total_bytes)> on_upload_progress;
    };

    struct Response
    {
        int status_code{0};
        std::string reason;
        HeaderList headers;
        std::string body;
    };

    struct FailureStats
    {
        std::uint64_t total_requests{0};
        std::uint64_t total_failures{0};
        std::uint64_t consecutive_failures{0};
    };

    struct LogEvent
    {
        LogSeverity severity{LogSeverity::Info};
        std::string method;
        std::string url;
        int status_code{0};
        ErrorKind error_kind{ErrorKind::None};
        bool retryable{false};
        std::uint64_t duration_ms{0};
        std::string message;
    };

    struct ClientOptions
    {
        TimeoutOptions timeout{};
        RedirectOptions redirects{};
        ProxyOptions proxy{};
        PoolOptions pool{};
        TlsOptions tls{};
        Limits limits{};
        DnsStrategy dns_strategy{DnsStrategy::HappyEyeballs};
        std::size_t io_buffer_bytes{32u * 1024u};
        std::string user_agent{"httpx/0.1"};
        bool redact_sensitive_data{true};
        bool use_proxy_from_environment{true};
        std::size_t max_retry_attempts{0};

        std::function<bool(const Error &error, std::size_t attempt)> should_retry;
        std::function<void(const LogEvent &event)> logger;
        std::function<Result<Response>(const Request &request, const ClientOptions &options)> transport;
    };

    struct FlatConfigEntry
    {
        std::string key;
        std::string value;
    };

    const char *ToString(HttpMethod method) noexcept;
    const char *ToString(ErrorKind kind) noexcept;
    const char *ToString(LogSeverity severity) noexcept;

    std::string RedactUrl(std::string_view url);
    std::string RedactHeaderValue(std::string_view key, std::string_view value);

    Result<ClientOptions> ParseOptionsFromFlatConfig(const std::vector<FlatConfigEntry> &entries);

    class Client
    {
    public:
        explicit Client(ClientOptions options = {});
        ~Client();

        Result<Response> Send(const Request &request);

        Result<Response> Get(std::string url, HeaderList headers = {});
        Result<Response> Delete(std::string url, HeaderList headers = {});
        Result<Response> Head(std::string url, HeaderList headers = {});
        Result<Response> Options(std::string url, HeaderList headers = {});
        Result<Response> Connect(std::string url, HeaderList headers = {});
        Result<Response> Trace(std::string url, HeaderList headers = {});
        Result<Response> Post(std::string url, std::string body, HeaderList headers = {});
        Result<Response> Put(std::string url, std::string body, HeaderList headers = {});
        Result<Response> Patch(std::string url, std::string body, HeaderList headers = {});

        Status ApplyFlatConfig(const std::vector<FlatConfigEntry> &entries);

        const ClientOptions &Options() const noexcept;
        FailureStats GetFailureStats() const;
        void ResetFailureStats();

    private:
        Result<Response> SendOnce(const Request &request,
                                  const ClientOptions &effective_options,
                                  std::chrono::steady_clock::time_point deadline);
        void EmitLog(const Request &request,
                     const Result<Response> &result,
                     std::uint64_t duration_ms,
                     const ClientOptions &effective_options,
                     LogSeverity severity) const;

        ClientOptions options_{};
        mutable std::mutex mu_{};
        FailureStats stats_{};
    };

} // namespace httpx
