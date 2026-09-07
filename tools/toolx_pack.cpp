#include "../src/detail/path_safety.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "argtool.h"
#include "cfgx.h"
#include "fsx.h"
#include "logsys.h"
#include "schemax.h"

namespace
{
namespace fs = std::filesystem;

constexpr int kExitSuccess = 0;
constexpr int kExitRuntimeError = 1;
constexpr int kExitUsageError = 2;
constexpr int kExitNotFound = 3;
constexpr int kExitManifestValidationFailed = 4;

struct PlannedStep
{
    std::string op;
    std::string src;
    std::string dst;
};

struct PackConfig
{
    std::string command;
    std::string name;
    std::string version;
    std::string source;
    std::string stage;
    std::string archive;
    std::string manifest;
    std::vector<std::string> includes;
    std::vector<std::string> excludes;
    bool remove_extra{false};
    bool dry_run{false};
    std::string journal;
    std::string log_file;
};

struct SelectedFile
{
    std::string relative_path;
    std::string source_path;
    std::string destination_path;
    std::uintmax_t size{0};
};

struct PackPlan
{
    fsx::BatchPlan batch;
    std::vector<PlannedStep> planned_steps;
    std::vector<SelectedFile> files;
    std::uintmax_t bytes{0};
};

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

cfgx::Node BuildPlannedStepsArray(const std::vector<PlannedStep>& steps)
{
    cfgx::Node::Array arr;
    arr.reserve(steps.size());
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        const auto& step = steps[i];
        arr.emplace_back(BuildDataObject({
            {"step", cfgx::Node(static_cast<std::int64_t>(i))},
            {"op", cfgx::Node(step.op)},
            {"src", cfgx::Node(step.src)},
            {"dst", cfgx::Node(step.dst)},
        }));
    }
    return cfgx::Node(std::move(arr));
}

cfgx::Node BuildCapabilitiesObject()
{
    const auto caps = fsx::QueryCapabilities();
    return BuildDataObject({
        {"recursive_walk", cfgx::Node(caps.recursive_walk)},
        {"tar_archive", cfgx::Node(caps.tar_archive)},
        {"zip_archive", cfgx::Node(caps.zip_archive)},
    });
}

void PrintJsonEnvelope(bool ok, int code, std::string_view message, const cfgx::Node& data,
                       const std::vector<cfgx::ValidationIssue>& issues = {})
{
    const cfgx::Node envelope = BuildDataObject({
        {"schema", cfgx::Node("toolx.pack.result")},
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

std::string NormalizeSlashes(std::string text)
{
#ifdef _WIN32
    std::replace(text.begin(), text.end(), '\\', '/');
#endif
    return text;
}

std::vector<std::string> SplitPath(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (begin <= text.size())
    {
        const std::size_t split = text.find('/', begin);
        const std::size_t end = split == std::string_view::npos ? text.size() : split;
        const std::string piece(text.substr(begin, end - begin));
        if (!piece.empty())
        {
            out.push_back(piece);
        }
        if (split == std::string_view::npos)
        {
            break;
        }
        begin = split + 1;
    }
    return out;
}

bool IsLikelyAbsolutePath(std::string_view text)
{
    if (text.empty())
    {
        return false;
    }
#ifdef _WIN32
    if (text.front() == '/' || text.front() == '\\')
        return true;
    return text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) != 0 && text[1] == ':';
#else
    return text.front() == '/';
#endif
}

bool NormalizeRelativePath(std::string_view raw, bool allow_wildcards, std::string* normalized, std::string* error)
{
    std::string text = NormalizeSlashes(std::string(raw));
    while (text.rfind("./", 0) == 0)
    {
        text.erase(0, 2);
    }
    while (!text.empty() && text.back() == '/')
    {
        text.pop_back();
    }

    if (text.empty())
    {
        *error = "relative path is empty";
        return false;
    }
    if (IsLikelyAbsolutePath(text))
    {
        *error = "absolute paths are not allowed: " + text;
        return false;
    }
    if (!allow_wildcards && text.find('*') != std::string::npos)
    {
        *error = "wildcards are not allowed in include paths: " + text;
        return false;
    }
    if (text.find('?') != std::string::npos)
    {
        *error = "the '?' wildcard is not supported: " + text;
        return false;
    }

    std::vector<std::string> cleaned;
    for (const auto& part : SplitPath(text))
    {
        if (part == ".")
        {
            continue;
        }
        if (part == "..")
        {
            *error = "parent traversal is not allowed: " + text;
            return false;
        }
        cleaned.push_back(part);
    }
    if (cleaned.empty())
    {
        *error = "relative path is empty";
        return false;
    }

    std::string out;
    for (std::size_t i = 0; i < cleaned.size(); ++i)
    {
        if (i != 0)
        {
            out.push_back('/');
        }
        out += cleaned[i];
    }
    *normalized = std::move(out);
    return true;
}

bool SegmentMatches(std::string_view pattern, std::string_view text)
{
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t star_text = 0;

    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            star_text = t;
            continue;
        }
        if (p < pattern.size() && pattern[p] == text[t])
        {
            ++p;
            ++t;
            continue;
        }
        if (star != std::string_view::npos)
        {
            p = star + 1;
            t = ++star_text;
            continue;
        }
        return false;
    }

    while (p < pattern.size() && pattern[p] == '*')
    {
        ++p;
    }
    return p == pattern.size();
}

