#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// 日志系统公共头（中文注释）。
// 建议使用 UTF-8 无 BOM 编码以避免在 Windows 下出现 GBK/UTF-8 混乱。
// 已在仓库根目录添加 `.editorconfig` 指定编码。

namespace logsys
{

    enum class LogLevel : std::uint8_t
    {
        Trace = 0,
        Debug,    // 调试日志
        Info,     // 一般信息日志
        Warning,  // 警告日志
        Error,    // 错误日志
        Fatal,    // 严重错误日志，表示发生了严重错误，程序无法继续运行。
        Critical, // 危急日志，表示发生了严重的错误，可能导致系统崩溃或数据丢失，但是程序可能可以继续运行。
    };
    // LogLevel: 日志级别，按严重性从低到高排列。

    enum class ErrorCategory : std::uint8_t
    {
        System = 0, // 系统错误，如文件 I/O、网络、数据库等底层错误。
        Io,         // I/O 错误，如文件读写、网络通信等相关错误。
        Net,        // 网络错误，如连接失败、超时、协议错误等。
        Db,         // 数据库错误，如查询失败、连接池耗尽等。
        Config,     // 配置错误，如缺失配置、配置格式错误等。
        Auth,       // 认证错误，如登录失败、权限不足等。
        Memory,     // 内存错误，如分配失败、泄漏检测等。
        Business,   // 业务错误，如违反业务规则等。
        ThirdParty, // 第三方服务错误，如 API 调用失败等。
        Security,   // 安全错误，如权限验证失败等。
        Count       // 错误类别数量，用于统计和数组大小。
    };
    // ErrorCategory: 用于对错误进行业务域分类，便于统计与分流。

    enum class ActionHint : std::uint8_t
    {
        Retry = 0, // 重试，表示该错误可能是暂时的，重试可能会成功。
        Fallback,  // 回退，表示可以使用备用方案继续运行。
        Abort,     // 中止，表示需要立即停止当前操作或程序。
        Ignore,    // 忽略，表示该错误可以被安全地忽略，不需要特别处理。
        Escalate,  // 升级，表示需要将该错误上报给更高级别的监控或人工干预。
    };
    // ActionHint: 针对错误推荐的后续动作提示（仅为建议性信息）。

    enum class ErrorSource : std::uint8_t
    {
        System = 0x01,     // 系统错误，如文件 I/O、网络、数据库等底层错误。
        Business = 0x02,   // 业务错误，如违反业务规则等。
        Exception = 0x03,  // 异常错误，如未处理的异常等。
        ThirdParty = 0x04, // 第三方服务错误，如 API 调用失败等。
        Reserved = 0x05,   // 保留，用于未来扩展。
    };
    // ErrorSource: 错误来源位段，构成 ErrorCode 的高位部分。

    enum class ModuleId : std::uint8_t
    {
        Core = 0x01,              // 核心模块，如日志系统本身的错误。
        Sink = 0x02,              // 输出模块，如控制台、文件、调试器等相关错误。
        Formatter = 0x03,         // 格式化模块，如文本格式化、JSON 格式化等相关错误。
        Encoding = 0x04,          // 编码模块，如字符编码转换等相关错误。
        Config = 0x05,            // 配置模块，如配置文件解析、验证等相关错误。
        Security = 0x06,          // 安全模块，如权限验证、加密等相关错误。
        ThirdPartyAdapter = 0x07, // 第三方适配器模块，如与第三方库的集成适配相关错误。
        BusinessCommon = 0x08,    // 业务通用模块，如常见的业务逻辑错误。
    };
    // ModuleId: 错误所属模块标识，用于 ErrorCode 的第二个字节。

    struct DefaultLoggerOptions
    {
        // Output threshold controls what is printed to sinks.
        LogLevel level{LogLevel::Fatal}; // 输出级别阈值，控制哪些日志会被发送到输出端（sink）。
        // Record threshold controls what is accepted into logger pipeline.
        LogLevel record_level{LogLevel::Info}; // 记录级别阈值，控制哪些日志事件会被接受进入日志系统进行处理。
        bool enable_console{true};             // 是否启用控制台输出。
        bool enable_file{false};               // 是否启用文件输出。
        std::string file_path{"app.log"};      // 日志文件路径，默认为 "app.log"。
        bool enable_debugger{false};           // 是否启用调试器输出。
        bool use_json_formatter{false};        // 是否使用 JSON 格式化器，默认为 false（使用文本格式化器）。
    };
    // DefaultLoggerOptions: 便捷的默认 logger 配置结构体，用于快速初始化。

    enum class TextField : std::uint32_t
    {
        Timestamp = 1u << 0,
        Level = 1u << 1,
        Code = 1u << 2,
        Category = 1u << 3,
        Location = 1u << 4,
        Function = 1u << 5,
        Message = 1u << 6,
        SysCode = 1u << 7,
        Vendor = 1u << 8,
        ExtFields = 1u << 9,
        ThreadId = 1u << 10,
    };
    // TextField: 文本字段掩码，用来选择输出哪些字段（位掩码）。

    enum class OutputOrderMode : std::uint8_t
    {
        ByTimeMixed = 0, // 按时间混合输出，所有日志事件根据时间戳排序输出。
        ByLevelGrouped,  // 按级别分组输出，先按日志级别分组，再按时间排序输出。
    };

    enum class RollingTimeMode : std::uint8_t
    {
        None = 0,
        Day,
        Hour,
    };

    struct RollingConfigV2
    {
        bool enabled{true};                                   // 是否启用基于大小的文件滚动。
        std::size_t max_file_size_bytes{10u * 1024u * 1024u}; // 单个日志文件的最大大小，默认为 10 MB。
        std::size_t keep_recent_files{5};                     // 保留最近的日志文件数量，超过后会删除最旧的文件。
        RollingTimeMode time_mode{RollingTimeMode::None};     // 按时间轮转策略，None 表示关闭，Day/Hour 表示按天/小时切分。
    };
    // RollingConfigV2: 文件滚动策略配置（基于大小轮替）。

