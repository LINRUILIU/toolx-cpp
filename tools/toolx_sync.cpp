#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "argtool.h"
#include "asyncx.h"
#include "cfgx.h"
#include "fsx.h"
#include "httpx.h"
#include "logsys.h"

namespace
{
    constexpr int kExitSuccess = 0;
    constexpr int kExitRuntimeError = 1;
    constexpr int kExitUsageError = 2;
    constexpr int kExitValidationFailed = 4;

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

    bool SplitOnce(std::string_view text, char delimiter, std::string *left, std::string *right)
    {
        const std::size_t pos = text.find(delimiter);
        if (pos == std::string_view::npos)
        {
            return false;
        }

        *left = TrimCopy(text.substr(0, pos));
        *right = TrimCopy(text.substr(pos + 1));
        return !left->empty() && !right->empty();
    }

    cfgx::Node BuildDataObject(std::initializer_list<std::pair<std::string, cfgx::Node>> fields)
    {
        cfgx::Node::Object obj;
        obj.reserve(fields.size());
        for (const auto &field : fields)
        {
            obj.push_back({field.first, field.second});
        }
        return cfgx::Node(std::move(obj));
    }

    cfgx::Node BuildIssueArray(const std::vector<cfgx::ValidationIssue> &issues)
    {
        cfgx::Node::Array arr;
        arr.reserve(issues.size());
        for (const auto &issue : issues)
        {
            arr.emplace_back(BuildDataObject({
                {"path", cfgx::Node(issue.path)},
                {"message", cfgx::Node(issue.message)},
            }));
        }
        return cfgx::Node(std::move(arr));
    }

    void PrintJsonEnvelope(bool ok,
                           int code,
                           std::string_view message,
                           const cfgx::Node &data,
                           const std::vector<cfgx::ValidationIssue> &issues = {})
    {
        const cfgx::Node envelope = BuildDataObject({
            {"schema", cfgx::Node("toolx.sync.result")},
            {"schema_version", cfgx::Node(std::int64_t(1))},
            {"ok", cfgx::Node(ok)},
            {"code", cfgx::Node(static_cast<std::int64_t>(code))},
            {"message", cfgx::Node(std::string(message))},
            {"issues", BuildIssueArray(issues)},
            {"data", data},
        });

        std::cout << cfgx::ToJson(envelope, 2) << "\n";
    }

    int ExitError(bool json_mode,
                  int code,
                  std::string_view message,
                  const cfgx::Node &data = cfgx::Node::MakeObject(),
                  const std::vector<cfgx::ValidationIssue> &issues = {})
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

    std::optional<cfgx::ConfigFormat> ParseFormat(std::string_view text)
    {
        std::string value(text);
        for (char &ch : value)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        if (value.empty() || value == "auto" || value == "unknown")
        {
            return cfgx::ConfigFormat::Unknown;
        }
        if (value == "json")
        {
            return cfgx::ConfigFormat::Json;
        }
        if (value == "ini" || value == "cfg")
        {
            return cfgx::ConfigFormat::Ini;
        }
        if (value == "yaml" || value == "yml")
        {
            return cfgx::ConfigFormat::Yaml;
        }
        if (value == "toml")
        {
            return cfgx::ConfigFormat::Toml;
        }
        return std::nullopt;
    }

    bool BuildValidationRules(const argtool::ParseResult &result,
                              std::vector<cfgx::ValidationRule> *rules,
                              std::string *error)
    {
        for (const auto &path : result.GetAll("require"))
        {
            if (TrimCopy(path).empty())
            {
                *error = "--require contains empty path";
                return false;
            }
            rules->push_back(cfgx::RequirePathRule(path));
        }

        for (const auto &spec : result.GetAll("range"))
        {
            std::string path;
            std::string bounds;
            if (!SplitOnce(spec, '=', &path, &bounds))
            {
                *error = "invalid --range format, expected PATH=MIN:MAX: " + spec;
                return false;
            }

            std::string min_text;
            std::string max_text;
            if (!SplitOnce(bounds, ':', &min_text, &max_text))
            {
                *error = "invalid --range bounds, expected MIN:MAX in: " + spec;
                return false;
            }

            char *min_end = nullptr;
            const double min_value = std::strtod(min_text.c_str(), &min_end);
            if (min_end == nullptr || *min_end != '\0')
            {
                *error = "invalid --range min value: " + min_text;
                return false;
            }

            char *max_end = nullptr;
            const double max_value = std::strtod(max_text.c_str(), &max_end);
            if (max_end == nullptr || *max_end != '\0')
            {
                *error = "invalid --range max value: " + max_text;
                return false;
            }

            if (min_value > max_value)
            {
                *error = "--range min must be <= max: " + spec;
                return false;
            }

            rules->push_back(cfgx::NumericRangeRule(path, min_value, max_value));
        }

        return true;
    }

