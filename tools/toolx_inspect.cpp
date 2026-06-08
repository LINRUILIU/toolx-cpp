#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "argtool.h"
#include "cfgx.h"
#include "logsys.h"
#include "schemax.h"
#include "tuix.h"

namespace
{
namespace fs = std::filesystem;

constexpr int kExitSuccess = 0;
constexpr int kExitRuntimeError = 1;
constexpr int kExitUsageError = 2;
constexpr int kExitNotFound = 3;
constexpr int kExitValidationFailed = 4;

enum class Focus
{
    Paths,
    Issues,
    Value,
};

struct InspectConfig
{
    std::string command;
    std::string file;
    std::string schema_file;
    std::string manifest;
    cfgx::ConfigFormat format{cfgx::ConfigFormat::Unknown};
    std::string format_text{"auto"};
    std::string path;
    std::string contains;
    std::size_t max_paths{50};
    std::size_t max_issues{50};
    Focus focus{Focus::Paths};
    int width{80};
    int height{18};
    bool no_ansi{false};
    std::string script;
    int ticks{-1};
    bool allow_issues{false};
    std::string log_file;
};

struct PathEntry
{
    std::string path;
    std::string kind;
    std::string preview;
};

struct InspectReport
{
    std::string effective_format;
    std::string root_kind;
    std::uint64_t path_count{0};
    std::uint64_t matched_path_count{0};
    std::uint64_t scalar_count{0};
    std::uint64_t object_count{0};
    std::uint64_t array_count{0};
    std::string selected_path;
    std::string selected_kind;
    std::string selected_value;
    std::vector<PathEntry> paths;
    std::vector<schemax::Issue> schema_issues;
    std::vector<std::string> frame;
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

std::string ToLowerCopy(std::string_view input)
{
    std::string out(input);
    for (char& ch : out)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool ContainsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
    {
        return true;
    }
    return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::string::npos;
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

cfgx::Node BuildStringArray(const std::vector<std::string>& values)
{
    cfgx::Node::Array arr;
    arr.reserve(values.size());
    for (const auto& value : values)
    {
        arr.emplace_back(cfgx::Node(value));
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

std::vector<cfgx::ValidationIssue> ToValidationIssues(const std::vector<schemax::Issue>& issues)
{
    std::vector<cfgx::ValidationIssue> out;
    out.reserve(issues.size());
    for (const auto& issue : issues)
    {
        out.push_back({issue.path, issue.code + ": " + issue.message});
    }
    return out;
}

void PrintJsonEnvelope(bool ok, int code, std::string_view message, const cfgx::Node& data,
                       const std::vector<cfgx::ValidationIssue>& issues = {})
{
    const cfgx::Node envelope = BuildDataObject({
        {"schema", cfgx::Node("toolx.inspect.result")},
        {"schema_version", cfgx::Node(std::int64_t(1))},
        {"ok", cfgx::Node(ok)},
        {"code", cfgx::Node(static_cast<std::int64_t>(code))},
        {"message", cfgx::Node(std::string(message))},
        {"issues", BuildIssueArray(issues)},
        {"data", data},
    });
    std::cout << cfgx::ToJson(envelope, 2) << "\n";
}

int ExitError(bool json_mode, int code, const std::string& message)
{
    if (json_mode)
    {
        PrintJsonEnvelope(false, code, message, BuildDataObject({}));
    }
    else
    {
        std::cerr << "error: " << message << "\n";
    }
    return code;
}

std::optional<cfgx::ConfigFormat> ParseFormat(std::string_view input)
{
    const std::string value = ToLowerCopy(TrimCopy(input));
    if (value.empty() || value == "auto")
    {
        return cfgx::ConfigFormat::Unknown;
    }
    if (value == "json")
    {
        return cfgx::ConfigFormat::Json;
    }
    if (value == "ini")
    {
        return cfgx::ConfigFormat::Ini;
    }
    if (value == "yaml")
    {
        return cfgx::ConfigFormat::Yaml;
    }
    if (value == "toml")
    {
        return cfgx::ConfigFormat::Toml;
    }
    return std::nullopt;
}

std::optional<Focus> ParseFocus(std::string_view input)
{
    const std::string value = ToLowerCopy(TrimCopy(input));
    if (value.empty() || value == "paths")
    {
        return Focus::Paths;
    }
    if (value == "issues")
    {
        return Focus::Issues;
    }
    if (value == "value")
    {
        return Focus::Value;
    }
    return std::nullopt;
}

std::string FocusName(Focus focus)
{
    switch (focus)
    {
    case Focus::Paths:
        return "paths";
    case Focus::Issues:
        return "issues";
    case Focus::Value:
        return "value";
    }
    return "paths";
}

cfgx::Node ManifestSchema()
{
    const char* schema_text = R"({
      "type": "object",
      "properties": {
        "file": {"type": "string"},
        "schema": {"type": "string"},
        "format": {"type": "string"},
        "path": {"type": "string"},
        "contains": {"type": "string"},
        "max_paths": {"type": "integer"},
        "max_issues": {"type": "integer"},
        "focus": {"type": "string"},
        "width": {"type": "integer"},
        "height": {"type": "integer"},
        "allow_issues": {"type": "boolean"}
      },
      "additionalProperties": false
    })";
    const auto parsed = cfgx::ParseJson(schema_text);
    return parsed.ok ? parsed.value : cfgx::Node::MakeObject();
}

bool ReadSizeField(const cfgx::Node& root, std::string_view field, std::size_t* out)
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
    *out = static_cast<std::size_t>(value);
    return true;
}

bool ReadIntField(const cfgx::Node& root, std::string_view field, int* out)
{
    const auto* node = root.Get(field);
    if (node == nullptr)
    {
        return false;
    }
    *out = static_cast<int>(node->AsInt(-1));
    return true;
}

cfgx::Result<InspectConfig> LoadManifestConfig(const std::string& path)
{
    InspectConfig config;
    if (path.empty())
    {
        return cfgx::Result<InspectConfig>{true, std::move(config), ""};
    }

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        return cfgx::Result<InspectConfig>{false, {}, "manifest file not found: " + path};
    }

