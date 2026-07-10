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
#include <utility>
#include <vector>

#include "argtool.h"
#include "asyncx.h"
#include "cfgx.h"
#include "fsx.h"
#include "httpx.h"
#include "logsys.h"
#include "schemax.h"

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

bool SplitOnce(std::string_view text, char delimiter, std::string* left, std::string* right)
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
    for (const auto& field : fields)
    {
        obj.push_back({field.first, field.second});
    }
    return cfgx::Node(std::move(obj));
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

cfgx::Node BuildSchemaIssueArray(const std::vector<schemax::Issue>& issues)
{
    cfgx::Node::Array arr;
    arr.reserve(issues.size());
    for (const auto& issue : issues)
    {
        arr.emplace_back(BuildDataObject({
            {"path", cfgx::Node(issue.path)},
            {"code", cfgx::Node(issue.code)},
            {"message", cfgx::Node(issue.message)},
        }));
    }
    return cfgx::Node(std::move(arr));
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

cfgx::Node BuildSourceTraceArray(const std::vector<cfgx::SourceAttribution>& trace)
{
    cfgx::Node::Array arr;
    arr.reserve(trace.size());
    for (const auto& entry : trace)
    {
        arr.emplace_back(BuildDataObject({
            {"path", cfgx::Node(entry.path)},
            {"layer", cfgx::Node(cfgx::ToString(entry.layer))},
        }));
    }
    return cfgx::Node(std::move(arr));
}

const char* ToActionString(fsx::BatchPlan::ActionKind kind) noexcept
{
    switch (kind)
    {
    case fsx::BatchPlan::ActionKind::AtomicWrite:
        return "atomic_write";
    case fsx::BatchPlan::ActionKind::SafeReplace:
        return "safe_replace";
    case fsx::BatchPlan::ActionKind::Rename:
        return "rename";
    case fsx::BatchPlan::ActionKind::CopyFile:
        return "copy_file";
    case fsx::BatchPlan::ActionKind::RemovePath:
        return "remove_path";
    case fsx::BatchPlan::ActionKind::CopyTree:
        return "copy_tree";
    }
    return "unknown";
}

cfgx::Node BuildPlanActionsArray(const fsx::BatchPlan& plan)
{
    cfgx::Node::Array arr;
    const auto& actions = plan.Actions();
    arr.reserve(actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        const auto& action = actions[i];
        arr.emplace_back(BuildDataObject({
            {"step", cfgx::Node(static_cast<std::int64_t>(i))},
            {"op", cfgx::Node(ToActionString(action.kind))},
            {"src", cfgx::Node(action.src)},
            {"dst", cfgx::Node(action.dst)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

void PrintJsonEnvelope(bool ok, int code, std::string_view message, const cfgx::Node& data,
                       const std::vector<cfgx::ValidationIssue>& issues = {})
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

std::optional<cfgx::ConfigFormat> ParseFormat(std::string_view text)
{
    std::string value(text);
    for (char& ch : value)
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

bool BuildValidationRules(const argtool::ParseResult& result, std::vector<cfgx::ValidationRule>* rules,
                          std::string* error)
{
    for (const auto& path : result.GetAll("require"))
    {
        if (TrimCopy(path).empty())
        {
            *error = "--require contains empty path";
            return false;
        }
        rules->push_back(cfgx::RequirePathRule(path));
    }

    for (const auto& spec : result.GetAll("range"))
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

        char* min_end = nullptr;
        const double min_value = std::strtod(min_text.c_str(), &min_end);
        if (min_end == nullptr || *min_end != '\0')
        {
            *error = "invalid --range min value: " + min_text;
            return false;
        }

        char* max_end = nullptr;
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

cfgx::Result<std::optional<cfgx::Node>> LoadOverlayLayer(const std::vector<std::string>& overlay_paths,
                                                         bool append_arrays)
{
    if (overlay_paths.empty())
    {
        return cfgx::Result<std::optional<cfgx::Node>>{true, std::nullopt, ""};
    }

    auto first = cfgx::LoadFromFile(overlay_paths.front());
    if (!first.ok)
    {
        return cfgx::Result<std::optional<cfgx::Node>>{false, std::nullopt,
                                                       "failed to load overlay config: " + first.error};
    }

    cfgx::Node local_layer = std::move(first.value);
    for (std::size_t i = 1; i < overlay_paths.size(); ++i)
    {
        auto next = cfgx::LoadFromFile(overlay_paths[i]);
        if (!next.ok)
        {
            return cfgx::Result<std::optional<cfgx::Node>>{false, std::nullopt,
                                                           "failed to load overlay config: " + next.error};
        }

        const auto merged = cfgx::Merge(local_layer, next.value, append_arrays);
        if (!merged.ok)
        {
            return cfgx::Result<std::optional<cfgx::Node>>{false, std::nullopt,
                                                           "failed to merge overlay config: " + merged.error};
        }
    }

    return cfgx::Result<std::optional<cfgx::Node>>{true, std::move(local_layer), ""};
}

void ConfigureLogging(bool json_mode, const std::string& log_file)
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
    (void)json_mode;
}

cfgx::RemoteFetcher MakeHttpxFetcher(httpx::Client* client)
{
    return [client](const cfgx::RemoteFetchRequest& request) -> cfgx::Result<cfgx::RemoteFetchResponse>
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

class ScopedRemoteFetcher
{
  public:
    explicit ScopedRemoteFetcher(cfgx::RemoteFetcher fetcher)
    {
        cfgx::SetRemoteFetcher(std::move(fetcher));
    }

    ScopedRemoteFetcher(const ScopedRemoteFetcher&) = delete;
    ScopedRemoteFetcher& operator=(const ScopedRemoteFetcher&) = delete;

    ScopedRemoteFetcher(ScopedRemoteFetcher&& other) noexcept
    {
        active_ = other.active_;
        other.active_ = false;
    }

    ScopedRemoteFetcher& operator=(ScopedRemoteFetcher&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    ~ScopedRemoteFetcher()
    {
        Reset();
    }

    void Reset()
    {
        if (active_)
        {
            cfgx::SetRemoteFetcher({});
            active_ = false;
        }
    }

  private:
    bool active_{true};
};

cfgx::Result<std::vector<schemax::Issue>> RunSchemaValidation(const std::string& schema_path,
                                                              const cfgx::Node& document)
{
    if (schema_path.empty())
    {
        return cfgx::Result<std::vector<schemax::Issue>>{true, {}, ""};
    }
    const auto loaded = cfgx::LoadFromFile(schema_path);
    if (!loaded.ok)
    {
        return cfgx::Result<std::vector<schemax::Issue>>{false, {}, "failed to load schema: " + loaded.error};
    }
    const auto compiled = schemax::Compile(loaded.value);
    if (!compiled.ok)
    {
        return cfgx::Result<std::vector<schemax::Issue>>{false, {}, "failed to compile schema: " + compiled.error};
    }
    return cfgx::Result<std::vector<schemax::Issue>>{true, schemax::Validate(document, compiled.value), ""};
}

} // namespace

int main(int argc, const char* const argv[])
{
    argtool::Parser parser;
    parser.SetProgramName("toolx-sync")
        .SetDescription("toolx-sync - validate and atomically publish composed config")
        .SetUsageExample("toolx-sync --base app.json --out resolved.json --require svc.port")
        .SetHelpLayout(argtool::HelpLayout::Fixed);

    parser.Option("base", 'b').String().ValueName("FILE").Description("Base config file.").Done();
    parser.Option("out", 'o').String().ValueName("FILE").Description("Resolved output file.").Done();
    parser.Option("overlay")
        .String()
        .ListValue()
        .ValueName("FILE")
        .Description("Local overlay config file. Repeatable.")
        .Done();
    parser.Option("snapshot", 's').String().ValueName("FILE").Description("Optional snapshot file.").Done();
    parser.Option("journal", 'j').String().ValueName("FILE").Description("Optional fsx journal file.").Done();
    parser.Option("schema").String().ValueName("FILE").Description("Optional schemax schema file.").Done();
    parser.Option("remote-url").String().ValueName("URL").Description("Optional remote config URL.").Done();
    parser.Flag("no-proxy-from-env")
        .Description("Disable HTTP(S)_PROXY and NO_PROXY for --remote-url requests.")
        .Done();
    parser.Option("remote-format")
        .String()
        .Default("auto")
        .ValueName("FORMAT")
        .Description("Remote format: auto/json/ini/yaml/toml.")
        .Done();
    parser.Option("require")
        .String()
        .ListValue()
        .ValueName("PATH")
        .Description("Validation rule: required path.")
        .Done();
    parser.Option("range")
        .String()
        .ListValue()
        .ValueName("PATH=MIN:MAX")
        .Description("Validation rule: numeric range.")
        .Done();
    parser.Option("log-file").String().ValueName("FILE").Description("Optional audit log file.").Done();
    parser.Option("indent", 'i').Int().Default("2").Description("JSON indent width.").Done();
    parser.Flag("append-arrays").Description("Append arrays while composing layers instead of replacing them.").Done();
    parser.Flag("dry-run").Description("Validate and print the publish plan without writing files.").Done();
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
        return ExitError(json_mode, kExitUsageError,
                         "unsupported --remote-format: " + parsed.GetString("remote-format"));
    }

    std::vector<cfgx::ValidationRule> rules;
    std::string validation_rule_error;
    if (!BuildValidationRules(parsed, &rules, &validation_rule_error))
    {
        return ExitError(json_mode, kExitUsageError, validation_rule_error);
    }

    ConfigureLogging(json_mode, parsed.GetString("log-file", ""));

    const bool append_arrays = parsed.GetBool("append-arrays", false);
    const bool dry_run = parsed.GetBool("dry-run", false);
    const bool use_proxy_from_environment = !parsed.GetBool("no-proxy-from-env", false);
    const std::vector<std::string> overlay_paths = parsed.GetAll("overlay");

    httpx::ClientOptions client_options;
    client_options.use_proxy_from_environment = use_proxy_from_environment;
    httpx::Client client(client_options);
    std::optional<ScopedRemoteFetcher> remote_fetcher;
    asyncx::ThreadPool pool;
    const std::string base_path = parsed.GetString("base");
    auto base_task = pool.Submit([base_path]() { return cfgx::LoadFromFile(base_path); });
    if (!base_task.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, base_task.error.message);
    }

    std::optional<std::future<cfgx::Result<cfgx::Node>>> remote_task;
    const std::string remote_url = parsed.GetString("remote-url", "");
    if (!remote_url.empty())
    {
        remote_fetcher.emplace(MakeHttpxFetcher(&client));
        auto submitted =
            pool.Submit([remote_url, remote_format]() { return cfgx::LoadFromRemote(remote_url, *remote_format); });
        if (!submitted.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, submitted.error.message);
        }
        remote_task.emplace(std::move(submitted.value));
    }

    auto base = base_task.value.get();
    if (!base.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, "failed to load base config: " + base.error);
    }

    std::optional<cfgx::Node> remote_layer;
    if (remote_task.has_value())
    {
        auto remote = remote_task->get();
        remote_fetcher.reset();
        if (!remote.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, "failed to load remote config: " + remote.error);
        }
        remote_layer = std::move(remote.value);
    }

    const auto overlay_layer = LoadOverlayLayer(overlay_paths, append_arrays);
    if (!overlay_layer.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, overlay_layer.error);
    }

    cfgx::ComposeOptions compose_options;
    compose_options.append_arrays = append_arrays;
    std::vector<cfgx::SourceAttribution> source_trace;
    const auto composed = cfgx::ComposeLayers(base.value, std::nullopt, overlay_layer.value, nullptr, compose_options,
                                              &source_trace, remote_layer);
    if (!composed.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, composed.error);
    }

    const auto validation = cfgx::Validate(composed.value, rules);
    std::vector<cfgx::ValidationIssue> combined_issues = validation.value;
    const auto schema_validation = RunSchemaValidation(parsed.GetString("schema", ""), composed.value);
    if (!schema_validation.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, schema_validation.error);
    }
    auto schema_cfgx_issues = schemax::ToCfgxIssues(schema_validation.value);
    combined_issues.insert(combined_issues.end(), schema_cfgx_issues.begin(), schema_cfgx_issues.end());
    if (!validation.ok || !schema_validation.value.empty())
    {
        return ExitError(
            json_mode, kExitValidationFailed, "validation failed",
            BuildDataObject({{"issues_count", cfgx::Node(static_cast<std::int64_t>(combined_issues.size()))},
                             {"schema_issues", BuildSchemaIssueArray(schema_validation.value)}}),
            combined_issues);
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

    const cfgx::Node publish_data = BuildDataObject({
        {"base", cfgx::Node(base_path)},
        {"overlays", BuildStringArray(overlay_paths)},
        {"remote_url", cfgx::Node(remote_url)},
        {"proxy_from_environment", cfgx::Node(use_proxy_from_environment)},
        {"out", cfgx::Node(out_path)},
        {"snapshot", cfgx::Node(snapshot_path)},
        {"journal", cfgx::Node(journal_path)},
        {"log_file", cfgx::Node(parsed.GetString("log-file", ""))},
        {"schema", cfgx::Node(parsed.GetString("schema", ""))},
        {"schema_issues", BuildSchemaIssueArray(schema_validation.value)},
        {"append_arrays", cfgx::Node(append_arrays)},
        {"dry_run", cfgx::Node(dry_run)},
        {"steps", cfgx::Node(static_cast<std::int64_t>(plan.Actions().size()))},
        {"planned_steps", BuildPlanActionsArray(plan)},
        {"source_trace", BuildSourceTraceArray(source_trace)},
    });

    if (dry_run)
    {
        pool.StopAndJoin(asyncx::StopMode::Drain);
        if (json_mode)
        {
            PrintJsonEnvelope(true, kExitSuccess, "dry run passed", publish_data);
        }
        else
        {
            std::cout << "dry_run=true\n";
            std::cout << "out=" << out_path << "\n";
            if (!snapshot_path.empty())
            {
                std::cout << "snapshot=" << snapshot_path << "\n";
            }
            std::cout << "steps=" << plan.Actions().size() << "\n";
        }
        return kExitSuccess;
    }

    fsx::RunOptions run_options;
    run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    run_options.rollback_mode = fsx::RollbackMode::BestEffort;
    run_options.journal_path = journal_path;
    run_options.keep_journal_on_success = !journal_path.empty();

    const auto run = fsx::Run(plan, run_options);
    if (!run.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, "failed to publish config: " + run.error);
    }

    pool.StopAndJoin(asyncx::StopMode::Drain);

    LOGI("toolx-sync published %s steps=%zu", out_path.c_str(), run.steps.size());
    logsys::Logger::Instance().Flush();

    if (json_mode)
    {
        PrintJsonEnvelope(true, kExitSuccess, "published", publish_data);
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