bool MatchSegments(const std::vector<std::string>& pattern, std::size_t p, const std::vector<std::string>& path,
                   std::size_t s)
{
    if (p == pattern.size())
    {
        return s == path.size();
    }
    if (pattern[p] == "**")
    {
        if (MatchSegments(pattern, p + 1, path, s))
        {
            return true;
        }
        return s < path.size() && MatchSegments(pattern, p, path, s + 1);
    }
    return s < path.size() && SegmentMatches(pattern[p], path[s]) && MatchSegments(pattern, p + 1, path, s + 1);
}

bool PatternMatches(std::string_view pattern, std::string_view relative_path)
{
    return MatchSegments(SplitPath(pattern), 0, SplitPath(relative_path), 0);
}

bool IsExcluded(const std::string& relative_path, const std::vector<std::string>& excludes)
{
    return std::any_of(excludes.begin(), excludes.end(),
                       [&](const std::string& pattern) { return PatternMatches(pattern, relative_path); });
}

fs::path JoinRelative(const std::string& root, const std::string& relative_path)
{
    fs::path out(root);
    for (const auto& part : SplitPath(relative_path))
    {
        out /= part;
    }
    return out;
}

std::string PathString(const fs::path& path)
{
    return path.string();
}

cfgx::Node ManifestSchema()
{
    const char* schema_text = R"({
      "type": "object",
      "properties": {
        "name": {"type": "string"},
        "version": {"type": "string"},
        "source": {"type": "string"},
        "stage": {"type": "string"},
        "archive": {"type": "string"},
        "include": {"type": "array", "items": {"type": "string"}},
        "exclude": {"type": "array", "items": {"type": "string"}},
        "remove_extra": {"type": "boolean"}
      },
      "additionalProperties": false
    })";

    const auto parsed = cfgx::ParseJson(schema_text);
    return parsed.ok ? parsed.value : cfgx::Node::MakeObject();
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

cfgx::Result<PackConfig> LoadManifestConfig(const std::string& path)
{
    if (path.empty())
    {
        return cfgx::Result<PackConfig>{true, PackConfig{}, ""};
    }
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
    {
        return cfgx::Result<PackConfig>{false, PackConfig{}, "manifest file not found: " + path};
    }

    auto loaded = cfgx::LoadFromFile(path);
    if (!loaded.ok)
    {
        return cfgx::Result<PackConfig>{false, PackConfig{}, "manifest parse failed: " + loaded.error};
    }

    const auto compiled = schemax::Compile(ManifestSchema());
    if (!compiled.ok)
    {
        return cfgx::Result<PackConfig>{false, PackConfig{}, "internal manifest schema failed: " + compiled.error};
    }

    const auto issues = schemax::Validate(loaded.value, compiled.value);
    if (!issues.empty())
    {
        std::string message = "manifest validation failed";
        if (!issues.front().path.empty())
        {
            message += ": " + issues.front().path + " " + issues.front().message;
        }
        return cfgx::Result<PackConfig>{false, PackConfig{}, message};
    }

    PackConfig config;
    config.manifest = path;
    if (const auto* name = loaded.value.Get("name"); name != nullptr)
    {
        config.name = name->AsString();
    }
    if (const auto* version = loaded.value.Get("version"); version != nullptr)
    {
        config.version = version->AsString();
    }
    if (const auto* source = loaded.value.Get("source"); source != nullptr)
    {
        config.source = source->AsString();
    }
    if (const auto* stage = loaded.value.Get("stage"); stage != nullptr)
    {
        config.stage = stage->AsString();
    }
    if (const auto* archive = loaded.value.Get("archive"); archive != nullptr)
    {
        config.archive = archive->AsString();
    }
    if (const auto* remove_extra = loaded.value.Get("remove_extra"); remove_extra != nullptr)
    {
        config.remove_extra = remove_extra->AsBool(false);
    }
    config.includes = ReadStringArrayField(loaded.value, "include");
    config.excludes = ReadStringArrayField(loaded.value, "exclude");
    return cfgx::Result<PackConfig>{true, std::move(config), ""};
}

