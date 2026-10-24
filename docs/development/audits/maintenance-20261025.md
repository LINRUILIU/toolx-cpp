# 2026-10-25 最后维护、独立审核与发布门禁

> Audience: ToolX 使用者、维护者和发布审查者
> Status: Final maintenance audit and pre-publication verification record
> Applies to: v0.3.5 最后手动维护发行
> Source of truth for: 本轮修复、独立审核范围、已执行验证与发布准入条件

本轮只维护十三个库和六个现有 CLI。没有恢复产品实验、引入新公共 API、依赖或新产品。
2026-10-25 随 v0.3.5 停止手动 bug/安全修复、功能开发和 issue 跟进。
保留源码、文档、测试及历史发行；现有 CI 的保留不构成继续有人维护的承诺。

## 已修复的确认问题

| 范围 | 实际失败及修复 |
| --- | --- |
| 数值解析 | argtool 六位舍入使 Range 判断失真；单位乘法可溢出，非有限值和 NUL 后缀被误接受。保留 round-trip 精度并完整校验。config 的独立 double 转换入口也在保存前拒绝非法输入。 |
| schema 与路径 | 大整数经 double 比较产生范围/enum 误判；特殊对象键路径与普通层级冲突。精确比较数值并按现有 cfgx 语法转义 schema/inspect 路径，含空键、root marker 与数组。 |
| cfgx 节点边界 | AsInt(2^63 double) 越界转换；SetAt(SIZE_MAX) 加一回绕导致越界。先检查排他上界和容器 max_size。 |
| cfgx 持久化 | 缓冲写入失败被误报成功；INI 字符串/双精度类型或内容损失，删除键复活、新 root 键误落入 section、转义键改变结构。close 检错，双写引用分隔符且保留字面反斜杠，按 ParsePath 令牌保存键及 section。 |
| cfgx 恢复与读取 | RestoreSnapshot 绕过验证规则；目录读取可能抛未处理的 I/O 异常。恢复前验证且拒绝时保留 current/trace/audit，四种文件读取统一分块检错。HTTP body/upload 与日志配置同型读取也处理错误。 |
| 归档 | 空文件 streambuf 插入使 tar 失败，单文件 root 生成同名目录；悬空链接可使提取越界；提取可截断正在读取的 tar 或硬链、删除含 tar 的祖先。修复 payload 复制和静态路径检查，写入前保护输入 tar，保留 Skip 消费语义并检查读/close 错误。 |
| 文件替换与链接 | 保留备份路径与源重合会删除新内容；CreateLink 覆盖先删旧目标，失败会丢数据。变更前拒绝别名/祖先冲突，链接先创建候选再发布，失败保旧。Windows 打开候选不跟随 reparse point。 |
| HTTP 长度 | chunk 尺寸、正文预算和缓冲上限运算可溢出。先检查声明及剩余预算，非法块不流出；合法大 chunk 与最大预算仍可成功。 |
| HTTP 期限与资源 | retry delay 和 blocking send 可越过总期限；超大 uint64 时长发生 chrono 溢出；失败 SOCKS 握手泄漏 socket。保留 nonblocking、WouldBlock 后按期限等待，饱和转换时长，提前建立 RAII。 |
| HTTP 发送与 TLS | POSIX socket 写失败的信号处理采用 Linux MSG_NOSIGNAL / BSD SO_NOSIGPIPE，不改全进程策略。OpenSSL 私有 BIO 与 mbedTLS 共用发送入口；BIO 类型索引进程只注册一次，避免会话耗尽全局有限索引。 |
| 等待与日志 | 已完成 future 在零/过期期限下误报 timeout；logger stop 等完整刷新间隔；argtool/logsys 的 JSON 未转义全部控制字符。使用 wait_until、可唤醒停止和完整 JSON 控制字符转义。 |
| CLI 写入隔离 | HTTP/log audit 可修改输入或创建缺失输入后报成功，NUL 绕过路径比较。Windows dangling junction 被误作普通缺失路径，随后污染 stage。日志初始化前检查主路径、轮转、静态别名及原生 reparse 属性，报告真实读失败。 |
| 发布开销与归档 | 文档 fixture 复制约 2 GB 本机实验/依赖；源码包缺少生成目录排除。过滤运行、缓存、依赖、实验日志及 Git 元数据（含 worktree 的 .git 文件），保留正式日志 fixture。 |

## 红绿证据与独立审核

新增回归来自实际复现，而非仅对实现写断言。包括旧值 1.2345678901234567 被截为
1.23457、48 字节输入被 audit 改为 117 字节、chunk 错误成功并流出 5 字节、停读上传
超过 watchdog、/dev/full 写失败误报成功，以及提取后原始 tar 变成 payload 等红证据。
源码修复后重跑对应回归和完整套件。

审核者不是对应修复作者；先整库检查，再由作者修复，最后由原独立审核者闭环。
初次和中间复审仍发现 P2 时均未准入，未用测试通过替代源码审核。

