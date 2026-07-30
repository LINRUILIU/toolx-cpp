#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "argtool.h"
#include "cfgx.h"
#include "httpx.h"
#include "logsys.h"
#include "schemax.h"

namespace
{
namespace fs = std::filesystem;

constexpr int kExitSuccess = 0;
constexpr int kExitRuntimeError = 1;
constexpr int kExitUsageError = 2;
constexpr int kExitNotFound = 3;
constexpr int kExitValidationFailed = 4;

struct StatusExpectation
{
    int min{200};
    int max{299};
    std::string text{"200:299"};
};

struct HttpCheck
{
    std::string name;
    std::string url;
    httpx::HttpMethod method{httpx::HttpMethod::Get};
    httpx::HeaderList headers;
    std::string body;
    std::string body_file;
    StatusExpectation expect_status;
    std::string expect_body_contains;
};

struct HttpConfig
{
    std::string command;
    std::string manifest;
    std::vector<HttpCheck> checks;
    httpx::HeaderList default_headers;
    std::uint64_t timeout_ms{60000};
    std::uint64_t connect_timeout_ms{5000};
    std::size_t retry{0};
    std::uint64_t retry_delay_ms{0};
    bool follow_redirects{false};
    bool use_proxy_from_environment{true};
    std::string log_file;
};

struct CheckResult
{
    std::string name;
    std::string url;
    std::string method;
    bool ok{false};
    int status{0};
    std::uint64_t duration_ms{0};
    std::string error_kind;
    std::string message;
    std::string expect_status;
    bool body_matched{false};
};

struct RunSummary
{
    std::vector<CheckResult> checks;
    std::uint64_t duration_ms{0};
    std::size_t passed{0};
    std::size_t transport_failed{0};
    std::size_t validation_failed{0};
    std::vector<std::string> warnings;
};

std::string TrimCopy(std::string_view input)
{
    std::size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0)
    {
        ++begin;
    }

    std::size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
    {
        --end;
    }

    return std::string(input.substr(begin, end - begin));
}