void ApplyCliOverrides(const argtool::ParseResult& parsed, PackConfig* config)
{
    if (parsed.Has("name") && !parsed.GetString("name").empty())
    {
        config->name = parsed.GetString("name");
    }
    if (parsed.Has("version") && !parsed.GetString("version").empty())
    {
        config->version = parsed.GetString("version");
    }
    if (parsed.Has("src") && !parsed.GetString("src").empty())
    {
        config->source = parsed.GetString("src");
    }
    if (parsed.Has("out") && !parsed.GetString("out").empty())
    {
        config->stage = parsed.GetString("out");
    }
    if (parsed.Has("archive") && !parsed.GetString("archive").empty())
    {
        config->archive = parsed.GetString("archive");
    }
    if (parsed.Has("include"))
    {
        config->includes = parsed.GetAll("include");
    }
    if (parsed.Has("exclude"))
    {
        config->excludes = parsed.GetAll("exclude");
    }
    if (parsed.Has("remove-extra"))
    {
        config->remove_extra = parsed.GetBool("remove-extra", false);
    }
    config->journal = parsed.GetString("journal", "");
    config->log_file = parsed.GetString("log-file", "");
    config->dry_run = parsed.GetBool("dry-run", false);
}

cfgx::Result<PackConfig> ResolveConfig(const argtool::ParseResult& parsed, const std::string& command)
{
    const std::string manifest_path = parsed.GetString("manifest", "");
    auto manifest = LoadManifestConfig(manifest_path);
    if (!manifest.ok)
    {
        return manifest;
    }

    PackConfig config = std::move(manifest.value);
    config.command = command;
    config.manifest = manifest_path;
    ApplyCliOverrides(parsed, &config);
    if (command == "plan")
    {
        config.dry_run = true;
    }
    if (command == "archive" && config.source.empty() && !config.stage.empty())
    {
        config.source = config.stage;
    }
    return cfgx::Result<PackConfig>{true, std::move(config), ""};
}

bool NormalizeConfigPaths(PackConfig* config, std::string* error)
{
    std::vector<std::string> includes;
    includes.reserve(config->includes.size());
    for (const auto& include : config->includes)
    {
        std::string normalized;
        if (!NormalizeRelativePath(include, false, &normalized, error))
        {
            return false;
        }
        includes.push_back(std::move(normalized));
    }
    config->includes = std::move(includes);

    std::vector<std::string> excludes;
    excludes.reserve(config->excludes.size());
    for (const auto& exclude : config->excludes)
    {
        std::string normalized;
        if (!NormalizeRelativePath(exclude, true, &normalized, error))
        {
            return false;
        }
        excludes.push_back(std::move(normalized));
    }
    config->excludes = std::move(excludes);
    return true;
}