    struct ScheduleConfigV2
    {
        bool periodic_flush_enabled{true};                                 // 是否启用定期刷新日志输出。
        std::chrono::milliseconds flush_interval{std::chrono::seconds(1)}; // 刷新间隔，默认为 1 秒。
    };
    // ScheduleConfigV2: 定期刷新/刷盘的相关配置。

    struct RenderConfigV2
    {
        bool enable_color{true};              // 是否启用颜色输出，默认为 true。
        bool light_theme_only{true};          // 是否仅在浅色主题终端启用颜色输出，默认为 true。
        bool allow_third_party_adapter{true}; // 是否允许第三方适配器使用颜色输出接口，默认为 true。
    };
    // RenderConfigV2: 控制控制台颜色输出与第三方适配器允许性。

    struct BackpressureConfigV2
    {
        bool drop_low_level_when_full{true};       // 当日志消费跟不上时，是否丢弃低于 drop_below_level 的日志事件以减轻压力。
        LogLevel drop_below_level{LogLevel::Info}; // 当日志队列满时，丢弃低于此级别的日志事件，默认为 Info。
        std::size_t queue_high_watermark{10000};   // 日志队列高水位线，当队列中的日志事件数量超过此值时，开始应用 backpressure 策略。
    };
    // BackpressureConfigV2: 当日志消费跟不上时的回压策略配置。

    struct RemoteConfigV2
    {
        bool enable_udp_syslog{false};         // 是否启用 UDP syslog 远程上报。
        std::string udp_host{"127.0.0.1"};     // 远程 syslog 目标主机。
        std::uint16_t udp_port{514};           // 远程 syslog 目标端口。
        std::uint8_t syslog_facility{1};       // syslog facility，默认 1(user-level messages)。
        std::string syslog_app_name{"logsys"}; // syslog app-name 字段。
        std::string syslog_hostname{"-"};      // syslog hostname 字段，"-" 表示未指定。
    };
    // RemoteConfigV2: 远程日志上报配置（当前支持 UDP syslog）。

    struct ProfileConfigV2
    {
        std::string name;                             // 配置档位名称，便于识别和管理。
        std::optional<std::string> file_path_pattern; // 可选的文件路径模式，支持简单的通配符（如 "src/*.cpp"）用于匹配日志事件来源文件。
        std::optional<ModuleId> module;               // 可选的模块匹配条件，用于根据日志事件的模块信息进行匹配。
        std::optional<std::uint32_t> text_field_mask; // 可选的文本字段掩码，覆盖全局配置以启用/禁用特定字段。
        std::optional<LogLevel> output_level;         // 可选的输出级别阈值，覆盖全局配置以调整此档位的输出级别。
        std::optional<LogLevel> record_level;         // 可选的记录级别阈值，覆盖全局配置以调整此档位的记录级别。
        std::optional<bool> enable_console;           // 可选的控制台输出启用标志，覆盖全局配置以启用/禁用控制台输出。
        std::optional<bool> enable_file;              // 可选的文件输出启用标志，覆盖全局配置以启用/禁用文件输出。
        std::optional<bool> enable_debugger;          // 可选的调试器输出启用标志，覆盖全局配置以启用/禁用调试器输出。
        std::optional<OutputOrderMode> output_order;  // 可选的输出顺序模式，覆盖全局配置以调整此档位的输出顺序策略。
    };
    // ProfileConfigV2: 针对不同文件/模块的配置覆写规则（按文件模式或模块匹配）。

    struct LoggerConfigV2
    {
        LogLevel global_record_level{LogLevel::Info};  // 全局记录级别阈值，控制哪些日志事件会被接受进入日志系统进行处理。
        LogLevel global_output_level{LogLevel::Fatal}; // 全局输出级别阈值，控制哪些日志会被发送到输出端（sink）。
        std::uint32_t global_text_field_mask{static_cast<std::uint32_t>(TextField::Level) |
                                             static_cast<std::uint32_t>(TextField::Message)}; // 全局文本字段掩码，控制默认输出哪些字段（位掩码）。
        bool global_enable_console{true};                                                     // 是否启用控制台输出。
        bool global_enable_file{false};                                                       // 是否启用文件输出。
        bool global_enable_debugger{false};                                                   // 是否启用调试器输出。
        OutputOrderMode output_order{OutputOrderMode::ByTimeMixed};                           // 输出顺序模式。

        RollingConfigV2 rolling{};               // 文件滚动配置。
        ScheduleConfigV2 schedule{};             // 定期刷新配置。
        RenderConfigV2 render{};                 // 渲染配置。
        BackpressureConfigV2 backpressure{};     // 回压配置。
        RemoteConfigV2 remote{};                 // 远程上报配置。
        std::vector<ProfileConfigV2> profiles{}; // 配置档位列表，按优先级顺序排列，解析时会依次匹配文件路径和模块信息以确定最终配置。
    };
    // LoggerConfigV2: 更完整的 V2 配置模型，包含路由、回压、渲染等子配置。

    struct ResolvedProfileV2
    {
        LogLevel record_level{LogLevel::Info};  // 记录级别阈值，控制哪些日志事件会被接受进入日志系统进行处理。
        LogLevel output_level{LogLevel::Fatal}; // 输出级别阈值，控制哪些日志会被发送到输出端（sink）。
        std::uint32_t text_field_mask{static_cast<std::uint32_t>(TextField::Level) |
                                      static_cast<std::uint32_t>(TextField::Message)}; // 文本字段掩码，控制输出哪些字段（位掩码）。
        bool enable_console{true};                                                     // 是否启用控制台输出。
        bool enable_file{false};                                                       // 是否启用文件输出。
        bool enable_debugger{false};                                                   // 是否启用调试器输出。
        OutputOrderMode output_order{OutputOrderMode::ByTimeMixed};                    // 输出顺序模式。
    };
    // ResolvedProfileV2: 将 ProfileConfigV2 与全局设置解析合并后得到的最终生效策略。

