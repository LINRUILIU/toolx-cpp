# ToolX 一级 API 盘点与风险审计

本文盘点 ToolX `0.3.x` 的一级 C++ API 边界。目标不是逐行解释源码，而是把产品和后续维护会依赖的公开接口、稳定承诺、副作用、错误模型、默认行为、当前风险点和补测候选记录清楚。

## 全局口径

- API 层不直接承诺 CLI exit code。`argtool::ParseResult::exit_code` 和各产品 CLI 的 exit code 只能作为调用方或工具层约定，库 API 本身以结构化结果、状态、布尔值、异常或回调表示错误。
- `0.3.x` 不承诺 ABI 稳定。Stable 模块承诺 public header 中既有名称保持源码可调用，已测字段语义和已文档化默认行为不破坏，除非安全或正确性修复需要 breaking change。
- `Stable` 表示可作为普通小工具和中小型 C++20 项目的稳定源码依赖。
- `Bounded stable` 表示 API 可用，但部分行为受 TLS backend、平台、系统配置、环境变量或可插拔回调影响。
- `Experimental` 表示当前 MVP 可用，但不承诺长期框架级兼容，也不承诺完整标准覆盖。

## `argtool`

**公开接口**

- 主要类型：`ValueType`、`RepeatMode`、`BoolFlagMode`、`RangePolicy`、`ParseErrorKind`、`RulePriority`、`RuleGroup`、`ValueCardinality`、`HelpLayout`。
- 主要结构体：`ParseError`、`ConstraintContext`、`ConstraintResult`、`ConstraintRule`、`ConvertResult`、`TraceEvent`、`SubcommandPath`、`OptionTemplate`、`PositionalTemplate`、`ParseResult`、`MutexGroup`、`DependencyRule`。
- 主要类和函数面：`IParseLogger`、`SubcommandRouter`、`SubcommandTree`、`Parser`、`Parser::OptionBuilder`、`Parser::PositionalBuilder`，以及 `Parser::Parse`、`HelpText`、`ResultToJson`、subcommand 注册和 dispatch API。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：builder/template 声明方式、`ParseResult` 基本字段、结构化 `ParseErrorKind`、约束管线顺序语义、help layout 选择、两层 subcommand path、JSON parse-result 的 `schema`/`schema_version` 和既有字段语义。

**副作用边界**

- 不读写文件，不访问网络，不启动线程，不依赖环境变量或 CWD。
- 可调用用户提供的 `IParseLogger`、`ValueConverter`、`ConstraintRule::Evaluator`、`UnknownOptionHandler`、`SubcommandRouter::Handler`，这些回调的副作用由调用方负责。

**错误模型**

- 解析错误通过 `ParseResult{ok=false, error=ParseError}` 返回。
- parser 配置错误通过 `std::invalid_argument` 抛出，主要发生在 builder `Done()`、模板导入、互斥/依赖/约束/转换器注册阶段。
- `SubcommandRouter::Dispatch` 返回 handler 的 `int`，并可通过 `std::string* error` 带出错误。

**默认行为与魔法行为**

- `RepeatMode::Override`、`BoolFlagMode::Switch`、`RangePolicy::Fail` 是默认语义。
- `RangePolicy::UseDefaultAndWarn` 会把越界值替换成默认值，并通过 `IParseLogger::OnWarning` 报告。
- `-h`/`--help` 是内置 help 请求；启用 legacy profile 后 `-?` 也会请求 help。
- 默认值会参与类型转换和 choices/range 校验，解析结果统一存入 `values` 的字符串列表。
- `UnknownOptionHandler` 可以吞掉未知选项，使原本应失败的输入成功。

**当前风险/不稳定点**

- `ParseResult::exit_code` 容易被误读为库层 CLI exit code contract，后续文档和调用方应强调它只是建议值。
- 配置期异常和运行期 `ParseResult` 两套错误模型并存，调用方需要明确 try/catch 边界。
- unknown handler、global/local converter 和自定义 constraint 都能改变解析语义，容易制造不可见的产品差异。
- `UseDefaultAndWarn` 是有意的自动纠偏，但如果调用方没有 logger，会变成静默回退。

**后续测试或修复候选**

- 补充“没有 logger 时 `UseDefaultAndWarn` 是否足够可观察”的契约测试或文档示例。
- 为 `ParseResult::exit_code` 增加 API 文档注释，明确不等同 CLI exit code。
- 补 unknown handler 与 trace/JSON 输出的组合测试，确认被吞掉的 token 是否可审计。
- 补 converter 抛异常或返回错误时的行为测试，避免自定义转换器导致未记录失败。

## `cfgx`

**公开接口**

