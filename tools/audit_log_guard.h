#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "logsys.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace toolx_cli
{

inline bool LoggingPathsOverlap(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    auto left = lhs.begin();
    auto right = rhs.begin();
    for (; left != lhs.end() && right != rhs.end(); ++left, ++right)
    {
#if defined(_WIN32)
        if (CompareStringOrdinal(left->native().c_str(), -1, right->native().c_str(), -1, TRUE) != CSTR_EQUAL)
#else
        if (*left != *right)
#endif
            return false;
    }
    return true;
}

inline bool ResolveLoggingBoundaryPath(const std::filesystem::path& path, std::filesystem::path* resolved,
                                       std::string* error)
{
    if (path.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
    {
        *error = "embedded null bytes are unsupported in audit paths";
        return false;
    }
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        *error = ec.message();
        return false;
    }

    // Match the existing sync/pack/inspect boundary: dangling links and
    // traversal after a missing component cannot be treated as new files.
    auto prefix = absolute.root_path();
    bool missing_component = false;
#if defined(_WIN32)
    const bool extended_root = absolute.root_name().native() == L"\\\\?";
    bool first_component = true;
#endif
    for (const auto& component : absolute.relative_path())
    {
#if defined(_WIN32)
        const auto& native = component.native();
        const bool extended_drive =
            first_component && extended_root && native.size() == 2 && native[1] == L':' &&
            ((native[0] >= L'A' && native[0] <= L'Z') || (native[0] >= L'a' && native[0] <= L'z'));
        first_component = false;
        if (extended_drive)
        {
            prefix += native;
            prefix += std::filesystem::path::preferred_separator;
            continue;
        }
        if (native.find(L':') != std::filesystem::path::string_type::npos)
        {
            *error = "Windows stream paths are unsupported with --log-file: " + path.string();
            return false;
        }
#endif
        if (missing_component && component == "..")
        {
            *error = "cannot confirm parent traversal after a missing path component: " + path.string();
            return false;
        }
        prefix /= component;
        const auto link_status = std::filesystem::symlink_status(prefix, ec);
        bool linked = std::filesystem::is_symlink(link_status);
#if defined(_WIN32)
        // MSVC may report a dangling junction as not-found. Native attributes
        // still identify its reparse point before the missing-path fast path.
        const auto attributes = GetFileAttributesW(prefix.c_str());
        linked = linked || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (linked)
        {
            std::error_code target_ec;
            const auto target_status = std::filesystem::status(prefix, target_ec);
            if (target_ec || !std::filesystem::exists(target_status) ||
                target_status.type() == std::filesystem::file_type::unknown)
            {
                *error = "cannot confirm symbolic link or reparse point target: " + prefix.string();
                return false;
            }
        }
        if (ec == std::errc::no_such_file_or_directory ||
            (!ec && link_status.type() == std::filesystem::file_type::not_found))
        {
            missing_component = true;
            ec.clear();
            continue;
        }
        if (ec)
        {
            *error = "cannot inspect path component " + prefix.string() + ": " + ec.message();
            return false;
        }
    }

    *resolved = std::filesystem::weakly_canonical(absolute, ec).lexically_normal();
    if (ec)
    {
        *error = ec.message();
        return false;
    }
    return true;
}

inline bool ValidateAuditLogPath(const std::string& log_file, const std::vector<std::string>& protected_paths,
                                 std::string* error)
{
    const std::filesystem::path log_path(log_file);
    std::filesystem::path resolved_log;
    if (!ResolveLoggingBoundaryPath(log_path, &resolved_log, error))
    {
        *error = "cannot resolve --log-file path: " + *error;
        return false;
    }
    for (const auto& name : protected_paths)
    {
        if (name.empty())
            continue;
        const std::filesystem::path path(name);
        std::filesystem::path resolved;
        if (!ResolveLoggingBoundaryPath(path, &resolved, error))
        {
            *error = "cannot resolve protected path for --log-file: " + name + ": " + *error;
            return false;
        }
        std::error_code ec;
        const bool same_file = std::filesystem::equivalent(log_path, path, ec) && !ec;
        if (same_file || LoggingPathsOverlap(resolved_log, resolved))
        {
            *error = "--log-file overlaps a protected input path: " + name;
            return false;
        }
    }
    return true;
}

inline bool ValidateAuditLogPaths(const std::string& log_file, const std::vector<std::string>& protected_paths,
                                  const logsys::RollingConfigV2& rolling, std::string* error)
{
    if (log_file.empty())
        return true;
    if (!ValidateAuditLogPath(log_file, protected_paths, error))
        return false;
    if (rolling.enabled && rolling.max_file_size_bytes > 0)
    {
        for (std::size_t i = 0; i < rolling.keep_recent_files; ++i)
        {
            const auto archive = log_file + "." + std::to_string(i + 1);
            if (!ValidateAuditLogPath(archive, protected_paths, error))
            {
                *error = "log rotation path " + archive + ": " + *error;
                return false;
            }
        }
    }
    return true;
}

} // namespace toolx_cli