    class ProfileResolverV2
    {
    public:
        static ResolvedProfileV2 Resolve(const LoggerConfigV2 &config,
                                         std::string_view file_path,
                                         ModuleId module);
    };
    // ProfileResolverV2: 根据文件路径与模块计算实际生效的配置（优先级由 module/file 决定）。

    constexpr std::uint32_t kTextFieldMaskDefault =
        static_cast<std::uint32_t>(TextField::Timestamp) |
        static_cast<std::uint32_t>(TextField::Level) |
        static_cast<std::uint32_t>(TextField::Code) |
        static_cast<std::uint32_t>(TextField::Category) |
        static_cast<std::uint32_t>(TextField::Location) |
        static_cast<std::uint32_t>(TextField::Function) |
        static_cast<std::uint32_t>(TextField::Message) |
        static_cast<std::uint32_t>(TextField::SysCode) |
        static_cast<std::uint32_t>(TextField::Vendor) |
        static_cast<std::uint32_t>(TextField::ExtFields) |
        static_cast<std::uint32_t>(TextField::ThreadId); // 默认文本字段掩码，启用所有字段输出。

    constexpr std::uint32_t kTextFieldMaskSimple =
        static_cast<std::uint32_t>(TextField::Level) |
        static_cast<std::uint32_t>(TextField::Message); // 简化文本字段掩码，仅输出日志级别和消息内容。

    using ErrorCode = std::uint32_t; // 错误代码类型，32 位无符号整数，结构为 [source:8][module:8][detail:16]。

    // Error code layout: [source:8][module:8][detail:16].
    // This keeps global uniqueness while preserving fast bit-split queries.
    constexpr ErrorCode MakeErrorCode(std::uint8_t source, std::uint8_t module, std::uint16_t detail) noexcept
    {
        return (static_cast<ErrorCode>(source) << 24) |
               (static_cast<ErrorCode>(module) << 16) |
               static_cast<ErrorCode>(detail); // 构造错误代码的 constexpr 函数，将 source、module 和 detail 组合成一个 32 位整数。
    }

    constexpr std::uint8_t ErrorSourcePart(ErrorCode code) noexcept
    {
        return static_cast<std::uint8_t>((code >> 24) & 0xFF);
    } // 从错误代码中提取 source 部分的 constexpr 函数。

    constexpr std::uint8_t ErrorModulePart(ErrorCode code) noexcept
    {
        return static_cast<std::uint8_t>((code >> 16) & 0xFF);
    } // 从错误代码中提取 module 部分的 constexpr 函数。

    constexpr std::uint16_t ErrorDetailPart(ErrorCode code) noexcept
    {
        return static_cast<std::uint16_t>(code & 0xFFFF);
    } // 从错误代码中提取 detail 部分的 constexpr 函数。

    const char *ToString(LogLevel level) noexcept;                      // 将日志级别枚举转换为字符串的函数。
    const char *ToString(ErrorCategory category) noexcept;              // 将错误类别枚举转换为字符串的函数。
    const char *ToString(ActionHint hint) noexcept;                     // 将动作提示枚举转换为字符串的函数。
    std::optional<LogLevel> ParseLogLevel(std::string_view level_text); // 将字符串解析为日志级别枚举的函数，返回 std::optional 包含解析结果，如果解析失败则为 std::nullopt。

    struct ExtField
    {
        std::string key;   // 扩展字段的键，最大长度为 32 字符。
        std::string value; // 扩展字段的值，最大长度为 128 字符。
    };

    struct LogEvent
    {
        static constexpr std::size_t kMaxExtFields = 8;  // 每条日志事件最多支持 8 个扩展字段。
        static constexpr std::size_t kMaxKeyLen = 32;    // 扩展字段键的最大长度。
        static constexpr std::size_t kMaxValueLen = 128; // 扩展字段值的最大长度。

        std::chrono::system_clock::time_point timestamp;      // 日志事件的时间戳，使用系统时钟的时间点表示。
        LogLevel level{LogLevel::Info};                       // 日志级别，默认为 Info。
        ErrorCode code{0};                                    // 错误代码，默认为 0，表示无错误。
        ErrorCategory category{ErrorCategory::Business};      // 错误类别，默认为 Business。
        std::string file;                                     // 日志事件来源的文件路径。
        std::uint32_t line{0};                                // 日志事件来源的行号。
        std::string function;                                 // 日志事件来源的函数名。
        std::string message;                                  // 日志事件的消息内容。
        std::uint32_t sys_code{0};                            // 系统错误代码。
        std::uint64_t thread_id{0};                           // 线程 ID。
        std::string vendor_name;                              // 供应商名称。
        std::string vendor_code;                              // 供应商代码。
        std::uint32_t text_field_mask{kTextFieldMaskDefault}; // 文本字段掩码。
        std::array<ExtField, kMaxExtFields> ext_fields{};     // 扩展字段数组。
        std::size_t ext_count{0};                             // 扩展字段数量。

        bool SetField(std::string key, std::string value); // 设置扩展字段的成员函数，返回是否成功设置（如键值长度合法且未超过最大数量）。
    };

    struct ErrorDictionaryEntry
    {
        ErrorCode code;                    // 错误代码，唯一标识一个错误类型。
        ErrorSource source;                // 错误来源。
        ModuleId module;                   // 模块 ID。
        ErrorCategory category;            // 错误类别。
        std::string_view message_template; // 消息模板。
        ActionHint action_hint_default;    // 默认动作提示。
        bool deprecated;                   // 是否已弃用。
        std::uint16_t version;             // 版本号。
        std::string_view vendor_name;      // 供应商名称。
        std::string_view vendor_code;      // 供应商代码。
    };