- 主要类型：`Result<T>`、`Status`、`ConfigFormat`、`NodeKind`、`SourceLayer`、`DiffKind`、`PathTokenKind`。
- 主要结构体：`SourceAttribution`、`TypePolicyOptions`、`ComposeOptions`、`DiffEntry`、`ReloadOptions`、`RemoteFetchRequest`、`RemoteFetchResponse`、`ParserAdapter`、`ReloadEvent`、`SnapshotAuditEntry`、`PathToken`、`ValidationIssue`、`ValidationRule`。
- 主要类和函数面：`Node`、`RuntimeOverrides`、`PollReloader`、path API、merge/layer API、validation rules、parser adapter registry、remote fetcher registry、JSON/INI/YAML/TOML load-save、snapshot import/export、lightweight encrypted persistence。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：`Node` value model、dot/index path grammar、JSON/INI load-save、validation rule helpers、layer precedence、polling reload and rollback semantics、snapshot/audit fields、parser adapter registry API、remote fetch callback shape。

**副作用边界**

- `LoadFromFile`、`SaveToFile`、encrypted load/save、snapshot import/export、`PollReloader` 会读写文件。
- `BuildEnvLayerFromEnvironment` 和 `PollReloader::ReloadNow` 默认读取进程环境变量。
- `LoadFromRemote` 不直接内置网络传输，但会调用全局 `RemoteFetcher` 回调；该回调可能访问网络。
- parser adapter registry 和 remote fetcher 是进程级全局状态。
- 相对路径由调用方传入，实际解析依赖当前工作目录。

**错误模型**

- 大多数 API 使用 `cfgx::Result<T>{ok,value,error}` 或 `cfgx::Status{ok,error}`。
- `Node::AsBool`、`AsInt`、`AsDouble`、`AsString` 使用 fallback，不暴露错误。
- `Exists` 返回 `bool`。

**默认行为与魔法行为**

- `DetectFormatFromPath` 会根据路径或 URL 后缀自动选择格式；`Unknown` 会导致 load/save 失败。
- YAML/TOML 是实用子集，不是完整规范实现。
- 环境层默认前缀是 `APP_CFG_`，环境值默认会经过 scalar 推断，除非 strict string policy 命中。
- `ComposeLayers` 的数组默认覆盖，只有 `append_arrays=true` 时追加。
- `ReloadOptions` 默认 `allow_remote_failure=true`、`allow_missing_local=true`、`allow_missing_base=false`、`debounce_ms=200`、`include_snapshots=false`。
- `Node::SetAt` 默认 `auto_expand=true`，越界设置会扩展数组。
- active parser adapter parse/dump 失败时会 fallback 到内建 parser/dumper。
- JSON Unicode escape 只完整覆盖简单场景，非 ASCII Unicode escape 有历史 fallback 行为。

**当前风险/不稳定点**

- 全局 parser adapter 和 remote fetcher 让测试和产品运行之间可能互相污染，需要明确生命周期。
- 自动格式检测、adapter fallback、scalar 推断和 `As*` fallback 都可能掩盖配置输入错误。
- YAML comment preserve 只覆盖简单 scalar mapping，结构性改写会 fallback 到完整 dump，可能改变格式和注释。
- remote failure 默认允许，新用户可能以为远程配置一定参与合成。
- 环境变量层默认参与 `PollReloader::ReloadNow`，在不同运行环境下结果可能不同。

**后续测试或修复候选**

- 补 parser adapter registry 和 remote fetcher 的并发/隔离测试，或标注非线程安全全局状态。
- 补 `PollReloader` 在默认环境变量存在时的 determinism 测试。
- 为 adapter fallback 增加可观测 warning 或显式选项，避免静默使用内建 parser。
- 补 YAML/TOML 子集边界测试，特别是注释保留失败、混合 mapping/sequence、Unicode escape。
- 补 `allow_remote_failure=true` 的产品级示例，明确 remote layer 缺失时是否应 fail closed。

## `fsx`

**公开接口**

- 主要类型：`RollbackMode`、`ConflictPolicy`、`OpType`、`DirectoryDiffKind`、`LinkType`、`WatchEventKind`。
- 主要结构体：`StepReport`、`RunOptions`、`RunResult`、`RecoverOptions`、`Status`、`WalkEntry`、`WalkOptions`、`WalkResult`、`DirectoryDiffEntry`、`DirectoryDiff`、`ArchiveOptions`、`WatchEvent`、`WatchPollResult`、`CapabilityInfo`。
- 主要类和函数面：`BatchPlan`、`IFileWatcher`、`Run`、`RecoverFromJournal`、`WalkDirectory`、`BuildDirectoryDiff`、`BuildSyncPlan`、`CreateArchive`、`ExtractArchive`、`CreateLink`、`CreateFileWatcher`、`QueryCapabilities`。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：batch plan action names、run/journal recovery reports、rollback counts、conflict policy semantics、directory walk/sync behavior、deterministic tar MVP、polling watcher and capability reporting。

**副作用边界**

- 会创建父目录、写临时文件、替换文件、rename、copy、递归复制、删除文件或目录树、创建 hard/symbolic link、创建/读取/删除 journal、创建/解压 tar archive。
- watcher 是轮询实现，会读取目标路径状态。
- 不访问网络；不会代表调用方启动长期后台线程。
- 相对路径依赖当前工作目录。

