#include <algorithm>
#include <array>
#include <charconv>
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

enum class InputFormat : std::uint8_t
{
    Auto,
    LogsysText,
    Jsonl,
};

constexpr std::array<logsys::LogLevel, 7> kAllLevels = {
    logsys::LogLevel::Trace, logsys::LogLevel::Debug, logsys::LogLevel::Info,     logsys::LogLevel::Warning,
    logsys::LogLevel::Error, logsys::LogLevel::Fatal, logsys::LogLevel::Critical,
};

struct ParsedRecord
{
    std::string file;
    std::uint64_t line_number{0};
    std::string raw;
    std::string time;
    logsys::LogLevel level{logsys::LogLevel::Info};
    std::string message;
};

struct Sample
{
    std::string file;
    std::uint64_t line_number{0};
    std::string time;
    std::string level;
    std::string message;
    std::string raw;
};

struct LogConfig
{
    std::string command;
    std::string manifest;
    std::vector<std::string> files;
    InputFormat format{InputFormat::Auto};
    std::array<bool, 7> levels{};
    bool has_levels{false};
    std::optional<logsys::LogLevel> min_level;
    std::string contains;
    std::string since;
    std::string until;
    std::size_t max_samples{20};
    std::optional<logsys::LogLevel> fail_on_level;
    std::optional<std::size_t> max_parse_errors;
    std::string log_file;
};

struct Summary
{
    std::vector<std::string> files;
    std::uint64_t lines_read{0};
    std::uint64_t blank_lines{0};
    std::uint64_t parsed{0};
    std::uint64_t matched{0};
    std::uint64_t parse_failures{0};
    std::uint64_t time_missing{0};
    std::array<std::uint64_t, 7> by_level{};
    std::string first_time;
    std::string last_time;
    std::vector<Sample> samples;
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

std::string ToLowerCopy(std::string_view input)
{
    std::string out(input);
    for (char& ch : out)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
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
        {"schema", cfgx::Node("toolx.log.result")},
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

std::optional<InputFormat> ParseFormat(std::string_view input)
{
    const std::string text = ToLowerCopy(TrimCopy(input));
    if (text == "auto")
    {
        return InputFormat::Auto;
    }
    if (text == "logsys-text")
    {
        return InputFormat::LogsysText;
    }
    if (text == "jsonl")
    {
        return InputFormat::Jsonl;
    }
    return std::nullopt;
}

std::string FormatName(InputFormat format)
{
    switch (format)
    {
    case InputFormat::Auto:
        return "auto";
    case InputFormat::LogsysText:
        return "logsys-text";
    case InputFormat::Jsonl:
        return "jsonl";
    }
    return "auto";
}

std::optional<logsys::LogLevel> ParseLevel(std::string_view input)
{
    return logsys::ParseLogLevel(TrimCopy(input));
}

std::string LevelName(logsys::LogLevel level)
{
    return ToLowerCopy(logsys::ToString(level));
}

std::size_t LevelIndex(logsys::LogLevel level)
{
    return static_cast<std::size_t>(level);
}

bool IsAtOrAbove(logsys::LogLevel actual, logsys::LogLevel threshold)
{
    return static_cast<int>(actual) >= static_cast<int>(threshold);
}

bool ParseSize(std::string_view input, std::size_t* value)
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

    *value = static_cast<std::size_t>(parsed);
    return true;
}

bool IsDigitAt(std::string_view text, std::size_t index)
{
    return index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0;
}

bool IsLogsysTimestamp(std::string_view text)
{
    if (text.size() < 23)
    {
        return false;
    }

    return IsDigitAt(text, 0) && IsDigitAt(text, 1) && IsDigitAt(text, 2) && IsDigitAt(text, 3) && text[4] == '-' &&
           IsDigitAt(text, 5) && IsDigitAt(text, 6) && text[7] == '-' && IsDigitAt(text, 8) && IsDigitAt(text, 9) &&
           text[10] == ' ' && IsDigitAt(text, 11) && IsDigitAt(text, 12) && text[13] == ':' && IsDigitAt(text, 14) &&
           IsDigitAt(text, 15) && text[16] == ':' && IsDigitAt(text, 17) && IsDigitAt(text, 18) && text[19] == '.' &&
           IsDigitAt(text, 20) && IsDigitAt(text, 21) && IsDigitAt(text, 22);
}

bool IsValidTimeFilter(std::string_view text)
{
    return text.empty() || (text.size() == 23 && IsLogsysTimestamp(text));
}

std::vector<std::string> ReadStringArrayField(const cfgx::Node& root, std::string_view field)
{
    std::vector<std::string> values;
    const auto* node = root.Get(field);
    const auto* arr = node == nullptr ? nullptr : node->TryArray();
    if (arr == nullptr)
    {
        return values;
    }
    values.reserve(arr->size());
    for (const auto& item : *arr)
    {
        values.push_back(item.AsString());
    }
    return values;
}

cfgx::Node ManifestSchema()
{
    const char* schema_text = R"({
      "type": "object",
      "required": ["files"],
      "properties": {
        "files": {"type": "array", "items": {"type": "string"}},
        "format": {"type": "string", "enum": ["auto", "logsys-text", "jsonl"]},
        "level": {"type": "array", "items": {"type": "string"}},
        "min_level": {"type": "string"},
        "contains": {"type": "string"},
        "since": {"type": "string"},
        "until": {"type": "string"},
        "max_samples": {"type": "integer"},
        "fail_on_level": {"type": "string"},
        "max_parse_errors": {"type": "integer"}
      },
      "additionalProperties": false
    })";

    const auto parsed = cfgx::ParseJson(schema_text);
    return parsed.ok ? parsed.value : cfgx::Node::MakeObject();
}