cfgx::Result<std::vector<SelectedFile>> SelectFiles(const PackConfig& config)
{
    std::error_code ec;
    if (!fs::exists(config.source, ec) || ec || !fs::is_directory(config.source, ec))
    {
        return cfgx::Result<std::vector<SelectedFile>>{false, {}, "source directory not found: " + config.source};
    }

    std::vector<SelectedFile> out;
    std::set<std::string> seen;
    auto add_file = [&](const std::string& relative_path) -> cfgx::Status
    {
        if (seen.find(relative_path) != seen.end() || IsExcluded(relative_path, config.excludes))
        {
            return cfgx::Status{true, ""};
        }
        std::string boundary_error;
        if (!toolx_detail::SafeChildPath(fs::path(config.source), fs::path(relative_path), &boundary_error) ||
            !toolx_detail::SafeChildPath(fs::path(config.stage), fs::path(relative_path), &boundary_error,
                                         config.remove_extra))
            return cfgx::Status{false, boundary_error};
        const fs::path source_file = JoinRelative(config.source, relative_path);
        std::error_code size_ec;
        const auto size = fs::file_size(source_file, size_ec);
        if (size_ec)
        {
            return cfgx::Status{false, "failed to read source file size: " + source_file.string()};
        }
        seen.insert(relative_path);
        out.push_back(SelectedFile{relative_path, PathString(source_file),
                                   PathString(JoinRelative(config.stage, relative_path)), size});
        return cfgx::Status{true, ""};
    };

    if (config.includes.empty())
    {
        fsx::WalkOptions options;
        options.recursive = true;
        options.include_directories = false;
        options.include_files = true;
        options.relative_path = true;
        const auto walked = fsx::WalkDirectory(config.source, options);
        if (!walked.ok)
        {
            return cfgx::Result<std::vector<SelectedFile>>{false, {}, walked.error};
        }
        for (const auto& entry : walked.entries)
        {
            const auto status = add_file(entry.path);
            if (!status.ok)
            {
                return cfgx::Result<std::vector<SelectedFile>>{false, {}, status.error};
            }
        }
    }
    else
    {
        for (const auto& include : config.includes)
        {
            std::string boundary_error;
            if (!toolx_detail::SafeChildPath(fs::path(config.source), fs::path(include), &boundary_error))
                return cfgx::Result<std::vector<SelectedFile>>{false, {}, boundary_error};
            const fs::path source_path = JoinRelative(config.source, include);
            if (!fs::exists(source_path, ec) || ec)
            {
                return cfgx::Result<std::vector<SelectedFile>>{false, {}, "include path not found: " + include};
            }
            if (fs::is_regular_file(source_path, ec))
            {
                const auto status = add_file(include);
                if (!status.ok)
                {
                    return cfgx::Result<std::vector<SelectedFile>>{false, {}, status.error};
                }
                continue;
            }
            if (fs::is_directory(source_path, ec))
            {
                fsx::WalkOptions options;
                options.recursive = true;
                options.include_directories = false;
                options.include_files = true;
                options.relative_path = true;
                const auto walked = fsx::WalkDirectory(source_path.string(), options);
                if (!walked.ok)
                {
                    return cfgx::Result<std::vector<SelectedFile>>{false, {}, walked.error};
                }
                for (const auto& entry : walked.entries)
                {
                    const auto status = add_file(include + "/" + entry.path);
                    if (!status.ok)
                    {
                        return cfgx::Result<std::vector<SelectedFile>>{false, {}, status.error};
                    }
                }
                continue;
            }
            return cfgx::Result<std::vector<SelectedFile>>{
                false, {}, "include path is not a regular file or directory: " + include};
        }
    }

    std::sort(out.begin(), out.end(),
              [](const SelectedFile& lhs, const SelectedFile& rhs) { return lhs.relative_path < rhs.relative_path; });
    return cfgx::Result<std::vector<SelectedFile>>{true, std::move(out), ""};
}

void AddTargetDirectories(const std::string& relative_path, std::set<std::string>* dirs)
{
    const auto parts = SplitPath(relative_path);
    std::string current;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        if (!current.empty())
        {
            current.push_back('/');
        }
        current += parts[i];
        dirs->insert(current);
    }
}