**错误模型**

- `Run` 和 `RecoverFromJournal` 返回 `RunResult`。
- walk/diff 返回 `WalkResult`/`DirectoryDiff`。
- archive/link 返回 `Status`。
- watcher poll 返回 `WatchPollResult`。

**默认行为与魔法行为**

- `RunOptions::conflict_policy` 默认 `Overwrite`，`overwrite_existing` 是 legacy 兼容字段。
- `RunOptions::rollback_mode` 默认 `BestEffort`，失败回滚不保证完全恢复。
- `RunOptions::keep_journal_on_success=false`，成功后会清理 journal。
- `RecoverOptions::cleanup_journal_on_success=true`，恢复成功后会清理 journal。
- `BuildSyncPlan` 默认 `remove_extra=true`，会为目标侧多余路径生成删除动作。
- tar archive 是 deterministic MVP；`QueryCapabilities().zip_archive=false`。

**当前风险/不稳定点**

- 默认 overwrite 和 sync remove_extra 对文件系统 blast radius 大，产品层必须显式 dry-run 或确认边界。
- BestEffort rollback 可能留下部分变更，`RunResult` 需要被调用方持久化或展示。
- 成功清理 journal 是合理默认，但会减少事后诊断证据。
- polling watcher 语义受文件系统 mtime 精度影响，短时间连续变化可能合并。
- symlink 能力平台相关，capability reporting 必须参与产品决策。

**后续测试或修复候选**

- 补 `Run` 在 journal 写失败时的可观察性测试。
- 补 `BuildSyncPlan(remove_extra=true)` 对目录树删除的保护测试，例如拒绝空 root 或同路径 root。
- 补 mtime 精度/快速连续写入的 watcher 测试或文档限制。
- 补 Strict rollback 失败路径和 nested directory rollback 场景。
- 为 destructive actions 增加更强的 path boundary helper 或产品层检查建议。

## `asyncx`

**公开接口**

- 主要类型：`ErrorKind`、`StopMode`、`BackpressurePolicy`、`TaskPriority`。
- 主要结构体：`TaskOptions`、`TaskGroupStats`、`Error`、`Status`、`Result<T>`、`PoolOptions`、`Stats`、`SchedulerStats`、`MetricsSnapshot`。
- 主要类和函数面：`CancellationToken`、`CancellationSource`、`ThreadPool`、`TaskGroup`、`WaitAll`、`WaitAllFor`、`WaitAny`、`WaitAnyFor`、`WaitAnyUntil`。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：thread pool lifecycle、post/submit family、wait helpers、scheduled task IDs、metrics fields、priority queue order intent、cooperative cancellation、task group stats、backpressure policy semantics。

**副作用边界**

- `ThreadPool` 会启动 worker 和 scheduler 线程；默认 `PoolOptions::start_immediately=true`，构造即可启动。
- `Wait*` helper 会阻塞当前线程；部分 wait-any 实现轮询 sleep。
- 不直接读写文件、访问网络、使用环境变量或全局 logger。
- 用户提交的 task 可以有任意副作用。

**错误模型**

- lifecycle/post/wait API 返回 `asyncx::Status`。
- submit/schedule API 返回 `asyncx::Result<T>`。
- `future` 中的用户任务异常由调用方在 `future::get()` 时处理。

**默认行为与魔法行为**

- `PoolOptions::worker_count=0` 会自动使用 hardware concurrency 相关默认。
- `BackpressurePolicy::Block` 默认会在队列满时阻塞调用方。
- `StopMode::Drain` 默认会执行已排队任务后停止。
- deadline 只约束入队或等待，不会强制中断已运行 task。
- cancellation 是协作式；正在运行的 task 必须主动检查 token。
- `SubmitWithOptions` 对预取消 token 会让 packaged task 抛 runtime error。

**当前风险/不稳定点**

- 构造即启动线程对库消费者来说有隐藏资源成本。
- Block backpressure 在调用方线程、回调或 UI loop 中使用时可能造成卡死。
- cooperative cancellation 和 deadline 语义容易被误解为强制取消。
- scheduler 和 worker 共享 pool lifecycle，Stop/Join 时序需要严格测试。

**后续测试或修复候选**

- 补 `start_immediately=false` 的生命周期矩阵测试。
- 补 submit task 抛异常时 stats 分类和 `TaskGroup` outcome 测试。
- 补 deadline 到期但 task 已运行时的明确行为测试。
- 补 backpressure Block 在 Stop/CancelPending 时解除阻塞的更多并发测试。
- 文档中强调 UI/日志回调内使用 `Post` 的 deadlock 风险。

## `logsys`

**公开接口**

