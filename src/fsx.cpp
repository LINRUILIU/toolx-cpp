#include "fsx.h"
#include "detail/path_safety.h"

#include "utils.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <system_error>
#include <thread>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
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
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fsx
{
namespace
{

using Path = std::filesystem::path;

struct UndoAction
{
    enum class Kind : std::uint8_t
    {
        RemovePath,
        MovePath
    };

    Kind kind{Kind::RemovePath};
    Path from;
    Path to;
};

struct ExecuteState
{
    const RunOptions* options{nullptr};
    std::vector<UndoAction> undo_stack;
    std::vector<Path> cleanup_after_commit;
};

struct ExecOutcome
{
    bool ok{false};
    bool skipped{false};
    std::string error;
};

bool ensure_parent(const Path& p, std::string* error)
{
    const auto st = utils::path::ensure_parent_dir(p.string());
    if (!st.ok)
    {
        *error = st.error;
        return false;
    }
    return true;
}

Path make_temp_path(const Path& base, std::string_view tag)
{
    static std::atomic<std::uint64_t> next_id{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream os;
    os << base.string() << "." << tag << ".tmp." << now << "." << next_id.fetch_add(1, std::memory_order_relaxed);
    return Path(os.str());
}

bool path_exists(const Path& p)
{
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
}

std::string boundary_key(Path path)
{
    std::error_code ec;
    path = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        ec.clear();
        path = std::filesystem::absolute(path, ec);
    }
    path = path.lexically_normal();

    std::string out = path.generic_string();
#if defined(_WIN32)
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    while (out.size() > 1 && out.back() == '/')
    {
        out.pop_back();
    }
    return out;
}

bool path_is_within(std::string_view child, std::string_view parent)
{
    if (child.size() <= parent.size())
    {
        return false;
    }
    if (child.compare(0, parent.size(), parent) != 0)
    {
        return false;
    }
    return parent == "/" || child[parent.size()] == '/';
}

bool path_is_at_or_within(std::string_view child, std::string_view parent)
{
    return child == parent || path_is_within(child, parent);
}

bool files_have_same_content(const Path& lhs, const Path& rhs)
{
    std::error_code ec;
    if (std::filesystem::file_size(lhs, ec) != std::filesystem::file_size(rhs, ec) || ec)
    {
        return false;
    }

    std::ifstream left(lhs, std::ios::binary);
    std::ifstream right(rhs, std::ios::binary);
    if (!left || !right)
    {
        return false;
    }

    std::array<char, 8192> left_buffer{};
    std::array<char, 8192> right_buffer{};
    while (left && right)
    {
        left.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        right.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        const auto left_count = left.gcount();
        const auto right_count = right.gcount();
        if (left_count != right_count)
        {
            return false;
        }
        if (!std::equal(left_buffer.begin(), left_buffer.begin() + left_count, right_buffer.begin()))
        {
            return false;
        }
    }
    return left.eof() && right.eof();
}

std::string escape_field(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (const char c : in)
    {
        if (c == '%' || c == '|' || c == '\n' || c == '\r')
        {
            const char hex[] = "0123456789ABCDEF";
            const unsigned char b = static_cast<unsigned char>(c);
            out.push_back('%');
            out.push_back(hex[(b >> 4) & 0x0F]);
            out.push_back(hex[b & 0x0F]);
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

int hex_to_int(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return 10 + c - 'A';
    }
    if (c >= 'a' && c <= 'f')
    {
        return 10 + c - 'a';
    }
    return -1;
}

std::string unescape_field(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const char c = in[i];
        if (c == '%' && (i + 2) < in.size())
        {
            const int hi = hex_to_int(in[i + 1]);
            const int lo = hex_to_int(in[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> split_pipe(std::string_view line)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i)
    {
        if (i == line.size() || line[i] == '|')
        {
            out.push_back(std::string(line.substr(start, i - start)));
            start = i + 1;
        }
    }
    return out;
}

constexpr std::string_view kJournalHeaderV1{"FSXJ1"};
constexpr std::string_view kJournalHeaderV2{"FSXJ2"};
constexpr std::string_view kJournalHeaderV3{"FSXJ3"};
constexpr std::string_view kJournalCommit{"COMMIT"};

bool journal_can_start(const RunOptions& options, std::string* error)
{
    if (options.journal_path.empty())
    {
        return true;
    }

    const Path journal(options.journal_path);
    std::error_code ec;
    const bool exists = std::filesystem::exists(journal, ec);
    if (ec)
    {
        if (error != nullptr)
        {
            *error = utils::err::join_context("fsx", "journal", "failed to inspect existing journal: " + ec.message());
        }
        return false;
    }
    if (!exists)
    {
        return true;
    }

    std::ifstream in(journal, std::ios::binary);
    if (!in.is_open())
    {
        if (error != nullptr)
        {
            *error = utils::err::join_context("fsx", "journal", "failed to inspect existing journal");
        }
        return false;
    }

    std::string header;
    if (!std::getline(in, header))
    {
        return true;
    }
    if (header == kJournalHeaderV2 || header == kJournalHeaderV3)
    {
        std::string line;
        while (std::getline(in, line))
        {
            if (line == kJournalCommit)
            {
                return true;
            }
        }
    }

    if (error != nullptr)
    {
        *error = utils::err::join_context(
            "fsx", "journal", "existing journal is incomplete; recover or remove it before starting a new run");
    }
    return false;
}

bool write_journal_bytes_sync(const Path& journal_path, std::string_view bytes, bool truncate,
                              std::string_view operation, std::string* error)
{
    auto fail = [error, operation](std::string detail)
    {
        if (error != nullptr)
        {
            *error = utils::err::join_context("fsx", "journal",
                                              "failed to " + std::string(operation) + ": " + std::move(detail));
        }
        return false;
    };

#if defined(_WIN32)
    const DWORD disposition = truncate ? CREATE_ALWAYS : OPEN_ALWAYS;
    HANDLE handle = CreateFileW(journal_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return fail(std::system_category().message(static_cast<int>(GetLastError())));
    }

    auto close_handle = [&handle]()
    {
        if (handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    };

    if (!truncate)
    {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_END))
        {
            const DWORD code = GetLastError();
            close_handle();
            return fail(std::system_category().message(static_cast<int>(code)));
        }
    }

    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) || written == 0)
        {
            const DWORD code = GetLastError();
            close_handle();
            return fail(std::system_category().message(static_cast<int>(code)));
        }
        offset += written;
    }

    if (!FlushFileBuffers(handle))
    {
        const DWORD code = GetLastError();
        close_handle();
        return fail(std::system_category().message(static_cast<int>(code)));
    }
    if (!CloseHandle(handle))
    {
        handle = INVALID_HANDLE_VALUE;
        return fail(std::system_category().message(static_cast<int>(GetLastError())));
    }
    handle = INVALID_HANDLE_VALUE;
    return true;
#else
    const int flags = O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND);
    const int fd = ::open(journal_path.c_str(), flags, 0600);
    if (fd < 0)
    {
        return fail(std::strerror(errno));
    }

    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const ssize_t written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            const int code = errno;
            (void)::close(fd);
            return fail(std::strerror(code));
        }
        if (written == 0)
        {
            (void)::close(fd);
            return fail("short write");
        }
        offset += static_cast<std::size_t>(written);
    }

    if (::fsync(fd) != 0)
    {
        const int code = errno;
        (void)::close(fd);
        return fail(std::strerror(code));
    }
    if (::close(fd) != 0)
    {
        return fail(std::strerror(errno));
    }
    return true;
