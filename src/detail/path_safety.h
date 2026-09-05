#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef CopyFile
#undef CopyFile
#endif
#endif

namespace toolx_detail
{
inline bool SafeRelativePath(const std::filesystem::path& path)
{
    const auto text = path.generic_string();
    if (text.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        text.find(':') != std::string::npos || text.find('\\') != std::string::npos ||
        text.find('\0') != std::string::npos)
        return false;
    for (const auto& component : path)
        if (component == ".." || component == "." || component.empty())
            return false;
    return true;
}

// The caller owns the root. Reject links/reparse points below it, including
// dangling links. This is a static boundary check, not a race-free sandbox.
inline bool SafeChildPath(const std::filesystem::path& root, const std::filesystem::path& relative, std::string* error)
{
    if (!SafeRelativePath(relative))
    {
        *error = "unsafe relative path: " + relative.generic_string();
        return false;
    }
    auto current = root;
    for (const auto& component : relative)
    {
        current /= component;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(current, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
        {
            *error = "cannot inspect path: " + current.string() + ": " + ec.message();
            return false;
        }
        bool linked = std::filesystem::is_symlink(status);
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        linked = linked || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (linked)
        {
            *error = "symbolic links and reparse points are not allowed below root: " + current.string();
            return false;
        }
    }
    return true;
}
} // namespace toolx_detail