- 主要类型：`LogLevel`、`ErrorCategory`、`ActionHint`、`ErrorSource`、`ModuleId`、`TextField`、`OutputOrderMode`、`RollingTimeMode`、`FatalPolicy`、`ErrorCode`。
- 主要结构体：`DefaultLoggerOptions`、`RollingConfigV2`、`ScheduleConfigV2`、`RenderConfigV2`、`BackpressureConfigV2`、`RemoteConfigV2`、`ProfileConfigV2`、`LoggerConfigV2`、`ResolvedProfileV2`、`ExtField`、`LogEvent`、`LogContext`、`LoggerMetricsSnapshot`、`ErrorDictionaryEntry`。
- 主要类和函数面：`ProfileResolverV2`、`ErrorDictionary`、`IFormatter`、`TextFormatter`、`JsonFormatter`、`ISink`、`ConsoleSink`、`FileSink`、`DebuggerSink`、`UdpSyslogSink`、`LevelRouter`、`LogStreamBuilder`、`Logger`、`TraceSpan`、日志宏和简化宏。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：logger configuration API、default/simple setup、sink and formatter interfaces、structured context fields、trace spans、metrics snapshot fields、async queue behavior、rolling config、JSON config V2 keys、fatal flush policy。

**副作用边界**

- 使用进程级全局 `Logger::Instance()` 和 `ErrorDictionary::Instance()`。
- console/debugger/file/UDP sink 会输出到对应外部系统。
- `FileSink` 会创建、写入、刷新、轮转、删除历史日志文件。
- `UdpSyslogSink` 会进行 UDP 网络发送。
- `StartPeriodicFlush` 和 async queue 会启动线程。
- `ResetErrorCountersForTestOnly` 依赖环境变量 `LOGSYS_ALLOW_TEST_API=1`。

**错误模型**

- 大多数 logger 配置和写入 API 返回 `void` 或 `bool`。
- sink 接口本身不返回错误；文件/UDP 写失败主要通过内部状态或丢弃行为体现。
- `LoadConfigV2FromJsonFile` 返回 `bool`。
- fatal policy 可在 `AbortAfterFlush` 下调用 `std::abort()`。

**默认行为与魔法行为**

- `DefaultLoggerOptions::level` 默认 `Fatal`，`record_level` 默认 `Info`，所以默认可能记录但不输出低级别日志。
- `LoggerConfigV2::fatal_policy` 默认 `FlushOnly`，不会 abort。
- `BackpressureConfigV2` 默认队列高水位后丢弃低于 `Info` 的日志。
- `ConfigureDefaultLogger`、`ConfigureSimpleLogger` 会重置路由和全局 logger 状态。
- JSON config loader 是轻量字段提取，不是完整 JSON schema 验证。
- `TraceSpan` 析构时写日志事件，RAII 作用域会产生隐式输出。

**当前风险/不稳定点**

- 全局 logger 状态让测试和库消费者容易互相影响。
- 默认 record/output 阈值不一致，容易误判日志“丢失”。
- 文件滚动会删除旧文件，保留数和时间轮转行为需要被产品层显式说明。
- UDP syslog 是 best-effort 风格，调用方不能依赖可靠投递。
- `AbortAfterFlush` 是强副作用配置，库消费者如果从配置文件启用会导致进程退出。
- config V2 轻量解析可能接受或忽略调用方以为会被严格校验的字段。

**后续测试或修复候选**

- 补全局 logger reset fixture 或测试隔离 helper，降低跨测试污染。
- 补 `AbortAfterFlush` 的隔离进程测试或明确禁止普通单测触发。
- 补 FileSink rolling 删除边界测试，包括 keep_recent_files 为 0 或极小值。
- 补 config V2 unknown/malformed field 行为测试，决定是严格失败还是忽略。
- 补 UDP sink 初始化失败和发送失败的可观察性测试。

## `resultx`

**公开接口**

- 主要类型：`ErrorDomain`、`ErrorKind`、`Error`、`Status`、`Result<T>`，来自 `sysx`。
- 主要函数：`MakeError`、`OkStatus`、`MapAsyncxErrorKind`、`MapHttpxErrorKind`、`FromSysx`、`FromCfgx`、`FromFsx`、`FromAsyncx`、`FromHttpx`、`FormatError`、`Propagate<T>`。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：跨模块 status/result adapter 的可调用性、基础 error kind 映射、`FormatError` 的 domain/kind/native code 信息。

**副作用边界**

- 不读写文件，不访问网络，不启动线程，不使用全局 logger，不依赖环境变量或 CWD。
- 只做值转换和错误归一化。

**错误模型**

- 输出统一为 `sysx::Status` 或 `sysx::Result<T>`。
- 源模块错误被映射到较粗的 `sysx::ErrorKind`/`ErrorDomain`。

**默认行为与魔法行为**

- `FromCfgx` 默认把错误映射为 `InvalidArgument/System`。
- `FromFsx` 默认把错误映射为 `Internal/System`。
- `FromHttpx` 默认使用 `Network` domain，并把 HTTP status 放入 `native_code`。
- `Propagate<T>` 成功时返回默认构造的 `T{}`。

**当前风险/不稳定点**