cfgx::Status AddRemoveExtraSteps(const PackConfig& config, const std::set<std::string>& target_files,
                                 fsx::BatchPlan* batch, std::vector<PlannedStep>* planned_steps)
{
    std::error_code ec;
    const bool exists = fs::exists(config.stage, ec);
    if (ec)
        return {false, "cannot inspect stage: " + ec.message()};
    if (!exists)
        return {true, ""};

    fsx::WalkOptions options;
    options.recursive = true;
    options.include_directories = true;
    options.include_files = true;
    options.relative_path = true;
    const auto walked = fsx::WalkDirectory(config.stage, options);
    if (!walked.ok)
    {
        return {false, walked.error};
    }

    std::set<std::string> target_dirs;
    for (const auto& file : target_files)
    {
        AddTargetDirectories(file, &target_dirs);
    }

    std::vector<std::string> extra_files;
    std::vector<std::string> extra_dirs;
    for (const auto& entry : walked.entries)
    {
        const std::string rel = entry.path;
        if (entry.is_directory)
        {
            if (target_dirs.find(rel) == target_dirs.end())
            {
                extra_dirs.push_back(rel);
            }
            continue;
        }
        if (target_files.find(rel) == target_files.end())
        {
            extra_files.push_back(rel);
        }
    }

    std::sort(extra_files.begin(), extra_files.end());
    std::sort(extra_dirs.begin(), extra_dirs.end(),
              [](const std::string& lhs, const std::string& rhs) { return lhs.size() > rhs.size(); });

    for (const auto& rel : extra_files)
    {
        const std::string path = PathString(JoinRelative(config.stage, rel));
        batch->AddRemovePath(path);
        planned_steps->push_back(PlannedStep{"remove_path", "", path});
    }
    for (const auto& rel : extra_dirs)
    {
        const std::string path = PathString(JoinRelative(config.stage, rel));
        batch->AddRemovePath(path);
        planned_steps->push_back(PlannedStep{"remove_path", "", path});
    }
    return {true, ""};
}

cfgx::Result<PackPlan> BuildStagePlan(const PackConfig& config)
{
    std::error_code ec;
    auto source = fs::absolute(config.source, ec);
    if (ec)
        return {false, {}, "cannot resolve source root: " + ec.message()};
    source = fs::weakly_canonical(source, ec);
    if (ec)
        return {false, {}, "cannot resolve source root: " + ec.message()};
    auto stage = fs::absolute(config.stage, ec);
    if (ec)
        return {false, {}, "cannot resolve stage root: " + ec.message()};
    stage = fs::weakly_canonical(stage, ec);
    if (ec)
        return {false, {}, "cannot resolve stage root: " + ec.message()};
    auto src = source.begin();
    auto dst = stage.begin();
    for (; src != source.end() && dst != stage.end(); ++src, ++dst)
    {
#ifdef _WIN32
        if (CompareStringOrdinal(src->c_str(), -1, dst->c_str(), -1, TRUE) != CSTR_EQUAL)
            break;
#else
        if (*src != *dst)
            break;
#endif
    }
    if (src == source.end() || dst == stage.end())
        return {false, {}, "source and stage roots must not overlap"};

    auto selected = SelectFiles(config);
    if (!selected.ok)
    {
        return cfgx::Result<PackPlan>{false, {}, selected.error};
    }

    PackPlan plan;
    std::set<std::string> target_files;
    plan.files = std::move(selected.value);
    for (const auto& file : plan.files)
    {
        plan.bytes += file.size;
        target_files.insert(file.relative_path);
    }
    if (config.remove_extra)
    {
        const auto removed = AddRemoveExtraSteps(config, target_files, &plan.batch, &plan.planned_steps);
        if (!removed.ok)
            return {false, {}, removed.error};
    }
    // Remove obsolete entries and type conflicts before copying their replacements.
    // These remain transactional BatchPlan operations, including on copy failure.
    for (const auto& file : plan.files)
    {
        plan.batch.AddCopyFile(file.source_path, file.destination_path);
        plan.planned_steps.push_back(PlannedStep{"copy_file", file.source_path, file.destination_path});
    }
    if (!config.archive.empty())
    {
        plan.planned_steps.push_back(PlannedStep{"create_archive", config.stage, config.archive});
    }
    return cfgx::Result<PackPlan>{true, std::move(plan), ""};
}