cfgx::Result<LogConfig> LoadManifestConfig(const std::string& path)
{
    LogConfig config;
    if (path.empty())
    {
        return cfgx::Result<LogConfig>{true, std::move(config), ""};
    }

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        return cfgx::Result<LogConfig>{false, {}, "manifest file not found: " + path};
    }

    auto loaded = cfgx::LoadFromFile(path);
    if (!loaded.ok)
    {
        return cfgx::Result<LogConfig>{false, {}, "manifest parse failed: " + loaded.error};
    }

    const auto compiled = schemax::Compile(ManifestSchema());
    if (!compiled.ok)
    {
        return cfgx::Result<LogConfig>{false, {}, "internal manifest schema failed: " + compiled.error};
    }

    const auto issues = schemax::Validate(loaded.value, compiled.value);
    if (!issues.empty())
    {
        std::string message = "manifest validation failed";
        if (!issues.front().path.empty())
        {
            message += ": " + issues.front().path + " " + issues.front().message;
        }
        return cfgx::Result<LogConfig>{false, {}, message};
    }

    config.manifest = path;
    config.files = ReadStringArrayField(loaded.value, "files");
    if (const auto* format = loaded.value.Get("format"); format != nullptr)
    {
        const auto parsed = ParseFormat(format->AsString());
        if (!parsed.has_value())
        {
            return cfgx::Result<LogConfig>{false, {}, "unsupported manifest format: " + format->AsString()};
        }
        config.format = *parsed;
    }
    for (const auto& item : ReadStringArrayField(loaded.value, "level"))
    {
        const auto parsed = ParseLevel(item);
        if (!parsed.has_value())
        {
            return cfgx::Result<LogConfig>{false, {}, "unsupported manifest level: " + item};
        }
        config.levels[LevelIndex(*parsed)] = true;
        config.has_levels = true;
    }
    if (const auto* min_level = loaded.value.Get("min_level"); min_level != nullptr)
    {
        const auto parsed = ParseLevel(min_level->AsString());
        if (!parsed.has_value())
        {
            return cfgx::Result<LogConfig>{false, {}, "unsupported manifest min_level: " + min_level->AsString()};
        }
        config.min_level = *parsed;
    }
    if (const auto* contains = loaded.value.Get("contains"); contains != nullptr)
    {
        config.contains = contains->AsString();
    }
    if (const auto* since = loaded.value.Get("since"); since != nullptr)
    {
        config.since = since->AsString();
    }
    if (const auto* until = loaded.value.Get("until"); until != nullptr)
    {
        config.until = until->AsString();
    }
    if (const auto* max_samples = loaded.value.Get("max_samples"); max_samples != nullptr)
    {
        if (max_samples->AsInt(-1) < 0)
        {
            return cfgx::Result<LogConfig>{false, {}, "manifest max_samples must be non-negative"};
        }
        config.max_samples = static_cast<std::size_t>(max_samples->AsInt(20));
    }
    if (const auto* fail_level = loaded.value.Get("fail_on_level"); fail_level != nullptr)
    {
        const auto parsed = ParseLevel(fail_level->AsString());
        if (!parsed.has_value())
        {
            return cfgx::Result<LogConfig>{false, {}, "unsupported manifest fail_on_level: " + fail_level->AsString()};
        }
        config.fail_on_level = *parsed;
    }
    if (const auto* max_parse_errors = loaded.value.Get("max_parse_errors"); max_parse_errors != nullptr)
    {
        if (max_parse_errors->AsInt(-1) < 0)
        {
            return cfgx::Result<LogConfig>{false, {}, "manifest max_parse_errors must be non-negative"};
        }
        config.max_parse_errors = static_cast<std::size_t>(max_parse_errors->AsInt(0));
    }

    return cfgx::Result<LogConfig>{true, std::move(config), ""};
}

