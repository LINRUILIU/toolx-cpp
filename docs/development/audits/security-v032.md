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


## 本地审查后续修复（2026-09-06）

本轮从干净提交 `19422f0946402400007e24df97ac414042c5dd2e` 开始；完整 Git
备份已验证，位于忽略目录 `temp/security-audit/pre-review-fixes.bundle`。
此前该提交的远端 CI 已全部通过，但本地审查仍发现下列缺口，因此保持草稿 PR。

| 本地评论 | 修复与回归证据 |
| --- | --- |
| remove-extra 删除新复制目标 | 删除多余项和类型冲突后再复制；黑盒覆盖文件/目录双向切换，以及 plan 不修改原内容 |
| source/stage 重叠删除源 | 规范化并按路径组件拒绝相同、祖先、后代关系；Windows 比较忽略大小写；覆盖 junction/symlink 根别名及源哨兵保留 |
| 候选文档通过正式预检 | 精确 release H1、Status: Released 和 changelog 版本标题；候选、缺失状态、近似版本均为负例，仓库当前候选仍禁止发布 |
| URL fragment 上网 | 解析时剥离 fragment；直接/代理/重定向实际报文测试，保留 %23，覆盖没有路径及只有 query 的 URL |
| multipart 碰撞反复扫描 | 随机 boundary 候选，最多八次扫描；私有生成器注入测试确定性验证碰撞和耗尽，fuzz 输入进入实际边界扫描函数 |
| POSIX 文件名回归 | 本机边界与可移植 tar 规则分离；walk/copy/sync/stage 保留冒号和反斜杠，不再把枚举名称强制改成分隔符 |
| 空白 Host | 校验去空白后的值，空格或 HTAB-only 在 transport 前失败 |
| coverage 缺失产物 | gcovr 分别指定 XML/HTML 输出及当前 build 搜索目录；验证两份产物和 src/include 文件范围，Codecov 禁止回退搜索且上传失败使 CI 失败 |
| 源码包与依赖文档矛盾 | 明确 .third_party 只在 Git 仓库保留、不进入源码包；归档合同增加依赖归档和编译器覆盖率中间文件负例 |
| remove-extra 测试提前失败 | 空 source 直接进入 stage 枚举，断言 code 3 与 walk 错误；旧版二进制同场景返回 code 0，证明测试能识别原吞错行为 |

额外回滚测试暴露了文件转目录后的新父目录没有撤销记录的问题。CopyFile 现在
在创建父目录之前写入 RemovePath undo，回滚仅删除空目录，使旧普通文件能够恢复。
测试在后续复制失败时检查旧文件、旧目录内容及 journal 成功清理。

旧版 `99cc91c` 二进制在隔离夹具内再次复现两种“返回成功但误删”，记录见
`temp/security-audit/review-reproduction.json`。新 HTTP/类型切换测试也在本轮
修改前失败，记录见 `review-red-tests.log`。碰撞造成资源消耗的前提是上传数据
受不可信输入控制；没有宣称所有上传均可远程触发攻击。

本轮不新增公共 API，树的独占控制前提保持不变。POSIX 原生文件操作恢复合法
名称，但 tar 名称仍拒绝跨平台歧义字符。候选发布说明不提前改成正式发布状态。


本轮最终本地验证：

- Windows MSVC Debug、Linux Clang 20 ASan/UBSan：各 42/42 CTest 通过。
- Linux 实际执行符号链接及 POSIX 名称回归，Windows junction 合同通过。
- clang-format 20、配置的全部 clang-tidy 文件及 httpx fuzz 文件通过。
- cfgx/httpx/fsx fuzz 各完成约 20 秒运行，未发现崩溃。
- 新建 GCC 15 构建目录直接运行 coverage 目标，42/42 测试通过；行覆盖率
  72.54%（9056/12485），分支覆盖率 43.87%（8826/20118）。XML 可被标准解析器
  读取，23 个 class 文件全部在 src/include；HTML 及 CMake 产物校验通过。
- coverage 目标补齐 journal failpoint 和 sync proxy 测试构建依赖，避免旧对象数据
  混入覆盖报告。未放宽 gcovr 的函数合并或最低覆盖率规则。
- 安装 smoke、ZIP 解包后的独立 consumer/smoke、源码包内容检查、发布预检与
  覆盖报告负例以及 66 篇文档检查通过。编译器覆盖率中间文件不进入源码包。