cfgx::Node BuildData(const PackConfig& config, const PackPlan& plan, std::size_t completed_steps,
                     const std::vector<std::string>& warnings)
{
    return BuildDataObject({
        {"command", cfgx::Node(config.command)},
        {"name", cfgx::Node(config.name)},
        {"version", cfgx::Node(config.version)},
        {"source", cfgx::Node(config.source)},
        {"stage", cfgx::Node(config.stage)},
        {"archive", cfgx::Node(config.archive)},
        {"manifest", cfgx::Node(config.manifest)},
        {"dry_run", cfgx::Node(config.dry_run)},
        {"remove_extra", cfgx::Node(config.remove_extra)},
        {"entries", cfgx::Node(static_cast<std::int64_t>(plan.files.size()))},
        {"bytes", cfgx::Node(static_cast<std::int64_t>(plan.bytes))},
        {"planned_steps", BuildPlannedStepsArray(plan.planned_steps)},
        {"completed_steps", cfgx::Node(static_cast<std::int64_t>(completed_steps))},
        {"archive_format", cfgx::Node(config.archive.empty() ? "" : "tar")},
        {"capabilities", BuildCapabilitiesObject()},
        {"warnings", BuildStringArray(warnings)},
    });
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

int RunStage(const PackConfig& config, bool json_mode)
{
    if (config.source.empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing required option --src");
    }
    if (config.stage.empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing required option --out");
    }

    auto plan = BuildStagePlan(config);
    if (!plan.ok)
    {
        return ExitError(json_mode, kExitNotFound, plan.error);
    }

    std::vector<std::string> warnings;
    if (config.dry_run)
    {
        const auto data = BuildData(config, plan.value, 0, warnings);
        if (json_mode)
        {
            PrintJsonEnvelope(true, kExitSuccess, config.command == "plan" ? "plan passed" : "dry run passed", data);
        }
        else
        {
            std::cout << "staged=" << config.stage << "\n";
            if (!config.archive.empty())
            {
                std::cout << "archive=" << config.archive << "\n";
            }
            std::cout << "entries=" << plan.value.files.size() << "\n";
            std::cout << "bytes=" << plan.value.bytes << "\n";
            std::cout << "steps=" << plan.value.planned_steps.size() << "\n";
        }
        return kExitSuccess;
    }

    ConfigureLogging(config.log_file);
    fsx::RunOptions run_options;
    run_options.conflict_policy = fsx::ConflictPolicy::Overwrite;
    run_options.rollback_mode = fsx::RollbackMode::BestEffort;
    run_options.journal_path = config.journal;
    run_options.keep_journal_on_success = !config.journal.empty();
    const auto run = fsx::Run(plan.value.batch, run_options);
    if (!run.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, "failed to stage files: " + run.error);
    }

    std::size_t completed_steps = run.completed_steps;
    if (!config.archive.empty())
    {
        const auto archived = fsx::CreateArchive(config.stage, config.archive);
        if (!archived.ok)
        {
            return ExitError(json_mode, kExitRuntimeError, "failed to create archive: " + archived.error);
        }
        ++completed_steps;
    }

    LOGI("toolx-pack staged %s entries=%zu archive=%s", config.stage.c_str(), plan.value.files.size(),
         config.archive.c_str());
    logsys::Logger::Instance().Flush();

    const auto data = BuildData(config, plan.value, completed_steps, warnings);
    if (json_mode)
    {
        PrintJsonEnvelope(true, kExitSuccess, "staged", data);
    }
    else
    {
        std::cout << "staged=" << config.stage << "\n";
        if (!config.archive.empty())
        {
            std::cout << "archive=" << config.archive << "\n";
        }
        std::cout << "entries=" << plan.value.files.size() << "\n";
        std::cout << "bytes=" << plan.value.bytes << "\n";
        std::cout << "steps=" << plan.value.planned_steps.size() << "\n";
    }
    return kExitSuccess;
}