- 归一化会丢失部分源模块细节，例如 `cfgx::Status::error` 只有字符串。
- 默认 error kind 可能不适合所有调用场景，调用方需要显式传 kind/domain。
- `Propagate<T>` 对不可默认构造类型不可用，对默认值语义敏感。

**后续测试或修复候选**

- 补所有源模块 error kind 映射表测试，避免新增 kind 后落入 `Internal`。
- 补 `FromCfgx`/`FromFsx` 显式 kind/domain 的调用示例。
- 评估是否需要保留 source domain string 或 source module tag。

## `utils` / `sysx` / `hashx` / `textcodec`

**公开接口**

- `utils`：`ParseResult<T>`、`Status`、`err`、`str`、`time`、`parse`、`path`、`hash` 命名空间。
- `sysx`：平台/编译器检测、`ErrorDomain`、`ErrorKind`、`Error`、`Status`、`Result<T>`、system/network error helpers、`time`、`sync`、`thread::Thread`。
- `hashx`：`Fnv1a32State`、`Fnv1a64State`、`Crc32State`、`Adler32State` 和对应 bytes/string hash 函数。
- `textcodec`：`DecodeError`、`Base64Variant`、URL encode/decode policy、`DecodeResult<T>`、hex/base64/url encode/decode 函数。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Stable`。
- 不可破坏项：常用字符串/时间/parse/path helper、`sysx` 统一错误模型、thread/time wrapper、现有非加密 hash 函数、hex/base64/url codec 选项和错误码。

**副作用边界**

- 大多数 API 纯计算。
- `utils::path::file_exists` 读取文件系统状态。
- `utils::path::ensure_parent_dir` 会创建父目录。
- `sysx::LastSystemError`/`LastNetworkError` 读取平台 last error。
- `sysx::time::SleepFor`/`SleepUntil` 阻塞当前线程。
- `sysx::thread::Thread` 包装并启动 `std::thread`。

**错误模型**

- `utils::parse` 使用 `utils::ParseResult<T>`。
- `utils::path::ensure_parent_dir` 使用 `utils::Status`。
- `sysx` 使用 `Status`/`Result<T>` 和结构化 `Error`。
- `hashx` 和大多数 encode 函数直接返回值；decode API 返回 `DecodeResult<T>`。

**默认行为与魔法行为**

- `utils::str::split` 默认保留空字段，`skip_empty=true` 才跳过。
- text display width helper 对 invalid UTF-8/GBK 有 byte-wise fallback。
- `textcodec::Base64Options` 默认 standard variant 且带 padding。
- URL decode 默认保留 `+`，只有 `PlusAsSpace` 才把 `+` 变成空格。

**当前风险/不稳定点**

- `hashx`/`utils::hash` 是非加密 hash，不能用于密码、签名或安全 token。
- `textcodec` 不是完整字符集转换套件，不覆盖 UTF-16/GBK 通用转码。
- display width 算法是实用估算，终端字体和 emoji 宽度可能不同。
- `ensure_parent_dir` 名字看似 helper，但实际会修改文件系统。

**后续测试或修复候选**

- 补 `ensure_parent_dir` 对根路径、空 parent、权限失败的跨平台测试。
- 补 hash 非加密说明到 header 或 README。
- 补 textcodec policy 组合冲突和 buffer capacity 边界测试。
- 补 display width 对 emoji、组合字符、East Asian Ambiguous 字符的测试或明确非目标。

## `httpx`

**公开接口**

- 主要类型：`HttpMethod`、`ErrorKind`、`LogSeverity`、`DnsStrategy`。
- 主要结构体：`Error`、`Status`、`Result<T>`、`TimeoutOptions`、`RedirectOptions`、`ProxyOptions`、`PoolOptions`、`TlsOptions`、`Limits`、`MultipartPart`、`Request`、`Response`、`FailureStats`、`RetryPolicy`、`CircuitBreakerOptions`、`CircuitSnapshot`、`DownloadOptions`、`LogEvent`、`ClientOptions`、`FlatConfigEntry`。
- 主要类和函数面：`Client`、`Client::Send`、verb helpers、`DownloadFile`、`UploadFile`、`ApplyFlatConfig`、stats/circuit snapshot、redaction helpers、flat config parser。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Bounded stable`。
- 不可破坏项：request/response model、verb helpers、redirect/retry/circuit/download/upload/proxy config shape、error classification covered by tests、redaction helper behavior。
- 边界：HTTPS 行为依赖 `HTTPX_ENABLE_OPENSSL` 或 `HTTPX_ENABLE_MBEDTLS`，且只应启用一个 TLS backend。

**副作用边界**

- `Client::Send` 和 verb helpers 会访问网络，除非调用方提供 custom `transport`。
- 默认 `use_proxy_from_environment=true`，会读取 proxy 环境变量。
- `DownloadFile` 会写 `output_path + temp_suffix`，成功后替换目标文件。
- `UploadFile` 会从磁盘读取整个文件作为 multipart part。
- retry delay 会阻塞当前线程。
- logger 是 `ClientOptions::logger` 回调，不使用 `logsys` 全局 logger。