cfgx::Result<LogConfig> ResolveConfig(const argtool::ParseResult& parsed, const std::string& command)
{
    if (parsed.Has("level") && parsed.Has("min-level"))
    {
        return cfgx::Result<LogConfig>{false, {}, "use either --level or --min-level, not both"};
    }

    auto manifest = LoadManifestConfig(parsed.GetString("manifest", ""));
    if (!manifest.ok)
    {
        return manifest;
    }

    LogConfig config = std::move(manifest.value);
    config.command = command;
    config.log_file = parsed.GetString("log-file", "");

    if (parsed.Has("file"))
    {
        config.files = parsed.GetAll("file");
    }
    if (parsed.Has("format"))
    {
        const auto format = ParseFormat(parsed.GetString("format", "auto"));
        if (!format.has_value())
        {
            return cfgx::Result<LogConfig>{false, {}, "unsupported format: " + parsed.GetString("format")};
        }
        config.format = *format;
    }
    if (parsed.Has("level"))
    {
        config.levels = {};
        config.has_levels = false;
        config.min_level = std::nullopt;
        for (const auto& item : parsed.GetAll("level"))
        {
            const auto level = ParseLevel(item);
            if (!level.has_value())
            {
                return cfgx::Result<LogConfig>{false, {}, "unsupported level: " + item};
            }
            config.levels[LevelIndex(*level)] = true;
            config.has_levels = true;
        }
    }
    if (parsed.Has("min-level"))
    {
        config.levels = {};
        config.has_levels = false;
        const auto level = ParseLevel(parsed.GetString("min-level"));
        if (!level.has_value())
        {
            return cfgx::Result<LogConfig>{false, {}, "unsupported min-level: " + parsed.GetString("min-level")};
        }
        config.min_level = *level;
    }
    if (parsed.Has("contains"))
    {
        config.contains = parsed.GetString("contains");
    }
    if (parsed.Has("since"))
    {
        config.since = parsed.GetString("since");
    }
    if (parsed.Has("until"))
    {
        config.until = parsed.GetString("until");
    }
    if (parsed.Has("max-samples"))
    {
        if (parsed.GetInt("max-samples", -1) < 0)
        {
            return cfgx::Result<LogConfig>{false, {}, "max-samples must be non-negative"};
        }
        config.max_samples = static_cast<std::size_t>(parsed.GetInt("max-samples", 20));
    }
    if (parsed.Has("fail-on-level"))
    {
        const auto level = ParseLevel(parsed.GetString("fail-on-level"));
        if (!level.has_value())
        {
            return cfgx::Result<LogConfig>{
                false, {}, "unsupported fail-on-level: " + parsed.GetString("fail-on-level")};
        }
        config.fail_on_level = *level;
    }
    if (parsed.Has("max-parse-errors"))
    {
        if (parsed.GetInt("max-parse-errors", -1) < 0)
        {
            return cfgx::Result<LogConfig>{false, {}, "max-parse-errors must be non-negative"};
        }
        config.max_parse_errors = static_cast<std::size_t>(parsed.GetInt("max-parse-errors", 0));
    }

    if (config.files.empty())
    {
        return cfgx::Result<LogConfig>{false, {}, "missing required option --file or --manifest"};
    }
    if (config.has_levels && config.min_level.has_value())
    {
        return cfgx::Result<LogConfig>{false, {}, "use either --level or --min-level, not both"};
    }
    if (!IsValidTimeFilter(config.since))
    {
        return cfgx::Result<LogConfig>{false, {}, "invalid --since timestamp"};
    }
    if (!IsValidTimeFilter(config.until))
    {
        return cfgx::Result<LogConfig>{false, {}, "invalid --until timestamp"};
    }
    if (!config.since.empty() && !config.until.empty() && config.since > config.until)
    {
        return cfgx::Result<LogConfig>{false, {}, "--since must be before or equal to --until"};
    }

    return cfgx::Result<LogConfig>{true, std::move(config), ""};
}

