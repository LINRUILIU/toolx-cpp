#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cfgx
{

    template <typename T>
    struct Result
    {
        bool ok{false};
        T value{};
        std::string error;
    }; // 结果模板

    struct Status
    {
        bool ok{false};
        std::string error;
    }; // 状态结构体

    enum class ConfigFormat
    {
        Unknown = 0,
        Json,
        Ini,
        Yaml,
        Toml,
    }; // 配置格式枚举

    enum class NodeKind
    {
        Null = 0,
        Bool,
        Integer,
        Double,
        String,
        Object,
        Array,
    }; // 节点类型枚举

    enum class SourceLayer
    {
        Base = 0,
        Env,
        Remote,
        Local,
        Runtime, // 运行时覆盖
    }; // 配置来源层枚举

    struct SourceAttribution
    {
        std::string path;
        SourceLayer layer{SourceLayer::Base};
    }; // 配置来源归属结构体

    struct TypePolicyOptions
    {
        bool strict_string_global{false};
        std::vector<std::pair<SourceLayer, bool>> strict_string_by_source;
        std::vector<std::pair<std::string, bool>> strict_string_by_path;
    }; // 类型策略选项结构体

    struct ComposeOptions
    {
        bool append_arrays{false};
    }; // 合成选项结构体

    enum class DiffKind
    {
        Added = 0,
        Removed,
        Changed,
    }; // 差异类型枚举

    struct DiffEntry
    {
        std::string path;
        DiffKind kind{DiffKind::Changed};
        SourceLayer layer{SourceLayer::Base};
    }; // 差异条目结构体

    struct ReloadOptions
    {
        std::uint64_t debounce_ms{200};
        bool include_snapshots{false};
        std::string env_prefix{"APP_CFG_"};
        std::string remote_url;
        ConfigFormat remote_format{ConfigFormat::Unknown};
        std::vector<std::pair<std::string, std::string>> remote_headers;
        bool allow_remote_failure{true};
        std::uint64_t remote_poll_interval_ms{0};
        bool allow_missing_base{false};
        bool allow_missing_local{true};
        TypePolicyOptions type_policy;
        ComposeOptions compose_options;
    }; // 重载选项结构体

    using HeaderList = std::vector<std::pair<std::string, std::string>>; // HTTP 风格请求头列表。

    struct RemoteFetchRequest
    {
        std::string url;
        HeaderList headers;
    }; // 远程拉取请求结构。

    struct RemoteFetchResponse
    {
        std::string body;
        HeaderList headers;
        int status_code{200};
    }; // 远程拉取响应结构。

    using RemoteFetcher = std::function<Result<RemoteFetchResponse>(const RemoteFetchRequest &request)>; // 可插拔远程拉取回调。

    class Node
    {
    public:
        using Object = std::vector<std::pair<std::string, Node>>; // 对象类型，使用键值对的向量表示
        using Array = std::vector<Node>;                          // 数组类型，使用节点的向量表示

        Node();
        explicit Node(bool value); // 显式构造函数，防止隐式类型转换
        explicit Node(std::int64_t value);
        explicit Node(double value);
        explicit Node(std::string value);
        explicit Node(const char *value);
        explicit Node(Object value);
        explicit Node(Array value);

        static Node MakeObject(); // 静态工厂方法，创建一个空对象节点
        static Node MakeArray();  // 静态工厂方法，创建一个空数组节点

        NodeKind Kind() const noexcept; // 获取节点类型

        bool IsNull() const noexcept; // 是否为null节点
        // Unified emptiness check: true for null, empty string, empty object, empty array.
        bool IsEmpty() const noexcept;     // 统一的空检查：对于null、空字符串、空对象和空数组返回true
        bool IsObject() const noexcept;    // 是否为对象节点
        bool IsArray() const noexcept;     // 是否为数组节点
        bool IsScalar() const noexcept;    // 是否为标量节点（布尔、整数、双精度或字符串）
        std::size_t Size() const noexcept; // 获取对象或数组的大小，对于其他类型返回0

        bool AsBool(bool fallback = false) const noexcept;            // 将节点转换为布尔值，如果类型不匹配则返回fallback
        std::int64_t AsInt(std::int64_t fallback = 0) const noexcept; // 将节点转换为整数，如果类型不匹配则返回fallback
        double AsDouble(double fallback = 0.0) const noexcept;        // 将节点转换为双精度浮点数，如果类型不匹配则返回fallback
        std::string AsString(std::string_view fallback = "") const;   // 将节点转换为字符串，如果类型不匹配则返回fallback

        // Object convenience operations.
        const Node *Get(std::string_view key) const noexcept; // 获取对象成员，如果类型不匹配或键不存在则返回nullptr
        Node *Get(std::string_view key) noexcept;             // 获取对象成员的可变版本，如果类型不匹配或键不存在则返回nullptr
        Status Set(std::string key, Node value);              // 设置对象成员，如果类型不匹配则返回错误状态
        Status Erase(std::string_view key);                   // 删除对象成员，如果类型不匹配或键不存在则返回错误状态

        // Array convenience operations.
        const Node *At(std::size_t index) const noexcept;                     // 获取数组元素，如果类型不匹配或索引越界则返回nullptr
        Node *At(std::size_t index) noexcept;                                 // 获取数组元素的可变版本，如果类型不匹配或索引越界则返回nullptr
        Status Push(Node value);                                              // 在数组末尾添加元素，如果类型不匹配则返回错误状态
        Status SetAt(std::size_t index, Node value, bool auto_expand = true); // 设置数组元素，如果auto_expand为true且索引越界则自动扩展数组，如果类型不匹配则返回错误状态
        Status EraseAt(std::size_t index);                                    // 删除数组元素，如果类型不匹配或索引越界则返回错误状态

        const Object *TryObject() const noexcept; // 尝试将节点视为对象，如果类型不匹配则返回nullptr
        Object *TryObject() noexcept;             // 尝试将节点视为对象的可变版本，如果类型不匹配则返回nullptr

        const Array *TryArray() const noexcept; // 尝试将节点视为数组，如果类型不匹配则返回nullptr
        Array *TryArray() noexcept;             // 尝试将节点视为数组的可变版本，如果类型不匹配则返回nullptr

    private:
        using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, Object, Array>; // 存储节点值的变体类型，支持多种类型的值
        // std::monostate表示空节点
        Storage data_; // 节点数据存储

        friend std::string ToJson(const Node &node, int indent);
        friend Result<Node> ParseJson(std::string_view text);
        friend Status Merge(Node &base, const Node &overlay, bool append_arrays);
        friend Result<const Node *> GetNode(const Node &root, std::string_view path);
        friend Result<Node *> GetNodeMutable(Node &root, std::string_view path);
        friend Status SetNode(Node &root, std::string_view path, Node value);
        friend Status RemoveNode(Node &root, std::string_view path);
    };

    class RuntimeOverrides
    {
    public:
        RuntimeOverrides();

        // Path-level patch set.
        Status Set(std::string_view path, Node value); // 设置路径级别的补丁
        // Path-level patch remove.
        Status Remove(std::string_view path); // 删除路径级别的补丁
        // Full root replace operation.
        Status Replace(Node value); // 全根替换操作

        void Clear() noexcept;       // 清除所有补丁
        bool Empty() const noexcept; // 检查是否没有任何补丁

        Result<Node> Materialize() const;

    private:
        enum class OpKind
        {
            Set = 0,
            Remove,
            Replace,
        }; // 操作类型枚举

        struct Op
        {
            std::uint64_t sequence{0};
            OpKind kind{OpKind::Set};
            std::string path;
            Node value;
        }; // 操作结构体，包含序列号、操作类型、路径和节点值

        std::vector<Op> ops_;            // 操作列表
        std::uint64_t next_sequence_{1}; // 下一个操作的序列号
    };

    struct ParserAdapter
    {
        std::string name; // 解析器名称
        // Parse text into Node for the specified format.
        std::function<Result<Node>(std::string_view text, ConfigFormat format)> parse; // 将文本解析为指定格式的节点
        // Dump Node into text for the specified format.
        std::function<Result<std::string>(const Node &root, ConfigFormat format, int indent)> dump; // 将节点转储为指定格式的文本
    };

    struct ReloadEvent
    {
        bool attempted{false};                       // 是否尝试过重新加载
        bool changed{false};                         // 是否检测到配置更改
        bool rolled_back{false};                     // 是否由于验证失败而回滚到旧配置
        std::string message;                         // 事件消息，可能包含错误信息或其他相关信息
        std::vector<DiffEntry> diff_paths;           // 配置更改的路径和类型的列表
        std::vector<SourceAttribution> source_trace; // 当前配置的来源追踪列表，显示每个路径来自哪个层
        std::optional<Node> old_snapshot;            // 旧配置快照，仅当ReloadOptions.include_snapshots为true时提供
        std::optional<Node> new_snapshot;            // 新配置快照，仅当ReloadOptions.include_snapshots为true时提供
    };

    struct SnapshotAuditEntry
    {
        std::uint64_t sequence{0};
        std::uint64_t timestamp_ms{0};
        bool rolled_back{false};
        std::string action;
        std::string message;
        std::vector<DiffEntry> diff_paths;
        std::vector<SourceAttribution> source_trace;
    };

    enum class PathTokenKind
    {
        Key = 0,
        Index,
    }; // 路径令牌类型枚举，表示路径中的键或数组索引

    struct PathToken
    {
        PathTokenKind kind{PathTokenKind::Key};
        std::string key;
        std::size_t index{0};
    }; // 路径令牌结构体，包含令牌类型、键名称和数组索引

    // Path grammar:
    // - keys are separated by '.'
    // - array index uses [n]
    // - special characters in keys are escaped via '\\', e.g. a\\.b[0] means key "a.b" then index 0.
    Result<std::vector<PathToken>> ParsePath(std::string_view path); // 解析路径字符串为路径令牌列表，支持键分隔符、数组索引和转义字符

    Result<const Node *> GetNode(const Node &root, std::string_view path); // 根据路径获取节点，如果路径无效或类型不匹配则返回错误
    Result<Node *> GetNodeMutable(Node &root, std::string_view path);      // 根据路径获取可变节点，如果路径无效或类型不匹配则返回错误
    Status SetNode(Node &root, std::string_view path, Node value);         // 根据路径设置节点值，如果路径无效或类型不匹配则返回错误
    Status RemoveNode(Node &root, std::string_view path);                  // 根据路径删除节点，如果路径无效或类型不匹配则返回错误
    bool Exists(const Node &root, std::string_view path);                  // 检查路径是否存在于节点中
    bool IsEmptyValue(const Node &node) noexcept;                          // 检查节点是否为“空值”，即null、空字符串、空对象或空数组

    // Merge behavior:
    // - object + object: merge recursively by key
    // - array + array: override by default, append when append_arrays=true
    // - mixed types: overlay replaces base
    Status Merge(Node &base, const Node &overlay, bool append_arrays = false); // 合并两个节点，根据类型进行不同的合并策略，支持对象递归合并、数组覆盖或追加，以及混合类型覆盖

    Result<Node> BuildEnvLayerFromPairs(const std::vector<std::pair<std::string, std::string>> &variables,
                                        std::string_view prefix = "APP_CFG_",
                                        const TypePolicyOptions &policy = {}); // 从环境变量对构建配置层，使用指定的前缀过滤环境变量，并根据类型策略选项解析值
    Result<Node> BuildEnvLayerFromEnvironment(std::string_view prefix = "APP_CFG_",
                                              const TypePolicyOptions &policy = {}); // 从当前环境构建配置层，使用指定的前缀过滤环境变量，并根据类型策略选项解析值

    Result<Node> ComposeLayers(const Node &base,
                               const std::optional<Node> &env_layer = std::nullopt,
                               const std::optional<Node> &local_layer = std::nullopt,
                               const RuntimeOverrides *runtime = nullptr,
                               const ComposeOptions &options = {},
                               std::vector<SourceAttribution> *source_trace = nullptr,
                               const std::optional<Node> &remote_layer = std::nullopt); // 合成多个配置层，按照优先级顺序进行合成，并生成来源追踪列表，支持基本层、环境层、远程层、本地层和运行时覆盖，以及合成选项

    struct ValidationIssue
    {
        std::string path;
        std::string message;
    }; // 验证问题结构体，包含问题路径和消息

    struct ValidationRule
    {
        std::string name;
        bool fail_fast{false};
        std::function<std::optional<ValidationIssue>(const Node &root)> evaluator;
    }; // 验证规则结构体，包含规则名称、是否失败快速以及评估函数

    Result<std::vector<ValidationIssue>> Validate(const Node &root, const std::vector<ValidationRule> &rules); // 验证配置节点，返回验证问题列表，如果没有问题则返回空列表，支持规则列表和失败快速选项
    ValidationRule RequirePathRule(std::string path, bool fail_fast = false);                                  // 验证规则：要求路径存在
    ValidationRule ExpectKindRule(std::string path, NodeKind expected_kind, bool fail_fast = false);           // 验证规则：期望路径节点的类型
    ValidationRule NumericRangeRule(std::string path,
                                    double min_value,
                                    double max_value,
                                    bool fail_fast = false); // 验证规则：数值范围检查，适用于整数和双精度节点
    ValidationRule ChoiceRule(std::string path,
                              std::vector<std::string> choices,
                              bool case_insensitive = true,
                              bool fail_fast = false); // 验证规则：选择检查，适用于标量节点，检查值是否在候选列表中，支持大小写不敏感选项
    ValidationRule MutexRule(std::vector<std::string> paths,
                             bool fail_fast = false); // 验证规则：互斥检查，确保指定路径列表中最多只有一个路径存在
    ValidationRule DependencyRule(std::string path,
                                  std::string depends_on,
                                  bool fail_fast = false); // 验证规则：依赖检查，确保如果路径存在则依赖路径也存在
    ValidationRule StringLengthRule(std::string path,
                                    std::size_t min_length,
                                    std::size_t max_length,
                                    bool fail_fast = false); // 验证规则：字符串长度检查，确保路径节点的字符串长度在指定范围内

    class PollReloader
    {
    public:
        PollReloader(std::string base_file_path,
                     std::string local_file_path = "",
                     const RuntimeOverrides *runtime = nullptr); // 构造函数，接受基本文件路径、本地文件路径和运行时覆盖的指针

        void SetRuntimeOverrides(const RuntimeOverrides *runtime) noexcept;       // 设置运行时覆盖的指针
        void SetValidationRules(std::vector<ValidationRule> rules);               // 设置验证规则列表
        void SetCallback(std::function<void(const ReloadEvent &event)> callback); // 设置重新加载事件的回调函数
        void SetOptions(ReloadOptions options);                                   // 设置重新加载选项

        Result<ReloadEvent> ReloadNow();                    // 立即尝试重新加载配置，返回重新加载事件结果
        Result<ReloadEvent> Tick(std::uint64_t now_ms = 0); // 轮询检查配置文件更改，返回重新加载事件结果，支持传入当前时间戳以进行去抖动处理

        const Node *Current() const noexcept;                                      // 获取当前有效配置节点的指针，如果没有有效配置则返回nullptr
        const std::vector<SourceAttribution> *CurrentSourceTrace() const noexcept; // 获取当前配置的来源追踪列表的指针，如果没有有效配置则返回nullptr
        Result<Node> SnapshotCurrent() const;                                      // 获取当前配置的快照，如果没有有效配置则返回错误
        Status ExportSnapshotToFile(std::string_view file_path,
                                    ConfigFormat preferred = ConfigFormat::Unknown,
                                    int indent = 2) const; // 导出当前快照到文件。
        Result<Node> ImportSnapshotFromFile(std::string_view file_path,
                                            ConfigFormat preferred = ConfigFormat::Unknown) const; // 从文件导入快照。
        Status RestoreSnapshot(const Node &snapshot,
                               const std::vector<SourceAttribution> *source_trace = nullptr); // 恢复到指定的配置快照和来源追踪，如果验证失败则返回错误状态
        Status RestoreSnapshotFromFile(std::string_view file_path,
                                       ConfigFormat preferred = ConfigFormat::Unknown,
                                       const std::vector<SourceAttribution> *source_trace = nullptr); // 从文件导入并恢复快照。
        const std::vector<SnapshotAuditEntry> &AuditTrail() const noexcept;                           // 获取重载/回滚/快照操作审计轨迹。
        void ClearAuditTrail() noexcept;                                                              // 清空审计轨迹。

    private:
        struct TrackedFileState
        {
            bool initialized{false};
            bool exists{false};
            std::uint64_t mtime{0};
            std::uint64_t content_hash{0};
        }; // 跟踪文件状态的结构体，包含是否初始化、是否存在、修改时间和内容哈希

        std::string base_file_path_;                             // 基本配置文件路径
        std::string local_file_path_;                            // 本地配置文件路径
        const RuntimeOverrides *runtime_{nullptr};               // 运行时覆盖的指针
        ReloadOptions options_;                                  // 重新加载选项
        std::vector<ValidationRule> rules_;                      // 验证规则列表
        std::function<void(const ReloadEvent &event)> callback_; // 重新加载事件的回调函数

        bool has_current_{false};                      // 是否有当前有效配置
        Node current_;                                 // 当前有效配置节点
        std::vector<SourceAttribution> current_trace_; // 当前配置的来源追踪列表

        TrackedFileState base_state_;       // 基本配置文件的跟踪状态
        TrackedFileState local_state_;      // 本地配置文件的跟踪状态
        bool pending_change_{false};        // 是否有待处理的配置更改
        std::uint64_t pending_since_ms_{0}; // 待处理的配置更改开始时间戳
        std::uint64_t last_remote_poll_ms_{0};
        std::vector<SnapshotAuditEntry> audit_trail_;
        std::uint64_t audit_sequence_{1};
    };

    ConfigFormat DetectFormatFromPath(std::string_view file_path);                // 从文件路径检测配置格式，基于文件扩展名进行判断，支持.json、.ini、.cfg、.yaml、.yml和.toml扩展名
    Status RegisterParserAdapter(ParserAdapter adapter, bool set_active = false); // 注册解析器适配器，如果set_active为true则同时设置为活动适配器
    Status UnregisterParserAdapter(std::string_view name);                        // 注销解析器适配器，根据名称进行注销，如果适配器不存在则返回错误状态
    Status SetActiveParserAdapter(std::string_view name);                         // 设置活动的解析器适配器，根据名称进行设置，如果名称为空则清除活动适配器，如果适配器不存在则返回错误状态
    std::string GetActiveParserAdapter();                                         // 获取当前活动的解析器适配器的名称，如果没有活动适配器则返回空字符串
    std::vector<std::string> ListParserAdapters();                                // 列出所有注册的解析器适配器的名称
    bool HasParserAdapter(std::string_view name);                                 // 检查是否存在指定名称的解析器适配器
    void ClearParserAdapters();                                                   // 清除所有注册的解析器适配器，并清除活动适配器

    const char *ToString(ConfigFormat format) noexcept;           // 将配置格式枚举转换为字符串表示，返回对应的字符串常量，如果格式未知则返回"unknown"
    const char *ToString(NodeKind kind) noexcept;                 // 将节点类型枚举转换为字符串表示，返回对应的字符串常量，如果类型未知则返回"unknown"
    const char *ToString(SourceLayer layer) noexcept;             // 将源层枚举转换为字符串表示，返回对应的字符串常量，如果层未知则返回"unknown"
    const char *ToString(DiffKind kind) noexcept;                 // 将差异类型枚举转换为字符串表示，返回对应的字符串常量，如果类型未知则返回"unknown"
    std::optional<NodeKind> ParseNodeKind(std::string_view text); // 解析节点类型字符串为枚举值，支持多种别名和大小写不敏感，如果无法解析则返回std::nullopt

    Result<Node> ParseJson(std::string_view text);        // 解析JSON文本为节点，如果解析成功则返回节点，否则返回错误信息
    std::string ToJson(const Node &node, int indent = 2); // 将节点转换为JSON文本，支持指定缩进级别，如果indent小于0则不进行缩进

    Status SetRemoteFetcher(RemoteFetcher fetcher); // 设置远程拉取回调，传入空回调表示清除。
    bool HasRemoteFetcher();                        // 检查是否已设置远程拉取回调。
    Result<Node> LoadFromRemote(std::string_view url,
                                ConfigFormat preferred = ConfigFormat::Unknown,
                                HeaderList headers = {}); // 通过远程回调获取文本并解析为配置节点。

    Result<Node> LoadFromFile(std::string_view file_path, ConfigFormat preferred = ConfigFormat::Unknown); // 从文件加载配置节点，支持指定首选格式，如果首选格式为Unknown则根据文件路径检测格式，如果加载或解析失败则返回错误信息
    Status SaveToFile(const Node &root,
                      std::string_view file_path,
                      ConfigFormat preferred = ConfigFormat::Unknown,
                      int indent = 2); // 将配置节点保存到文件，支持指定首选格式和缩进级别，如果首选格式为Unknown则根据文件路径检测格式，如果保存或序列化失败则返回错误信息
    Status SaveEncryptedToFile(const Node &root,
                               std::string_view file_path,
                               std::string_view key,
                               ConfigFormat preferred = ConfigFormat::Unknown,
                               int indent = 2); // 使用静态密钥加密后写入配置文件。
    Result<Node> LoadEncryptedFromFile(std::string_view file_path,
                                       std::string_view key,
                                       ConfigFormat preferred = ConfigFormat::Unknown); // 使用静态密钥解密并加载配置文件。

} // namespace cfgx