- 等待本轮提交的远端 CI；按约 15 分钟定时回查，不合并、不发布。


## 本地审查修复后的远端 CI 回查

`556e6d4` 的 Windows、macOS、Linux Clang、OpenSSL 和 sanitizer/fuzz 均通过。
两个 GCC job 的构建、测试、覆盖率生成和 GitHub artifact 上传也通过，但 Codecov
拒绝无认证上传：`Token required - not valid tokenless upload`。仓库未配置 Codecov
secret，因此改用当前固定版本 Action 已支持的 GitHub OIDC，通过 job 范围的
`contents: read` / `id-token: write` 和 `use_oidc: true` 获取短期上传凭证。
保持 `fail_ci_if_error: true`、显式 XML 和禁止回退搜索，不改变覆盖率门槛。
此变更仅涉及 CI 认证；验证固定 Action 输入/文档和 workflow 配置后推送回查。


## 第二轮本地审查修复（2026-09-07）

本轮基线为干净提交 `828210abbf716b2ebbca9cab4b0100af5957a1d3`；完整 Git
备份 `temp/security-audit/review2-baseline.bundle` 已验证。该提交远端 12 项检查
已通过，但本地评论仍发现两个 P1、两个 P2 和一个 P3 测试缺口。

- **FSXJ3 新父目录恢复：** 上轮普通父目录 RemovePath 记录可用于同进程回滚，
  却被 FSXJ3 恢复的非 staging 路径保护拒绝。本轮通过临时目录创建父目录，使用
  既有 staging REMOVE 与逆向 MOVE 记录；不更改格式，也不放宽恢复保护。
  子进程回归覆盖首次创建前后、嵌套父目录创建后和目标被外部文件占用的冲突，
  并要求子进程准确以 failpoint 退出码 86 结束。修复前测试明确报出
  `FSXJ3 refuses to remove a non-staging path while recovering`。
- **正式发布文档：** docs-check 根据当前版本与状态检查候选/正式文档，正式状态
  复用 release preflight。独立夹具验证完整候选、完整正式、未完成转换、未知状态。
  相同正式夹具在基线 docs-check 上失败，在修复后通过。实际仓库仍保持候选状态，不能通过正式发布预检。
- **相对重定向：** 按 RFC 3986 分离 authority、path、query、fragment，保留
  query-only / fragment-only 引用的完整原路径，正确继承或清空 query，仅对路径
  去除点段。覆盖真实 loopback 报文、跨主机引用和百分号编码点段。
- **显式 pack 选择：** include/exclude 使用本机路径语义且不裁剪空格。Linux
  黑盒覆盖反斜杠、冒号、`C:` 前缀、前导反斜杠及首尾空格的精确选择和排除。
  Windows 分隔符与根路径限制、tar 可移植成员名限制保持有效。
- **multipart fuzz：** 分支选择使用除去主 switch 编号后的位，两个生成器均可达，
  并增加耗尽种子；生产 boundary 选择仍最多扫描八次。

本轮无公共 API 变更。继续保留草稿 PR，不合并、不打发布 tag。


本轮验证结果：

- Windows MSVC Debug、Linux Clang 20 ASan/UBSan 各 43/43 CTest 通过。
  收紧退出码断言后，两平台再次通过进程中断恢复回归。
- GCC 15 coverage 目标完成 43/43 测试；行覆盖率 72.65%（9109/12538），
  分支覆盖率 44.06%（8938/20288），高于 70%/40% 门槛。XML 可解析，全部
  23 个 class 在 src/include 范围，HTML 与产物校验通过。
- cfgx/httpx/fsx libFuzzer 分别完成 203491、210502、102674 次输入执行，
  每项约 20 秒，未发现崩溃。
- 安装 smoke、ZIP 独立 consumer/smoke、源码包内容和 66 篇文档检查通过。
- clang-format 20 全量检查通过。全部配置 lint 文件已检查；HTTP 新代码的
  两处重复/布尔表达式告警修正后，HTTP 与 fuzz 定向 clang-tidy 复验通过。
  failpoint 子进程及 runner 的补充 lint 通过。最终 HTTP 修改后，两平台 HTTP
  测试再次通过，GCC 完整 coverage 测试与报告重新生成。