#endif
}

bool journal_append_line(const RunOptions& options, std::string_view line, std::string_view operation,
                         std::string* error)
{
    if (options.journal_path.empty())
    {
        return true;
    }
    return write_journal_bytes_sync(Path(options.journal_path), line, false, operation, error);
}

bool journal_write_header(const RunOptions& options, std::string* error)
{
    if (!journal_can_start(options, error))
    {
        return false;
    }
    if (options.journal_path.empty())
    {
        return true;
    }

    return write_journal_bytes_sync(Path(options.journal_path), std::string(kJournalHeaderV3) + "\n", true,
                                    "write journal header", error);
}

bool journal_write_undo(const RunOptions& options, const UndoAction& undo, std::string* error)
{
    if (undo.kind == UndoAction::Kind::RemovePath)
    {
        return journal_append_line(options, "UNDO|REMOVE|" + escape_field(undo.from.string()) + "\n",
                                   "append journal undo", error);
    }
    return journal_append_line(
        options, "UNDO|MOVE|" + escape_field(undo.from.string()) + "|" + escape_field(undo.to.string()) + "\n",
        "append journal undo", error);
}

bool journal_write_commit(const RunOptions& options, std::string* error)
{
    return journal_append_line(options, std::string(kJournalCommit) + "\n", "commit journal", error);
}

void trigger_journal_failpoint_after_sync(const ExecuteState& state)
{
#if defined(FSX_ENABLE_TEST_FAILPOINTS)
    if (state.options == nullptr || state.options->journal_path.empty())
    {
        return;
    }
    const char* value = std::getenv("TOOLX_FSX_TEST_FAILPOINT");
    if (value != nullptr && std::strcmp(value, "after-journal-sync-before-mutation") == 0)
    {
        std::_Exit(86);
    }
#else
    (void)state;
#endif
}

bool record_undo(ExecuteState* state, const UndoAction& undo, std::string* error, bool trigger_failpoint)
{
    if (state == nullptr || state->options == nullptr)
    {
        if (error != nullptr)
        {
            *error = utils::err::join_context("fsx", "journal", "missing execution state");
        }
        return false;
    }
    if (!journal_write_undo(*state->options, undo, error))
    {
        return false;
    }
    state->undo_stack.push_back(undo);
    if (trigger_failpoint)
    {
        trigger_journal_failpoint_after_sync(*state);
    }
    return true;
}

void track_cleanup_after_commit(ExecuteState* state, const Path& path)
{
    if (state != nullptr)
    {
        state->cleanup_after_commit.push_back(path);
    }
}

bool move_path_without_overwrite(const Path& from, const Path& to, std::string_view operation, std::string* error)
{
    if (!path_exists(from))
    {
        *error = utils::err::join_context("fsx", std::string(operation), "source path not found");
        return false;
    }
    if (!ensure_parent(to, error))
    {
        return false;
    }
    if (path_exists(to))
    {
        *error = utils::err::join_context("fsx", std::string(operation), "destination already exists");
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec)
    {
        *error = utils::err::join_context("fsx", std::string(operation), ec.message());
        return false;
    }
    return true;
}

bool write_temporary_file(const Path& temp, std::string_view data, std::string* error)
{
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        *error = utils::err::join_context("fsx", "atomic_write", "failed to create temporary file");
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out.good())
    {
        out.close();
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        *error = utils::err::join_context("fsx", "atomic_write", "failed to write temporary file");
        return false;
    }
    return true;
}

bool copy_to_temporary_file(const Path& src, const Path& temp, std::string* error)
{
    std::error_code ec;
    std::filesystem::copy_file(src, temp, std::filesystem::copy_options::none, ec);
    if (ec)
    {
        const std::string detail = ec.message();
        std::filesystem::remove(temp, ec);
        *error = utils::err::join_context("fsx", "copy_file", detail);
        return false;
    }
    return true;
}

bool record_temporary_cleanup(ExecuteState* state, const Path& temp, std::string* error)
{
    return record_undo(state, {UndoAction::Kind::RemovePath, temp, {}}, error, false);
}

bool write_file_atomic(const Path& target, std::string_view data, ConflictPolicy conflict_policy, ExecuteState* state,
                       std::string* error, bool* skipped)
{
    if (skipped != nullptr)
    {
        *skipped = false;
    }
    if (!ensure_parent(target, error))
    {
        return false;
    }
    const bool had_existing = path_exists(target);
    if (had_existing && conflict_policy == ConflictPolicy::Skip)
    {
        if (skipped != nullptr)
        {
            *skipped = true;
        }
        return true;
    }
    if (had_existing && conflict_policy == ConflictPolicy::Fail)
    {
        *error = utils::err::join_context("fsx", "atomic_write", "target exists and overwrite is disabled");
        return false;
    }

    const Path temp = make_temp_path(target, "new");
    if (!write_temporary_file(temp, data, error))
    {
        return false;
    }
    if (!record_temporary_cleanup(state, temp, error))
    {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return false;
    }

    if (had_existing)
    {
        const Path previous_backup = make_temp_path(target, "old");
        if (!record_undo(state, {UndoAction::Kind::MovePath, previous_backup, target}, error, true) ||
            !move_path_without_overwrite(target, previous_backup, "atomic_write", error))
        {
            return false;
        }
        track_cleanup_after_commit(state, previous_backup);
    }

    if (!record_undo(state, {UndoAction::Kind::MovePath, target, temp}, error, true))
    {
        return false;
    }
    return move_path_without_overwrite(temp, target, "atomic_write", error);
}

bool move_existing_destination_to_backup(const Path& dst, const Path& destination_backup, bool keep_backup,
                                         ExecuteState* state, std::string_view operation, std::string* error)
{
    if (path_exists(destination_backup))
    {
        const Path prior_backup = make_temp_path(destination_backup, "prior");
        if (!record_undo(state, {UndoAction::Kind::MovePath, prior_backup, destination_backup}, error, true) ||
            !move_path_without_overwrite(destination_backup, prior_backup, operation, error))
        {
            return false;
        }
        track_cleanup_after_commit(state, prior_backup);
    }

    if (!record_undo(state, {UndoAction::Kind::MovePath, destination_backup, dst}, error, true) ||
        !move_path_without_overwrite(dst, destination_backup, operation, error))
    {
        return false;
    }
    if (!keep_backup)
    {
        track_cleanup_after_commit(state, destination_backup);
    }
    return true;
}