    class ErrorDictionary
    {
    public:
        static const ErrorDictionary &Instance();                          // 获取全局错误字典实例的静态函数。
        std::optional<ErrorDictionaryEntry> Find(ErrorCode code) const;    // 根据错误代码查找对应的错误字典条目的函数，返回 std::optional 包含查找结果，如果未找到则为 std::nullopt。
        const std::vector<ErrorDictionaryEntry> &Entries() const noexcept; // 获取错误字典中所有条目的函数，返回一个包含所有错误字典条目的常量引用。

    private:
        ErrorDictionary();
        std::vector<ErrorDictionaryEntry> entries_;
    };

    class IFormatter
    {
    public:
        virtual ~IFormatter() = default;                             // 虚析构函数，确保派生类对象能够正确销毁。
        virtual std::string Format(const LogEvent &event) const = 0; // 纯虚函数，定义日志事件格式化接口，接受一个 LogEvent 对象并返回格式化后的字符串。
    };

    class TextFormatter final : public IFormatter
    {
    public:
        std::string Format(const LogEvent &event) const override; // 实现 IFormatter 接口的文本格式化器，重写 Format 函数以提供日志事件的文本格式化逻辑。
    };

    class JsonFormatter final : public IFormatter
    {
    public:
        std::string Format(const LogEvent &event) const override; // 实现 IFormatter 接口的 JSON 格式化器，重写 Format 函数以提供日志事件的 JSON 格式化逻辑。
    };

    class ISink
    {
    public:
        virtual ~ISink() = default;                      // 虚析构函数，确保派生类对象能够正确销毁。
        virtual void Write(const std::string &line) = 0; // 纯虚函数，定义日志写入接口，接受一个字符串参数。
        virtual void Flush() = 0;                        // 纯虚函数，定义日志刷新接口。
    };

    class ConsoleSink final : public ISink
    {
    public:
        void Write(const std::string &line) override; // 实现 ISink 接口的控制台输出器，重写 Write 函数以提供日志写入逻辑。
        void Flush() override;                        // 实现 ISink 接口的控制台输出器，重写 Flush 函数以提供日志刷新逻辑。

    private:
        std::mutex mu_;
    };

    class FileSink final : public ISink
    {
    public:
        explicit FileSink(std::string path, RollingConfigV2 rolling = {}); // 构造函数，接受日志文件路径和可选的滚动配置参数。
        ~FileSink() override;                                              // 析构函数，确保文件资源能够正确释放。
        void Write(const std::string &line) override;                      // 实现 ISink 接口的文件输出器，重写 Write 函数以提供日志写入逻辑。
        void Flush() override;                                             // 实现 ISink 接口的文件输出器，重写 Flush 函数以提供日志刷新逻辑。
        void UpdateRollingConfig(RollingConfigV2 rolling);                 // 更新滚动配置的函数，允许在运行时调整文件滚动策略。

    private:
        void OpenLocked();                                            // 在持有锁的情况下打开日志文件的私有函数。
        void RotateLocked();                                          // 在持有锁的情况下执行日志文件滚动的私有函数。
        void RotateByTimeLocked();                                    // 在持有锁的情况下执行按时间轮转。
        void PruneTimeRotatedFilesLocked();                           // 删除超过保留上限的时间轮转历史文件。
        std::string RotatedPath(std::size_t index) const;             // 生成滚动日志文件路径的私有函数，根据索引返回对应的文件路径。
        std::string CurrentTimeSlotKey() const;                       // 计算当前时间槽 key（按天或按小时）。
        std::string TimeRotatedPath(std::string_view slot_key) const; // 生成按时间轮转后的目标路径。

        std::string path_;                  // 日志文件的基本路径。
        std::mutex mu_;                     // 保护文件操作的互斥锁。
        std::FILE *file_{nullptr};          // 文件指针，初始为 nullptr。
        std::size_t current_size_{0};       // 当前日志文件的大小，单位为字节。
        RollingConfigV2 rolling_{};         // 当前的滚动配置。
        std::string current_time_slot_key_; // 当前打开文件对应的时间槽 key。
    };

    class DebuggerSink final : public ISink
    {
    public:
        void Write(const std::string &line) override; // 实现 ISink 接口的调试器输出器，重写 Write 函数以提供日志写入逻辑。
        void Flush() override;                        // 实现 ISink 接口的调试器输出器，重写 Flush 函数以提供日志刷新逻辑。
    };

    class UdpSyslogSink final : public ISink
    {
    public:
        explicit UdpSyslogSink(RemoteConfigV2 config = {});           // 构造函数，配置 UDP 目标地址和 syslog 元数据。
        ~UdpSyslogSink() override;                                    // 析构时关闭 socket 与平台网络资源。
        void Write(const std::string &line) override;                 // 发送一条 syslog 文本到远端。
        void WriteWithLevel(const std::string &line, LogLevel level); // 按日志级别映射 syslog severity 并发送。
        void Flush() override;                                        // UDP 为无缓冲发送，Flush 为 no-op。

    private:
        std::string BuildPayload(const std::string &line, LogLevel level) const; // 构造 RFC5424 最小报文。
        static std::uint8_t SeverityFromLevel(LogLevel level);                   // 将 LogLevel 映射到 syslog severity。

        std::mutex mu_;
        std::string host_;
        std::uint16_t port_{514};
        std::uint8_t facility_{1};
        std::string app_name_{"logsys"};
        std::string hostname_{"-"};
        std::intptr_t socket_{-1};
        std::vector<std::uint8_t> endpoint_{};
        int endpoint_len_{0};
        bool ready_{false};
        bool network_initialized_{false};
    };

    using SinkPtr = std::shared_ptr<ISink>; // ISink 的智能指针类型定义，便于管理输出器对象的生命周期。