    void ConfigureLogging(bool json_mode, const std::string &log_file)
    {
        logsys::DefaultLoggerOptions options;
        options.level = logsys::LogLevel::Info;
        options.record_level = logsys::LogLevel::Info;
        options.enable_console = false;
        options.enable_file = !log_file.empty();
        options.enable_debugger = false;
        options.file_path = log_file;

        auto &logger = logsys::Logger::Instance();
        logger.ConfigureDefaultLogger(options);
        logger.SetDefaultOrigin(logsys::ErrorSource::Business,
                                logsys::ModuleId::BusinessCommon,
                                logsys::ErrorCategory::Business);
        (void)json_mode;
    }

    cfgx::RemoteFetcher MakeHttpxFetcher(httpx::Client *client)
    {
        return [client](const cfgx::RemoteFetchRequest &request) -> cfgx::Result<cfgx::RemoteFetchResponse>
        {
            cfgx::Result<cfgx::RemoteFetchResponse> out;
            httpx::Request http_request;
            http_request.method = httpx::HttpMethod::Get;
            http_request.url = request.url;
            http_request.headers = request.headers;

            const auto response = client->Send(http_request);
            if (!response.ok)
            {
                out.ok = false;
                out.error = response.error.message;
                return out;
            }

            if (response.value.status_code < 200 || response.value.status_code >= 300)
            {
                out.ok = false;
                out.error = "remote config returned HTTP status " + std::to_string(response.value.status_code);
                return out;
            }

            out.ok = true;
            out.value.body = response.value.body;
            out.value.headers = response.value.headers;
            out.value.status_code = response.value.status_code;
            return out;
        };
    }

} // namespace