std::string ToUpperCopy(std::string_view input)
{
    std::string out(input);
    for (char& ch : out)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool ParseUnsigned(std::string_view input, std::uint64_t* value)
{
    const std::string text = TrimCopy(input);
    if (text.empty())
    {
        return false;
    }
    std::uint64_t parsed = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
    {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseSize(std::string_view input, std::size_t* value)
{
    std::uint64_t parsed = 0;
    if (!ParseUnsigned(input, &parsed))
    {
        return false;
    }
    *value = static_cast<std::size_t>(parsed);
    return true;
}

bool ParseStatusInt(std::string_view input, int* value)
{
    const std::string text = TrimCopy(input);
    if (text.empty())
    {
        return false;
    }
    int parsed = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end || parsed < 100 || parsed > 999)
    {
        return false;
    }
    *value = parsed;
    return true;
}

cfgx::Node BuildDataObject(std::initializer_list<std::pair<std::string, cfgx::Node>> fields)
{
    cfgx::Node::Object obj;
    obj.reserve(fields.size());
    for (const auto& field : fields)
    {
        obj.push_back({field.first, field.second});
    }
    return cfgx::Node(std::move(obj));
}

cfgx::Node BuildStringArray(const std::vector<std::string>& items)
{
    cfgx::Node::Array arr;
    arr.reserve(items.size());
    for (const auto& item : items)
    {
        arr.emplace_back(cfgx::Node(item));
    }
    return cfgx::Node(std::move(arr));
}

cfgx::Node BuildIssueArray(const std::vector<cfgx::ValidationIssue>& issues)
{
    cfgx::Node::Array arr;
    arr.reserve(issues.size());
    for (const auto& issue : issues)
    {
        arr.emplace_back(BuildDataObject({
            {"path", cfgx::Node(issue.path)},
            {"message", cfgx::Node(issue.message)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

void PrintJsonEnvelope(bool ok, int code, std::string_view message, const cfgx::Node& data,
                       const std::vector<cfgx::ValidationIssue>& issues = {})
{
    const cfgx::Node envelope = BuildDataObject({
        {"schema", cfgx::Node("toolx.http.result")},
        {"schema_version", cfgx::Node(std::int64_t(1))},
        {"ok", cfgx::Node(ok)},
        {"code", cfgx::Node(static_cast<std::int64_t>(code))},
        {"message", cfgx::Node(std::string(message))},
        {"issues", BuildIssueArray(issues)},
        {"data", data},
    });

    std::cout << cfgx::ToJson(envelope, 2) << "\n";
}

int ExitError(bool json_mode, int code, std::string_view message, const cfgx::Node& data = cfgx::Node::MakeObject(),
              const std::vector<cfgx::ValidationIssue>& issues = {})
{
    if (json_mode)
    {
        PrintJsonEnvelope(false, code, message, data, issues);
    }
    else
    {
        std::cerr << "error: " << message << "\n";
    }
    return code;
}

std::optional<httpx::HttpMethod> ParseMethod(std::string_view input)
{
    const std::string method = ToUpperCopy(TrimCopy(input));
    if (method == "GET")
    {
        return httpx::HttpMethod::Get;
    }
    if (method == "POST")
    {
        return httpx::HttpMethod::Post;
    }
    if (method == "PUT")
    {
        return httpx::HttpMethod::Put;
    }
    if (method == "PATCH")
    {
        return httpx::HttpMethod::Patch;
    }
    if (method == "DELETE")
    {
        return httpx::HttpMethod::Delete;
    }
    if (method == "HEAD")
    {
        return httpx::HttpMethod::Head;
    }
    if (method == "OPTIONS")
    {
        return httpx::HttpMethod::Options;
    }
    return std::nullopt;
}

bool ParseHeader(std::string_view spec, std::pair<std::string, std::string>* header, std::string* error)
{
    const std::string text = TrimCopy(spec);
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos)
    {
        *error = "invalid header format, expected KEY:VALUE: " + text;
        return false;
    }

    std::string key = TrimCopy(std::string_view(text.data(), colon));
    std::string value = TrimCopy(std::string_view(text.data() + colon + 1, text.size() - colon - 1));
    if (key.empty())
    {
        *error = "header key cannot be empty";
        return false;
    }
    *header = {std::move(key), std::move(value)};
    return true;
}

cfgx::Result<httpx::HeaderList> ParseHeaders(const std::vector<std::string>& specs)
{
    httpx::HeaderList headers;
    headers.reserve(specs.size());
    for (const auto& spec : specs)
    {
        std::pair<std::string, std::string> header;
        std::string error;
        if (!ParseHeader(spec, &header, &error))
        {
            return cfgx::Result<httpx::HeaderList>{false, {}, error};
        }
        headers.push_back(std::move(header));
    }
    return cfgx::Result<httpx::HeaderList>{true, std::move(headers), ""};
}

cfgx::Result<StatusExpectation> ParseStatusExpectation(std::string_view raw)
{
    const std::string text = TrimCopy(raw);
    if (text.empty())
    {
        return cfgx::Result<StatusExpectation>{false, {}, "expected status cannot be empty"};
    }

    const std::size_t colon = text.find(':');
    StatusExpectation expectation;
    expectation.text = text;
    if (colon == std::string::npos)
    {
        int status = 0;
        if (!ParseStatusInt(text, &status))
        {
            return cfgx::Result<StatusExpectation>{false, {}, "invalid expected status: " + text};
        }
        expectation.min = status;
        expectation.max = status;
        return cfgx::Result<StatusExpectation>{true, expectation, ""};
    }

    int min_status = 0;
    int max_status = 0;
    if (!ParseStatusInt(std::string_view(text.data(), colon), &min_status) ||
        !ParseStatusInt(std::string_view(text.data() + colon + 1, text.size() - colon - 1), &max_status) ||
        min_status > max_status)
    {
        return cfgx::Result<StatusExpectation>{false, {}, "invalid expected status range: " + text};
    }
    expectation.min = min_status;
    expectation.max = max_status;
    return cfgx::Result<StatusExpectation>{true, expectation, ""};
}

std::vector<std::string> ReadStringArrayField(const cfgx::Node& root, std::string_view field)
{
    std::vector<std::string> out;
    const auto* value = root.Get(field);
    if (value == nullptr)
    {
        return out;
    }
    const auto* arr = value->TryArray();
    if (arr == nullptr)
    {
        return out;
    }
    out.reserve(arr->size());
    for (const auto& item : *arr)
    {
        out.push_back(item.AsString());
    }
    return out;
}

cfgx::Node ManifestSchema()
{
    const char* schema_text = R"({
      "type": "object",
      "required": ["checks"],
      "properties": {
        "checks": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["url"],
            "properties": {
              "name": {"type": "string"},
              "url": {"type": "string"},
              "method": {"type": "string"},
              "headers": {"type": "array", "items": {"type": "string"}},
              "body": {"type": "string"},
              "body_file": {"type": "string"},
              "expect_status": {"type": "string"},
              "expect_body_contains": {"type": "string"}
            },
            "additionalProperties": false
          }
        },
        "timeout_ms": {"type": "integer"},
        "connect_timeout_ms": {"type": "integer"},
        "retry": {"type": "integer"},
        "retry_delay_ms": {"type": "integer"},
        "follow_redirects": {"type": "boolean"},
        "use_proxy_from_environment": {"type": "boolean"},
        "headers": {"type": "array", "items": {"type": "string"}}
      },
      "additionalProperties": false
    })";

    const auto parsed = cfgx::ParseJson(schema_text);
    return parsed.ok ? parsed.value : cfgx::Node::MakeObject();
}

bool ReadU64Field(const cfgx::Node& root, std::string_view field, std::uint64_t* out)
{
    const auto* node = root.Get(field);
    if (node == nullptr)
    {
        return false;
    }
    const auto value = node->AsInt(-1);
    if (value < 0)
    {
        return false;
    }
    *out = static_cast<std::uint64_t>(value);
    return true;
}

bool ReadSizeField(const cfgx::Node& root, std::string_view field, std::size_t* out)
{
    std::uint64_t value = 0;
    if (!ReadU64Field(root, field, &value))
    {
        return false;
    }
    *out = static_cast<std::size_t>(value);
    return true;
}

cfgx::Result<HttpCheck> ParseManifestCheck(const cfgx::Node& node, std::size_t index)
{
    HttpCheck check;
    check.name = "check-" + std::to_string(index + 1);
    if (const auto* name = node.Get("name"); name != nullptr)
    {
        check.name = name->AsString();
    }
    if (const auto* url = node.Get("url"); url != nullptr)
    {
        check.url = url->AsString();
    }
    if (const auto* method = node.Get("method"); method != nullptr)
    {
        const auto parsed = ParseMethod(method->AsString());
        if (!parsed.has_value())
        {
            return cfgx::Result<HttpCheck>{false, {}, "unsupported method in manifest check: " + method->AsString()};
        }
        check.method = *parsed;
    }
    if (const auto* body = node.Get("body"); body != nullptr)
    {
        check.body = body->AsString();
    }
    if (const auto* body_file = node.Get("body_file"); body_file != nullptr)
    {
        check.body_file = body_file->AsString();
    }
    if (const auto* status = node.Get("expect_status"); status != nullptr)
    {
        auto parsed = ParseStatusExpectation(status->AsString());
        if (!parsed.ok)
        {
            return cfgx::Result<HttpCheck>{false, {}, parsed.error};
        }
        check.expect_status = parsed.value;
    }
    if (const auto* contains = node.Get("expect_body_contains"); contains != nullptr)
    {
        check.expect_body_contains = contains->AsString();
    }

    auto headers = ParseHeaders(ReadStringArrayField(node, "headers"));
    if (!headers.ok)
    {
        return cfgx::Result<HttpCheck>{false, {}, headers.error};
    }
    check.headers = std::move(headers.value);

    return cfgx::Result<HttpCheck>{true, std::move(check), ""};
}

cfgx::Result<HttpConfig> LoadManifestConfig(const std::string& path)
{
    HttpConfig config;
    if (path.empty())
    {
        return cfgx::Result<HttpConfig>{true, std::move(config), ""};
    }

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        return cfgx::Result<HttpConfig>{false, {}, "manifest file not found: " + path};
    }

    auto loaded = cfgx::LoadFromFile(path);
    if (!loaded.ok)
    {
        return cfgx::Result<HttpConfig>{false, {}, "manifest parse failed: " + loaded.error};
    }

    const auto compiled = schemax::Compile(ManifestSchema());
    if (!compiled.ok)
    {
        return cfgx::Result<HttpConfig>{false, {}, "internal manifest schema failed: " + compiled.error};
    }

    const auto issues = schemax::Validate(loaded.value, compiled.value);
    if (!issues.empty())
    {
        std::string message = "manifest validation failed";
        if (!issues.front().path.empty())
        {
            message += ": " + issues.front().path + " " + issues.front().message;
        }
        return cfgx::Result<HttpConfig>{false, {}, message};
    }

    config.manifest = path;
    (void)ReadU64Field(loaded.value, "timeout_ms", &config.timeout_ms);
    (void)ReadU64Field(loaded.value, "connect_timeout_ms", &config.connect_timeout_ms);
    (void)ReadSizeField(loaded.value, "retry", &config.retry);
    (void)ReadU64Field(loaded.value, "retry_delay_ms", &config.retry_delay_ms);
    if (const auto* follow = loaded.value.Get("follow_redirects"); follow != nullptr)
    {
        config.follow_redirects = follow->AsBool(false);
    }
    if (const auto* use_proxy = loaded.value.Get("use_proxy_from_environment"); use_proxy != nullptr)
    {
        config.use_proxy_from_environment = use_proxy->AsBool(true);
    }

    auto headers = ParseHeaders(ReadStringArrayField(loaded.value, "headers"));
    if (!headers.ok)
    {
        return cfgx::Result<HttpConfig>{false, {}, headers.error};
    }
    config.default_headers = std::move(headers.value);

    const auto* checks = loaded.value.Get("checks");
    const auto* check_array = checks == nullptr ? nullptr : checks->TryArray();
    if (check_array == nullptr)
    {
        return cfgx::Result<HttpConfig>{false, {}, "manifest checks must be an array"};
    }
    config.checks.reserve(check_array->size());
    for (std::size_t i = 0; i < check_array->size(); ++i)
    {
        auto check = ParseManifestCheck((*check_array)[i], i);
        if (!check.ok)
        {
            return cfgx::Result<HttpConfig>{false, {}, check.error};
        }
        config.checks.push_back(std::move(check.value));
    }
    return cfgx::Result<HttpConfig>{true, std::move(config), ""};
}

void ApplyCliOverrides(const argtool::ParseResult& parsed, HttpConfig* config)
{
    if (parsed.Has("timeout-ms"))
    {
        config->timeout_ms = static_cast<std::uint64_t>(std::max(0, parsed.GetInt("timeout-ms", 0)));
    }
    if (parsed.Has("connect-timeout-ms"))
    {
        config->connect_timeout_ms = static_cast<std::uint64_t>(std::max(0, parsed.GetInt("connect-timeout-ms", 0)));
    }
    if (parsed.Has("retry"))
    {
        config->retry = static_cast<std::size_t>(std::max(0, parsed.GetInt("retry", 0)));
    }
    if (parsed.Has("retry-delay-ms"))
    {
        config->retry_delay_ms = static_cast<std::uint64_t>(std::max(0, parsed.GetInt("retry-delay-ms", 0)));
    }
    if (parsed.Has("follow-redirects"))
    {
        config->follow_redirects = parsed.GetBool("follow-redirects", false);
    }
    if (parsed.Has("no-proxy-from-env"))
    {
        config->use_proxy_from_environment = false;
    }
    if (parsed.Has("header"))
    {
        auto headers = ParseHeaders(parsed.GetAll("header"));
        if (headers.ok)
        {
            config->default_headers = std::move(headers.value);
        }
    }
    config->log_file = parsed.GetString("log-file", "");
}

void ApplyCliCheckOverrides(const argtool::ParseResult& parsed, HttpCheck* check)
{
    if (parsed.Has("method") && !parsed.GetString("method").empty())
    {
        const auto method = ParseMethod(parsed.GetString("method"));
        if (method.has_value())
        {
            check->method = *method;
        }
    }
    if (parsed.Has("body"))
    {
        check->body = parsed.GetString("body");
        check->body_file.clear();
    }
    if (parsed.Has("body-file"))
    {
        check->body_file = parsed.GetString("body-file");
        check->body.clear();
    }
    if (parsed.Has("expect-status"))
    {
        auto expectation = ParseStatusExpectation(parsed.GetString("expect-status"));
        if (expectation.ok)
        {
            check->expect_status = expectation.value;
        }
    }
    if (parsed.Has("expect-body-contains"))
    {
        check->expect_body_contains = parsed.GetString("expect-body-contains");
    }
}

cfgx::Result<HttpConfig> ResolveConfig(const argtool::ParseResult& parsed, const std::string& command)
{
    const std::string manifest_path = parsed.GetString("manifest", "");
    auto manifest = LoadManifestConfig(manifest_path);
    if (!manifest.ok)
    {
        return manifest;
    }

    HttpConfig config = std::move(manifest.value);
    config.command = command;
    ApplyCliOverrides(parsed, &config);

    const bool has_url = parsed.Has("url") && !parsed.GetString("url").empty();
    if (has_url && !manifest_path.empty())
    {
        return cfgx::Result<HttpConfig>{false, {}, "use either --url or --manifest, not both"};
    }
    if (has_url)
    {
        HttpCheck check;
        check.name = "check-1";
        check.url = parsed.GetString("url");
        ApplyCliCheckOverrides(parsed, &check);
        config.checks.push_back(std::move(check));
    }
    else if (!manifest_path.empty())
    {
        for (auto& check : config.checks)
        {
            ApplyCliCheckOverrides(parsed, &check);
        }
    }

    return cfgx::Result<HttpConfig>{true, std::move(config), ""};
}

cfgx::Status ValidateConfig(const HttpConfig& config)
{
    if (config.checks.empty())
    {
        return cfgx::Status{false, "missing required option --url or --manifest"};
    }
    if (config.timeout_ms == 0)
    {
        return cfgx::Status{false, "--timeout-ms must be greater than 0"};
    }
    if (config.connect_timeout_ms == 0)
    {
        return cfgx::Status{false, "--connect-timeout-ms must be greater than 0"};
    }
    for (const auto& check : config.checks)
    {
        if (TrimCopy(check.url).empty())
        {
            return cfgx::Status{false, "check url cannot be empty"};
        }
        if (!check.body.empty() && !check.body_file.empty())
        {
            return cfgx::Status{false, "--body and --body-file are mutually exclusive"};
        }
        if (check.method == httpx::HttpMethod::Head && !check.expect_body_contains.empty())
        {
            return cfgx::Status{false, "HEAD checks cannot use --expect-body-contains"};
        }
    }
    return cfgx::Status{true, ""};
}

cfgx::Result<std::string> ReadBodyFile(const std::string& path)
{
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        return cfgx::Result<std::string>{false, {}, "body file not found: " + path};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return cfgx::Result<std::string>{false, {}, "failed to open body file: " + path};
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return cfgx::Result<std::string>{true, std::move(text), ""};
}

void ConfigureLogging(const std::string& log_file)
{
    logsys::DefaultLoggerOptions options;
    options.level = logsys::LogLevel::Info;
    options.record_level = logsys::LogLevel::Info;
    options.enable_console = false;
    options.enable_file = !log_file.empty();
    options.enable_debugger = false;
    options.file_path = log_file;

    auto& logger = logsys::Logger::Instance();
    logger.ConfigureDefaultLogger(options);
    logger.SetDefaultOrigin(logsys::ErrorSource::Business, logsys::ModuleId::BusinessCommon,
                            logsys::ErrorCategory::Business);
}

httpx::ClientOptions BuildClientOptions(const HttpConfig& config)
{
    httpx::ClientOptions options;
    options.timeout.total_ms = config.timeout_ms;
    options.timeout.read_write_ms = config.timeout_ms;
    options.timeout.connect_ms = config.connect_timeout_ms;
    options.redirects.follow = config.follow_redirects;
    options.max_retry_attempts = config.retry;
    options.retry_policy.delay_ms = config.retry_delay_ms;
    options.use_proxy_from_environment = config.use_proxy_from_environment;
    return options;
}

httpx::Request BuildRequest(const HttpConfig& config, const HttpCheck& check, std::string body)
{
    httpx::Request request;
    request.method = check.method;
    request.url = check.url;
    request.headers = config.default_headers;
    request.headers.insert(request.headers.end(), check.headers.begin(), check.headers.end());
    request.body = std::move(body);
    request.follow_redirect = config.follow_redirects;
    return request;
}

cfgx::Node BuildCheckResultArray(const std::vector<CheckResult>& checks)
{
    cfgx::Node::Array arr;
    arr.reserve(checks.size());
    for (const auto& check : checks)
    {
        arr.emplace_back(BuildDataObject({
            {"name", cfgx::Node(check.name)},
            {"url", cfgx::Node(httpx::RedactUrl(check.url))},
            {"method", cfgx::Node(check.method)},
            {"ok", cfgx::Node(check.ok)},
            {"status", cfgx::Node(static_cast<std::int64_t>(check.status))},
            {"duration_ms", cfgx::Node(static_cast<std::int64_t>(check.duration_ms))},
            {"error_kind", cfgx::Node(check.error_kind)},
            {"message", cfgx::Node(check.message)},
            {"expect_status", cfgx::Node(check.expect_status)},
            {"body_matched", cfgx::Node(check.body_matched)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

cfgx::Node BuildData(const HttpConfig& config, const RunSummary& summary)
{
    return BuildDataObject({
        {"command", cfgx::Node(config.command)},
        {"manifest", cfgx::Node(config.manifest)},
        {"proxy_from_environment", cfgx::Node(config.use_proxy_from_environment)},
        {"checked", cfgx::Node(static_cast<std::int64_t>(summary.checks.size()))},
        {"passed", cfgx::Node(static_cast<std::int64_t>(summary.passed))},
        {"failed", cfgx::Node(static_cast<std::int64_t>(summary.checks.size() - summary.passed))},
        {"duration_ms", cfgx::Node(static_cast<std::int64_t>(summary.duration_ms))},
        {"checks", BuildCheckResultArray(summary.checks)},
        {"warnings", BuildStringArray(summary.warnings)},
    });
}

std::vector<cfgx::ValidationIssue> BuildValidationIssues(const RunSummary& summary)
{
    std::vector<cfgx::ValidationIssue> issues;
    for (const auto& check : summary.checks)
    {
        if (!check.ok)
        {
            issues.push_back(cfgx::ValidationIssue{check.name, check.message});
        }
    }
    return issues;
}

RunSummary RunChecks(const HttpConfig& config)
{
    const auto run_started = std::chrono::steady_clock::now();
    RunSummary summary;
    summary.checks.reserve(config.checks.size());

    ConfigureLogging(config.log_file);
    httpx::Client client(BuildClientOptions(config));

    for (const auto& check : config.checks)
    {
        CheckResult result;
        result.name = check.name;
        result.url = check.url;
        result.method = httpx::ToString(check.method);
        result.expect_status = check.expect_status.text;

        std::string body = check.body;
        if (!check.body_file.empty())
        {
            auto loaded = ReadBodyFile(check.body_file);
            if (!loaded.ok)
            {
                result.message = loaded.error;
                result.error_kind = "not_found";
                ++summary.transport_failed;
                summary.checks.push_back(std::move(result));
                continue;
            }
            body = std::move(loaded.value);
        }

        const auto started = std::chrono::steady_clock::now();
        const auto response = client.Send(BuildRequest(config, check, std::move(body)));
        result.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());

        if (!response.ok)
        {
            result.error_kind = httpx::ToString(response.error.kind);
            result.message = response.error.message;
            ++summary.transport_failed;
            LOGE("toolx-http check failed url=%s method=%s error=%s message=%s", check.url.c_str(),
                 result.method.c_str(), result.error_kind.c_str(), result.message.c_str());
            summary.checks.push_back(std::move(result));
            continue;
        }

        result.status = response.value.status_code;
        const bool status_ok = result.status >= check.expect_status.min && result.status <= check.expect_status.max;
        result.body_matched = check.expect_body_contains.empty() ||
                              response.value.body.find(check.expect_body_contains) != std::string::npos;
        result.ok = status_ok && result.body_matched;
        if (!status_ok)
        {
            result.message =
                "status " + std::to_string(result.status) + " outside expected " + check.expect_status.text;
        }
        else if (!result.body_matched)
        {
            result.message = "response body did not contain expected text";
        }
        else
        {
            result.message = "passed";
            ++summary.passed;
        }

        if (!result.ok)
        {
            ++summary.validation_failed;
        }
        LOGI("toolx-http check url=%s method=%s status=%d ok=%s duration_ms=%llu", check.url.c_str(),
             result.method.c_str(), result.status, result.ok ? "true" : "false",
             static_cast<unsigned long long>(result.duration_ms));
        summary.checks.push_back(std::move(result));
    }

    summary.duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - run_started).count());
    logsys::Logger::Instance().Flush();
    return summary;
}

int RunCheck(const HttpConfig& config, bool json_mode)
{
    const RunSummary summary = RunChecks(config);
    const auto data = BuildData(config, summary);

    if (summary.transport_failed > 0)
    {
        return ExitError(json_mode, kExitRuntimeError, "runtime failed", data, BuildValidationIssues(summary));
    }
    if (summary.validation_failed > 0)
    {
        return ExitError(json_mode, kExitValidationFailed, "preflight validation failed", data,
                         BuildValidationIssues(summary));
    }

    if (json_mode)
    {
        PrintJsonEnvelope(true, kExitSuccess, "checks passed", data);
    }
    else
    {
        std::cout << "checked=" << summary.checks.size() << "\n";
        std::cout << "passed=" << summary.passed << "\n";
        std::cout << "failed=" << (summary.checks.size() - summary.passed) << "\n";
        if (!summary.checks.empty())
        {
            std::cout << "url=" << httpx::RedactUrl(summary.checks.front().url) << "\n";
            std::cout << "status=" << summary.checks.front().status << "\n";
            std::cout << "duration_ms=" << summary.checks.front().duration_ms << "\n";
        }
    }
    return kExitSuccess;
}

} // namespace

int main(int argc, const char* const argv[])
{
    argtool::Parser parser;
    parser.SetProgramName("toolx-http")
        .SetDescription("toolx-http - preflight runtime HTTP endpoints")
        .SetUsageExample("toolx-http check --url http://127.0.0.1:8080/health --expect-status 200 --json")
        .SetHelpLayout(argtool::HelpLayout::Fixed);

    parser.AddSubcommandRoot("check", "Check one or more HTTP endpoints");

    parser.Option("url").String().ValueName("URL").Description("Endpoint URL for a single check.").Done();
    parser.Option("manifest").String().ValueName("FILE").Description("Optional HTTP preflight manifest.").Done();
    parser.Option("method")
        .String()
        .Choices({"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"})
        .ValueName("METHOD")
        .Description("HTTP method.")
        .Done();
    parser.Option("header").String().ListValue().ValueName("KEY:VALUE").Description("HTTP header. Repeatable.").Done();
    parser.Option("body").String().ValueName("TEXT").Description("Request body text.").Done();
    parser.Option("body-file").String().ValueName("FILE").Description("Request body file.").Done();
    parser.Option("expect-status")
        .String()
        .ValueName("N|MIN:MAX")
        .Description("Expected HTTP status or inclusive status range.")
        .Done();
    parser.Option("expect-body-contains")
        .String()
        .ValueName("TEXT")
        .Description("Expected response body substring.")
        .Done();
    parser.Option("timeout-ms").Int().ValueName("N").Description("Total request timeout in milliseconds.").Done();
    parser.Option("connect-timeout-ms").Int().ValueName("N").Description("Connection timeout in milliseconds.").Done();
    parser.Option("retry").Int().ValueName("N").Description("Number of retry attempts for retryable errors.").Done();
    parser.Option("retry-delay-ms")
        .Int()
        .ValueName("N")
        .Description("Delay between retry attempts in milliseconds.")
        .Done();
    parser.Option("log-file").String().ValueName("FILE").Description("Optional audit log file.").Done();
    parser.Flag("follow-redirects").Description("Follow HTTP redirects.").Done();
    parser.Flag("no-proxy-from-env").Description("Disable HTTP(S)_PROXY and NO_PROXY environment handling.").Done();
    parser.Flag("json").Description("Emit machine-readable JSON envelope.").Done();

    const auto parsed = parser.Parse(argc, argv);
    const bool json_mode = parsed.GetBool("json", false);
    if (parsed.help_requested)
    {
        if (json_mode)
        {
            PrintJsonEnvelope(true, kExitSuccess, "help requested",
                              BuildDataObject({{"help", cfgx::Node(parser.HelpText())}}));
        }
        else
        {
            std::cout << parser.HelpText();
        }
        return kExitSuccess;
    }

    if (!parsed.ok)
    {
        const std::string message = parsed.error.has_value() ? parsed.error->message : "parse failed";
        return ExitError(json_mode, kExitUsageError, message);
    }
    if (!parsed.subcommand_path.has_value() || parsed.subcommand_path->root.empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing subcommand");
    }
    if (parsed.subcommand_path->root != "check")
    {
        return ExitError(json_mode, kExitUsageError, "unsupported subcommand: " + parsed.subcommand_path->root);
    }

    auto config = ResolveConfig(parsed, parsed.subcommand_path->root);
    if (!config.ok)
    {
        const int code = config.error.find("not found") != std::string::npos ? kExitNotFound : kExitValidationFailed;
        return ExitError(json_mode, code, config.error);
    }

    if (parsed.Has("header"))
    {
        auto parsed_headers = ParseHeaders(parsed.GetAll("header"));
        if (!parsed_headers.ok)
        {
            return ExitError(json_mode, kExitUsageError, parsed_headers.error);
        }
    }
    if (parsed.Has("method"))
    {
        const auto method = ParseMethod(parsed.GetString("method"));
        if (!method.has_value())
        {
            return ExitError(json_mode, kExitUsageError, "unsupported method: " + parsed.GetString("method"));
        }
    }
    if (parsed.Has("expect-status"))
    {
        const auto expectation = ParseStatusExpectation(parsed.GetString("expect-status"));
        if (!expectation.ok)
        {
            return ExitError(json_mode, kExitUsageError, expectation.error);
        }
    }

    const auto validation = ValidateConfig(config.value);
    if (!validation.ok)
    {
        return ExitError(json_mode, kExitUsageError, validation.error);
    }

    bool missing_body_file = false;
    for (const auto& check : config.value.checks)
    {
        if (!check.body_file.empty())
        {
            std::error_code ec;
            if (!fs::exists(check.body_file, ec) || ec)
            {
                missing_body_file = true;
                break;
            }
        }
    }
    if (missing_body_file)
    {
        return ExitError(json_mode, kExitNotFound, "body file not found");
    }

    return RunCheck(config.value, json_mode);
}