    class LevelRouter
    {
    public:
        void AddDefaultSink(SinkPtr sink);                  // 添加默认输出器的函数，接受一个 ISink 的智能指针，添加到默认输出器列表中。
        void AddLevelSink(LogLevel level, SinkPtr sink);    // 添加级别特定输出器的函数，接受一个日志级别和一个 ISink 的智能指针，将该输出器添加到指定级别的输出器列表中。
        std::vector<SinkPtr> Resolve(LogLevel level) const; // 根据日志级别解析出对应的输出器列表的函数，接受一个日志级别参数，返回一个包含所有适用于该级别的输出器的列表。
        void Clear();                                       // 清除所有路由配置的函数，移除所有默认输出器和级别特定输出器。

    private:
        std::vector<SinkPtr> defaults_;                     // 默认输出器列表，适用于所有日志级别。
        std::map<LogLevel, std::vector<SinkPtr>> by_level_; // 级别特定输出器映射，根据日志级别存储对应的输出器列表。
    };

    class Logger; // 前向声明 Logger 类，LogStreamBuilder 需要引用 Logger。

    class LogStreamBuilder
    {
    public:
        LogStreamBuilder(Logger &logger, LogLevel level, ErrorCode code, ErrorCategory category,
                         const char *file, std::uint32_t line, const char *function); // 构造函数，接受 Logger 引用、日志级别、错误代码、错误类别、文件路径、行号和函数名等参数，用于初始化日志事件构建器。
        ~LogStreamBuilder();

        template <typename T>
        LogStreamBuilder &operator<<(const T &v)
        {
            ss_ << v;
            return *this;
        } // 重载流插入运算符，允许使用类似于 std::ostringstream 的方式构建日志消息内容。

        LogStreamBuilder &SetField(std::string key, std::string value); // 设置扩展字段的成员函数，接受键值对参数，返回当前对象的引用以支持链式调用。

    private:
        Logger &logger_;        // 引用 Logger 对象，用于在析构时提交构建完成的日志事件。
        LogEvent event_;        // 日志事件对象，用于存储日志事件的各个字段。
        std::ostringstream ss_; // 内部使用的字符串流，用于构建日志消息内容。
    };

    class Logger
    {
    public:
        friend class LogStreamBuilder;

        static Logger &Instance(); // 获取全局 Logger 实例的静态函数。
        ~Logger();                 // 析构时停止后台线程并清理资源。

        void SetFormatter(std::shared_ptr<IFormatter> formatter); // 设置日志格式化器的函数，接受一个 IFormatter 的智能指针，用于指定日志事件的格式化方式。
        void SetLevel(LogLevel level);                            // 设置输出级别阈值的函数，接受一个日志级别参数，控制哪些日志会被发送到输出端（sink）。
        LogLevel Level() const noexcept;                          // 获取当前输出级别阈值的函数，返回一个日志级别值。
        void SetRecordLevel(LogLevel level);                      // 设置记录级别阈值的函数，接受一个日志级别参数，控制哪些日志事件会被接受进入日志系统进行处理。
        LogLevel RecordLevel() const noexcept;                    // 获取当前记录级别阈值的函数，返回一个日志级别值。

        void AddDefaultSink(SinkPtr sink);               // 添加默认输出器的函数，接受一个 ISink 的智能指针，添加到默认输出器列表中。
        void AddLevelSink(LogLevel level, SinkPtr sink); // 添加级别特定输出器的函数，接受一个日志级别和一个 ISink 的智能指针，将该输出器添加到指定级别的输出器列表中。

        void SetFlushOnFatal(bool enabled); // 设置是否在记录 Fatal 级别日志时自动刷新输出的函数，接受一个布尔值参数，控制该行为的启用与禁用。
        bool FlushOnFatal() const noexcept; // 获取当前是否在记录 Fatal 级别日志时自动刷新输出的函数，返回一个布尔值。