| 独立审核 | 覆盖与闭环证据 | 最终结论 |
| --- | --- | --- |
| final_clearance_config | cfgx/argtool/schemax/utils/sysx/textcodec/resultx 与 config/sync/inspect；独立 UBSan 探针覆盖 589 种字符串、64 个嵌套键、8 个数组特殊键的多轮往返，以及验证拒绝保状态、读失败和 JSON round-trip。 | 未发现剩余 P2 及以上问题 |
| final_clearance_storage | fsx/hashx/pack、journal/原子替换、备份/链接/归档、五 CLI 隔离；独立 Linux 84/84，Windows 81 通过、7 项符号链接权限跳过；独立重编五 CLI 后 25/25 junction 黑盒通过，另复核输入 tar 保护。 | 未发现剩余 P2 及以上问题 |
| final_runtime_signoff | httpx/TLS/asyncx/logsys 与对应 CLI；核对 nonblocking/deadline、饱和转换、socket/BIO 所有权、retry flags、信号策略与 JSON/I/O。发现每会话 BIO 索引消耗后再次闭环，确认线程安全静态注册一次且处理注册失败。 | 未发现剩余 P2 及以上问题 |

结论限定于本轮实际审核的源码和合同；它不声称数学上证明没有任何 bug。
文档排除的并发写入、对抗性路径竞态、任意回调抢占和断电保证没有被补造。

## 最终本地门禁

| 环境/检查 | 实际结果 |
| --- | --- |
| Windows x64 MSVC 19.51 / v145，Release 独立构建 | 44/44 CTest 通过；日志 temp/maintenance-20261025/msvc-release-final-pass.log |
| WSL Ubuntu 原生文件系统，GCC 15.2 Debug | ASan + UBSan + float-cast-overflow，启用 leak 检查和 halt；43/43 CTest 通过；日志同目录 linux-sanitizers-final-pass.log |
| OpenSSL 3.5.5 独立后端构建 | ASan + UBSan 76/76；包含两次实际 HTTPS 的 BIO 索引消耗红绿、证书/主机名、读写期限和正常失败场景 |
| mbedTLS 3.6.5 独立后端构建 | ASan + UBSan 72/72；另运行实际本地 TLS 成功与断连上传探针 |
| 安装消费 | 全新 Release stage、独立 find_package 消费工程编译运行，六 CLI release smoke |
| 格式与文档 | format-check、git diff --check、77 Markdown/13 模块/6 CLI docs-check、v0.3.5 正式 preflight |
| 归档 | Windows Release ZIP 解压消费/CLI smoke；源码 TGZ 内容检查（正式 fixture 保留） |

CTest 数量是注册项数量，后端独立套件数量是 GTest 用例数量，二者不相加比较。
Windows 无 symlink 特权的分支有明确跳过；原生 junction 25 项没有权限跳过。
macOS/BSD 尚未在本地运行，必须由对应提交的远端 CI 补充，不能用 Linux 结果替代。

首个远端提交 5ff3723 的 Windows、GCC、OpenSSL、sanitizer/fuzz 检查通过；macOS
实际暴露了 libc++ 目录读取可表现为 EOF、以及空 symlink target 可创建的差异。
原失败回归保留，在六个文件读取入口显式拒绝目录、CreateLink 显式拒绝空参数，
并新增 INI 目录不是空配置用例。独立 storage 审核再次确认补丁无剩余 P2+；新提交
须重新通过全部 CI，不能借用前一提交的绿色检查打标签。

首轮本地统一验证因新增目录回归误写退出码，以及 Windows PowerShell 环境没有
Get-FileHash 而失败；已改为既有 runtime exit 1 与直接 .NET SHA256，再完整重跑通过。
此前 Debug 44/44、GCC 43/43 仅保留为早期阶段证据，不冒充后续修复的验证。
文档 fixture 从约 152.5 秒降至约 10 秒，只代表本机测试开销，不作为运行库性能收益。

## 发布准入与保留边界

发布来自独立 codex/final-v035-release 工作区。原 D:/copilot-tools 的未提交研究、
实验和历史提案保留，不混入最终标签，也不恢复运行。MSVC 通过 ASCII junction 访问
发布源码，以避开本机 CMake/MSBuild 的非 ASCII batch 路径问题；仅在进程中排除
机器失效 MSVC PATH，未改系统配置。Linux 从源码包进入新的 WSL 原生目录构建。

标签必须指向经过审核和检查的提交。推送前核对远端 main；分支 CI 的 Linux GCC、
Clang、Windows、macOS、OpenSSL、sanitizer/fuzz、安装及归档门禁全部通过后，才推进
main 并推送 v0.3.5。tag 驱动发布流水线重新构建三平台、验证并生成 SHA256SUMS。
最终外部事实由 [CI](https://github.com/LINRUILIU/toolx-cpp/actions/workflows/ci.yml)、
[发布流水线](https://github.com/LINRUILIU/toolx-cpp/actions/workflows/release.yml)与
[v0.3.5 发行](https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.3.5)交叉确认。

Logger 私有布局变化要求消费者重编；API 签名、JSON envelope、FSXJ4 与默认 TLS
选择不变。INI 的匹配定界引号双写语法及转义路径修复会纠正旧错误解释；迁移见
[v0.3.5 说明](../../releases/v0.3.5.md)。旧发行正文不重写，不自动归档 GitHub 仓库或关闭 issues。