**错误模型**

- API 使用 `httpx::Status` 和 `httpx::Result<T>`，错误含 `ErrorKind`、`http_status`、`retryable`、`message`。
- circuit open 返回 `ErrorKind::CircuitOpen`。
- TLS/backend 缺失返回 transport/TLS 类错误，不抛异常。

**默认行为与魔法行为**

- timeout 默认 connect 5s、read/write 30s、total 60s。
- redirect 默认不 follow。
- TLS 默认 verify peer/host。
- DNS 默认 `HappyEyeballs`。
- user agent 默认 `httpx/0.1`。
- sensitive data redaction 默认开启。
- `RetryPolicy::max_attempts=0` 时走 legacy `max_retry_attempts`；非 0 表示总 Send attempts，包含第一次。
- transient retry 默认开启，但受 attempts 上限约束。

**当前风险/不稳定点**

- TLS backend 差异会导致同一 API 在不同构建上表现不同；mbedTLS peer verification 当前要求 CA file。
- 默认 proxy-from-env 让测试和生产行为受环境影响。
- retry attempts 的 legacy/new 字段并存，容易配置误解。
- `DownloadFile` 替换目标文件，失败时 temp 文件和已有目标的状态需要被调用方理解。
- `UploadFile` 读整文件，不适合大文件 streaming。
- `httpx` 不是 `curl` 替代，不覆盖完整 CLI/协议/认证生态。

**后续测试或修复候选**

- 补 no-proxy/环境变量优先级和大小写变量名测试。
- 补 TLS backend capability reporting 或 runtime introspection 文档。
- 补 retry legacy/new 字段冲突时的优先级测试。
- 补 `DownloadFile` 失败路径 temp 文件清理和 overwrite=false 测试。
- 补 upload 大文件限制文档或 streaming 非目标说明。

## `schemax`

**公开接口**