        void SetDefaultOrigin(ErrorSource source, ModuleId module, ErrorCategory category); // 设置默认错误来源、模块和类别的函数，接受一个错误来源、一个模块 ID 和一个错误类别参数，用于指定默认的日志事件来源信息。
        ErrorCategory DefaultCategory() const noexcept;                                     // 获取当前默认错误类别的函数，返回一个错误类别值。
        ErrorCode DefaultCodeForLevel(LogLevel level) const;                                // 根据日志级别获取默认错误代码的函数，接受一个日志级别参数，返回一个错误代码值。
        void ConfigureDefaultLogger(const DefaultLoggerOptions &options = {});              // 使用 DefaultLoggerOptions 配置默认 logger 的函数，接受一个 DefaultLoggerOptions 结构体参数，提供便捷的默认配置选项。
        void ConfigureSimpleLogger(LogLevel output_level = LogLevel::Fatal,
                                   bool enable_console = true,
                                   bool enable_file = false,
                                   std::string file_path = "app.log",
                                   LogLevel record_level = LogLevel::Info);                              // 使用简单参数配置默认 logger 的函数，接受多个参数用于快速设置输出级别、记录级别和输出选项。
        void ApplyConfigV2(const LoggerConfigV2 &config);                                                // 应用完整 LoggerConfigV2 配置的函数，接受一个 LoggerConfigV2 结构体参数，提供更灵活和全面的配置选项。
        const LoggerConfigV2 &CurrentConfigV2() const noexcept;                                          // 获取当前生效的 LoggerConfigV2 配置的函数，返回一个常量引用指向当前配置。
        bool LoadConfigV2FromJsonFile(const std::string &file_path);                                     // 从 JSON 文件加载 LoggerConfigV2 配置的函数，接受一个文件路径参数，返回一个布尔值表示加载是否成功。
        bool SetLevelFromString(std::string_view level_text);                                            // 从字符串设置输出级别的函数，接受一个字符串视图参数，返回一个布尔值表示解析和设置是否成功。
        bool SetLevelFromArgs(int argc, const char *const argv[], std::string_view key = "--log-level"); // 从命令行参数设置输出级别的函数，接受 argc 和 argv 参数以及一个可选的键参数，返回一个布尔值表示解析和设置是否成功。
        void SetOutputEnabled(bool enable_console, bool enable_file, bool enable_debugger);              // 同时设置多个输出选项的函数，接受三个布尔值参数，分别控制控制台、文件和调试器输出的启用与禁用。
        void EnableConsoleOutput(bool enabled);                                                          // 设置是否启用控制台输出的函数，接受一个布尔值参数，控制该行为的启用与禁用。
        void EnableFileOutput(bool enabled);                                                             // 设置是否启用文件输出的函数，接受一个布尔值参数，控制该行为的启用与禁用。
        void EnableDebuggerOutput(bool enabled);                                                         // 设置是否启用调试器输出的函数，接受一个布尔值参数，控制该行为的启用与禁用。
        void SetTextFieldMask(std::uint32_t mask);                                                       // 设置文本字段掩码的函数，接受一个 32 位无符号整数参数，控制输出哪些字段（位掩码）。
        std::uint32_t TextFieldMask() const noexcept;                                                    // 获取当前文本字段掩码的函数，返回一个 32 位无符号整数。
        void SetTextFieldEnabled(TextField field, bool enabled);                                         // 设置单个文本字段启用状态的函数，接受一个 TextField 枚举值和一个布尔值参数，控制该字段的启用与禁用。
        void SetAutoFillMissingMetadata(bool enabled);                                                   // 设置是否自动填充缺失元数据的函数，接受一个布尔值参数，控制该行为的启用与禁用。
        bool AutoFillMissingMetadata() const noexcept;                                                   // 获取当前是否自动填充缺失元数据的函数，返回一个布尔值。
        void StartPeriodicFlush(std::chrono::milliseconds interval);                                     // 启动定期刷新日志输出的函数，接受一个时间间隔参数，控制刷新频率。
        void StopPeriodicFlush();                                                                        // 停止定期刷新日志输出的函数。
        std::uint64_t DroppedByBackpressureCount() const noexcept;                                       // 获取因回压策略而被丢弃的日志事件数量的函数，返回一个 64 位无符号整数。
        void ResetBackpressureCountersForTestOnly();                                                     // 重置回压相关计数器的函数，仅供测试使用。

        // V2 async extension point: currently synchronous forwarding.
        void Enqueue(LogEvent event);     // 将日志事件加入处理队列的函数，接受一个 LogEvent 对象，目前实现为同步转发。
        void LogEventNow(LogEvent event); // 立即处理日志事件的函数，接受一个 LogEvent 对象，直接进行日志处理而不经过队列。

        template <typename... Args>
        void Logf(LogLevel level, ErrorCode code, ErrorCategory category,
                  const char *file, std::uint32_t line, const char *function,
                  std::uint32_t sys_code, const char *vendor_name, const char *vendor_code,
                  const char *fmt, Args... args) // 格式化日志事件并记录的函数模板，接受日志级别、错误代码、错误类别、文件路径、行号、函数名、系统错误代码、供应商名称、供应商代码以及格式字符串和参数，构建日志事件并提交处理。
        {
            if (level < record_level_.load(std::memory_order_relaxed))
            {
                return;
            }

            LogEvent event;
            event.timestamp = std::chrono::system_clock::now();
            event.level = level;
            event.code = code;
            event.category = category;
            event.file = file ? file : "";
            event.line = line;
            event.function = function ? function : "";
            event.sys_code = sys_code;
            event.thread_id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            event.vendor_name = vendor_name ? vendor_name : "";
            event.vendor_code = vendor_code ? vendor_code : "";

            char buffer[2048] = {0};
#if defined(_MSC_VER) // MSVC 的 std::snprintf 可能会触发安全警告，禁用该警告以避免编译问题。
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            std::snprintf(buffer, sizeof(buffer), fmt, args...);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            event.message = Sanitize(buffer);
            Enqueue(std::move(event));
        }

        template <typename... Args>
        void LogDefaultf(LogLevel level,
                         const char *file, std::uint32_t line, const char *function,
                         const char *fmt, Args... args) // 使用默认错误代码和类别格式化日志事件并记录的函数模板，接受日志级别、文件路径、行号、函数名以及格式字符串和参数，构建日志事件并提交处理，错误代码和类别由当前默认设置决定。
        {
            const auto code = DefaultCodeForLevel(level);
            const auto category = static_cast<ErrorCategory>(default_category_.load(std::memory_order_relaxed));
            Logf(level, code, category, file, line, function, 0, "", "", fmt, args...);
        }

        std::uint64_t GetErrorCountByCode(ErrorCode code) const;             // 根据错误代码获取对应的错误事件数量的函数，接受一个错误代码参数，返回一个 64 位无符号整数表示该错误代码的事件数量。
        std::uint64_t GetErrorCountByCategory(ErrorCategory category) const; // 根据错误类别获取对应的错误事件数量的函数，接受一个错误类别参数，返回一个 64 位无符号整数表示该错误类别的事件数量。

        // Test-only API guarded by LOGSYS_ALLOW_TEST_API=1 environment variable.
        // Returns false when reset is blocked.
        bool ResetErrorCountersForTestOnly(); // 重置错误计数器的函数，仅供测试使用，受环境变量 LOGSYS_ALLOW_TEST_API=1 控制，返回一个布尔值表示是否成功重置（当重置被阻止时返回 false）。
        void Flush();                         // 刷新所有输出器的函数，确保所有待处理的日志事件都被写入输出端。

    private:
        struct PendingOutputV2
        {
            LogLevel level{LogLevel::Info};
            std::string line;
            bool allow_console{true};
            bool allow_file{true};
            bool allow_debugger{true};
            bool color_console{false};
            bool light_theme_only{true};
        }; // PendingOutputV2: V2 版本的待输出日志事件结构，包含日志级别、格式化后的日志行以及输出选项。