int RunArchive(PackConfig config, bool json_mode)
{
    if (config.source.empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing required option --src");
    }
    if (config.archive.empty())
    {
        return ExitError(json_mode, kExitUsageError, "missing required option --archive");
    }

    std::error_code ec;
    if (!fs::exists(config.source, ec) || ec || !fs::is_directory(config.source, ec))
    {
        return ExitError(json_mode, kExitNotFound, "source directory not found: " + config.source);
    }
    if (config.stage.empty())
    {
        config.stage = config.source;
    }

    fsx::WalkOptions walk_options;
    walk_options.recursive = true;
    walk_options.include_directories = false;
    walk_options.include_files = true;
    walk_options.relative_path = true;
    const auto walked = fsx::WalkDirectory(config.source, walk_options);
    if (!walked.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, walked.error);
    }

    PackPlan plan;
    for (const auto& entry : walked.entries)
    {
        const fs::path source_file = JoinRelative(config.source, entry.path);
        std::error_code size_ec;
        const auto size = fs::file_size(source_file, size_ec);
        if (!size_ec)
        {
            plan.bytes += size;
        }
        plan.files.push_back(SelectedFile{entry.path, PathString(source_file), entry.path, size});
    }
    plan.planned_steps.push_back(PlannedStep{"create_archive", config.source, config.archive});

    std::vector<std::string> warnings;
    if (config.dry_run)
    {
        const auto data = BuildData(config, plan, 0, warnings);
        if (json_mode)
        {
            PrintJsonEnvelope(true, kExitSuccess, "dry run passed", data);
        }
        else
        {
            std::cout << "staged=" << config.stage << "\n";
            std::cout << "archive=" << config.archive << "\n";
            std::cout << "entries=" << plan.files.size() << "\n";
            std::cout << "bytes=" << plan.bytes << "\n";
            std::cout << "steps=" << plan.planned_steps.size() << "\n";
        }
        return kExitSuccess;
    }

    ConfigureLogging(config.log_file);
    const auto archived = fsx::CreateArchive(config.source, config.archive);
    if (!archived.ok)
    {
        return ExitError(json_mode, kExitRuntimeError, "failed to create archive: " + archived.error);
    }

    LOGI("toolx-pack archived %s entries=%zu", config.archive.c_str(), plan.files.size());
    logsys::Logger::Instance().Flush();

    const auto data = BuildData(config, plan, 1, warnings);
    if (json_mode)
    {
        PrintJsonEnvelope(true, kExitSuccess, "archived", data);
    }
    else
    {
        std::cout << "staged=" << config.stage << "\n";
        std::cout << "archive=" << config.archive << "\n";
        std::cout << "entries=" << plan.files.size() << "\n";
        std::cout << "bytes=" << plan.bytes << "\n";
        std::cout << "steps=" << plan.planned_steps.size() << "\n";
    }
    return kExitSuccess;
}

} // namespace

int main(int argc, const char* const argv[])
{
    argtool::Parser parser;
    parser.SetProgramName("toolx-pack")
        .SetDescription("toolx-pack - stage release trees and create deterministic tar archives")
        .SetUsageExample("toolx-pack stage --src build/install --out dist/toolx --archive dist/toolx.tar")
        .SetHelpLayout(argtool::HelpLayout::Fixed);

    parser.AddSubcommandRoot("stage", "Stage a release-shaped output tree")
        .AddSubcommandRoot("archive", "Create a deterministic tar archive from a staged tree")
        .AddSubcommandRoot("plan", "Print the staging plan without writing files");

    parser.Option("src").String().ValueName("DIR").Description("Source directory.").Done();
    parser.Option("out").String().ValueName("DIR").Description("Stage output directory.").Done();
    parser.Option("archive").String().ValueName("FILE").Description("Optional tar archive output.").Done();
    parser.Option("manifest").String().ValueName("FILE").Description("Optional pack manifest.").Done();
    parser.Option("name").String().ValueName("TEXT").Description("Package name override.").Done();
    parser.Option("version").String().ValueName("TEXT").Description("Package version override.").Done();
    parser.Option("include")
        .String()
        .ListValue()
        .ValueName("PATH")
        .Description("Relative include path. Repeatable.")
        .Done();
    parser.Option("exclude")
        .String()
        .ListValue()
        .ValueName("PATTERN")
        .Description("Relative exclude pattern. Repeatable.")
        .Done();
    parser.Option("journal").String().ValueName("FILE").Description("Optional fsx journal file.").Done();
    parser.Option("log-file").String().ValueName("FILE").Description("Optional audit log file.").Done();
    parser.Flag("remove-extra").Description("Remove staged files absent from the selected source set.").Done();
    parser.Flag("dry-run").Description("Report planned work without writing files.").Done();
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
    auto resolved = ResolveConfig(parsed, command);
    if (!resolved.ok)
    {
        const int code =
            resolved.error.find("not found") != std::string::npos ? kExitNotFound : kExitManifestValidationFailed;
        return ExitError(json_mode, code, resolved.error);
    }

    std::string path_error;
    if (!NormalizeConfigPaths(&resolved.value, &path_error))
    {
        return ExitError(json_mode, kExitUsageError, path_error);
    }

    if (command == "archive")
    {
        return RunArchive(std::move(resolved.value), json_mode);
    }
    return RunStage(resolved.value, json_mode);
}
