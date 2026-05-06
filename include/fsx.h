#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <memory>
#include <vector>

namespace fsx
{

    enum class RollbackMode
    {
        BestEffort,
        Strict
    }; // 回滚模式，BestEffort表示尽最大努力回滚，但不保证完全回滚成功；Strict表示严格回滚，如果回滚过程中发生错误，则整个回滚视为失败。

    enum class ConflictPolicy
    {
        Fail,
        Overwrite,
        Skip
    }; // 冲突处理策略，Fail表示遇到冲突时操作失败；Overwrite表示覆盖现有文件；Skip表示跳过该操作。

    enum class OpType
    {
        AtomicWrite,
        SafeReplace,
        Rename
    }; // 操作类型，AtomicWrite表示原子写入；SafeReplace表示安全替换；Rename表示重命名。

    struct StepReport
    {
        std::size_t step{0};
        OpType op{OpType::AtomicWrite};
        std::string src;
        std::string dst;
        std::string error;
        std::string recovery_source;
        bool rolled_back{false};
        bool skipped{false};
        bool ok{false};
    }; // 步骤报告，包含步骤编号、操作类型、源路径、目标路径、错误信息、回滚来源、是否已回滚、是否被跳过以及操作是否成功等信息。

    struct RunOptions
    {
        bool fail_fast{true};
        bool overwrite_existing{true}; // legacy option, overridden by conflict_policy when set.
        bool default_backup{false};
        std::string backup_suffix{".bak"};
        RollbackMode rollback_mode{RollbackMode::BestEffort};
        ConflictPolicy conflict_policy{ConflictPolicy::Overwrite};
        std::string journal_path;
        bool keep_journal_on_success{false};
    }; // 运行选项，包含是否快速失败、是否覆盖现有文件、默认备份选项、备份文件后缀、回滚模式、冲突处理策略、日志路径以及成功后是否保留日志等选项。

    struct RunResult
    {
        bool ok{false};
        std::string error;
        std::vector<StepReport> steps;
        std::size_t completed_steps{0};
        std::size_t skipped_steps{0};
        std::size_t rolled_back_steps{0};
    }; // 运行结果，包含操作是否成功、错误信息、步骤报告列表、已完成步骤数、已跳过步骤数以及已回滚步骤数等信息。

    struct RecoverOptions
    {
        RollbackMode rollback_mode{RollbackMode::BestEffort};
        bool cleanup_journal_on_success{true};
    }; // 恢复选项，包含回滚模式以及成功后是否清理日志等选项。

    struct Status
    {
        bool ok{false};
        std::string error;
    };

    struct WalkEntry
    {
        std::string path;
        bool is_directory{false};
        std::uintmax_t size{0};
    };

    struct WalkOptions
    {
        bool recursive{true};
        bool include_directories{true};
        bool include_files{true};
        bool relative_path{true};
    };

    struct WalkResult
    {
        bool ok{false};
        std::string error;
        std::vector<WalkEntry> entries;
    };

    enum class LinkType
    {
        Hard,
        Symbolic
    };

    enum class WatchEventKind
    {
        None,
        Created,
        Modified,
        Removed
    };

    struct WatchEvent
    {
        WatchEventKind kind{WatchEventKind::None};
        std::string path;
    };

    struct WatchPollResult
    {
        bool ok{true};
        bool has_event{false};
        WatchEvent event;
        std::string error;
    };

    struct CapabilityInfo
    {
        bool recursive_walk{true};
        bool watcher_polling{true};
        bool hard_link{true};
        bool symbolic_link{false};
        bool zip_archive{false}; // Future capability; stable API reports false until implemented.
        bool tar_archive{false}; // Future capability; stable API reports false until implemented.
    };

    class IFileWatcher
    {
    public:
        virtual ~IFileWatcher() = default;
        virtual WatchPollResult Poll(int timeout_ms = 0) = 0;
    };

    class BatchPlan
    {
    public:
        enum class ActionKind
        {
            AtomicWrite,
            SafeReplace,
            Rename
        }; // 操作种类，AtomicWrite表示原子写入；SafeReplace表示安全替换；Rename表示重命名。

        struct Action
        {
            ActionKind kind{ActionKind::AtomicWrite};
            std::string src;
            std::string dst;
            std::string payload;
            bool backup{false};
        }; // 操作，包含操作类型、源路径、目标路径、负载数据以及是否需要备份等信息。

        BatchPlan &AddAtomicWrite(std::string path, std::string data);                    // 添加原子写入操作，参数为目标路径和写入数据。
        BatchPlan &AddSafeReplace(std::string src, std::string dst, bool backup = false); // 添加安全替换操作，参数为源路径、目标路径以及是否需要备份。
        BatchPlan &AddRename(std::string src, std::string dst);                           // 添加重命名操作，参数为源路径和目标路径。

        const std::vector<Action> &Actions() const; // 获取操作列表，返回一个包含所有操作的向量。

    private:
        std::vector<Action> actions_;
    };

    RunResult Run(const BatchPlan &plan, const RunOptions &options = {}); // 执行批处理计划，参数为批处理计划和运行选项，返回运行结果。
    RunResult RecoverFromJournal(std::string_view journal_path,
                                 const RecoverOptions &options = {}); // 从日志中恢复，参数为日志路径和恢复选项，返回运行结果。

    WalkResult WalkDirectory(std::string_view root, const WalkOptions &options = {}); // 目录遍历接口（phase D 骨架）。
    Status CreateLink(std::string_view target,
                      std::string_view link_path,
                      LinkType type,
                      bool overwrite = false); // 链接操作接口（phase D 骨架）。
    std::unique_ptr<IFileWatcher> CreateFileWatcher(std::string path); // 文件变更监控抽象（轮询实现）。
    CapabilityInfo QueryCapabilities();                                // 查询当前可用能力。

    const char *ToString(OpType op) noexcept;             // 将操作类型转换为字符串，参数为操作类型，返回对应的字符串表示。
    const char *ToString(RollbackMode mode) noexcept;     // 将回滚模式转换为字符串，参数为回滚模式，返回对应的字符串表示。
    const char *ToString(ConflictPolicy policy) noexcept; // 将冲突处理策略转换为字符串，参数为冲突处理策略，返回对应的字符串表示。
    const char *ToString(WatchEventKind kind) noexcept;
    const char *ToString(LinkType type) noexcept;

} // namespace fsx