        struct CodeBucket
        {
            std::atomic<std::uint32_t> key{0};
            std::atomic<std::uint64_t> count{0};
        }; // CodeBucket: 错误代码计数桶结构，包含一个原子变量存储错误代码（key）和一个原子变量存储该错误代码的事件数量（count）。

        static constexpr std::size_t kCodeBucketCount = 4096; // 错误代码桶的数量，用于分散错误代码计数以减少竞争。

        Logger();
        void IncrementCounters(ErrorCode code, ErrorCategory category); // 增加错误代码和类别计数的私有函数，接受一个错误代码和一个错误类别参数，更新对应的计数器。
        void ApplyRoutingLocked();                                      // 在持有锁的情况下应用路由配置的私有函数，更新输出器列表以匹配当前配置。
        void FlushGroupedOutputsLocked();                               // 在持有锁的情况下刷新分组输出的私有函数，处理并输出所有待输出日志事件。
        void StartAsyncWorker();                                        // 启动异步日志工作线程。
        void StopAsyncWorker();                                         // 停止异步日志工作线程并排空队列。
        static std::string Sanitize(std::string message);               // 静态函数，清理日志消息内容以移除或转义不安全的字符，接受一个字符串参数，返回处理后的字符串。

        mutable std::mutex mu_;                                                                          // 保护 Logger 内部状态的互斥锁，mutable 允许在 const 函数中修改。
        std::shared_ptr<IFormatter> formatter_;                                                          // 当前使用的日志格式化器，默认为 nullptr，表示使用默认格式化方式。
        LevelRouter router_;                                                                             // 日志级别路由器，根据日志级别确定输出器列表。
        DefaultLoggerOptions default_options_{};                                                         // 默认 logger 配置选项，提供便捷的默认设置。
        LoggerConfigV2 config_v2_{};                                                                     // 当前生效的 V2 配置，包含全局设置和配置档位列表。
        std::atomic<LogLevel> record_level_{LogLevel::Info};                                             // 记录级别阈值，控制哪些日志事件会被接受进入日志系统进行处理。
        std::atomic<LogLevel> level_{LogLevel::Fatal};                                                   // 输出级别阈值，控制哪些日志会被发送到输出端（sink）。
        std::atomic<bool> flush_on_fatal_{true};                                                         // 是否在记录 Fatal 级别日志时自动刷新输出。
        std::atomic<std::uint32_t> text_field_mask_{kTextFieldMaskDefault};                              // 文本字段掩码，控制输出哪些字段（位掩码）。
        std::atomic<bool> auto_fill_missing_metadata_{true};                                             // 是否自动填充缺失元数据。
        std::atomic<std::uint8_t> default_source_{static_cast<std::uint8_t>(ErrorSource::Business)};     // 默认错误来源，使用 uint8_t 存储以节省空间。
        std::atomic<std::uint8_t> default_module_{static_cast<std::uint8_t>(ModuleId::BusinessCommon)};  // 默认模块 ID，使用 uint8_t 存储以节省空间。
        std::atomic<std::uint8_t> default_category_{static_cast<std::uint8_t>(ErrorCategory::Business)}; // 默认错误类别，使用 uint8_t 存储以节省空间。
        std::atomic<bool> periodic_flush_running_{false};                                                // 定期刷新线程是否正在运行的标志。
        std::jthread periodic_flush_thread_{};                                                           // 定期刷新线程，使用 jthread 自动管理线程生命周期。
        std::atomic<std::uint64_t> pending_event_count_{0};                                              // 当前待处理日志事件的数量，用于回压策略。
        std::atomic<std::uint64_t> dropped_by_backpressure_{0};                                          // 因回压策略而被丢弃的日志事件数量。
        std::vector<PendingOutputV2> grouped_outputs_{};                                                 // 分组待输出日志事件列表，用于批量处理和输出。
        std::mutex async_mu_;                                                                            // 保护异步队列和工作线程状态。
        std::condition_variable async_cv_;                                                               // 通知异步工作线程有新事件。
        std::condition_variable async_drain_cv_;                                                         // 用于 Flush 等待异步队列排空。
        std::deque<LogEvent> async_queue_{};                                                             // 异步待处理事件队列。
        bool async_worker_stop_requested_{false};                                                        // 异步线程停止标志。
        bool async_worker_processing_{false};                                                            // 异步线程是否正在处理一个事件。
        std::thread::id async_worker_thread_id_{};                                                       // 异步线程 id，用于避免线程内 Flush 死锁。
        std::jthread async_worker_thread_{};                                                             // 异步日志工作线程。

        // Category counters are fixed-size atomics to avoid runtime map allocations.
        std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(ErrorCategory::Count)> category_counts_{}; // 错误类别计数数组，使用固定大小的原子变量存储每个错误类别的事件数量，避免运行时的 map 分配。
        std::array<CodeBucket, kCodeBucketCount> code_counts_{};                                                   // 错误代码计数数组，使用固定大小的 CodeBucket 结构存储错误代码和对应的事件数量，通过哈希分散错误代码以减少竞争。
    };

} // namespace logsys

#define LOGSYS_MAKE_ERROR_CODE(source, module, detail) \
    ::logsys::MakeErrorCode(static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(module), static_cast<std::uint16_t>(detail)) // 宏定义，简化错误代码的创建，接受错误来源、模块和细节参数，调用 MakeErrorCode 函数生成错误代码。

#define LOGSYS_ERROR_SOURCE(code) ::logsys::ErrorSourcePart(code) // 宏定义，从错误代码中提取错误来源部分，调用 ErrorSourcePart 函数。
#define LOGSYS_ERROR_MODULE(code) ::logsys::ErrorModulePart(code) // 宏定义，从错误代码中提取错误模块部分，调用 ErrorModulePart 函数。
#define LOGSYS_ERROR_DETAIL(code) ::logsys::ErrorDetailPart(code) // 宏定义，从错误代码中提取错误细节部分，调用 ErrorDetailPart 函数。