InputFormat DetectFormat(InputFormat configured, const std::string& first_non_blank)
{
    if (configured != InputFormat::Auto)
    {
        return configured;
    }
    return !first_non_blank.empty() && first_non_blank.front() == '{' ? InputFormat::Jsonl : InputFormat::LogsysText;
}

cfgx::Result<ParsedRecord> ParseJsonLine(std::string_view raw, std::string file, std::uint64_t line_number)
{
    const auto parsed = cfgx::ParseJson(raw);
    if (!parsed.ok)
    {
        return cfgx::Result<ParsedRecord>{false, {}, parsed.error};
    }
    if (!parsed.value.IsObject())
    {
        return cfgx::Result<ParsedRecord>{false, {}, "json log line must be an object"};
    }

    const auto* level_node = parsed.value.Get("level");
    if (level_node == nullptr || level_node->Kind() != cfgx::NodeKind::String)
    {
        return cfgx::Result<ParsedRecord>{false, {}, "json log line missing string field level"};
    }

    const auto level = ParseLevel(level_node->AsString());
    if (!level.has_value())
    {
        return cfgx::Result<ParsedRecord>{false, {}, "unsupported json log level: " + level_node->AsString()};
    }

    ParsedRecord record;
    record.file = std::move(file);
    record.line_number = line_number;
    record.raw = std::string(raw);
    record.level = *level;
    if (const auto* time = parsed.value.Get("time");
        time != nullptr && time->Kind() == cfgx::NodeKind::String && IsLogsysTimestamp(time->AsString()))
    {
        record.time = time->AsString();
    }
    if (const auto* msg = parsed.value.Get("msg"); msg != nullptr && msg->Kind() == cfgx::NodeKind::String)
    {
        record.message = msg->AsString();
    }
    return cfgx::Result<ParsedRecord>{true, std::move(record), ""};
}

cfgx::Result<ParsedRecord> ParseTextLine(std::string_view raw, std::string file, std::uint64_t line_number)
{
    std::string_view rest = raw;
    std::string time;
    if (IsLogsysTimestamp(rest))
    {
        time = std::string(rest.substr(0, 23));
        rest = rest.substr(23);
    }

    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0)
    {
        rest.remove_prefix(1);
    }

    const std::size_t split = rest.find_first_of(" \t\r\n");
    const std::string token = split == std::string_view::npos ? std::string(rest) : std::string(rest.substr(0, split));
    const auto level = ParseLevel(token);
    if (!level.has_value())
    {
        return cfgx::Result<ParsedRecord>{false, {}, "missing logsys level token"};
    }

    ParsedRecord record;
    record.file = std::move(file);
    record.line_number = line_number;
    record.raw = std::string(raw);
    record.time = std::move(time);
    record.level = *level;
    if (split != std::string_view::npos)
    {
        record.message = TrimCopy(rest.substr(split + 1));
    }
    return cfgx::Result<ParsedRecord>{true, std::move(record), ""};
}

bool MatchesFilters(const LogConfig& config, const ParsedRecord& record, Summary* summary)
{
    if (config.has_levels && !config.levels[LevelIndex(record.level)])
    {
        return false;
    }
    if (config.min_level.has_value() && !IsAtOrAbove(record.level, *config.min_level))
    {
        return false;
    }
    if (!config.contains.empty() && record.raw.find(config.contains) == std::string::npos)
    {
        return false;
    }

    const bool time_filter_active = !config.since.empty() || !config.until.empty();
    if (time_filter_active && record.time.empty())
    {
        ++summary->time_missing;
        return false;
    }
    if (!config.since.empty() && record.time < config.since)
    {
        return false;
    }
    return config.until.empty() || record.time <= config.until;
}

