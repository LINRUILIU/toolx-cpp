#include "fsx.h"

#include "utils.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsx
{
    namespace
    {

        using Path = std::filesystem::path;

        struct UndoAction
        {
            enum class Kind
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
            std::vector<UndoAction> undo_stack;
        };

        struct ExecOutcome
        {
            bool ok{false};
            bool skipped{false};
            std::string error;
        };

        bool ensure_parent(const Path &p, std::string *error)
        {
            const auto st = utils::path::ensure_parent_dir(p.string());
            if (!st.ok)
            {
                *error = st.error;
                return false;
            }
            return true;
        }

        Path make_temp_path(const Path &base, std::string_view tag)
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            std::ostringstream os;
            os << base.string() << "." << tag << ".tmp." << now;
            return Path(os.str());
        }

        bool path_exists(const Path &p)
        {
            std::error_code ec;
            return std::filesystem::exists(p, ec) && !ec;
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

        void journal_write_header(const RunOptions &options)
        {
            if (options.journal_path.empty())
            {
                return;
            }
            std::ofstream j(options.journal_path, std::ios::binary | std::ios::trunc);
            if (!j.is_open())
            {
                return;
            }
            j << "FSXJ1\n";
        }

        void journal_write_undo(const RunOptions &options, const UndoAction &undo)
        {
            if (options.journal_path.empty())
            {
                return;
            }

            std::ofstream j(options.journal_path, std::ios::binary | std::ios::app);
            if (!j.is_open())
            {
                return;
            }

            if (undo.kind == UndoAction::Kind::RemovePath)
            {
                j << "UNDO|REMOVE|" << escape_field(undo.from.string()) << "\n";
                return;
            }

            j << "UNDO|MOVE|" << escape_field(undo.from.string()) << "|"
              << escape_field(undo.to.string()) << "\n";
        }

        bool move_file_force(const Path &from,
                             const Path &to,
                             ConflictPolicy conflict_policy,
                             bool *skipped,
                             std::string *error)
        {
            std::error_code ec;
            if (skipped != nullptr)
            {
                *skipped = false;
            }

            if (!path_exists(from))
            {
                *error = utils::err::join_context("fsx", "move_file", "source file not found");
                return false;
            }

            if (!ensure_parent(to, error))
            {
                return false;
            }

            if (path_exists(to))
            {
                if (conflict_policy == ConflictPolicy::Skip)
                {
                    if (skipped != nullptr)
                    {
                        *skipped = true;
                    }
                    return true;
                }

                if (conflict_policy == ConflictPolicy::Fail)
                {
                    *error = utils::err::join_context("fsx", "move_file", "destination already exists");
                    return false;
                }

                std::filesystem::remove(to, ec);
                if (ec)
                {
                    *error = utils::err::join_context("fsx", "move_file", ec.message());
                    return false;
                }
            }

            std::filesystem::rename(from, to, ec);
            if (ec)
            {
                *error = utils::err::join_context("fsx", "move_file", ec.message());
                return false;
            }

            return true;
        }

        bool write_file_atomic(const Path &target,
                               std::string_view data,
                               ConflictPolicy conflict_policy,
                               ExecuteState *state,
                               std::string *error,
                               bool *skipped)
        {
            if (skipped != nullptr)
            {
                *skipped = false;
            }

            if (!ensure_parent(target, error))
            {
                return false;
            }

            Path previous_backup;
            const bool had_existing = path_exists(target);
            if (had_existing)
            {
                if (conflict_policy == ConflictPolicy::Skip)
                {
                    if (skipped != nullptr)
                    {
                        *skipped = true;
                    }
                    return true;
                }

                if (conflict_policy == ConflictPolicy::Fail)
                {
                    *error = utils::err::join_context("fsx", "atomic_write", "target exists and overwrite is disabled");
                    return false;
                }

                previous_backup = make_temp_path(target, "old");
                bool ignored = false;
                if (!move_file_force(target, previous_backup, ConflictPolicy::Fail, &ignored, error))
                {
                    return false;
                }
            }

            const Path temp = make_temp_path(target, "new");
            {
                std::ofstream out(temp.string(), std::ios::binary);
                if (!out.is_open())
                {
                    if (had_existing)
                    {
                        std::string restore_error;
                        bool ignored = false;
                        (void)move_file_force(previous_backup, target, ConflictPolicy::Overwrite, &ignored, &restore_error);
                    }
                    *error = utils::err::join_context("fsx", "atomic_write", "failed to create temporary file");
                    return false;
                }

                out.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!out.good())
                {
                    out.close();
                    std::error_code ec;
                    std::filesystem::remove(temp, ec);
                    if (had_existing)
                    {
                        std::string restore_error;
                        bool ignored = false;
                        (void)move_file_force(previous_backup, target, ConflictPolicy::Overwrite, &ignored, &restore_error);
                    }
                    *error = utils::err::join_context("fsx", "atomic_write", "failed to write temporary file");
                    return false;
                }
            }

            bool skipped_apply = false;
            if (!move_file_force(temp, target, ConflictPolicy::Overwrite, &skipped_apply, error))
            {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
                if (had_existing)
                {
                    std::string restore_error;
                    bool ignored = false;
                    (void)move_file_force(previous_backup, target, ConflictPolicy::Overwrite, &ignored, &restore_error);
                }
                return false;
            }

            if (had_existing)
            {
                state->undo_stack.push_back({UndoAction::Kind::MovePath, target, previous_backup});
            }
            else
            {
                state->undo_stack.push_back({UndoAction::Kind::RemovePath, target, {}});
            }

            return true;
        }

        bool safe_replace_file(const Path &src,
                               const Path &dst,
                               bool backup,
                               const RunOptions &options,
                               ExecuteState *state,
                               std::string *error,
                               bool *skipped)
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
            Path destination_backup;

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

                if (backup || options.default_backup)
                {
                    destination_backup = Path(dst.string() + options.backup_suffix);
                    bool ignored = false;
                    if (!move_file_force(dst, destination_backup, ConflictPolicy::Overwrite, &ignored, error))
                    {
                        return false;
                    }
                }
                else
                {
                    destination_backup = make_temp_path(dst, "old");
                    bool ignored = false;
                    if (!move_file_force(dst, destination_backup, ConflictPolicy::Fail, &ignored, error))
                    {
                        return false;
                    }
                }
            }

            bool skipped_move = false;
            if (!move_file_force(src, dst, options.conflict_policy, &skipped_move, error))
            {
                if (destination_exists)
                {
                    std::string restore_error;
                    bool ignored = false;
                    (void)move_file_force(destination_backup, dst, ConflictPolicy::Overwrite, &ignored, &restore_error);
                }
                return false;
            }

            if (skipped_move)
            {
                if (skipped != nullptr)
                {
                    *skipped = true;
                }
                return true;
            }

            state->undo_stack.push_back({UndoAction::Kind::MovePath, dst, src});

            if (destination_exists)
            {
                state->undo_stack.push_back({UndoAction::Kind::MovePath, destination_backup, dst});
            }

            return true;
        }

        bool rename_file(const Path &src,
                         const Path &dst,
                         const RunOptions &options,
                         ExecuteState *state,
                         std::string *error,
                         bool *skipped)
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

            Path destination_backup;
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

                destination_backup = make_temp_path(dst, "old");
                bool ignored = false;
                if (!move_file_force(dst, destination_backup, ConflictPolicy::Fail, &ignored, error))
                {
                    return false;
                }
            }

            bool skipped_move = false;
            if (!move_file_force(src, dst, options.conflict_policy, &skipped_move, error))
            {
                if (destination_exists)
                {
                    std::string restore_error;
                    bool ignored = false;
                    (void)move_file_force(destination_backup, dst, ConflictPolicy::Overwrite, &ignored, &restore_error);
                }
                return false;
            }

            if (skipped_move)
            {
                if (skipped != nullptr)
                {
                    *skipped = true;
                }
                return true;
            }

            state->undo_stack.push_back({UndoAction::Kind::MovePath, dst, src});
            if (destination_exists)
            {
                state->undo_stack.push_back({UndoAction::Kind::MovePath, destination_backup, dst});
            }

            return true;
        }

        bool rollback(ExecuteState *state, RunResult *result, RollbackMode mode)
        {
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

            if (!all_ok && mode == RollbackMode::Strict)
            {
                return false;
            }

            return true;
        }

        ConflictPolicy effective_policy(const RunOptions &options)
        {
            return options.conflict_policy;
        }

        struct PollingSnapshot
        {
            bool exists{false};
            bool is_directory{false};
            std::uint64_t mtime{0};
            std::uintmax_t size{0};
        };

        PollingSnapshot CapturePollingSnapshot(const Path &path)
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

            const auto mtime = std::filesystem::last_write_time(path, ec);
            if (!ec)
            {
                out.mtime = static_cast<std::uint64_t>(mtime.time_since_epoch().count());
            }
            return out;
        }

        using PollingTree = std::unordered_map<std::string, PollingSnapshot>;

        PollingTree CapturePollingTree(const Path &root)
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
                 !ec && it != std::filesystem::recursive_directory_iterator();
                 ++it)
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

        bool SnapshotChanged(const PollingSnapshot &before, const PollingSnapshot &after)
        {
            return before.exists != after.exists ||
                   before.is_directory != after.is_directory ||
                   before.mtime != after.mtime ||
                   before.size != after.size;
        }

        class PollingFileWatcher final : public IFileWatcher
        {
        public:
            explicit PollingFileWatcher(std::string path)
                : path_(std::move(path))
            {
            }

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

                for (const auto &kv : now)
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

                for (const auto &kv : tree_last_)
                {
                    if (now.find(kv.first) == now.end())
                    {
                        ordered[kv.first] = WatchEventKind::Removed;
                    }
                }

                tree_last_ = now;
                for (const auto &kv : ordered)
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
            }
            return OpType::AtomicWrite;
        }

    } // namespace

    BatchPlan &BatchPlan::AddAtomicWrite(std::string path, std::string data)
    {
        Action op;
        op.kind = ActionKind::AtomicWrite;
        op.dst = std::move(path);
        op.payload = std::move(data);
        actions_.push_back(std::move(op));
        return *this;
    }

    BatchPlan &BatchPlan::AddSafeReplace(std::string src, std::string dst, bool backup)
    {
        Action op;
        op.kind = ActionKind::SafeReplace;
        op.src = std::move(src);
        op.dst = std::move(dst);
        op.backup = backup;
        actions_.push_back(std::move(op));
        return *this;
    }

    BatchPlan &BatchPlan::AddRename(std::string src, std::string dst)
    {
        Action op;
        op.kind = ActionKind::Rename;
        op.src = std::move(src);
        op.dst = std::move(dst);
        actions_.push_back(std::move(op));
        return *this;
    }

    const std::vector<BatchPlan::Action> &BatchPlan::Actions() const
    {
        return actions_;
    }

    RunResult Run(const BatchPlan &plan, const RunOptions &options)
    {
        RunResult result;
        ExecuteState state;
        journal_write_header(options);

        const auto &actions = plan.Actions();
        result.steps.reserve(actions.size());

        for (std::size_t i = 0; i < actions.size(); ++i)
        {
            const auto &action = actions[i];

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
                outcome.ok = safe_replace_file(Path(action.src), Path(action.dst), action.backup, options, &state, &outcome.error, &skipped);
                outcome.skipped = skipped;
            }
            else if (action.kind == BatchPlan::ActionKind::Rename)
            {
                bool skipped = false;
                outcome.ok = rename_file(Path(action.src), Path(action.dst), options, &state, &outcome.error, &skipped);
                outcome.skipped = skipped;
            }

            report.ok = outcome.ok;
            report.error = outcome.error;
            report.skipped = outcome.skipped;
            result.steps.push_back(report);

            if (!outcome.ok)
            {
                result.error = outcome.error;
                if (options.fail_fast)
                {
                    const bool rollback_ok = rollback(&state, &result, options.rollback_mode);
                    for (auto &step : result.steps)
                    {
                        if (step.ok)
                        {
                            step.rolled_back = true;
                        }
                    }
                    if (!rollback_ok)
                    {
                        result.error = utils::err::join_context("fsx", "rollback", "strict rollback failed");
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
            if (!state.undo_stack.empty())
            {
                journal_write_undo(options, state.undo_stack.back());
            }
        }

        result.ok = true;
        if (!options.journal_path.empty() && !options.keep_journal_on_success)
        {
            std::error_code ec;
            std::filesystem::remove(Path(options.journal_path), ec);
        }
        return result;
    }

    RunResult RecoverFromJournal(std::string_view journal_path,
                                 const RecoverOptions &options)
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
        if (!std::getline(in, header) || header != "FSXJ1")
        {
            result.error = utils::err::join_context("fsx", "recover", "invalid journal header");
            return result;
        }

        std::vector<UndoAction> undo_list;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty())
            {
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

        for (std::size_t i = undo_list.size(); i > 0; --i)
        {
            const auto &undo = undo_list[i - 1];
            StepReport step;
            step.step = undo_list.size() - i;
            step.recovery_source = std::string(journal_path);
            step.ok = true;

            std::error_code ec;
            if (undo.kind == UndoAction::Kind::RemovePath)
            {
                step.op = OpType::AtomicWrite;
                step.src = undo.from.string();
                if (std::filesystem::exists(undo.from, ec))
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

                if (!std::filesystem::exists(undo.from, ec))
                {
                    step.skipped = true;
                    ++result.skipped_steps;
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

            if (!step.ok)
            {
                result.steps.push_back(step);
                result.error = step.error;
                if (options.rollback_mode == RollbackMode::Strict)
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

    WalkResult WalkDirectory(std::string_view root, const WalkOptions &options)
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

        auto push_entry = [&](const Path &p, bool is_dir)
        {
            WalkEntry entry;
            entry.is_directory = is_dir;
            if (options.relative_path)
            {
                entry.path = std::filesystem::relative(p, root_path, ec).generic_string();
                if (ec)
                {
                    entry.path = p.generic_string();
                    ec.clear();
                }
            }
            else
            {
                entry.path = p.generic_string();
            }

            if (!is_dir)
            {
                entry.size = std::filesystem::file_size(p, ec);
                if (ec)
                {
                    entry.size = 0;
                    ec.clear();
                }
            }
            out.entries.push_back(std::move(entry));
        };

        if (options.recursive)
        {
            for (auto it = std::filesystem::recursive_directory_iterator(root_path, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator();
                 ++it)
            {
                const bool is_dir = it->is_directory(ec);
                if (ec)
                {
                    out.error = utils::err::join_context("fsx", "walk", ec.message());
                    return out;
                }
                if (is_dir && options.include_directories)
                {
                    push_entry(it->path(), true);
                }
                if (!is_dir && options.include_files)
                {
                    push_entry(it->path(), false);
                }
            }
        }
        else
        {
            for (auto it = std::filesystem::directory_iterator(root_path, ec);
                 !ec && it != std::filesystem::directory_iterator();
                 ++it)
            {
                const bool is_dir = it->is_directory(ec);
                if (ec)
                {
                    out.error = utils::err::join_context("fsx", "walk", ec.message());
                    return out;
                }
                if (is_dir && options.include_directories)
                {
                    push_entry(it->path(), true);
                }
                if (!is_dir && options.include_files)
                {
                    push_entry(it->path(), false);
                }
            }
        }

        std::sort(out.entries.begin(), out.entries.end(), [](const WalkEntry &lhs, const WalkEntry &rhs)
                  { return lhs.path < rhs.path; });
        out.ok = true;
        return out;
    }

    Status CreateLink(std::string_view target,
                      std::string_view link_path,
                      LinkType type,
                      bool overwrite)
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

    Status ArchivePlaceholder(std::string_view source_dir,
                              std::string_view output_path,
                              std::string_view format)
    {
        Status out;
        const Path src{std::string(source_dir)};
        const Path dst{std::string(output_path)};
        std::error_code ec;
        if (!std::filesystem::exists(src, ec) || ec)
        {
            out.error = utils::err::join_context("fsx", "archive", "source directory not found");
            return out;
        }

        std::string ensure_error;
        if (!ensure_parent(dst, &ensure_error))
        {
            out.error = ensure_error;
            return out;
        }

        out.error = "archive format '" + std::string(format) + "' is not implemented yet";
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
        return caps;
    }

    const char *ToString(OpType op) noexcept
    {
        switch (op)
        {
        case OpType::AtomicWrite:
            return "AtomicWrite";
        case OpType::SafeReplace:
            return "SafeReplace";
        case OpType::Rename:
            return "Rename";
        }
        return "Unknown";
    }

    const char *ToString(RollbackMode mode) noexcept
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

    const char *ToString(ConflictPolicy policy) noexcept
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

    const char *ToString(WatchEventKind kind) noexcept
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

    const char *ToString(LinkType type) noexcept
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

} // namespace fsx