#define LOG_TRACE(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Trace, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Trace 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_DEBUG(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Debug, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Debug 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_INFO(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Info, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Info 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_WARNING(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Warning, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Warning 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_ERROR(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Error, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Error 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_FATAL(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Fatal, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Fatal 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_CRITICAL(code, category, fmt, ...) \
    ::logsys::Logger::Instance().Logf(::logsys::LogLevel::Critical, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, 0, "", "", fmt, ##__VA_ARGS__) // 宏定义，记录 Critical 级别日志，接受错误代码、错误类别、格式字符串和参数，调用 Logger 的 Logf 函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_TRACE_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Trace, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Trace 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_DEBUG_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Debug, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Debug 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_INFO_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Info, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Info 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_WARNING_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Warning, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Warning 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_ERROR_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Error, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Error 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_FATAL_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Fatal, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Fatal 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

#define LOG_CRITICAL_STREAM(code, category) \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Critical, code, category, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Critical 级别日志流构建器，接受错误代码和错误类别参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据。

// Simple macros for small projects. They use default source/module/category.
#define LOGT(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Trace, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Trace 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGD(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Debug, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Debug 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGI(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Info, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Info 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGW(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Warning, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Warning 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGE(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Error, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Error 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGF(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Fatal, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Fatal 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGC(fmt, ...) \
    ::logsys::Logger::Instance().LogDefaultf(::logsys::LogLevel::Critical, __FILE__, static_cast<std::uint32_t>(__LINE__), __func__, fmt, ##__VA_ARGS__) // 宏定义，记录 Critical 级别日志，接受格式字符串和参数，调用 Logger 的 LogDefaultf 函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGT_STREAM() \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Trace, ::logsys::Logger::Instance().DefaultCodeForLevel(::logsys::LogLevel::Trace), ::logsys::Logger::Instance().DefaultCategory(), __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Trace 级别日志流构建器，接受格式字符串和参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGD_STREAM() \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Debug, ::logsys::Logger::Instance().DefaultCodeForLevel(::logsys::LogLevel::Debug), ::logsys::Logger::Instance().DefaultCategory(), __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Debug 级别日志流构建器，接受格式字符串和参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGI_STREAM() \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Info, ::logsys::Logger::Instance().DefaultCodeForLevel(::logsys::LogLevel::Info), ::logsys::Logger::Instance().DefaultCategory(), __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Info 级别日志流构建器，接受格式字符串和参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGW_STREAM() \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Warning, ::logsys::Logger::Instance().DefaultCodeForLevel(::logsys::LogLevel::Warning), ::logsys::Logger::Instance().DefaultCategory(), __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Warning 级别日志流构建器，接受格式字符串和参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGE_STREAM() \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Error, ::logsys::Logger::Instance().DefaultCodeForLevel(::logsys::LogLevel::Error), ::logsys::Logger::Instance().DefaultCategory(), __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Error 级别日志流构建器，接受格式字符串和参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

#define LOGC_STREAM() \
    ::logsys::LogStreamBuilder(::logsys::Logger::Instance(), ::logsys::LogLevel::Critical, ::logsys::Logger::Instance().DefaultCodeForLevel(::logsys::LogLevel::Critical), ::logsys::Logger::Instance().DefaultCategory(), __FILE__, static_cast<std::uint32_t>(__LINE__), __func__) // 宏定义，创建 Critical 级别日志流构建器，接受格式字符串和参数，调用 LogStreamBuilder 构造函数，并自动填充文件路径、行号和函数名等元数据，使用默认错误代码和类别。

// Lowercase convenience API for tiny projects.
#define log_info(fmt, ...) LOGI(fmt, ##__VA_ARGS__)  // 宏定义，记录 Info 级别日志的简化版本，接受格式字符串和参数，调用 LOGI 宏。
#define log_warn(fmt, ...) LOGW(fmt, ##__VA_ARGS__)  // 宏定义，记录 Warning 级别日志的简化版本，接受格式字符串和参数，调用 LOGW 宏。
#define log_error(fmt, ...) LOGE(fmt, ##__VA_ARGS__) // 宏定义，记录 Error 级别日志的简化版本，接受格式字符串和参数，调用 LOGE 宏。
#define log_fatal(fmt, ...) LOGF(fmt, ##__VA_ARGS__) // 宏定义，记录 Fatal 级别日志的简化版本，接受格式字符串和参数，调用 LOGF 宏。

// Stream-style aliases; use as: log_debug << "x=" << v << std::endl;
#define log_debug LOGD_STREAM()           // 宏定义，创建 Debug 级别日志流构建器的简化版本，接受格式字符串和参数，调用 LOGD_STREAM 宏。
#define log_trace LOGT_STREAM()           // 宏定义，创建 Trace 级别日志流构建器的简化版本，接受格式字符串和参数，调用 LOGT_STREAM 宏。
#define log_info_stream LOGI_STREAM()     // 宏定义，创建 Info 级别日志流构建器的简化版本，接受格式字符串和参数，调用 LOGI_STREAM 宏。
#define log_warn_stream LOGW_STREAM()     // 宏定义，创建 Warning 级别日志流构建器的简化版本，接受格式字符串和参数，调用 LOGW_STREAM 宏。
#define log_error_stream LOGE_STREAM()    // 宏定义，创建 Error 级别日志流构建器的简化版本，接受格式字符串和参数，调用 LOGE_STREAM 宏。
#define log_critical_stream LOGC_STREAM() // 宏定义，创建 Critical 级别日志流构建器的简化版本，接受格式字符串和参数，调用 LOGC_STREAM 宏。