- 主要结构体：`Issue`、`Options`。
- 主要类和函数面：`Schema`、`Compile`、`Validate`、`ToCfgxIssues`。
- 支持的 MVP keyword：`type`、`required`、`properties`、`items`、`minimum`、`maximum`、`enum`、`minLength`、`maxLength`、`additionalProperties`。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Experimental`。
- 不可破坏项：当前 MVP public names、supported keyword 的基本含义、`Issue{path,code,message}` shape、`Options::fail_fast`、`ToCfgxIssues` 转换入口。

**副作用边界**

- 不读写文件，不访问网络，不启动线程，不读环境变量，不使用全局 logger。
- 基于 `cfgx::Node` 做纯内存 compile/validate。

**错误模型**

- `Compile` 返回 `cfgx::Result<Schema>`，schema 本身 malformed 或 unknown keyword 会失败。
- `Validate` 返回 `std::vector<Issue>`；validation issue 是业务数据，不是 transport error。
- `ToCfgxIssues` 返回 `std::vector<cfgx::ValidationIssue>`。

**默认行为与魔法行为**

- `Options::fail_fast=false` 默认收集多个 issue。
- unknown keyword 在 compile 阶段直接失败，不会像部分 JSON Schema 实现那样忽略。
- root-only issue path 使用 `$`，其他路径使用 cfgx dot/index notation。

**当前风险/不稳定点**

- 不是完整 JSON Schema，不支持 `$ref`、combinators、format、pattern、draft selection。
- issue code taxonomy 只对 MVP codes 稳定，不承诺长期完整分类。
- unknown keyword fail-fast 对 typo 友好，但对标准 JSON Schema 文档兼容性差。

**后续测试或修复候选**

- 补 nested array/object path 稳定性测试。
- 补 `additionalProperties=true/false` 和缺省值语义测试。
- 补 enum 对不同 scalar 类型的比较测试。
- 补 compile error message 断言，确保产品 CLI 能给出可操作错误。
- 如果未来支持 `$ref`，先新增 feature flag 或新稳定性分类，避免误破 MVP。

## `tuix`

**公开接口**

- 主要类型：`Color`、`Position`、`TerminalSize`、`CursorStyle`、`Key`、`KeyModifier`、`MouseButton`、`MouseAction`、`EventType`、`PollStatus`、`InputConsumeMode`、`InputConsumeSupport`。
- 主要结构体：`KeyEvent`、`MouseEvent`、`ResizeEvent`、`InputEvent`、`PollResult`、`InputOptions`、`FrameCell`、`Rect`、`Insets`、`CellStyle`、`Theme`。
- 主要类和函数面：`InputSource`、`FrameBuffer`、`Widget`、`Layout`、`VerticalLayout`、`HorizontalLayout`、`Label`、`Panel`、`TextInput`、`ListView`、`Button`、`Terminal`、`Application`、`CreateConsoleInputSource`、`CreateStreamInputSource`。

**稳定性分类与 0.3.x 不可破坏项**

- 分类：`Experimental`。
- 不可破坏项：terminal primitives、frame buffer diff rendering、poll-only input abstraction、theme/styled cells、basic gap/padding/flex layout、MVP widgets used by tests/examples。

**副作用边界**

- `Terminal` writes escape sequences or Win32 console calls to the provided stream/console backend。
- `CreateConsoleInputSource` reads console input；`CreateStreamInputSource` reads the supplied stream。
- `Application::Run`/`Tick` drives input polling, widget event handling and rendering。
- 不访问网络，不读写普通文件，不使用全局 logger。
- input polling 的轮询路径可能短暂 sleep。

**错误模型**

- terminal primitives 通常返回 `bool`。
- input polling 返回 `PollResult{status,event,message}`。
- widget event handling 用 `bool` 表示 handled/unhandled。
- `Application::Run` 返回 `int` loop result，但这不是产品 CLI exit code contract。

**默认行为与魔法行为**

- `InputOptions` 默认 exclusive consume、mouse enabled、resize events enabled。
- `TeeBack` 明确是 experimental，可能降级为 exclusive consume，并通过 message 说明。
- `PeekOnly` 在 stream rewind 不可用时可能降级。
- `FrameBuffer::Put(..., display_width=0)` auto-estimates display width。
- `TextInput` editing is byte-oriented and intended for ASCII command/config input。
- `Terminal` backend 依赖 ANSI 启用状态和平台 console 支持。

**当前风险/不稳定点**

- 不是稳定 TUI framework；没有完整 retained UI tree、advanced widgets 或长期 event model。
- terminal size, cursor, color and input behavior are platform/backend dependent。
- TextInput 字节级编辑会破坏用户对多字节字符编辑的预期。
- `TeeBack`/`PeekOnly` degraded modes may surprise callers that assume non-consuming reads。
- focus traversal and hit testing are MVP behavior, not a full framework contract。

**后续测试或修复候选**

- 在可行范围内补 Windows console backend capability 测试。
- 补 TextInput 多字节输入行为测试，即使期望结果是“不支持”。
- 补 `TeeBack` degraded mode in `Application` flow, not only raw input source。
- 补 layout rounding/flex edge cases for zero-size bounds and many children。
- 补 terminal backend fallback 文档，避免调用方做框架级假设。

## API 边界矩阵

| 模块 | 稳定性 | 错误模型 | 读文件 | 写文件/建目录 | 删除/替换 | 网络 | 线程 | 全局状态/logger | 环境/CWD |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `argtool` | Stable | `ParseResult` + 配置期异常 + dispatch `int` | 否 | 否 | 否 | 否 | 否 | 可调用 `IParseLogger` | 无环境/CWD |
| `cfgx` | Stable | `Result<T>` / `Status` / fallback bool/value | 是 | 是 | 写入覆盖文件 | 通过 remote fetcher 回调 | 否 | parser adapter 和 remote fetcher 全局状态 | 读环境变量；相对路径依赖 CWD |
| `fsx` | Stable | `RunResult` / `Status` / poll result | 是 | 是 | 是，含递归删除和替换 | 否 | 否 | 无 | 相对路径依赖 CWD |
| `asyncx` | Stable | `Status` / `Result<T>` / `future` 异常 | 否 | 否 | 否 | 否 | 是 | 无 | 无环境/CWD |
| `logsys` | Stable | 多数 `void`/`bool`，sink 错误有限可见 | 可读 config | 是 | rolling 会删除/rename | UDP syslog 可联网 | 是 | `Logger::Instance()` | `LOGSYS_ALLOW_TEST_API`；相对 file path 依赖 CWD |
| `resultx` | Stable | `sysx::Status` / `sysx::Result<T>` | 否 | 否 | 否 | 否 | 否 | 无 | 无环境/CWD |
| `utils/sysx/hashx/textcodec` | Stable | helper result/status 或直接返回值 | `file_exists` | `ensure_parent_dir` | 否 | 否 | `sysx::thread` 可启动线程 | 无 | path helper 相对路径依赖 CWD |
| `httpx` | Bounded stable | `Status` / `Result<T>` | upload 读文件 | download 写 temp/final | download 替换目标 | 是 | retry delay 阻塞，不自建 worker | client logger 回调，无全局 logger | 默认读 proxy env；相对 file path 依赖 CWD |
| `schemax` | Experimental | `cfgx::Result<Schema>` + issue vector | 否 | 否 | 否 | 否 | 否 | 无 | 无环境/CWD |
| `tuix` | Experimental | `bool` / `PollResult` / handled bool / `int` loop result | console/stream input | terminal 输出 | 否 | 否 | polling 可能 sleep | 无 | console/backend 依赖平台 |

## 风险登记表

| 模块 | 风险类型 | 证据位置 | 影响 | 建议补测/修复方向 |
| --- | --- | --- | --- | --- |
| `argtool` | API/CLI contract 混淆 | `include/argtool.h` `ParseResult::exit_code`；`docs/stability.md` CLI Contracts | 调用方可能把库建议值当成产品 exit code | header/doc 明确非 CLI contract；补 API 层示例 |
| `argtool` | 静默纠偏 | `RangePolicy::UseDefaultAndWarn`；`src/argtool.cpp` fallback warning | 无 logger 时越界输入变默认值 | 增加无 logger 场景测试或返回 trace |
| `argtool` | handler 吞错 | `SetUnknownOptionHandler` | 产品参数 typo 可能被吞掉 | 补 trace/JSON 审计测试 |
| `cfgx` | 全局状态污染 | `SetRemoteFetcher`、parser adapter registry | 测试和产品流程互相影响 | 增加 scope helper 或线程安全/生命周期文档 |
| `cfgx` | 环境依赖 | `ReloadOptions::env_prefix="APP_CFG_"`；`BuildEnvLayerFromEnvironment` | 不同机器配置结果不同 | 补默认环境层 determinism 测试 |
| `cfgx` | 自动 fallback | adapter parse/dump fallback；`As*` fallback | 输入错误可能不暴露 | 增加可观测 warning 或 strict option |
| `cfgx` | 格式子集 | YAML/TOML parser/dumper MVP | 用户以为完整规范兼容 | 补边界用例和非目标说明 |
| `fsx` | 文件破坏半径 | 默认 `ConflictPolicy::Overwrite`、`BuildSyncPlan(remove_extra=true)` | 误删/覆盖目标树 | 增加 path boundary 检查和 dry-run 示例 |
| `fsx` | 回滚非强一致 | 默认 `RollbackMode::BestEffort` | 失败后可能留下部分变更 | 补 Strict/BestEffort 失败矩阵测试 |
| `fsx` | 诊断证据被清理 | journal 成功后默认清理 | 成功后无法审计 journal | 文档强调 `keep_journal_on_success` |
| `asyncx` | 隐式线程启动 | `PoolOptions::start_immediately=true` | 构造对象即占用资源 | 补 lifecycle 测试和文档提示 |
| `asyncx` | 阻塞默认 | `BackpressurePolicy::Block` | UI/回调线程可能卡住 | 补 stop unblock 测试和使用建议 |
| `asyncx` | 取消语义误解 | cooperative cancellation/deadline 注释 | 用户以为会强制中断 task | 补 running task deadline 测试 |
| `logsys` | 全局 logger | `Logger::Instance()` macros | 库和测试间状态污染 | 增加 reset fixture 或局部 logger 示例 |
| `logsys` | 默认阈值差异 | `DefaultLoggerOptions` level vs record_level | 记录了但不输出，误判丢日志 | 补文档和默认模式测试说明 |
| `logsys` | 强副作用 fatal | `FatalPolicy::AbortAfterFlush` | 配置可导致进程 abort | 隔离进程测试，产品配置加警告 |
| `logsys` | 文件/网络副作用 | `FileSink` rolling；`UdpSyslogSink` | 删除旧日志或 UDP 发送失败不可见 | 补 rolling/UDP 失败测试 |
| `resultx` | 细节丢失 | `include/resultx.h` module error mapping | 源模块错误被压扁 | 增加映射表测试，考虑 source tag |
| `utils/sysx/hashx/textcodec` | helper 有副作用 | `ensure_parent_dir`、`sysx::thread::Thread` | 调用 helper 实际改 FS 或启动线程 | header/doc 标注 side effect |
| `utils/sysx/hashx/textcodec` | 安全误用 | non-cryptographic hash | 被误用于安全场景 | README/header 增加非加密声明 |
| `httpx` | backend 差异 | `HTTPX_ENABLE_OPENSSL` / `HTTPX_ENABLE_MBEDTLS` | HTTPS 行为构建相关 | 增加 runtime capability 文档/测试 |
| `httpx` | 环境代理 | `use_proxy_from_environment=true` | 测试/生产受 proxy env 影响 | 补 no_proxy 和 env priority 测试 |
| `httpx` | retry 字段混用 | `retry_policy.max_attempts` vs `max_retry_attempts` | 配置含义误解 | 补冲突优先级测试 |
| `schemax` | 标准兼容误解 | MVP keyword whitelist | 标准 JSON Schema 文档 compile fail | 文档强调 subset；补 unknown keyword 示例 |
| `schemax` | issue code 早期化 | MVP codes only | 下游过早依赖完整 taxonomy | 标注仅 MVP code 稳定 |
| `tuix` | framework 误解 | `docs/stability.md` Experimental Foundation | 用户以为是稳定 TUI framework | 文档保持非目标和 experimental 标签 |
| `tuix` | 输入降级 | `TeeBack`/`PeekOnly` degrade message | 非消费读取假设被破坏 | 补 Application flow 降级测试 |
| `tuix` | 多字节编辑 | `TextInput` byte-oriented ASCII | CJK/emoji 编辑体验不稳定 | 补“不支持”测试或实现 Unicode cursor |