bool safe_replace_file(const Path& src, const Path& dst, bool backup, const RunOptions& options, ExecuteState* state,
                       std::string* error, bool* skipped)
{
    if (skipped != nullptr)
    {
        *skipped = false;
    }

    if (!path_exists(src))
    {
        *error = utils::err::join_context("fsx", "safe_replace", "source file not found");
        return false;
    }

    if (!ensure_parent(dst, error))
    {
        return false;
    }

    const bool destination_exists = path_exists(dst);
    if (destination_exists)
    {
        if (options.conflict_policy == ConflictPolicy::Skip)
        {
            if (skipped != nullptr)
            {
                *skipped = true;
            }
            return true;
        }

        if (options.conflict_policy == ConflictPolicy::Fail)
        {
            *error = utils::err::join_context("fsx", "safe_replace", "destination exists and overwrite is disabled");
            return false;
        }

        const bool keep_backup = backup || options.default_backup;
        const Path destination_backup =
            keep_backup ? Path(dst.string() + options.backup_suffix) : make_temp_path(dst, "old");
        if (!move_existing_destination_to_backup(dst, destination_backup, keep_backup, state, "safe_replace", error))
        {
            return false;
        }
    }

    if (!record_undo(state, {UndoAction::Kind::MovePath, dst, src}, error, true))
    {
        return false;
    }
    return move_path_without_overwrite(src, dst, "safe_replace", error);
}

bool rename_file(const Path& src, const Path& dst, const RunOptions& options, ExecuteState* state, std::string* error,
                 bool* skipped)
{
    if (skipped != nullptr)
    {
        *skipped = false;
    }
    if (!path_exists(src))
    {
        *error = utils::err::join_context("fsx", "rename", "source file not found");
        return false;
    }
    if (!ensure_parent(dst, error))
    {
        return false;
    }

    const bool destination_exists = path_exists(dst);
    if (destination_exists)
    {
        if (options.conflict_policy == ConflictPolicy::Skip)
        {
            if (skipped != nullptr)
            {
                *skipped = true;
            }
            return true;
        }
        if (options.conflict_policy == ConflictPolicy::Fail)
        {
            *error = utils::err::join_context("fsx", "rename", "destination exists and overwrite is disabled");
            return false;
        }
        const Path destination_backup = make_temp_path(dst, "old");
        if (!move_existing_destination_to_backup(dst, destination_backup, false, state, "rename", error))
        {
            return false;
        }
    }

    if (!record_undo(state, {UndoAction::Kind::MovePath, dst, src}, error, true))
    {
        return false;
    }
    return move_path_without_overwrite(src, dst, "rename", error);
}

bool copy_file_tracked(const Path& src, const Path& dst, const RunOptions& options, ExecuteState* state,
                       std::string* error, bool* skipped)
{
    if (skipped != nullptr)
    {
        *skipped = false;
    }
    if (!path_exists(src))
    {
        *error = utils::err::join_context("fsx", "copy_file", "source file not found");
        return false;
    }
    if (!ensure_parent(dst, error))
    {
        return false;
    }

    const bool destination_exists = path_exists(dst);
    if (destination_exists && options.conflict_policy == ConflictPolicy::Skip)
    {
        if (skipped != nullptr)
        {
            *skipped = true;
        }
        return true;
    }

    if (destination_exists && options.conflict_policy == ConflictPolicy::Fail)
    {
        *error = utils::err::join_context("fsx", "copy_file", "destination exists");
        return false;
    }

    const Path temp = make_temp_path(dst, "copy");
    if (!copy_to_temporary_file(src, temp, error))
    {
        return false;
    }
    if (!record_temporary_cleanup(state, temp, error))
    {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return false;
    }

    if (destination_exists)
    {
        const Path destination_backup = make_temp_path(dst, "old");
        if (!move_existing_destination_to_backup(dst, destination_backup, false, state, "copy_file", error))
        {
            return false;
        }
    }

    if (!record_undo(state, {UndoAction::Kind::MovePath, dst, temp}, error, true))
    {
        return false;
    }
    return move_path_without_overwrite(temp, dst, "copy_file", error);
}

bool remove_path_tracked(const Path& target, const RunOptions& options, ExecuteState* state, std::string* error,
                         bool* skipped)
{
    if (skipped != nullptr)
    {
        *skipped = false;
    }
    if (!path_exists(target) || options.conflict_policy == ConflictPolicy::Skip)
    {
        if (skipped != nullptr)
        {
            *skipped = true;
        }
        return true;
    }

    const Path backup = make_temp_path(target, "removed");
    if (!record_undo(state, {UndoAction::Kind::MovePath, backup, target}, error, true) ||
        !move_path_without_overwrite(target, backup, "remove_path", error))
    {
        return false;
    }
    track_cleanup_after_commit(state, backup);
    return true;
}

bool copy_tree_tracked(const Path& src, const Path& dst, const RunOptions& options, ExecuteState* state,
                       std::string* error, bool* skipped)
{
    if (skipped != nullptr)
    {
        *skipped = false;
    }
    if (!path_exists(src) || !std::filesystem::is_directory(src))
    {
        *error = utils::err::join_context("fsx", "copy_tree", "source directory not found");
        return false;
    }

    WalkOptions walk_options;
    walk_options.recursive = true;
    walk_options.include_directories = false;
    walk_options.include_files = true;
    walk_options.relative_path = true;
    const auto walked = WalkDirectory(src.string(), walk_options);
    if (!walked.ok)
    {
        *error = walked.error;
        return false;
    }

    bool any_skipped = false;
    for (const auto& entry : walked.entries)
    {
        if (!toolx_detail::SafeChildPath(src, Path(entry.path), error) ||
            !toolx_detail::SafeChildPath(dst, Path(entry.path), error))
            return false;
        const Path child_src = src / Path(entry.path);
        const Path child_dst = dst / Path(entry.path);
        bool child_skipped = false;
        if (!copy_file_tracked(child_src, child_dst, options, state, error, &child_skipped))
        {
            return false;
        }
        any_skipped = any_skipped || child_skipped;
    }
    if (skipped != nullptr)
    {
        *skipped = any_skipped && walked.entries.empty();
    }
    return true;
}

bool rollback(ExecuteState* state, RunResult* result, RollbackMode mode)
{
    (void)mode;
    bool all_ok = true;
    for (auto it = state->undo_stack.rbegin(); it != state->undo_stack.rend(); ++it)
    {
        std::error_code ec;
        if (it->kind == UndoAction::Kind::RemovePath)
        {
            if (std::filesystem::exists(it->from, ec))
            {
                std::filesystem::remove(it->from, ec);
                if (!ec)
                {
                    ++result->rolled_back_steps;
                }
                else
                {
                    all_ok = false;
                }
            }
            continue;
        }

        if (it->kind == UndoAction::Kind::MovePath)
        {
            if (std::filesystem::exists(it->from, ec))
            {
                if (std::filesystem::exists(it->to, ec))
                {
                    all_ok = false;
                    continue;
                }
                std::filesystem::rename(it->from, it->to, ec);
                if (!ec)
                {
                    ++result->rolled_back_steps;
                }
                else
                {
                    all_ok = false;
                }
            }
        }
    }

    return all_ok;
}

void cleanup_after_success(ExecuteState* state)
{
    if (state == nullptr)
    {
        return;
    }
    for (const auto& path : state->cleanup_after_commit)
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
}