int main(int argc, const char *const argv[])
{
    argtool::Parser parser;
    parser.SetProgramName("toolx-sync")
        .SetDescription("toolx-sync - validate and atomically publish composed config")
        .SetUsageExample("toolx-sync --base app.json --out resolved.json --require svc.port")
        .SetHelpLayout(argtool::HelpLayout::Fixed);

    parser.Option("base", 'b').String().ValueName("FILE").Description("Base config file.").Done();
    parser.Option("out", 'o').String().ValueName("FILE").Description("Resolved output file.").Done();
    parser.Option("snapshot", 's').String().ValueName("FILE").Description("Optional snapshot file.").Done();
    parser.Option("journal", 'j').String().ValueName("FILE").Description("Optional fsx journal file.").Done();
    parser.Option("remote-url").String().ValueName("URL").Description("Optional remote config URL.").Done();
    parser.Option("remote-format").String().Default("auto").ValueName("FORMAT").Description("Remote format: auto/json/ini/yaml/toml.").Done();
    parser.Option("require").String().ListValue().ValueName("PATH").Description("Validation rule: required path.").Done();
    parser.Option("range").String().ListValue().ValueName("PATH=MIN:MAX").Description("Validation rule: numeric range.").Done();
    parser.Option("log-file").String().ValueName("FILE").Description("Optional audit log file.").Done();
    parser.Option("indent", 'i').Int().Default("2").Description("JSON indent width.").Done();
    parser.Flag("json").Description("Emit machine-readable JSON envelope.").Done();

    const auto parsed = parser.Parse(argc, argv);
    const bool json_mode = parsed.GetBool("json", false);
    if (parsed.help_requested)
    {
        if (json_mode)
        {
            PrintJsonEnvelope(true,
                              kExitSuccess,
                              "help requested",
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

    if (!parsed.Has("base") || parsed.GetString("base").empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing required option --base");
    }
    if (!parsed.Has("out") || parsed.GetString("out").empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing required option --out");
    }

    const auto remote_format = ParseFormat(parsed.GetString("remote-format", "auto"));
    if (!remote_format.has_value())
    {
        return ExitError(json_mode, kExitUsageError, "unsupported --remote-format: " + parsed.GetString("remote-format"));
    }

    std::vector<cfgx::ValidationRule> rules;
    std::string validation_rule_error;
    if (!BuildValidationRules(parsed, &rules, &validation_rule_error))
    {
        return ExitError(json_mode, kExitUsageError, validation_rule_error);
    }

    ConfigureLogging(json_mode, parsed.GetString("log-file", ""));

    asyncx::ThreadPool pool;
    const std::string base_path = parsed.GetString("base");
    auto base_task = pool.Submit([base_path]()
                                 { return cfgx::LoadFromFile(base_path); });
    if (!base_task.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, base_task.error.message);
    }

    httpx::Client client;
    std::optional<std::future<cfgx::Result<cfgx::Node>>> remote_task;
    const std::string remote_url = parsed.GetString("remote-url", "");
    if (!remote_url.empty())
    {
        cfgx::SetRemoteFetcher(MakeHttpxFetcher(&client));
        auto submitted = pool.Submit([remote_url, remote_format]()
                                     { return cfgx::LoadFromRemote(remote_url, *remote_format); });
        if (!submitted.ok)
        {
            cfgx::SetRemoteFetcher({});
            return ExitError(json_mode, kExitRuntimeError, submitted.error.message);
        }
        remote_task.emplace(std::move(submitted.value));
    }

    auto base = base_task.value.get();
    if (!base.ok)
    {
        cfgx::SetRemoteFetcher({});
        return ExitError(json_mode, kExitRuntimeError, "failed to load base config: " + base.error);
    }

    std::optional<cfgx::Node> remote_layer;
    if (remote_task.has_value())
    {
        auto remote = remote_task->get();
        cfgx::SetRemoteFetcher({});
        if (!remote.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, "failed to load remote config: " + remote.error);
        }
        remote_layer = std::move(remote.value);
    }

    const auto composed = cfgx::ComposeLayers(base.value,
                                             std::nullopt,
                                             std::nullopt,
                                             nullptr,
                                             cfgx::ComposeOptions{},
                                             nullptr,
                                             remote_layer);
    if (!composed.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, composed.error);
    }

    const auto validation = cfgx::Validate(composed.value, rules);
    if (!validation.ok)
    {
        return ExitError(json_mode,
                         kExitValidationFailed,
                         "validation failed",
                         BuildDataObject({{"issues_count", cfgx::Node(static_cast<std::int64_t>(validation.value.size()))}}),
                         validation.value);
    }

    const int indent = parsed.GetInt("indent", 2);
    const std::string serialized = cfgx::ToJson(composed.value, indent);
    const std::string out_path = parsed.GetString("out");
    const std::string snapshot_path = parsed.GetString("snapshot", "");
    const std::string journal_path = parsed.GetString("journal", "");

    fsx::BatchPlan plan;
    plan.AddAtomicWrite(out_path, serialized);
    if (!snapshot_path.empty())
    {
        plan.AddAtomicWrite(snapshot_path, serialized);
    }

    fsx::RunOptions run_options;
    run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    run_options.rollback_mode = fsx::RollbackMode::BestEffort;
    run_options.journal_path = journal_path;

    const auto run = fsx::Run(plan, run_options);
    if (!run.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, "failed to publish config: " + run.error);
    }

    pool.StopAndJoin(asyncx::StopMode::Drain);

    LOGI("toolx-sync published %s steps=%zu", out_path.c_str(), run.steps.size());
    logsys::Logger::Instance().Flush();

    const cfgx::Node data = BuildDataObject({
        {"base", cfgx::Node(base_path)},
        {"remote_url", cfgx::Node(remote_url)},
        {"out", cfgx::Node(out_path)},
        {"snapshot", cfgx::Node(snapshot_path)},
        {"journal", cfgx::Node(journal_path)},
        {"steps", cfgx::Node(static_cast<std::int64_t>(run.steps.size()))},
    });

    if (json_mode)
    {
        PrintJsonEnvelope(true, kExitSuccess, "published", data);
    }
    else
    {
        std::cout << "published=" << out_path << "\n";
        if (!snapshot_path.empty())
        {
            std::cout << "snapshot=" << snapshot_path << "\n";
        }
        if (!journal_path.empty())
        {
            std::cout << "journal=" << journal_path << "\n";
        }
        std::cout << "steps=" << run.steps.size() << "\n";
    }

    return kExitSuccess;
}