    auto loaded = cfgx::LoadFromFile(path);
    if (!loaded.ok)
    {
        return cfgx::Result<InspectConfig>{false, {}, "manifest parse failed: " + loaded.error};
    }

    const auto compiled = schemax::Compile(ManifestSchema());
    if (!compiled.ok)
    {
        return cfgx::Result<InspectConfig>{false, {}, "internal manifest schema failed: " + compiled.error};
    }
    const auto issues = schemax::Validate(loaded.value, compiled.value);
    if (!issues.empty())
    {
        std::string message = "manifest validation failed";
        if (!issues.front().path.empty())
        {
            message += ": " + issues.front().path + " " + issues.front().message;
        }
        return cfgx::Result<InspectConfig>{false, {}, message};
    }

    config.manifest = path;
    if (const auto* file = loaded.value.Get("file"); file != nullptr)
    {
        config.file = file->AsString();
    }
    if (const auto* schema = loaded.value.Get("schema"); schema != nullptr)
    {
        config.schema_file = schema->AsString();
    }
    if (const auto* format = loaded.value.Get("format"); format != nullptr)
    {
        auto parsed = ParseFormat(format->AsString());
        if (!parsed.has_value())
        {
            return cfgx::Result<InspectConfig>{false, {}, "unsupported manifest format: " + format->AsString()};
        }
        config.format = *parsed;
        config.format_text = ToLowerCopy(format->AsString());
    }
    if (const auto* path_node = loaded.value.Get("path"); path_node != nullptr)
    {
        config.path = path_node->AsString();
    }
    if (const auto* contains = loaded.value.Get("contains"); contains != nullptr)
    {
        config.contains = contains->AsString();
    }
    (void)ReadSizeField(loaded.value, "max_paths", &config.max_paths);
    (void)ReadSizeField(loaded.value, "max_issues", &config.max_issues);
    if (const auto* focus = loaded.value.Get("focus"); focus != nullptr)
    {
        auto parsed = ParseFocus(focus->AsString());
        if (!parsed.has_value())
        {
            return cfgx::Result<InspectConfig>{false, {}, "unsupported manifest focus: " + focus->AsString()};
        }
        config.focus = *parsed;
    }
    (void)ReadIntField(loaded.value, "width", &config.width);
    (void)ReadIntField(loaded.value, "height", &config.height);
    if (const auto* allow = loaded.value.Get("allow_issues"); allow != nullptr)
    {
        config.allow_issues = allow->AsBool(false);
    }
    return cfgx::Result<InspectConfig>{true, std::move(config), ""};
}

void ApplyCliOverrides(const argtool::ParseResult& parsed, InspectConfig* config)
{
    if (parsed.Has("file") && !parsed.GetString("file").empty())
    {
        config->file = parsed.GetString("file");
    }
    if (parsed.Has("schema") && !parsed.GetString("schema").empty())
    {
        config->schema_file = parsed.GetString("schema");
    }
    if (parsed.Has("format") && !parsed.GetString("format").empty())
    {
        if (const auto format = ParseFormat(parsed.GetString("format")); format.has_value())
        {
            config->format = *format;
            config->format_text = ToLowerCopy(parsed.GetString("format"));
        }
    }
    if (parsed.Has("path"))
    {
        config->path = parsed.GetString("path");
    }
    if (parsed.Has("contains"))
    {
        config->contains = parsed.GetString("contains");
    }
    if (parsed.Has("max-paths"))
    {
        config->max_paths = static_cast<std::size_t>(std::max(0, parsed.GetInt("max-paths", 0)));
    }
    if (parsed.Has("max-issues"))
    {
        config->max_issues = static_cast<std::size_t>(std::max(0, parsed.GetInt("max-issues", 0)));
    }
    if (parsed.Has("focus"))
    {
        if (const auto focus = ParseFocus(parsed.GetString("focus")); focus.has_value())
        {
            config->focus = *focus;
        }
    }
    if (parsed.Has("width"))
    {
        config->width = parsed.GetInt("width", config->width);
    }
    if (parsed.Has("height"))
    {
        config->height = parsed.GetInt("height", config->height);
    }
    if (parsed.Has("no-ansi"))
    {
        config->no_ansi = parsed.GetBool("no-ansi", false);
    }
    if (parsed.Has("script"))
    {
        config->script = parsed.GetString("script");
    }
    if (parsed.Has("ticks"))
    {
        config->ticks = parsed.GetInt("ticks", config->ticks);
    }
    if (parsed.Has("allow-issues"))
    {
        config->allow_issues = parsed.GetBool("allow-issues", false);
    }
    config->log_file = parsed.GetString("log-file", "");
}

cfgx::Result<InspectConfig> ResolveConfig(const argtool::ParseResult& parsed, const std::string& command)
{
    const std::string manifest_path = parsed.GetString("manifest", "");
    auto manifest = LoadManifestConfig(manifest_path);
    if (!manifest.ok)
    {
        return manifest;
    }

    InspectConfig config = std::move(manifest.value);
    config.command = command;
    config.manifest = manifest_path;
    ApplyCliOverrides(parsed, &config);

    if (config.file.empty())
    {
        return cfgx::Result<InspectConfig>{false, {}, "missing required option --file or --manifest with file"};
    }
    if (config.max_paths == 0)
    {
        return cfgx::Result<InspectConfig>{false, {}, "--max-paths must be greater than 0"};
    }
    if (config.max_issues == 0)
    {
        return cfgx::Result<InspectConfig>{false, {}, "--max-issues must be greater than 0"};
    }
    if (config.width < 40)
    {
        return cfgx::Result<InspectConfig>{false, {}, "--width must be at least 40"};
    }
    if (config.height < 8)
    {
        return cfgx::Result<InspectConfig>{false, {}, "--height must be at least 8"};
    }
    if (config.ticks < -1)
    {
        return cfgx::Result<InspectConfig>{false, {}, "--ticks must be -1 or greater"};
    }
    return cfgx::Result<InspectConfig>{true, std::move(config), ""};
}

std::string PreviewScalar(const cfgx::Node& node)
{
    switch (node.Kind())
    {
    case cfgx::NodeKind::Null:
        return "null";
    case cfgx::NodeKind::Bool:
        return node.AsBool(false) ? "true" : "false";
    case cfgx::NodeKind::Integer:
        return std::to_string(node.AsInt());
    case cfgx::NodeKind::Double:
    {
        std::ostringstream oss;
        oss << node.AsDouble();
        return oss.str();
    }
    case cfgx::NodeKind::String:
        return node.AsString();
    case cfgx::NodeKind::Object:
        return "{...}";
    case cfgx::NodeKind::Array:
        return "[...]";
    }
    return "";
}

void AddStats(const cfgx::Node& node, InspectReport* report)
{
    if (node.IsObject())
    {
        ++report->object_count;
    }
    else if (node.IsArray())
    {
        ++report->array_count;
    }
    else
    {
        ++report->scalar_count;
    }
}

void CollectPaths(const cfgx::Node& node, const std::string& path, InspectReport* report, std::vector<PathEntry>* all)
{
    AddStats(node, report);
    all->push_back({path, cfgx::ToString(node.Kind()), PreviewScalar(node)});
    ++report->path_count;

    if (const auto* object = node.TryObject(); object != nullptr)
    {
        for (const auto& entry : *object)
        {
            const std::string child = path == "$" ? entry.first : path + "." + entry.first;
            CollectPaths(entry.second, child, report, all);
        }
        return;
    }
    if (const auto* array = node.TryArray(); array != nullptr)
    {
        for (std::size_t i = 0; i < array->size(); ++i)
        {
            CollectPaths((*array)[i], path + "[" + std::to_string(i) + "]", report, all);
        }
    }
}

const cfgx::Node* FindNodeAtPath(const cfgx::Node& root, const std::string& path, std::string* error)
{
    if (path.empty() || path == "$")
    {
        return &root;
    }
    auto parsed = cfgx::ParsePath(path);
    if (!parsed.ok)
    {
        *error = "invalid --path: " + parsed.error;
        return nullptr;
    }
    auto found = cfgx::GetNode(root, path);
    if (!found.ok)
    {
        *error = "path not found: " + path;
        return nullptr;
    }
    return found.value;
}

std::vector<schemax::Issue> LoadAndValidateSchema(const InspectConfig& config, const cfgx::Node& document,
                                                  std::string* error)
{
    if (config.schema_file.empty())
    {
        return {};
    }

    std::error_code ec;
    if (!fs::exists(config.schema_file, ec) || ec)
    {
        *error = "schema file not found: " + config.schema_file;
        return {};
    }
    auto loaded = cfgx::LoadFromFile(config.schema_file);
    if (!loaded.ok)
    {
        *error = "schema parse failed: " + loaded.error;
        return {};
    }
    auto compiled = schemax::Compile(loaded.value);
    if (!compiled.ok)
    {
        *error = "schema compile failed: " + compiled.error;
        return {};
    }
    return schemax::Validate(document, compiled.value);
}

cfgx::Result<InspectReport> BuildReport(const InspectConfig& config)
{
    std::error_code ec;
    if (!fs::exists(config.file, ec) || ec)
    {
        return cfgx::Result<InspectReport>{false, {}, "config file not found: " + config.file};
    }

    auto loaded = cfgx::LoadFromFile(config.file, config.format);
    if (!loaded.ok)
    {
        return cfgx::Result<InspectReport>{false, {}, "config load failed: " + loaded.error};
    }

    InspectReport report;
    const cfgx::ConfigFormat detected =
        config.format == cfgx::ConfigFormat::Unknown ? cfgx::DetectFormatFromPath(config.file) : config.format;
    report.effective_format = cfgx::ToString(detected);
    report.root_kind = cfgx::ToString(loaded.value.Kind());

    std::string schema_error;
    report.schema_issues = LoadAndValidateSchema(config, loaded.value, &schema_error);
    if (!schema_error.empty())
    {
        return cfgx::Result<InspectReport>{false, {}, schema_error};
    }

    std::vector<PathEntry> all_paths;
    CollectPaths(loaded.value, "$", &report, &all_paths);

    const cfgx::Node* selected = nullptr;
    if (!config.path.empty())
    {
        std::string path_error;
        selected = FindNodeAtPath(loaded.value, config.path, &path_error);
        if (selected == nullptr)
        {
            return cfgx::Result<InspectReport>{false, {}, path_error};
        }
        report.selected_path = config.path == "$" ? "$" : config.path;
    }

    for (const auto& entry : all_paths)
    {
        if (!ContainsInsensitive(entry.path, config.contains) && !ContainsInsensitive(entry.preview, config.contains))
        {
            continue;
        }
        ++report.matched_path_count;
        if (report.paths.size() < config.max_paths)
        {
            report.paths.push_back(entry);
        }
        if (selected == nullptr)
        {
            report.selected_path = entry.path;
            if (entry.path == "$")
            {
                selected = &loaded.value;
            }
            else
            {
                std::string ignored;
                selected = FindNodeAtPath(loaded.value, entry.path, &ignored);
            }
        }
    }

    if (selected == nullptr)
    {
        report.selected_path = "$";
        selected = &loaded.value;
    }
    report.selected_kind = cfgx::ToString(selected->Kind());
    report.selected_value = PreviewScalar(*selected);
    return cfgx::Result<InspectReport>{true, std::move(report), ""};
}

std::string Clip(std::string text, std::size_t width)
{
    if (text.size() <= width)
    {
        return text;
    }
    if (width <= 3)
    {
        return text.substr(0, width);
    }
    return text.substr(0, width - 3) + "...";
}

void PutLine(tuix::FrameBuffer* frame, std::uint16_t x, std::uint16_t y, std::uint16_t width, std::string text)
{
    if (y >= frame->height() || x >= frame->width())
    {
        return;
    }
    text = Clip(std::move(text), width);
    (void)frame->Put(x, y, text);
}

std::vector<std::string> FrameToLines(const tuix::FrameBuffer& frame)
{
    std::vector<std::string> lines;
    lines.reserve(frame.height());
    for (std::uint16_t y = 0; y < frame.height(); ++y)
    {
        std::string line;
        for (std::uint16_t x = 0; x < frame.width(); ++x)
        {
            const auto* cell = frame.Get(x, y);
            if (cell == nullptr || cell->continuation)
            {
                continue;
            }
            line += cell->utf8.empty() ? " " : cell->utf8;
        }
        while (!line.empty() && line.back() == ' ')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> RenderFrame(const InspectConfig& config, const InspectReport& report)
{
    const auto width = static_cast<std::uint16_t>(std::max(40, config.width));
    const auto height = static_cast<std::uint16_t>(std::max(8, config.height));
    tuix::FrameBuffer frame(width, height, ' ');

    PutLine(&frame, 0, 0, width, "toolx-inspect | file=" + config.file);
    PutLine(&frame, 0, 1, width,
            "format=" + report.effective_format + " root=" + report.root_kind + " paths=" +
                std::to_string(report.path_count) + " matched=" + std::to_string(report.matched_path_count) +
                " issues=" + std::to_string(report.schema_issues.size()) + " focus=" + FocusName(config.focus));

    const std::uint16_t body_top = 3;
    const std::uint16_t value_top = height > 4 ? static_cast<std::uint16_t>(height - 3) : body_top;
    const std::uint16_t left_width = static_cast<std::uint16_t>(width / 2);
    const std::uint16_t right_width = static_cast<std::uint16_t>(width - left_width - 1);
    PutLine(&frame, 0, 2, left_width, "[paths]");
    PutLine(&frame, static_cast<std::uint16_t>(left_width + 1), 2, right_width, "[schema]");

    const std::uint16_t list_rows = value_top > body_top ? static_cast<std::uint16_t>(value_top - body_top) : 0;
    for (std::uint16_t row = 0; row < list_rows; ++row)
    {
        if (row < report.paths.size())
        {
            const auto& entry = report.paths[row];
            const std::string mark = entry.path == report.selected_path ? "> " : "  ";
            PutLine(&frame, 0, static_cast<std::uint16_t>(body_top + row), left_width,
                    mark + entry.path + " = " + entry.preview);
        }
        if (row < report.schema_issues.size())
        {
            const auto& issue = report.schema_issues[row];
            PutLine(&frame, static_cast<std::uint16_t>(left_width + 1), static_cast<std::uint16_t>(body_top + row),
                    right_width, issue.path + " [" + issue.code + "] " + issue.message);
        }
        else if (row == 0 && report.schema_issues.empty())
        {
            PutLine(&frame, static_cast<std::uint16_t>(left_width + 1), static_cast<std::uint16_t>(body_top + row),
                    right_width, "schema: ok");
        }
    }

    PutLine(&frame, 0, value_top, width, "[value]");
    PutLine(&frame, 0, static_cast<std::uint16_t>(value_top + 1), width,
            report.selected_path + " (" + report.selected_kind + ") = " + report.selected_value);
    PutLine(&frame, 0, static_cast<std::uint16_t>(value_top + 2), width, "Tab/Arrows navigate | Esc exits");
    return FrameToLines(frame);
}

cfgx::Node BuildPathArray(const std::vector<PathEntry>& paths)
{
    cfgx::Node::Array arr;
    arr.reserve(paths.size());
    for (const auto& path : paths)
    {
        arr.emplace_back(BuildDataObject({
            {"path", cfgx::Node(path.path)},
            {"kind", cfgx::Node(path.kind)},
            {"preview", cfgx::Node(path.preview)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

cfgx::Node BuildSchemaIssueArray(const std::vector<schemax::Issue>& issues, std::size_t max_issues)
{
    cfgx::Node::Array arr;
    const std::size_t count = std::min(max_issues, issues.size());
    arr.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& issue = issues[i];
        arr.emplace_back(BuildDataObject({
            {"path", cfgx::Node(issue.path)},
            {"code", cfgx::Node(issue.code)},
            {"message", cfgx::Node(issue.message)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

cfgx::Node BuildData(const InspectConfig& config, const InspectReport& report)
{
    return BuildDataObject({
        {"command", cfgx::Node(config.command)},
        {"file", cfgx::Node(config.file)},
        {"schema_file", cfgx::Node(config.schema_file)},
        {"manifest", cfgx::Node(config.manifest)},
        {"format", cfgx::Node(report.effective_format)},
        {"root_kind", cfgx::Node(report.root_kind)},
        {"path_count", cfgx::Node(static_cast<std::int64_t>(report.path_count))},
        {"matched_path_count", cfgx::Node(static_cast<std::int64_t>(report.matched_path_count))},
        {"scalar_count", cfgx::Node(static_cast<std::int64_t>(report.scalar_count))},
        {"object_count", cfgx::Node(static_cast<std::int64_t>(report.object_count))},
        {"array_count", cfgx::Node(static_cast<std::int64_t>(report.array_count))},
        {"selected_path", cfgx::Node(report.selected_path)},
        {"selected_kind", cfgx::Node(report.selected_kind)},
        {"selected_value", cfgx::Node(report.selected_value)},
        {"schema_issue_count", cfgx::Node(static_cast<std::int64_t>(report.schema_issues.size()))},
        {"schema_issues", BuildSchemaIssueArray(report.schema_issues, config.max_issues)},
        {"paths", BuildPathArray(report.paths)},
        {"frame", BuildStringArray(report.frame)},
        {"capabilities", BuildDataObject({
                             {"report", cfgx::Node(true)},
                             {"render", cfgx::Node(true)},
                             {"scripted_run", cfgx::Node(true)},
                             {"interactive_run", cfgx::Node(true)},
                         })},
        {"warnings", BuildStringArray(report.warnings)},
    });
}

void PrintPlainReport(const InspectConfig& config, const InspectReport& report)
{
    std::cout << "file=" << config.file << "\n";
    std::cout << "format=" << report.effective_format << "\n";
    std::cout << "root_kind=" << report.root_kind << "\n";
    std::cout << "paths=" << report.path_count << "\n";
    std::cout << "matched_paths=" << report.matched_path_count << "\n";
    std::cout << "schema_issues=" << report.schema_issues.size() << "\n";
    std::cout << "selected_path=" << report.selected_path << "\n";
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

std::string ScriptToInput(std::string_view script)
{
    std::string input;
    std::size_t begin = 0;
    while (begin <= script.size())
    {
        const std::size_t comma = script.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? script.size() : comma;
        const std::string raw_token = TrimCopy(script.substr(begin, end - begin));
        const std::string token = ToLowerCopy(raw_token);
        if (token == "tab")
        {
            input.push_back('\t');
        }
        else if (token == "shift-tab")
        {
            input += "\x1B[Z";
        }
        else if (token == "up")
        {
            input += "\x1B[A";
        }
        else if (token == "down")
        {
            input += "\x1B[B";
        }
        else if (token == "left")
        {
            input += "\x1B[D";
        }
        else if (token == "right")
        {
            input += "\x1B[C";
        }
        else if (token == "enter")
        {
            input.push_back('\n');
        }
        else if (token == "esc")
        {
            input.push_back('\x1B');
        }
        else if (token.rfind("text:", 0) == 0)
        {
            input += raw_token.substr(5);
        }

        if (comma == std::string_view::npos)
        {
            break;
        }
        begin = comma + 1;
    }
    return input;
}

std::unique_ptr<tuix::InputSource> BuildInputSource(const InspectConfig& config, std::istringstream* scripted)
{
    tuix::InputOptions options;
    options.consume_mode = tuix::InputConsumeMode::ExclusiveConsume;
    if (!config.script.empty())
    {
        scripted->str(ScriptToInput(config.script));
        scripted->clear();
        return tuix::CreateStreamInputSource(*scripted, options);
    }
    return tuix::CreateConsoleInputSource(options);
}

int RunInteractive(const InspectConfig& config, const InspectReport& report)
{
    int max_ticks = config.ticks;
    if (!config.script.empty() && config.ticks == -1)
    {
        // Scripted runs should not hang if the script omitted esc.
        max_ticks = 8;
    }

    tuix::Terminal terminal(std::cout, !config.no_ansi);
    tuix::Application app(&terminal);
    auto root = std::make_shared<tuix::VerticalLayout>();
    root->SetPadding({1, 1, 1, 1});
    root->SetGap(1);
    root->SetFlexWeights({1, 5, 1});

    auto title = std::make_shared<tuix::Label>("toolx-inspect | " + config.file);
    auto main_row = std::make_shared<tuix::HorizontalLayout>();
    main_row->SetGap(1);
    main_row->SetFlexWeights({1, 1});

    std::vector<std::string> path_lines;
    for (const auto& path : report.paths)
    {
        path_lines.push_back(path.path + " = " + path.preview);
    }
    if (path_lines.empty())
    {
        path_lines.push_back("(no matched paths)");
    }
    auto path_panel = std::make_shared<tuix::Panel>("paths");
    auto path_list = std::make_shared<tuix::ListView>(std::move(path_lines));
    path_panel->AddChild(path_list);

    std::vector<std::string> issue_lines;
    for (std::size_t i = 0; i < std::min(config.max_issues, report.schema_issues.size()); ++i)
    {
        const auto& issue = report.schema_issues[i];
        issue_lines.push_back(issue.path + " [" + issue.code + "] " + issue.message);
    }
    if (issue_lines.empty())
    {
        issue_lines.push_back("schema: ok");
    }
    auto issue_panel = std::make_shared<tuix::Panel>("schema");
    auto issue_list = std::make_shared<tuix::ListView>(std::move(issue_lines));
    issue_panel->AddChild(issue_list);

    main_row->AddChild(path_panel);
    main_row->AddChild(issue_panel);
    auto status = std::make_shared<tuix::TextInput>(report.selected_path + " = " + report.selected_value);

    root->AddChild(title);
    root->AddChild(main_row);
    root->AddChild(status);

    app.SetRoot(root);
    if (config.focus == Focus::Issues)
    {
        app.SetFocusedWidget(issue_list.get());
    }
    else if (config.focus == Focus::Value)
    {
        app.SetFocusedWidget(status.get());
    }
    else
    {
        app.SetFocusedWidget(path_list.get());
    }
    std::istringstream scripted;
    app.SetInputSource(BuildInputSource(config, &scripted));
    return app.Run(max_ticks, 0);
}

int RunCommand(InspectConfig config, bool json_mode)
{
    ConfigureLogging(config.log_file);
    auto report_result = BuildReport(config);
    if (!report_result.ok)
    {
        int code = kExitRuntimeError;
        if (report_result.error.find("invalid --path") == 0 || report_result.error.find("path not found") == 0)
        {
            code = kExitRuntimeError;
        }
        else if (report_result.error.find("not found") != std::string::npos)
        {
            code = kExitNotFound;
        }
        else if (report_result.error.find("schema ") == 0)
        {
            code = kExitValidationFailed;
        }
        return ExitError(json_mode, code, report_result.error);
    }

    InspectReport report = std::move(report_result.value);
    if (config.command == "render" || config.command == "run")
    {
        report.frame = RenderFrame(config, report);
    }

    const bool issue_failure = !config.allow_issues && !report.schema_issues.empty();
    const int code = issue_failure ? kExitValidationFailed : kExitSuccess;
    const std::string message = issue_failure ? "schema issues found" : "inspected";
    const auto validation_issues =
        issue_failure ? ToValidationIssues(report.schema_issues) : std::vector<cfgx::ValidationIssue>{};

    if (config.command == "run" && !json_mode)
    {
        const int run_code = RunInteractive(config, report);
        if (run_code != 0)
        {
            return run_code;
        }
    }
    else if (json_mode)
    {
        PrintJsonEnvelope(!issue_failure, code, message, BuildData(config, report), validation_issues);
    }
    else if (config.command == "render")
    {
        for (const auto& line : report.frame)
        {
            std::cout << line << "\n";
        }
    }
    else
    {
        PrintPlainReport(config, report);
    }

    if (issue_failure)
    {
        if (!json_mode)
        {
            std::cerr << "error: schema issues found\n";
        }
        LOGE("toolx-inspect schema_issues=%zu file=%s", report.schema_issues.size(), config.file.c_str());
    }
    else
    {
        LOGI("toolx-inspect command=%s file=%s paths=%llu issues=%zu", config.command.c_str(), config.file.c_str(),
             static_cast<unsigned long long>(report.path_count), report.schema_issues.size());
    }
    return code;
}

} // namespace

int main(int argc, const char* const argv[])
{
    argtool::Parser parser;
    parser.SetProgramName("toolx-inspect")
        .SetDescription("toolx-inspect - inspect config files and schema issues")
        .SetUsageExample("toolx-inspect report --file app.json --schema schema.json --json")
        .SetHelpLayout(argtool::HelpLayout::Fixed);

    parser.AddSubcommandRoot("report", "Emit stable config inspection summary");
    parser.AddSubcommandRoot("render", "Render a deterministic terminal inspection frame");
    parser.AddSubcommandRoot("run", "Run terminal config inspector");

    parser.Option("file").String().ValueName("FILE").Description("Config file to inspect.").Done();
    parser.Option("schema").String().ValueName("FILE").Description("Optional schemax schema file.").Done();
    parser.Option("manifest").String().ValueName("FILE").Description("Optional inspect manifest.").Done();
    parser.Option("format")
        .String()
        .Choices({"auto", "json", "ini", "yaml", "toml"})
        .ValueName("FORMAT")
        .Description("Input config format.")
        .Done();
    parser.Option("path").String().ValueName("PATH").Description("Initial selected cfgx path.").Done();
    parser.Option("contains").String().ValueName("TEXT").Description("Filter paths by path or scalar preview.").Done();
    parser.Option("max-paths").Int().ValueName("N").Description("Maximum paths included in output.").Done();
    parser.Option("max-issues").Int().ValueName("N").Description("Maximum schema issues included in output.").Done();
    parser.Option("focus")
        .String()
        .Choices({"paths", "issues", "value"})
        .ValueName("FOCUS")
        .Description("Initial focused pane.")
        .Done();
    parser.Option("width").Int().ValueName("N").Description("Render width.").Done();
    parser.Option("height").Int().ValueName("N").Description("Render height.").Done();
    parser.Option("script").String().ValueName("TEXT").Description("Comma-separated scripted run tokens.").Done();
    parser.Option("ticks").Int().ValueName("N").Description("Maximum run ticks; -1 means unbounded.").Done();
    parser.Option("log-file").String().ValueName("FILE").Description("Optional audit log file.").Done();
    parser.Flag("no-ansi").Description("Disable ANSI terminal output for run.").Done();
    parser.Flag("allow-issues").Description("Return success even when schema issues are found.").Done();
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

    const std::string command = parsed.subcommand_path->root;
    if (command != "report" && command != "render" && command != "run")
    {
        return ExitError(json_mode, kExitUsageError, "unsupported subcommand: " + command);
    }

    auto config = ResolveConfig(parsed, command);
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

    return RunCommand(std::move(config.value), json_mode);
}