ConflictPolicy effective_policy(const RunOptions& options)
{
    return options.conflict_policy;
}

struct PollingSnapshot
{
    bool exists{false};
    bool is_directory{false};
    std::uint64_t mtime{0};
    std::uintmax_t size{0};
    std::uint64_t content_hash{0};
};

std::uint64_t HashFileContent(const Path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return 0;
    }

    std::uint64_t hash = 1469598103934665603ull;
    char buffer[4096] = {0};
    while (input.good())
    {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i)
        {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

PollingSnapshot CapturePollingSnapshot(const Path& path)
{
    PollingSnapshot out;
    std::error_code ec;
    out.exists = std::filesystem::exists(path, ec) && !ec;
    if (!out.exists)
    {
        return out;
    }

    out.is_directory = std::filesystem::is_directory(path, ec);
    if (ec)
    {
        out.is_directory = false;
        ec.clear();
    }

    out.size = std::filesystem::is_regular_file(path, ec) ? std::filesystem::file_size(path, ec) : 0;
    if (ec)
    {
        out.size = 0;
    }
    else if (out.size > 0)
    {
        out.content_hash = HashFileContent(path);
    }

    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (!ec)
    {
        out.mtime = static_cast<std::uint64_t>(mtime.time_since_epoch().count());
    }
    return out;
}

using PollingTree = std::unordered_map<std::string, PollingSnapshot>;

PollingTree CapturePollingTree(const Path& root)
{
    PollingTree out;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
    {
        return out;
    }
    if (!std::filesystem::is_directory(root, ec) || ec)
    {
        return out;
    }

    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it)
    {
        const Path entry_path = it->path();
        std::string rel = std::filesystem::relative(entry_path, root, ec).generic_string();
        if (ec)
        {
            rel = entry_path.generic_string();
            ec.clear();
        }
        out[rel] = CapturePollingSnapshot(entry_path);
    }
    return out;
}

bool SnapshotChanged(const PollingSnapshot& before, const PollingSnapshot& after)
{
    return before.exists != after.exists || before.is_directory != after.is_directory || before.mtime != after.mtime ||
           before.size != after.size || before.content_hash != after.content_hash;
}

class PollingFileWatcher final : public IFileWatcher
{
  public:
    explicit PollingFileWatcher(std::string path) : path_(std::move(path)) {}

