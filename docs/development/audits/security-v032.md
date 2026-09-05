# v0.3.2 安全修复与验证记录

> Audience: ToolX 维护者与发布审查者
> Status: 修复分支验证记录；等待远端 CI
> Applies to: v0.3.2 candidate
> Source of truth for: 本轮安全修复的复现起点、边界与验证证据

## 可复现起点

- 原始提交：`99cc91c55acfdf615066feb59e134775afd6b5c6`，开始时工作区干净。
- 修复分支：`codex/security-release-hardening`。
- 本地 `temp/security-audit/baseline.bundle` 已通过 `git bundle verify`，包含完整历史。
- 独立原始工作树保存在 `temp/security-audit/baseline-src`；只加入回归测试和其构建入口。
- 原始代码在全新 MSVC 19.51 Debug 构建中通过原有 37 个 CTest 项目。

证据目录和工具缓存不进入源码包；复现不依赖这些缓存，可从原始提交重新构建。

## 复现与修复

| 问题 | 原代码证据 | 修复 |
| --- | --- | --- |
| 链接导致目录边界逃逸 | Linux 回归修改了测试沙箱内根目录之外的哨兵文件；walk、copy、sync、archive 负向断言失败 | 使用词法相对路径，拒绝根目录下的链接/reparse point，检查各消费入口 |
| HTTP 请求注入与长度歧义 | 非法 URL、Header、multipart 元数据和长度组合进入自定义 transport | 每次发送/重定向前统一验证；二进制正文保持原样 |
| pack 误删合法名称 | 旧 CLI 删除 `report.new.tmp.txt` 等合法产物 | 删除按子串猜测临时文件的清理；仅由 fsx 按事务记录清理 |
| remove-extra 吞掉枚举错误 | 非目录 stage 可以被当成成功计划 | 检查失败返回错误；补黑盒合同 |
| 调度器释放后读取 | 新 ASan 门禁在 `StopCancelPendingClearsScheduledTasks` 报 heap-use-after-free | 等待前复制 deadline，不把容器元素引用跨越解锁期 |
| 发布版本与源码包污染 | tag 未强制关联版本，说明缺失回退模板 | 发布 preflight、负向合同和实际源码归档内容检查 |

## 验证方式

正常构建启用 tests/tools/examples，禁用 benchmarks，运行完整 CTest。
`security_regression_tests` 可加入原始提交并单独运行；HTTP 两项在 Windows/Linux
均失败，文件链接项在 Linux 失败。Windows 符号链接权限不足时仅跳过该项，另有
无需该权限的 junction 黑盒合同；Linux 不允许因链接创建失败跳过。

本轮额外使用 Clang 20.1.8 的 ASan/UBSan 和 libFuzzer。短时 fuzz 覆盖 cfgx
JSON/path 解析、httpx 输入验证、fsx tar 解包，三个目标每次运行约 20 秒。
短时通过只作为门禁，不代表完成长期模糊测试或穷尽安全审计。

GCC 15.2 / gcovr 7.2 实测基线约为行 72.3%、分支 43.6%，CI 下限设为
70% / 40%。clang-tidy 启用检查均按错误处理；相邻参数设计启发式不参与门禁，
其余局部例外均在源码旁注明理由，公共函数签名保持不变。

安装验证包括独立 consumer、安装树 smoke、ZIP 解包后 consumer/smoke，以及
实际源码 tar 清单检查。远端 Linux GCC/Clang、Windows、macOS、OpenSSL 与新
sanitizer/fuzz job 必须在合并前完成；本轮不执行合并或发布 tag。

## 兼容性与限制

- 公共签名不变；此前接受的危险元数据、链接树和歧义报文现在返回错误。
- 树操作根目录由调用者选择。路径检查不是防并发替换的文件系统沙箱，源/目标树
  在规划与执行期间必须由调用者控制；任意 BatchPlan 和恢复 journal 仍是可信输入。
- multipart 的 Content-Type/Content-Length 由客户端生成；不接受调用者覆盖。
- CMake 显式配置仍要求 3.20，preset schema 降为 3，对应最低 3.21，文档已区分。
- 六 CLI 的公共 helper 重构、统一版本/能力功能仍按计划留到发布后评估。

## 提交前最终结果

- Windows MSVC Debug：41/41 个 CTest 项目通过；junction 黑盒验证通过。
- Linux Clang 20 ASan/UBSan：41/41 通过；实际执行符号链接回归。
- cfgx/httpx/fsx libFuzzer 均完成约 20 秒 smoke，未发现崩溃。
- 所有配置的生产/合同 lint 文件与新 fuzz 文件通过 clang-tidy；clang-format 20 全量检查通过。
- 最终 GCC/gcovr 覆盖率：行 72.3%（8995/12437），分支 43.7%（8746/20032），超过 70%/40% 门槛。
- 独立安装 consumer、安装 smoke、ZIP 解包消费、源码 tar 内容检查、preflight 和文档检查均通过。
- 远端 CI 待分支推送后检查；保留草稿 PR，不执行合并。

## 首轮远端 CI 回查

提交 3fd12d6 的 Linux GCC/Clang、macOS、OpenSSL 和 sanitizer/fuzz 均通过。
Windows 在安全回归测试清理阶段失败：哨兵文件的 ifstream 尚未析构，导致
remove_all 遇到文件共享冲突。补丁将读取限制在独立作用域，保留全部安全断言。
本地 Windows 安全/junction 两个 CTest 项目及 Linux ASan/UBSan 符号链接回归
通过；等待补丁提交的远端 Windows CI 验证。