void AddMatchedRecord(const LogConfig& config, const ParsedRecord& record, Summary* summary)
{
    ++summary->matched;
    ++summary->by_level[LevelIndex(record.level)];
    if (!record.time.empty())
    {
        if (summary->first_time.empty() || record.time < summary->first_time)
        {
            summary->first_time = record.time;
        }
        if (summary->last_time.empty() || record.time > summary->last_time)
        {
            summary->last_time = record.time;
        }
    }
    else if (config.since.empty() && config.until.empty())
    {
        ++summary->time_missing;
    }

    if (summary->samples.size() < config.max_samples)
    {
        summary->samples.push_back(Sample{
            record.file,
            record.line_number,
            record.time,
            LevelName(record.level),
            record.message,
            record.raw,
        });
    }
}

cfgx::Result<Summary> SummarizeLogs(const LogConfig& config)
{
    Summary summary;
    summary.files = config.files;

    for (const auto& file : config.files)
    {
        std::error_code ec;
        if (!fs::exists(file, ec) || ec)
        {
            return cfgx::Result<Summary>{false, {}, "log file not found: " + file};
        }

        std::ifstream in(file, std::ios::binary);
        if (!in)
        {
            return cfgx::Result<Summary>{false, {}, "failed to open log file: " + file};
        }

        InputFormat active_format = config.format;
        bool detected = config.format != InputFormat::Auto;
        std::string line;
        std::uint64_t line_number = 0;
        while (std::getline(in, line))
        {
            ++line_number;
            ++summary.lines_read;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            const std::string trimmed = TrimCopy(line);
            if (trimmed.empty())
            {
                ++summary.blank_lines;
                continue;
            }
            if (!detected)
            {
                active_format = DetectFormat(config.format, trimmed);
                detected = true;
            }

            cfgx::Result<ParsedRecord> parsed = active_format == InputFormat::Jsonl
                                                    ? ParseJsonLine(line, file, line_number)
                                                    : ParseTextLine(line, file, line_number);
            if (!parsed.ok)
            {
                ++summary.parse_failures;
                if (summary.warnings.size() < 20)
                {
                    summary.warnings.push_back(file + ":" + std::to_string(line_number) +
                                               " parse failed: " + parsed.error);
                }
                continue;
            }

            ++summary.parsed;
            if (MatchesFilters(config, parsed.value, &summary))
            {
                AddMatchedRecord(config, parsed.value, &summary);
            }
        }
    }

    return cfgx::Result<Summary>{true, std::move(summary), ""};
}

cfgx::Node BuildLevelCountsObject(const Summary& summary)
{
    return BuildDataObject({
        {"trace", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Trace)]))},
        {"debug", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Debug)]))},
        {"info", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Info)]))},
        {"warning", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Warning)]))},
        {"error", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Error)]))},
        {"fatal", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Fatal)]))},
        {"critical", cfgx::Node(static_cast<std::int64_t>(summary.by_level[LevelIndex(logsys::LogLevel::Critical)]))},
    });
}