    WatchPollResult Poll(int timeout_ms) override
    {
        WatchPollResult result;
        if (timeout_ms < 0)
        {
            result.ok = false;
            result.error = "timeout_ms must be >= 0";
            return result;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true)
        {
            if (!initialized_)
            {
                Initialize();
                initialized_ = true;
            }
            else
            {
                RefreshEvents();
                if (!pending_.empty())
                {
                    result.has_event = true;
                    result.event = pending_.front();
                    pending_.pop_front();
                    return result;
                }
            }

            if (timeout_ms == 0 || std::chrono::steady_clock::now() >= deadline)
            {
                result.has_event = false;
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

  private:
    void Initialize()
    {
        const PollingSnapshot now = CapturePollingSnapshot(path_);
        watching_directory_ = now.exists && now.is_directory;
        if (watching_directory_)
        {
            tree_last_ = CapturePollingTree(path_);
        }
        else
        {
            last_ = now;
        }
    }

    void RefreshEvents()
    {
        if (watching_directory_)
        {
            RefreshDirectoryEvents();
        }
        else
        {
            RefreshSinglePathEvents();
        }
    }

    void RefreshSinglePathEvents()
    {
        const PollingSnapshot now = CapturePollingSnapshot(path_);
        WatchEventKind kind = WatchEventKind::None;
        if (!last_.exists && now.exists)
        {
            kind = WatchEventKind::Created;
        }
        else if (last_.exists && !now.exists)
        {
            kind = WatchEventKind::Removed;
        }
        else if (last_.exists && now.exists && SnapshotChanged(last_, now))
        {
            kind = WatchEventKind::Modified;
        }

        last_ = now;
        if (kind != WatchEventKind::None)
        {
            pending_.push_back(WatchEvent{kind, path_.generic_string()});
        }
    }

    void RefreshDirectoryEvents()
    {
        const PollingTree now = CapturePollingTree(path_);
        std::map<std::string, WatchEventKind> ordered;

        for (const auto& kv : now)
        {
            const auto it = tree_last_.find(kv.first);
            if (it == tree_last_.end())
            {
                ordered[kv.first] = WatchEventKind::Created;
                continue;
            }
            if (SnapshotChanged(it->second, kv.second))
            {
                ordered[kv.first] = WatchEventKind::Modified;
            }
        }

        for (const auto& kv : tree_last_)
        {
            if (now.find(kv.first) == now.end())
            {
                ordered[kv.first] = WatchEventKind::Removed;
            }
        }

        tree_last_ = now;
        for (const auto& kv : ordered)
        {
            pending_.push_back(WatchEvent{kv.second, kv.first});
        }
    }

    Path path_;
    bool initialized_{false};
    bool watching_directory_{false};
    PollingSnapshot last_{};
    PollingTree tree_last_{};
    std::deque<WatchEvent> pending_;
};

OpType to_public_op(BatchPlan::ActionKind kind)
{
    switch (kind)
    {
    case BatchPlan::ActionKind::AtomicWrite:
        return OpType::AtomicWrite;
    case BatchPlan::ActionKind::SafeReplace:
        return OpType::SafeReplace;
    case BatchPlan::ActionKind::Rename:
        return OpType::Rename;
    case BatchPlan::ActionKind::CopyFile:
        return OpType::CopyFile;
    case BatchPlan::ActionKind::RemovePath:
        return OpType::RemovePath;
    case BatchPlan::ActionKind::CopyTree:
        return OpType::CopyTree;
    }
    return OpType::AtomicWrite;
}

} // namespace

BatchPlan& BatchPlan::AddAtomicWrite(std::string path, std::string data)
{
    Action op;
    op.kind = ActionKind::AtomicWrite;
    op.dst = std::move(path);
    op.payload = std::move(data);
    actions_.push_back(std::move(op));
    return *this;
}

BatchPlan& BatchPlan::AddSafeReplace(std::string src, std::string dst, bool backup)
{
    Action op;
    op.kind = ActionKind::SafeReplace;
    op.src = std::move(src);
    op.dst = std::move(dst);
    op.backup = backup;
    actions_.push_back(std::move(op));
    return *this;
}

BatchPlan& BatchPlan::AddRename(std::string src, std::string dst)
{
    Action op;
    op.kind = ActionKind::Rename;
    op.src = std::move(src);
    op.dst = std::move(dst);
    actions_.push_back(std::move(op));
    return *this;
}

BatchPlan& BatchPlan::AddCopyFile(std::string src, std::string dst)
{
    Action op;
    op.kind = ActionKind::CopyFile;
    op.src = std::move(src);
    op.dst = std::move(dst);
    actions_.push_back(std::move(op));
    return *this;
}

BatchPlan& BatchPlan::AddRemovePath(std::string path)
{
    Action op;
    op.kind = ActionKind::RemovePath;
    op.dst = std::move(path);
    actions_.push_back(std::move(op));
    return *this;
}

BatchPlan& BatchPlan::AddCopyTree(std::string src, std::string dst)
{
    Action op;
    op.kind = ActionKind::CopyTree;
    op.src = std::move(src);
    op.dst = std::move(dst);
    actions_.push_back(std::move(op));
    return *this;
}

const std::vector<BatchPlan::Action>& BatchPlan::Actions() const
{
    return actions_;
}

bool BatchPlan::ok() const noexcept
{
    return ok_;
}

const std::string& BatchPlan::error() const noexcept
{
    return error_;
}

void BatchPlan::MarkInvalid(std::string error)
{
    ok_ = false;
    error_ = std::move(error);
    actions_.clear();
}

RunResult Run(const BatchPlan& plan, const RunOptions& options)
{
    RunResult result;
    if (!plan.ok())
    {
        result.error =
            plan.error().empty() ? utils::err::join_context("fsx", "run", "invalid batch plan") : plan.error();
        return result;
    }

    ExecuteState state;
    state.options = &options;
    std::string journal_error;
    if (!journal_write_header(options, &journal_error))
    {
        result.error = journal_error;
        return result;
    }

    auto rollback_and_mark = [&]()
    {
        const bool rollback_ok = rollback(&state, &result, options.rollback_mode);
        for (auto& step : result.steps)
        {
            if (step.ok)
            {
                step.rolled_back = true;
            }
        }
        if (!rollback_ok)
        {
            result.error = utils::err::join_context("fsx", "rollback", "rollback failed");
        }
        return rollback_ok;
    };

    auto cleanup_rolled_back_journal = [&]()
    {
        if (options.journal_path.empty())
        {
            return;
        }

        std::error_code cleanup_ec;
        std::filesystem::remove(Path(options.journal_path), cleanup_ec);
        if (cleanup_ec)
        {
            result.error +=
                "; " + utils::err::join_context("fsx", "journal",
                                                "failed to remove rolled-back journal: " + cleanup_ec.message());
        }
    };

    const auto& actions = plan.Actions();
    result.steps.reserve(actions.size());
    bool had_step_failure = false;

    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        const auto& action = actions[i];

        StepReport report;
        report.step = i;
        report.op = to_public_op(action.kind);
        report.src = action.src;
        report.dst = action.dst;

        ExecOutcome outcome;
        const ConflictPolicy policy = effective_policy(options);

        if (action.kind == BatchPlan::ActionKind::AtomicWrite)
        {
            bool skipped = false;
            outcome.ok = write_file_atomic(Path(action.dst), action.payload, policy, &state, &outcome.error, &skipped);
            outcome.skipped = skipped;
        }
        else if (action.kind == BatchPlan::ActionKind::SafeReplace)
        {
            bool skipped = false;
            outcome.ok = safe_replace_file(Path(action.src), Path(action.dst), action.backup, options, &state,
                                           &outcome.error, &skipped);
            outcome.skipped = skipped;
        }
        else if (action.kind == BatchPlan::ActionKind::Rename)
        {
            bool skipped = false;
            outcome.ok = rename_file(Path(action.src), Path(action.dst), options, &state, &outcome.error, &skipped);
            outcome.skipped = skipped;
        }
        else if (action.kind == BatchPlan::ActionKind::CopyFile)
        {
            bool skipped = false;
            outcome.ok =
                copy_file_tracked(Path(action.src), Path(action.dst), options, &state, &outcome.error, &skipped);
            outcome.skipped = skipped;
        }
        else if (action.kind == BatchPlan::ActionKind::RemovePath)
        {
            bool skipped = false;
            outcome.ok = remove_path_tracked(Path(action.dst), options, &state, &outcome.error, &skipped);
            outcome.skipped = skipped;
        }
        else if (action.kind == BatchPlan::ActionKind::CopyTree)
        {
            bool skipped = false;
            outcome.ok =
                copy_tree_tracked(Path(action.src), Path(action.dst), options, &state, &outcome.error, &skipped);
            outcome.skipped = skipped;
        }

        report.ok = outcome.ok;
        report.error = outcome.error;
        report.skipped = outcome.skipped;
        result.steps.push_back(report);

        if (!outcome.ok)
        {
            result.error = outcome.error;
            had_step_failure = true;
            if (options.fail_fast)
            {
                if (rollback_and_mark())
                {
                    cleanup_rolled_back_journal();
                }
                result.ok = false;
                return result;
            }
            continue;
        }

        if (outcome.skipped)
        {
            ++result.skipped_steps;
            continue;
        }

        ++result.completed_steps;
    }

    if (had_step_failure)
    {
        result.ok = false;
        return result;
    }

    journal_error.clear();
    if (!journal_write_commit(options, &journal_error))
    {
        result.error = journal_error;
        return result;
    }

    result.ok = true;
    cleanup_after_success(&state);
    if (!options.journal_path.empty() && !options.keep_journal_on_success)
    {
        std::error_code ec;
        std::filesystem::remove(Path(options.journal_path), ec);
    }
    return result;
}

RunResult RecoverFromJournal(std::string_view journal_path, const RecoverOptions& options)
{
    RunResult result;
    result.ok = false;

    if (journal_path.empty())
    {
        result.error = utils::err::join_context("fsx", "recover", "empty journal path");
        return result;
    }

    std::ifstream in(std::string(journal_path), std::ios::binary);
    if (!in.is_open())
    {
        result.error = utils::err::join_context("fsx", "recover", "failed to open journal");
        return result;
    }

    std::string header;
    if (!std::getline(in, header) ||
        (header != kJournalHeaderV1 && header != kJournalHeaderV2 && header != kJournalHeaderV3))
    {
        result.error = utils::err::join_context("fsx", "recover", "invalid journal header");
        return result;
    }

    const bool version_two_or_three = header == kJournalHeaderV2 || header == kJournalHeaderV3;
    const bool version_three = header == kJournalHeaderV3;
    bool committed = false;
    std::vector<UndoAction> undo_list;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }

        if (version_two_or_three && line == kJournalCommit)
        {
            committed = true;
            continue;
        }

        const auto parts = split_pipe(line);
        if (parts.size() < 3 || parts[0] != "UNDO")
        {
            continue;
        }

