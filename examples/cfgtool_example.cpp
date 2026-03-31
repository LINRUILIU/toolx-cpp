#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "argtool.h"
#include "cfgx.h"

namespace
{

    constexpr int kExitSuccess = 0;
    constexpr int kExitRuntimeError = 1;
    constexpr int kExitUsageError = 2;
    constexpr int kExitNotFound = 3;
    constexpr int kExitValidationFailed = 4;

    bool WantsJsonOutput(int argc, const char *const argv[])
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--json")
            {
                return true;
            }
        }
        return false;
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

    cfgx::Node BuildStringArray(const std::vector<std::string> &items)
    {
        cfgx::Node::Array arr;
        arr.reserve(items.size());
        for (const auto &item : items)
        {
            arr.emplace_back(cfgx::Node(item));
        }
        return cfgx::Node(std::move(arr));
    }

    void PrintJsonEnvelope(bool ok,
                           int code,
                           std::string_view message,
                           const cfgx::Node &data,
                           const std::vector<cfgx::ValidationIssue> &issues = {})
    {
        cfgx::Node::Array issue_arr;
        issue_arr.reserve(issues.size());
        for (const auto &issue : issues)
        {
            issue_arr.emplace_back(BuildDataObject({
                {"path", cfgx::Node(issue.path)},
                {"message", cfgx::Node(issue.message)},
            }));
        }

        const cfgx::Node envelope = BuildDataObject({
            {"schema", cfgx::Node("cfgtool.result")},
            {"schema_version", cfgx::Node(std::int64_t(2))},
            {"ok", cfgx::Node(ok)},
            {"code", cfgx::Node(static_cast<std::int64_t>(code))},
            {"message", cfgx::Node(std::string(message))},
            {"issues", cfgx::Node(std::move(issue_arr))},
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

    int ExitSuccess(bool json_mode,
                    std::string_view message,
                    const cfgx::Node &data,
                    const std::vector<cfgx::ValidationIssue> &issues = {})
    {
        if (json_mode)
        {
            PrintJsonEnvelope(true, kExitSuccess, message, data, issues);
        }
        return kExitSuccess;
    }

    class CliLogger final : public argtool::IParseLogger
    {
    public:
        void OnError(const argtool::ParseError &error) override
        {
            std::cerr << "[cfgtool.parse-error] kind=" << static_cast<int>(error.kind)
                      << " field=" << error.field
                      << " token=" << error.token
                      << " message=" << error.message << "\n";
        }

        void OnWarning(std::string_view message) override
        {
            std::cerr << "[cfgtool.parse-warning] " << message << "\n";
        }
    };

    cfgx::Result<cfgx::Node> BuildNodeFromRaw(std::string_view type_text, std::string_view raw)
    {
        const std::string type = [&]()
        {
            std::string out(type_text);
            for (char &ch : out)
            {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return out;
        }();

        if (type == "string")
        {
            return cfgx::Result<cfgx::Node>{true, cfgx::Node(std::string(raw)), ""};
        }
        if (type == "int")
        {
            std::int64_t value = 0;
            const auto *begin = raw.data();
            const auto *end = raw.data() + raw.size();
            const auto parsed = std::from_chars(begin, end, value);
            if (parsed.ec != std::errc() || parsed.ptr != end)
            {
                return cfgx::Result<cfgx::Node>{false, cfgx::Node{}, "invalid int value"};
            }
            return cfgx::Result<cfgx::Node>{true, cfgx::Node(value), ""};
        }
        if (type == "double")
        {
            char *parse_end = nullptr;
            const std::string copy(raw);
            const double value = std::strtod(copy.c_str(), &parse_end);
            if (parse_end == nullptr || *parse_end != '\0')
            {
                return cfgx::Result<cfgx::Node>{false, cfgx::Node{}, "invalid double value"};
            }
            return cfgx::Result<cfgx::Node>{true, cfgx::Node(value), ""};
        }
        if (type == "bool")
        {
            if (raw == "true" || raw == "1" || raw == "yes" || raw == "on")
            {
                return cfgx::Result<cfgx::Node>{true, cfgx::Node(true), ""};
            }
            if (raw == "false" || raw == "0" || raw == "no" || raw == "off")
            {
                return cfgx::Result<cfgx::Node>{true, cfgx::Node(false), ""};
            }
            return cfgx::Result<cfgx::Node>{false, cfgx::Node{}, "invalid bool value"};
        }
        if (type == "null")
        {
            return cfgx::Result<cfgx::Node>{true, cfgx::Node(), ""};
        }
        if (type == "json")
        {
            return cfgx::ParseJson(raw);
        }

        return cfgx::Result<cfgx::Node>{false, cfgx::Node{}, "unsupported value type: " + type};
    }

    bool RequireFields(const argtool::ParseResult &result,
                       const std::vector<std::string> &keys,
                       std::string *error)
    {
        for (const auto &key : keys)
        {
            if (!result.Has(key) || result.GetString(key).empty())
            {
                *error = "missing required option --" + key;
                return false;
            }
        }
        return true;
    }

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
        if (pos == std::string::npos)
        {
            return false;
        }

        *left = TrimCopy(text.substr(0, pos));
        *right = TrimCopy(text.substr(pos + 1));
        return !left->empty() && !right->empty();
    }

    std::vector<std::string> SplitTokens(std::string_view text, char delimiter)
    {
        std::vector<std::string> out;
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            const std::size_t split = text.find(delimiter, begin);
            const std::size_t end = (split == std::string_view::npos) ? text.size() : split;
            const std::string token = TrimCopy(text.substr(begin, end - begin));
            if (!token.empty())
            {
                out.push_back(token);
            }
            if (split == std::string_view::npos)
            {
                break;
            }
            begin = split + 1;
        }
        return out;
    }

    bool BuildValidationRules(const argtool::ParseResult &result,
                              std::vector<cfgx::ValidationRule> *rules,
                              std::string *error)
    {
        const bool fail_fast = result.GetBool("fail-fast", false);

        for (const auto &path : result.GetAll("require"))
        {
            if (TrimCopy(path).empty())
            {
                *error = "--require contains empty path";
                return false;
            }
            rules->push_back(cfgx::RequirePathRule(path, fail_fast));
        }

        for (const auto &spec : result.GetAll("expect"))
        {
            std::string path;
            std::string kind_text;
            if (!SplitOnce(spec, '=', &path, &kind_text))
            {
                *error = "invalid --expect format, expected PATH=TYPE: " + spec;
                return false;
            }

            const auto kind = cfgx::ParseNodeKind(kind_text);
            if (!kind.has_value())
            {
                *error = "unknown expected type in --expect: " + kind_text;
                return false;
            }

            rules->push_back(cfgx::ExpectKindRule(path, *kind, fail_fast));
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

            rules->push_back(cfgx::NumericRangeRule(path, min_value, max_value, fail_fast));
        }

        for (const auto &spec : result.GetAll("choice"))
        {
            std::string path;
            std::string choices_text;
            if (!SplitOnce(spec, '=', &path, &choices_text))
            {
                *error = "invalid --choice format, expected PATH=V1|V2: " + spec;
                return false;
            }

            auto choices = SplitTokens(choices_text, '|');
            if (choices.empty())
            {
                *error = "--choice requires at least one candidate: " + spec;
                return false;
            }

            rules->push_back(cfgx::ChoiceRule(path, std::move(choices), true, fail_fast));
        }

        for (const auto &spec : result.GetAll("mutex"))
        {
            auto paths = SplitTokens(spec, ',');
            if (paths.size() < 2)
            {
                *error = "--mutex requires at least two paths separated by ',': " + spec;
                return false;
            }

            rules->push_back(cfgx::MutexRule(std::move(paths), fail_fast));
        }

        for (const auto &spec : result.GetAll("depends"))
        {
            std::string path;
            std::string depends_on;
            if (!SplitOnce(spec, '=', &path, &depends_on))
            {
                *error = "invalid --depends format, expected PATH=DEPENDS_ON: " + spec;
                return false;
            }

            rules->push_back(cfgx::DependencyRule(path, depends_on, fail_fast));
        }

        for (const auto &spec : result.GetAll("strlen"))
        {
            std::string path;
            std::string bounds;
            if (!SplitOnce(spec, '=', &path, &bounds))
            {
                *error = "invalid --strlen format, expected PATH=MIN:MAX: " + spec;
                return false;
            }

            std::string min_text;
            std::string max_text;
            if (!SplitOnce(bounds, ':', &min_text, &max_text))
            {
                *error = "invalid --strlen bounds, expected MIN:MAX in: " + spec;
                return false;
            }

            std::uint64_t min_len = 0;
            {
                const auto *begin = min_text.data();
                const auto *end = min_text.data() + min_text.size();
                const auto parsed = std::from_chars(begin, end, min_len);
                if (parsed.ec != std::errc() || parsed.ptr != end)
                {
                    *error = "invalid --strlen min value: " + min_text;
                    return false;
                }
            }

            std::uint64_t max_len = 0;
            {
                const auto *begin = max_text.data();
                const auto *end = max_text.data() + max_text.size();
                const auto parsed = std::from_chars(begin, end, max_len);
                if (parsed.ec != std::errc() || parsed.ptr != end)
                {
                    *error = "invalid --strlen max value: " + max_text;
                    return false;
                }
            }

            if (min_len > max_len)
            {
                *error = "--strlen min must be <= max: " + spec;
                return false;
            }

            rules->push_back(cfgx::StringLengthRule(path,
                                                    static_cast<std::size_t>(min_len),
                                                    static_cast<std::size_t>(max_len),
                                                    fail_fast));
        }

        return true;
    }

    std::string CanonicalText(const cfgx::Node &node)
    {
        return cfgx::ToJson(node, 0);
    }

} // namespace

int main(int argc, const char *const argv[])
{
    const bool requested_json = WantsJsonOutput(argc, argv);
    CliLogger logger;

    argtool::Parser parser;
    parser.SetProgramName("cfgtool")
        .SetDescription("cfgtool - thin CLI over cfgx")
        .SetUsageExample("cfgtool get --file app.json --path svc.port")
        .SetHelpLayout(argtool::HelpLayout::Fixed)
        .EnableTrace(false)
        .SetLogger(&logger);

    // Keep cli contract aligned with argtool style: explicit subcommand roots and stable help layout.
    parser.AddSubcommandRoot("load", "Load config and print normalized output")
        .AddSubcommandRoot("adapters", "List parser adapters and active adapter")
        .AddSubcommandRoot("adapter-activate", "Activate parser adapter for current process")
        .AddSubcommandRoot("snapshot-export", "Export current reloader snapshot to file")
        .AddSubcommandRoot("snapshot-restore", "Restore snapshot file and write resolved config")
        .AddSubcommandRoot("get", "Get value by config path")
        .AddSubcommandRoot("set", "Set value by config path")
        .AddSubcommandRoot("exists", "Check if a path exists")
        .AddSubcommandRoot("merge", "Merge two config files")
        .AddSubcommandRoot("validate", "Run validation rules against config")
        .AddSubcommandRoot("reload-dryrun", "Dry-run candidate config reload and report whether effective config changes");

    parser.Option("file", 'f').String().ValueName("FILE").Description("Input config file path.").Done();
    parser.Option("path", 'p').String().ValueName("PATH").Description("Config path (dot + [index] with escaping).").Done();
    parser.Option("value", 'v').String().ValueName("VALUE").Description("Raw value to set.").Done();
    parser.Option("type", 't').String().Default("string").Choices({"string", "int", "double", "bool", "null", "json"}).Description("Value type for --value.").Done();
    parser.Option("base", 'b').String().ValueName("FILE").Description("Base config for merge.").Done();
    parser.Option("overlay", 'o').String().ValueName("FILE").Description("Overlay config for merge.").Done();
    parser.Option("out", 'w').String().ValueName("FILE").Description("Output file path.").Done();
    parser.Option("snapshot", 's').String().ValueName("FILE").Description("Snapshot file path.").Done();
    parser.Option("current").String().ValueName("FILE").Description("Current config file for reload-dryrun.").Done();
    parser.Option("candidate").String().ValueName("FILE").Description("Candidate config file for reload-dryrun.").Done();
    parser.Option("adapter").String().ValueName("NAME").Description("Parser adapter name used by adapter-activate.").Done();
    parser.Option("indent", 'i').Int().Default("2").Description("JSON indent width.").Done();
    parser.Option("require").String().ListValue().ValueName("PATH").Description("Validation rule: required path.").Done();
    parser.Option("expect").String().ListValue().ValueName("PATH=TYPE").Description("Validation rule: expected node kind.").Done();
    parser.Option("range").String().ListValue().ValueName("PATH=MIN:MAX").Description("Validation rule: numeric range check.").Done();
    parser.Option("choice").String().ListValue().ValueName("PATH=V1|V2").Description("Validation rule: value must be one of candidates.").Done();
    parser.Option("mutex").String().ListValue().ValueName("PATH1,PATH2[,PATHN]").Description("Validation rule: listed paths are mutually exclusive.").Done();
    parser.Option("depends").String().ListValue().ValueName("PATH=DEPENDS_ON").Description("Validation rule: PATH requires DEPENDS_ON.").Done();
    parser.Option("strlen").String().ListValue().ValueName("PATH=MIN:MAX").Description("Validation rule: string length range.").Done();
    parser.Flag("append-arrays", 'a').Description("Append arrays during merge instead of override.").Done();
    parser.Flag("fail-fast").Description("Validation: stop on first issue.").Done();
    parser.Flag("json").Description("Emit machine-readable JSON envelope output.").Done();

    const argtool::ParseResult result = parser.Parse(argc, argv);
    if (result.help_requested)
    {
        if (requested_json)
        {
            PrintJsonEnvelope(true,
                              kExitSuccess,
                              "help requested",
                              BuildDataObject({
                                  {"help", cfgx::Node(parser.HelpText())},
                              }));
        }
        else
        {
            std::cout << parser.HelpText();
        }
        return kExitSuccess;
    }

    if (!result.ok)
    {
        const std::string message = result.error.has_value() ? result.error->message : "parse failed";
        if (requested_json)
        {
            PrintJsonEnvelope(false,
                              kExitUsageError,
                              message,
                              BuildDataObject({
                                  {"help", cfgx::Node(parser.HelpText())},
                              }));
        }
        else
        {
            std::cerr << "error: " << message << "\n\n";
            std::cerr << parser.HelpText();
        }
        return kExitUsageError;
    }

    const bool json_mode = result.GetBool("json", requested_json);
    if (!result.subcommand_path.has_value() || result.subcommand_path->root.empty())
    {
        if (json_mode)
        {
            PrintJsonEnvelope(false,
                              kExitUsageError,
                              "missing subcommand",
                              BuildDataObject({
                                  {"help", cfgx::Node(parser.HelpText())},
                              }));
        }
        else
        {
            std::cerr << "error: missing subcommand\n\n";
            std::cerr << parser.HelpText();
        }
        return kExitUsageError;
    }

    const std::string command = result.subcommand_path->root;
    const int indent = result.GetInt("indent", 2);

    if (command == "adapters")
    {
        const std::vector<std::string> adapters = cfgx::ListParserAdapters();
        const std::string active = cfgx::GetActiveParserAdapter();

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"active", cfgx::Node(active)},
                                   {"count", cfgx::Node(static_cast<std::int64_t>(adapters.size()))},
                                   {"adapters", BuildStringArray(adapters)},
                               }));
        }

        std::cout << "active=" << (active.empty() ? "(none)" : active) << "\n";
        std::cout << "count=" << adapters.size() << "\n";
        for (const auto &name : adapters)
        {
            std::cout << name << "\n";
        }
        return kExitSuccess;
    }

    if (command == "adapter-activate")
    {
        std::string error;
        if (!RequireFields(result, {"adapter"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const std::string adapter_name = result.GetString("adapter");
        const auto activate_st = cfgx::SetActiveParserAdapter(adapter_name);
        if (!activate_st.ok)
        {
            return ExitError(json_mode,
                             kExitNotFound,
                             activate_st.error,
                             BuildDataObject({
                                 {"command", cfgx::Node(command)},
                                 {"adapter", cfgx::Node(adapter_name)},
                             }));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"adapter", cfgx::Node(adapter_name)},
                                   {"active", cfgx::Node(cfgx::GetActiveParserAdapter())},
                               }));
        }

        std::cout << "active=" << cfgx::GetActiveParserAdapter() << "\n";
        return kExitSuccess;
    }

    if (command == "load")
    {
        std::string error;
        if (!RequireFields(result, {"file"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto loaded = cfgx::LoadFromFile(result.GetString("file"));
        if (!loaded.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, loaded.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"file", cfgx::Node(result.GetString("file"))},
                                   {"format", cfgx::Node(cfgx::ToString(cfgx::DetectFormatFromPath(result.GetString("file"))))},
                                   {"root_kind", cfgx::Node(cfgx::ToString(loaded.value.Kind()))},
                                   {"config", loaded.value},
                               }));
        }

        std::cout << "format=" << cfgx::ToString(cfgx::DetectFormatFromPath(result.GetString("file"))) << "\n";
        std::cout << "root_kind=" << cfgx::ToString(loaded.value.Kind()) << "\n";
        std::cout << cfgx::ToJson(loaded.value, indent) << "\n";
        return kExitSuccess;
    }

    if (command == "snapshot-export")
    {
        std::string error;
        if (!RequireFields(result, {"file", "out"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        cfgx::PollReloader reloader(result.GetString("file"));
        cfgx::ReloadOptions options;
        options.debounce_ms = 0;
        reloader.SetOptions(options);

        const auto loaded = reloader.ReloadNow();
        if (!loaded.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             "failed to initialize reloader: " + loaded.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto exported = reloader.ExportSnapshotToFile(result.GetString("out"), cfgx::ConfigFormat::Unknown, indent);
        if (!exported.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             exported.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"file", cfgx::Node(result.GetString("file"))},
                                   {"out", cfgx::Node(result.GetString("out"))},
                                   {"audit_entries", cfgx::Node(static_cast<std::int64_t>(reloader.AuditTrail().size()))},
                               }));
        }

        std::cout << "ok\n";
        return kExitSuccess;
    }

    if (command == "snapshot-restore")
    {
        std::string error;
        if (!RequireFields(result, {"file", "snapshot"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const std::string out_path = result.Has("out") ? result.GetString("out") : result.GetString("file");

        cfgx::PollReloader reloader(result.GetString("file"));
        cfgx::ReloadOptions options;
        options.debounce_ms = 0;
        reloader.SetOptions(options);

        const auto loaded = reloader.ReloadNow();
        if (!loaded.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             "failed to initialize reloader: " + loaded.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto restored = reloader.RestoreSnapshotFromFile(result.GetString("snapshot"), cfgx::ConfigFormat::Unknown, nullptr);
        if (!restored.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             restored.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto *current = reloader.Current();
        if (current == nullptr)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             "no active config after snapshot restore",
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto saved = cfgx::SaveToFile(*current, out_path, cfgx::ConfigFormat::Unknown, indent);
        if (!saved.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             saved.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"file", cfgx::Node(result.GetString("file"))},
                                   {"snapshot", cfgx::Node(result.GetString("snapshot"))},
                                   {"out", cfgx::Node(out_path)},
                                   {"audit_entries", cfgx::Node(static_cast<std::int64_t>(reloader.AuditTrail().size()))},
                               }));
        }

        std::cout << "ok\n";
        return kExitSuccess;
    }

    if (command == "get")
    {
        std::string error;
        if (!RequireFields(result, {"file", "path"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto loaded = cfgx::LoadFromFile(result.GetString("file"));
        if (!loaded.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, loaded.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto value = cfgx::GetNode(loaded.value, result.GetString("path"));
        if (!value.ok || value.value == nullptr)
        {
            return ExitError(json_mode,
                             kExitNotFound,
                             value.error,
                             BuildDataObject({
                                 {"command", cfgx::Node(command)},
                                 {"path", cfgx::Node(result.GetString("path"))},
                             }));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"path", cfgx::Node(result.GetString("path"))},
                                   {"kind", cfgx::Node(cfgx::ToString(value.value->Kind()))},
                                   {"value", *value.value},
                               }));
        }

        std::cout << cfgx::ToJson(*value.value, indent) << "\n";
        return kExitSuccess;
    }

    if (command == "exists")
    {
        std::string error;
        if (!RequireFields(result, {"file", "path"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto loaded = cfgx::LoadFromFile(result.GetString("file"));
        if (!loaded.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, loaded.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const bool found = cfgx::Exists(loaded.value, result.GetString("path"));
        if (json_mode)
        {
            const cfgx::Node data = BuildDataObject({
                {"command", cfgx::Node(command)},
                {"path", cfgx::Node(result.GetString("path"))},
                {"exists", cfgx::Node(found)},
            });
            if (found)
            {
                return ExitSuccess(true, "ok", data);
            }
            PrintJsonEnvelope(false, kExitNotFound, "path not found", data);
            return kExitNotFound;
        }

        std::cout << (found ? "true" : "false") << "\n";
        return found ? kExitSuccess : kExitNotFound;
    }

    if (command == "set")
    {
        std::string error;
        if (!RequireFields(result, {"file", "path", "value"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        cfgx::Node root;
        const std::string file = result.GetString("file");
        if (std::filesystem::exists(file))
        {
            auto loaded = cfgx::LoadFromFile(file);
            if (!loaded.ok)
            {
                return ExitError(json_mode, kExitRuntimeError, loaded.error, BuildDataObject({{"command", cfgx::Node(command)}}));
            }
            root = std::move(loaded.value);
        }
        else
        {
            root = cfgx::Node::MakeObject();
        }

        const auto converted = BuildNodeFromRaw(result.GetString("type"), result.GetString("value"));
        if (!converted.ok)
        {
            return ExitError(json_mode, kExitUsageError, converted.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto set_st = cfgx::SetNode(root, result.GetString("path"), converted.value);
        if (!set_st.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, set_st.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto save_st = cfgx::SaveToFile(root, file, cfgx::ConfigFormat::Unknown, indent);
        if (!save_st.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, save_st.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"file", cfgx::Node(file)},
                                   {"path", cfgx::Node(result.GetString("path"))},
                                   {"type", cfgx::Node(result.GetString("type"))},
                               }));
        }

        std::cout << "ok\n";
        return kExitSuccess;
    }

    if (command == "merge")
    {
        std::string error;
        if (!RequireFields(result, {"base", "overlay", "out"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        auto base = cfgx::LoadFromFile(result.GetString("base"));
        if (!base.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, base.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        auto overlay = cfgx::LoadFromFile(result.GetString("overlay"));
        if (!overlay.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, overlay.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const bool append_arrays = result.GetBool("append-arrays", false);
        const auto merge_st = cfgx::Merge(base.value, overlay.value, append_arrays);
        if (!merge_st.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, merge_st.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto save_st = cfgx::SaveToFile(base.value, result.GetString("out"), cfgx::ConfigFormat::Unknown, indent);
        if (!save_st.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, save_st.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        if (json_mode)
        {
            return ExitSuccess(true,
                               "ok",
                               BuildDataObject({
                                   {"command", cfgx::Node(command)},
                                   {"out", cfgx::Node(result.GetString("out"))},
                                   {"append_arrays", cfgx::Node(result.GetBool("append-arrays", false))},
                               }));
        }

        std::cout << "ok\n";
        return kExitSuccess;
    }

    if (command == "validate")
    {
        std::string error;
        if (!RequireFields(result, {"file"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto loaded = cfgx::LoadFromFile(result.GetString("file"));
        if (!loaded.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, loaded.error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        std::vector<cfgx::ValidationRule> rules;
        if (!BuildValidationRules(result, &rules, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto validation = cfgx::Validate(loaded.value, rules);
        if (json_mode)
        {
            const cfgx::Node data = BuildDataObject({
                {"command", cfgx::Node(command)},
                {"issues_count", cfgx::Node(static_cast<std::int64_t>(validation.value.size()))},
            });
            if (validation.ok)
            {
                return ExitSuccess(true, "validation passed", data, validation.value);
            }
            PrintJsonEnvelope(false, kExitValidationFailed, "validation failed", data, validation.value);
            return kExitValidationFailed;
        }

        std::cout << "issues=" << validation.value.size() << "\n";
        for (std::size_t i = 0; i < validation.value.size(); ++i)
        {
            std::cout << "[" << i + 1 << "] path=" << validation.value[i].path
                      << " message=" << validation.value[i].message << "\n";
        }
        return validation.ok ? kExitSuccess : kExitValidationFailed;
    }

    if (command == "reload-dryrun")
    {
        std::string error;
        if (!RequireFields(result, {"current", "candidate"}, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto current = cfgx::LoadFromFile(result.GetString("current"));
        if (!current.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             "failed to load current config: " + current.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto candidate = cfgx::LoadFromFile(result.GetString("candidate"));
        if (!candidate.ok)
        {
            return ExitError(json_mode,
                             kExitRuntimeError,
                             "failed to load candidate config: " + candidate.error,
                             BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        std::vector<cfgx::ValidationRule> rules;
        if (!BuildValidationRules(result, &rules, &error))
        {
            return ExitError(json_mode, kExitUsageError, error, BuildDataObject({{"command", cfgx::Node(command)}}));
        }

        const auto validation = cfgx::Validate(candidate.value, rules);

        const std::string current_text = CanonicalText(current.value);
        const std::string candidate_text = CanonicalText(candidate.value);
        const bool changed = current_text != candidate_text;

        if (json_mode)
        {
            const cfgx::Node data = BuildDataObject({
                {"command", cfgx::Node(command)},
                {"current_format", cfgx::Node(cfgx::ToString(cfgx::DetectFormatFromPath(result.GetString("current"))))},
                {"candidate_format", cfgx::Node(cfgx::ToString(cfgx::DetectFormatFromPath(result.GetString("candidate"))))},
                {"changed", cfgx::Node(changed)},
                {"issues_count", cfgx::Node(static_cast<std::int64_t>(validation.value.size()))},
            });
            if (validation.ok)
            {
                return ExitSuccess(true, "validation passed", data, validation.value);
            }
            PrintJsonEnvelope(false, kExitValidationFailed, "validation failed", data, validation.value);
            return kExitValidationFailed;
        }

        std::cout << "reload_dryrun.current_format="
                  << cfgx::ToString(cfgx::DetectFormatFromPath(result.GetString("current"))) << "\n";
        std::cout << "reload_dryrun.candidate_format="
                  << cfgx::ToString(cfgx::DetectFormatFromPath(result.GetString("candidate"))) << "\n";
        std::cout << "reload_dryrun.changed=" << (changed ? "true" : "false") << "\n";
        std::cout << "reload_dryrun.validation_issues=" << validation.value.size() << "\n";

        for (std::size_t i = 0; i < validation.value.size(); ++i)
        {
            std::cout << "[" << i + 1 << "] path=" << validation.value[i].path
                      << " message=" << validation.value[i].message << "\n";
        }

        return validation.ok ? kExitSuccess : kExitValidationFailed;
    }

    return ExitError(json_mode,
                     kExitUsageError,
                     "unsupported subcommand: " + command,
                     BuildDataObject({{"command", cfgx::Node(command)}}));
}