cfgx::Node BuildSamplesArray(const std::vector<Sample>& samples)
{
    cfgx::Node::Array arr;
    arr.reserve(samples.size());
    for (const auto& sample : samples)
    {
        arr.emplace_back(BuildDataObject({
            {"file", cfgx::Node(sample.file)},
            {"line_number", cfgx::Node(static_cast<std::int64_t>(sample.line_number))},
            {"time", cfgx::Node(sample.time)},
            {"level", cfgx::Node(sample.level)},
            {"message", cfgx::Node(sample.message)},
            {"raw", cfgx::Node(sample.raw)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

cfgx::Node BuildFilterObject(const LogConfig& config)
{
    std::vector<std::string> exact_levels;
    for (const auto level : kAllLevels)
    {
        if (config.levels[LevelIndex(level)])
        {
            exact_levels.push_back(LevelName(level));
        }
    }
    return BuildDataObject({
        {"level", BuildStringArray(exact_levels)},
        {"min_level", cfgx::Node(config.min_level.has_value() ? LevelName(*config.min_level) : "")},
        {"contains", cfgx::Node(config.contains)},
        {"since", cfgx::Node(config.since)},
        {"until", cfgx::Node(config.until)},
        {"fail_on_level", cfgx::Node(config.fail_on_level.has_value() ? LevelName(*config.fail_on_level) : "")},
        {"max_parse_errors",
         cfgx::Node(config.max_parse_errors.has_value() ? static_cast<std::int64_t>(*config.max_parse_errors)
                                                        : std::int64_t(-1))},
    });
}

cfgx::Node BuildCapabilitiesObject()
{
    return BuildDataObject({
        {"offline_only", cfgx::Node(true)},
        {"formats", BuildStringArray({"logsys-text", "jsonl"})},
        {"time_window_filter", cfgx::Node(true)},
        {"gate_failures", cfgx::Node(true)},
    });
}

cfgx::Node BuildSummaryData(const LogConfig& config, const Summary& summary)
{
    return BuildDataObject({
        {"command", cfgx::Node(config.command)},
        {"manifest", cfgx::Node(config.manifest)},
        {"files", BuildStringArray(summary.files)},
        {"file_count", cfgx::Node(static_cast<std::int64_t>(summary.files.size()))},
        {"format", cfgx::Node(FormatName(config.format))},
        {"filters", BuildFilterObject(config)},
        {"lines_read", cfgx::Node(static_cast<std::int64_t>(summary.lines_read))},
        {"blank_lines", cfgx::Node(static_cast<std::int64_t>(summary.blank_lines))},
        {"parsed", cfgx::Node(static_cast<std::int64_t>(summary.parsed))},
        {"matched", cfgx::Node(static_cast<std::int64_t>(summary.matched))},
        {"parse_failures", cfgx::Node(static_cast<std::int64_t>(summary.parse_failures))},
        {"time_missing", cfgx::Node(static_cast<std::int64_t>(summary.time_missing))},
        {"by_level", BuildLevelCountsObject(summary)},
        {"first_time", cfgx::Node(summary.first_time)},
        {"last_time", cfgx::Node(summary.last_time)},
        {"samples", BuildSamplesArray(summary.samples)},
        {"capabilities", BuildCapabilitiesObject()},
        {"warnings", BuildStringArray(summary.warnings)},
    });
}

void PrintPlainSummary(const Summary& summary)
{
    std::cout << "files=" << summary.files.size() << "\n";
    std::cout << "lines=" << summary.lines_read << "\n";
    std::cout << "matched=" << summary.matched << "\n";
    std::cout << "parse_failures=" << summary.parse_failures << "\n";
    std::cout << "warnings=" << summary.by_level[LevelIndex(logsys::LogLevel::Warning)] << "\n";
    std::cout << "errors=" << summary.by_level[LevelIndex(logsys::LogLevel::Error)] << "\n";
    std::cout << "fatals=" << summary.by_level[LevelIndex(logsys::LogLevel::Fatal)] << "\n";
    std::cout << "critical=" << summary.by_level[LevelIndex(logsys::LogLevel::Critical)] << "\n";
}

std::vector<cfgx::ValidationIssue> BuildGateIssues(const LogConfig& config, const Summary& summary)
{
    std::vector<cfgx::ValidationIssue> issues;
    if (config.max_parse_errors.has_value() && summary.parse_failures > *config.max_parse_errors)
    {
        issues.push_back(cfgx::ValidationIssue{
            "$.parse_failures",
            "parse failures exceed max_parse_errors",
        });
    }
    if (config.fail_on_level.has_value())
    {
        for (std::size_t i = LevelIndex(*config.fail_on_level); i < summary.by_level.size(); ++i)
        {
            if (summary.by_level[i] > 0)
            {
                issues.push_back(cfgx::ValidationIssue{
                    "$.by_level",
                    "matched log level is at or above fail_on_level",
                });
                break;
            }
        }
    }
    return issues;
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

int RunSummarize(const LogConfig& config, bool json_mode)
{
    ConfigureLogging(config.log_file);
    auto summary = SummarizeLogs(config);
    if (!summary.ok)
    {
        const int code = summary.error.find("not found") != std::string::npos ? kExitNotFound : kExitRuntimeError;
        return ExitError(json_mode, code, summary.error);
    }

    const auto issues = BuildGateIssues(config, summary.value);
    const cfgx::Node data = BuildSummaryData(config, summary.value);
    if (!issues.empty())
    {
        if (json_mode)
        {
            PrintJsonEnvelope(false, kExitValidationFailed, "log gate failed", data, issues);
        }
        else
        {
            PrintPlainSummary(summary.value);
            std::cerr << "error: log gate failed\n";
        }
        LOGE("toolx-log gate failed files=%zu lines=%llu matched=%llu parse_failures=%llu", summary.value.files.size(),
             static_cast<unsigned long long>(summary.value.lines_read),
             static_cast<unsigned long long>(summary.value.matched),
             static_cast<unsigned long long>(summary.value.parse_failures));
        return kExitValidationFailed;
    }

    if (json_mode)
    {
        PrintJsonEnvelope(true, kExitSuccess, "summarized", data);
    }
    else
    {
        PrintPlainSummary(summary.value);
    }
    LOGI("toolx-log summarized files=%zu lines=%llu matched=%llu parse_failures=%llu", summary.value.files.size(),
         static_cast<unsigned long long>(summary.value.lines_read),
         static_cast<unsigned long long>(summary.value.matched),
         static_cast<unsigned long long>(summary.value.parse_failures));
    return kExitSuccess;
}

} // namespace

int main(int argc, const char* const argv[])
{
    argtool::Parser parser;
    parser.SetProgramName("toolx-log")
        .SetDescription("toolx-log - summarize runtime logs")
        .SetUsageExample("toolx-log summarize --file app.log --min-level error --json")
        .SetHelpLayout(argtool::HelpLayout::Fixed);

    parser.AddSubcommandRoot("summarize", "Summarize offline log files");

    parser.Option("file")
        .String()
        .ListValue()
        .ValueName("FILE")
        .Description("Log file to summarize. Repeatable.")
        .Done();
    parser.Option("manifest").String().ValueName("FILE").Description("Optional log summary manifest.").Done();
    parser.Option("format")
        .String()
        .Choices({"auto", "logsys-text", "jsonl"})
        .ValueName("FORMAT")
        .Description("Input log format.")
        .Done();
    parser.Option("level")
        .String()
        .ListValue()
        .ValueName("LEVEL")
        .Description("Exact log level filter. Repeatable.")
        .Done();
    parser.Option("min-level").String().ValueName("LEVEL").Description("Minimum severity filter.").Done();
    parser.Option("contains").String().ValueName("TEXT").Description("Raw line substring filter.").Done();
    parser.Option("since")
        .String()
        .ValueName("TIME")
        .Description("Include records at or after YYYY-MM-DD HH:MM:SS.mmm.")
        .Done();
    parser.Option("until")
        .String()
        .ValueName("TIME")
        .Description("Include records at or before YYYY-MM-DD HH:MM:SS.mmm.")
        .Done();
    parser.Option("max-samples").Int().ValueName("N").Description("Maximum matched samples in JSON output.").Done();
    parser.Option("fail-on-level")
        .String()
        .ValueName("LEVEL")
        .Description("Fail if matched records are at or above this level.")
        .Done();
    parser.Option("max-parse-errors")
        .Int()
        .ValueName("N")
        .Description("Fail if parse failures exceed this threshold.")
        .Done();
    parser.Option("log-file").String().ValueName("FILE").Description("Optional audit log file.").Done();
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
    if (parsed.subcommand_path->root != "summarize")
    {
        return ExitError(json_mode, kExitUsageError, "unsupported subcommand: " + parsed.subcommand_path->root);
    }

    auto config = ResolveConfig(parsed, parsed.subcommand_path->root);
    if (!config.ok)
    {
        int code = kExitUsageError;
        if (config.error.find("not found") != std::string::npos)
        {
            code = kExitNotFound;
        }
        else if (config.error.rfind("manifest", 0) == 0 || config.error.rfind("unsupported manifest", 0) == 0)
        {
            code = kExitValidationFailed;
        }
        return ExitError(json_mode, code, config.error);
    }

    return RunSummarize(config.value, json_mode);
}