        if (parts[1] == "REMOVE")
        {
            UndoAction u;
            u.kind = UndoAction::Kind::RemovePath;
            u.from = Path(unescape_field(parts[2]));
            undo_list.push_back(std::move(u));
        }
        else if (parts[1] == "MOVE" && parts.size() >= 4)
        {
            UndoAction u;
            u.kind = UndoAction::Kind::MovePath;
            u.from = Path(unescape_field(parts[2]));
            u.to = Path(unescape_field(parts[3]));
            undo_list.push_back(std::move(u));
        }
    }

    if (committed)
    {
        result.error = utils::err::join_context("fsx", "recover", "journal records a completed transaction");
        return result;
    }

    for (std::size_t i = undo_list.size(); i > 0; --i)
    {
        const auto& undo = undo_list[i - 1];
        StepReport step;
        step.step = undo_list.size() - i;
        step.recovery_source = std::string(journal_path);
        step.ok = true;

        std::error_code ec;
        if (undo.kind == UndoAction::Kind::RemovePath)
        {
            step.op = OpType::AtomicWrite;
            step.src = undo.from.string();
            const bool exists = std::filesystem::exists(undo.from, ec);
            if (ec)
            {
                step.ok = false;
                step.error = utils::err::join_context("fsx", "recover", ec.message());
            }
            else if (version_three && undo.from.generic_string().find(".tmp.") == std::string::npos)
            {
                step.ok = false;
                step.error = utils::err::join_context("fsx", "recover",
                                                      "FSXJ3 refuses to remove a non-staging path while recovering");
            }
            else if (exists)
            {
                std::filesystem::remove(undo.from, ec);
                if (ec)
                {
                    step.ok = false;
                    step.error = utils::err::join_context("fsx", "recover", ec.message());
                }
                else
                {
                    step.rolled_back = true;
                    ++result.rolled_back_steps;
                }
            }
            else
            {
                step.skipped = true;
                ++result.skipped_steps;
            }
        }
        else
        {
            step.op = OpType::Rename;
            step.src = undo.from.string();
            step.dst = undo.to.string();

            const bool source_exists = std::filesystem::exists(undo.from, ec);
            if (ec)
            {
                step.ok = false;
                step.error = utils::err::join_context("fsx", "recover", ec.message());
            }
            else if (!source_exists)
            {
                step.skipped = true;
                ++result.skipped_steps;
            }
            else
            {
                const bool destination_exists = std::filesystem::exists(undo.to, ec);
                if (ec)
                {
                    step.ok = false;
                    step.error = utils::err::join_context("fsx", "recover", ec.message());
                }
                else if (version_three && destination_exists)
                {
                    step.ok = false;
                    step.error = utils::err::join_context(
                        "fsx", "recover", "FSXJ3 recovery conflict: destination appeared after the journal entry");
                }
                else
                {
                    std::filesystem::rename(undo.from, undo.to, ec);
                    if (ec)
                    {
                        step.ok = false;
                        step.error = utils::err::join_context("fsx", "recover", ec.message());
                    }
                    else
                    {
                        step.rolled_back = true;
                        ++result.rolled_back_steps;
                    }
                }
            }
        }

        if (!step.ok)
        {
            result.steps.push_back(step);
            result.error = step.error;
            if (version_three || options.rollback_mode == RollbackMode::Strict)
            {
                result.ok = false;
                return result;
            }
        }
        else
        {
            if (!step.skipped)
            {
                ++result.completed_steps;
            }
            result.steps.push_back(step);
        }
    }

    result.ok = true;
    in.close();
    if (options.cleanup_journal_on_success)
    {
        std::error_code ec;
        std::filesystem::remove(Path(std::string(journal_path)), ec);
    }
    return result;
}

WalkResult WalkDirectory(std::string_view root, const WalkOptions& options)
{
    WalkResult out;
    const Path root_path{std::string(root)};

    std::error_code ec;
    if (!std::filesystem::exists(root_path, ec) || ec)
    {
        out.error = utils::err::join_context("fsx", "walk", "root does not exist");
        return out;
    }
    if (!std::filesystem::is_directory(root_path, ec) || ec)
    {
        out.error = utils::err::join_context("fsx", "walk", "root is not a directory");
        return out;
    }

    auto push_entry = [&](const Path& p) -> bool
    {
        const auto relative = p.lexically_relative(root_path);
        if (!toolx_detail::SafeChildPath(root_path, relative, &out.error))
            return false;
        const auto status = std::filesystem::symlink_status(p, ec);
        if (ec)
        {
            out.error = utils::err::join_context("fsx", "walk", ec.message());
            return false;
        }
        const bool is_dir = std::filesystem::is_directory(status);
        if (!is_dir && !std::filesystem::is_regular_file(status))
        {
            out.error = utils::err::join_context("fsx", "walk", "unsupported file type");
            return false;
        }
        if ((is_dir && !options.include_directories) || (!is_dir && !options.include_files))
            return true;
        WalkEntry entry;
        entry.is_directory = is_dir;
        entry.path = options.relative_path ? relative.generic_string() : p.generic_string();
        if (!is_dir)
        {
            entry.size = std::filesystem::file_size(p, ec);
            if (ec)
            {
                out.error = utils::err::join_context("fsx", "walk", ec.message());
                return false;
            }
        }
        out.entries.push_back(std::move(entry));
        return true;
    };

    if (options.recursive)
    {
        for (auto it = std::filesystem::recursive_directory_iterator(root_path, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            if (!push_entry(it->path()))
                return out;
    }
    else
    {
        for (auto it = std::filesystem::directory_iterator(root_path, ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            if (!push_entry(it->path()))
                return out;
    }
    if (ec)
    {
        out.error = utils::err::join_context("fsx", "walk", ec.message());
        return out;
    }

    std::sort(out.entries.begin(), out.entries.end(),
              [](const WalkEntry& lhs, const WalkEntry& rhs) { return lhs.path < rhs.path; });
    out.ok = true;
    return out;
}

DirectoryDiff BuildDirectoryDiff(std::string_view source_root, std::string_view destination_root, bool include_removed)
{
    DirectoryDiff diff;
    WalkOptions options;
    options.recursive = true;
    options.include_directories = false;
    options.include_files = true;
    options.relative_path = true;

    const auto source_walk = WalkDirectory(source_root, options);
    if (!source_walk.ok)
    {
        diff.error = source_walk.error;
        return diff;
    }

    std::map<std::string, WalkEntry> source_entries;
    std::map<std::string, WalkEntry> destination_entries;
    for (const auto& entry : source_walk.entries)
    {
        source_entries[entry.path] = entry;
    }

    const Path dst_root{std::string(destination_root)};
    std::error_code ec;
    if (std::filesystem::exists(dst_root, ec) && !ec)
    {
        const auto destination_walk = WalkDirectory(destination_root, options);
        if (!destination_walk.ok)
        {
            diff.error = destination_walk.error;
            return diff;
        }
        for (const auto& entry : destination_walk.entries)
        {
            destination_entries[entry.path] = entry;
        }
    }

    if (ec)
    {
        diff.error = utils::err::join_context("fsx", "diff", ec.message());
        return diff;
    }
    const Path src_root{std::string(source_root)};
    for (const auto& [relative, source_entry] : source_entries)
    {
        if (!toolx_detail::SafeChildPath(src_root, Path(relative), &diff.error) ||
            !toolx_detail::SafeChildPath(dst_root, Path(relative), &diff.error))
            return diff;
        const auto dst_it = destination_entries.find(relative);
        DirectoryDiffKind kind = DirectoryDiffKind::Added;
        if (dst_it != destination_entries.end())
        {
            const auto source_path = src_root / Path(relative);
            const auto destination_path = dst_root / Path(relative);
            if (dst_it->second.size == source_entry.size && files_have_same_content(source_path, destination_path))
            {
                continue;
            }
            kind = DirectoryDiffKind::Modified;
        }
        diff.entries.push_back(
            {kind, relative, (src_root / Path(relative)).string(), (dst_root / Path(relative)).string()});
    }

    if (include_removed)
    {
        for (const auto& [relative, destination_entry] : destination_entries)
        {
            if (source_entries.find(relative) == source_entries.end())
            {
                if (!toolx_detail::SafeChildPath(dst_root, Path(relative), &diff.error))
                    return diff;
                (void)destination_entry;
                diff.entries.push_back(
                    {DirectoryDiffKind::Removed, relative, "", (dst_root / Path(relative)).string()});
            }
        }
    }

    std::sort(diff.entries.begin(), diff.entries.end(),
              [](const DirectoryDiffEntry& lhs, const DirectoryDiffEntry& rhs)
              {
                  if (lhs.relative_path != rhs.relative_path)
                  {
                      return lhs.relative_path < rhs.relative_path;
                  }
                  return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
              });
    diff.ok = true;
    return diff;
}

BatchPlan BuildSyncPlan(std::string_view source_root, std::string_view destination_root, bool remove_extra)
{
    BatchPlan plan;
    if (std::string(source_root).empty() || std::string(destination_root).empty())
    {
        plan.MarkInvalid(utils::err::join_context("fsx", "sync", "source and destination roots must not be empty"));
        return plan;
    }

    const std::string source_key = boundary_key(Path(std::string(source_root)));
    const std::string destination_key = boundary_key(Path(std::string(destination_root)));
    if (source_key == destination_key)
    {
        plan.MarkInvalid(utils::err::join_context("fsx", "sync", "source and destination roots must be different"));
        return plan;
    }
    if (path_is_within(destination_key, source_key) || path_is_within(source_key, destination_key))
    {
        plan.MarkInvalid(utils::err::join_context("fsx", "sync", "source and destination roots must not overlap"));
        return plan;
    }

    const auto diff = BuildDirectoryDiff(source_root, destination_root, remove_extra);
    if (!diff.ok)
    {
        plan.MarkInvalid(diff.error.empty() ? utils::err::join_context("fsx", "sync", "failed to build directory diff")
                                            : diff.error);
        return plan;
    }

    for (const auto& entry : diff.entries)
    {
        if (entry.kind == DirectoryDiffKind::Removed)
        {
            plan.AddRemovePath(entry.destination_path);
        }
        else
        {
            plan.AddCopyFile(entry.source_path, entry.destination_path);
        }
    }
    return plan;
}

namespace
{
void write_tar_octal(char* field, std::size_t width, std::uintmax_t value)
{
    std::snprintf(field, width, "%0*llo", static_cast<int>(width - 1), static_cast<unsigned long long>(value));
}

bool write_tar_header(std::ostream& out, std::string name, bool directory, std::uintmax_t size, std::string* error)
{
    if (!toolx_detail::SafeRelativePath(Path(name)))
    {
        *error = utils::err::join_context("fsx", "tar", "unsafe archive entry name");
        return false;
    }
    if (name.size() > 100)
    {
        *error = utils::err::join_context("fsx", "tar", "entry path exceeds ustar MVP limit");
        return false;
    }
    if (directory && name.back() != '/')
    {
        name.push_back('/');
    }

    std::array<char, 512> header{};
    std::memcpy(header.data(), name.data(), name.size());
    write_tar_octal(header.data() + 100, 8, 0644);
    write_tar_octal(header.data() + 108, 8, 0);
    write_tar_octal(header.data() + 116, 8, 0);
    write_tar_octal(header.data() + 124, 12, directory ? 0 : size);
    write_tar_octal(header.data() + 136, 12, 0);
    std::memset(header.data() + 148, ' ', 8);
    header[156] = directory ? '5' : '0';
    std::memcpy(header.data() + 257, "ustar", 5);
    std::memcpy(header.data() + 263, "00", 2);

    unsigned int checksum = 0;
    for (unsigned char ch : header)
    {
        checksum += ch;
    }
    std::snprintf(header.data() + 148, 8, "%06o", checksum);
    header[154] = '\0';
    header[155] = ' ';

    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    return out.good();
}

std::uintmax_t parse_tar_octal(const char* field, std::size_t width)
{
    std::uintmax_t value = 0;
    for (std::size_t i = 0; i < width && field[i] != '\0' && field[i] != ' '; ++i)
    {
        if (field[i] >= '0' && field[i] <= '7')
        {
            value = (value * 8u) + static_cast<std::uintmax_t>(field[i] - '0');
        }
    }
    return value;
}

bool tar_name_is_safe(std::string_view name)
{
    if (name.empty() || name.find('\\') != std::string_view::npos || name.find(':') != std::string_view::npos)
    {
        return false;
    }

    const Path path{std::string(name)};
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    for (const auto& part : path)
    {
        if (part == "." || part == "..")
        {
            return false;
        }
    }
    return true;
}

std::size_t bounded_cstr_len(const char* text, std::size_t max_len)
{
    std::size_t len = 0;
    while (len < max_len && text[len] != '\0')
    {
        ++len;
    }
    return len;
}
} // namespace

Status CreateArchive(std::string_view source_root, std::string_view archive_path, const ArchiveOptions& options)
{
    Status status;
    const Path root{std::string(source_root)};
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
    {
        status.error = utils::err::join_context("fsx", "tar", "source root does not exist");
        return status;
    }

    std::string ensure_error;
    if (!ensure_parent(Path(std::string(archive_path)), &ensure_error))
    {
        status.error = ensure_error;
        return status;
    }

    if (path_exists(Path(std::string(archive_path))) && options.conflict_policy == ConflictPolicy::Fail)
    {
        status.error = utils::err::join_context("fsx", "tar", "archive already exists");
        return status;
    }

    std::ofstream out(std::string(archive_path), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        status.error = utils::err::join_context("fsx", "tar", "failed to open archive for writing");
        return status;
    }

    WalkOptions walk_options;
    walk_options.recursive = true;
    walk_options.include_directories = true;
    walk_options.include_files = true;
    walk_options.relative_path = true;

    if (options.include_root_directory)
    {
        std::string root_name = root.filename().generic_string();
        if (!write_tar_header(out, root_name, true, 0, &status.error))
        {
            return status;
        }
    }

    const auto walked =
        std::filesystem::is_directory(root, ec) ? WalkDirectory(root.string(), walk_options) : WalkResult{};
    if (std::filesystem::is_regular_file(root, ec))
    {
        const std::string name = root.filename().generic_string();
        const std::uintmax_t size = std::filesystem::file_size(root, ec);
        if (!write_tar_header(out, name, false, size, &status.error))
        {
            return status;
        }
        std::ifstream in(root, std::ios::binary);
        out << in.rdbuf();
        const std::uintmax_t padding = (512u - (size % 512u)) % 512u;
        if (padding > 0)
        {
            std::array<char, 512> zeros{};
            out.write(zeros.data(), static_cast<std::streamsize>(padding));
        }
    }
    else
    {
        if (!walked.ok)
        {
            status.error = walked.error;
            return status;
        }
        for (const auto& entry : walked.entries)
        {
            if (!toolx_detail::SafeChildPath(root, Path(entry.path), &status.error))
                return status;
            std::string name = entry.path;
            if (options.include_root_directory)
            {
                name = (root.filename().generic_string() + "/").append(name);
            }
            if (!write_tar_header(out, name, entry.is_directory, entry.size, &status.error))
            {
                return status;
            }
            if (!entry.is_directory)
            {
                std::ifstream in(root / Path(entry.path), std::ios::binary);
                out << in.rdbuf();
                const std::uintmax_t padding = (512u - (entry.size % 512u)) % 512u;
                if (padding > 0)
                {
                    std::array<char, 512> zeros{};
                    out.write(zeros.data(), static_cast<std::streamsize>(padding));
                }
            }
        }
    }

    std::array<char, 1024> end{};
    out.write(end.data(), static_cast<std::streamsize>(end.size()));
    status.ok = out.good();
    if (!status.ok)
    {
        status.error = utils::err::join_context("fsx", "tar", "failed to finish archive");
    }
    return status;
}

Status ExtractArchive(std::string_view archive_path, std::string_view destination_root, const ArchiveOptions& options)
{
    Status status;
    std::ifstream in(std::string(archive_path), std::ios::binary);
    if (!in.is_open())
    {
        status.error = utils::err::join_context("fsx", "tar", "failed to open archive for reading");
        return status;
    }

    const Path root{std::string(destination_root)};
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        status.error = utils::err::join_context("fsx", "tar", ec.message());
        return status;
    }
    const std::string root_key = boundary_key(root);
    if (root_key.empty())
    {
        status.error = utils::err::join_context("fsx", "tar", "failed to canonicalize destination root");
        return status;
    }

    for (;;)
    {
        std::array<char, 512> header{};
        in.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (in.gcount() == 0)
        {
            break;
        }
        if (in.gcount() != static_cast<std::streamsize>(header.size()))
        {
            status.error = utils::err::join_context("fsx", "tar", "truncated header");
            return status;
        }
        if (std::all_of(header.begin(), header.end(), [](char ch) { return ch == '\0'; }))
        {
            break;
        }

        std::string name(header.data(), bounded_cstr_len(header.data(), 100));
        if (!tar_name_is_safe(name))
        {
            status.error = utils::err::join_context("fsx", "tar", "unsafe archive entry");
            return status;
        }
        const bool directory = header[156] == '5';
        const std::uintmax_t size = parse_tar_octal(header.data() + 124, 12);
        const Path target = root / Path(name);
        if (!path_is_at_or_within(boundary_key(target), root_key))
        {
            status.error = utils::err::join_context("fsx", "tar", "archive entry escapes destination root");
            return status;
        }

        if (directory)
        {
            std::filesystem::create_directories(target, ec);
            if (ec)
            {
                status.error = utils::err::join_context("fsx", "tar", ec.message());
                return status;
            }
            continue;
        }

        if (path_exists(target) && options.conflict_policy == ConflictPolicy::Fail)
        {
            status.error = utils::err::join_context("fsx", "tar", "destination exists");
            return status;
        }
        if (!ensure_parent(target, &status.error))
        {
            return status;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            status.error = utils::err::join_context("fsx", "tar", "failed to create output file");
            return status;
        }

        std::array<char, 4096> buffer{};
        std::uintmax_t remaining = size;
        while (remaining > 0)
        {
            const auto chunk = static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining, buffer.size()));
            in.read(buffer.data(), chunk);
            if (in.gcount() != chunk)
            {
                status.error = utils::err::join_context("fsx", "tar", "truncated file payload");
                return status;
            }
            out.write(buffer.data(), chunk);
            remaining -= static_cast<std::uintmax_t>(chunk);
        }

        const std::uintmax_t padding = (512u - (size % 512u)) % 512u;
        if (padding > 0)
        {
            in.ignore(static_cast<std::streamsize>(padding));
        }
    }

    status.ok = true;
    return status;
}

Status CreateLink(std::string_view target, std::string_view link_path, LinkType type, bool overwrite)
{
    Status out;
    const Path target_path{std::string(target)};
    const Path link{std::string(link_path)};

    std::string ensure_error;
    if (!ensure_parent(link, &ensure_error))
    {
        out.error = ensure_error;
        return out;
    }

    std::error_code ec;
    if (std::filesystem::exists(link, ec))
    {
        if (!overwrite)
        {
            out.error = utils::err::join_context("fsx", "link", "link path already exists");
            return out;
        }
        std::filesystem::remove(link, ec);
        if (ec)
        {
            out.error = utils::err::join_context("fsx", "link", ec.message());
            return out;
        }
    }

    if (type == LinkType::Hard)
    {
        std::filesystem::create_hard_link(target_path, link, ec);
    }
    else
    {
        std::filesystem::create_symlink(target_path, link, ec);
    }

    if (ec)
    {
        out.error = utils::err::join_context("fsx", "link", ec.message());
        return out;
    }

    out.ok = true;
    return out;
}

std::unique_ptr<IFileWatcher> CreateFileWatcher(std::string path)
{
    return std::make_unique<PollingFileWatcher>(std::move(path));
}

CapabilityInfo QueryCapabilities()
{
    CapabilityInfo caps;
#if defined(_WIN32)
    caps.symbolic_link = false;
#else
    caps.symbolic_link = true;
#endif
    caps.tar_archive = true;
    return caps;
}

const char* ToString(OpType op) noexcept
{
    switch (op)
    {
    case OpType::AtomicWrite:
        return "AtomicWrite";
    case OpType::SafeReplace:
        return "SafeReplace";
    case OpType::Rename:
        return "Rename";
    case OpType::CopyFile:
        return "CopyFile";
    case OpType::RemovePath:
        return "RemovePath";
    case OpType::CopyTree:
        return "CopyTree";
    }
    return "Unknown";
}

const char* ToString(RollbackMode mode) noexcept
{
    switch (mode)
    {
    case RollbackMode::BestEffort:
        return "BestEffort";
    case RollbackMode::Strict:
        return "Strict";
    }
    return "Unknown";
}

const char* ToString(ConflictPolicy policy) noexcept
{
    switch (policy)
    {
    case ConflictPolicy::Fail:
        return "Fail";
    case ConflictPolicy::Overwrite:
        return "Overwrite";
    case ConflictPolicy::Skip:
        return "Skip";
    }
    return "Unknown";
}

const char* ToString(WatchEventKind kind) noexcept
{
    switch (kind)
    {
    case WatchEventKind::None:
        return "None";
    case WatchEventKind::Created:
        return "Created";
    case WatchEventKind::Modified:
        return "Modified";
    case WatchEventKind::Removed:
        return "Removed";
    }
    return "Unknown";
}

const char* ToString(LinkType type) noexcept
{
    switch (type)
    {
    case LinkType::Hard:
        return "Hard";
    case LinkType::Symbolic:
        return "Symbolic";
    }
    return "Unknown";
}

const char* ToString(DirectoryDiffKind kind) noexcept
{
    switch (kind)
    {
    case DirectoryDiffKind::Added:
        return "Added";
    case DirectoryDiffKind::Modified:
        return "Modified";
    case DirectoryDiffKind::Removed:
        return "Removed";
    }
    return "Unknown";
}

} // namespace fsx
