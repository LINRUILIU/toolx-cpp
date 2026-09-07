# ToolX 维护指南

> Audience: ToolX 维护者与发布负责人
> Status: Canonical maintainer guide
> Applies to: `v0.3.2` release-candidate cycle
> Source of truth for: 本地质量门禁、打包、发布与维护流程

公开构建要求位于
[`build-and-compatibility.md`](../build-and-compatibility.md)，第三方参与方式位于
[`dependencies.md`](../dependencies.md)。本文件只说明维护操作与准入规则。

## 发布形态

ToolX 安装树包含 13 个导出 CMake target，以及在 `TOOLX_BUILD_TOOLS=ON`
时安装的六个 CLI：

- `toolx-config`
- `toolx-sync`
- `toolx-pack`
- `toolx-http`
- `toolx-log`
- `toolx-inspect`

发布归档直接来自 `cmake --install` 规则，而不是手工复制构建目录。

## CMake 维护选项

| 选项 | 默认值 | 维护用途 |
| --- | --- | --- |
| `TOOLX_BUILD_TESTS` | `ON` | 单元、集成、CLI 合同与 GoogleTest FetchContent |
| `TOOLX_BUILD_EXAMPLES` | `ON` | cookbook、桥接、showcase 和可选 benchmark |
| `TOOLX_BUILD_TOOLS` | `ON` | 六个可安装 CLI |
| `TOOLX_BUILD_BENCHMARKS` | 直接配置 `ON`，preset/CI `OFF` | 性能观察，不作为发布门禁 |
| `TOOLX_ENABLE_CLANG_TIDY` | `OFF` | 编译期静态分析 |
| `TOOLX_ENABLE_COVERAGE` | `OFF` | GCC/Clang coverage |

旧的 `COPILOT_*` alias 只保留一个兼容周期。新脚本与文档不得继续使用。

## 本地循环

快速循环：

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

文档/展示循环不要求下载 GoogleTest：

```bash
cmake -S . -B build/docs -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DTOOLX_BUILD_TESTS=OFF \
  -DTOOLX_BUILD_EXAMPLES=ON \
  -DTOOLX_BUILD_TOOLS=ON \
  -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build/docs --parallel
cmake -P cmake/docs_check.cmake
```

## 发布级门禁

候选版本在打 tag 前依次完成：

```bash
cmake -S . -B build-release -DTOOLX_BUILD_TESTS=ON \
  -DTOOLX_BUILD_EXAMPLES=ON -DTOOLX_BUILD_TOOLS=ON \
  -DTOOLX_BUILD_BENCHMARKS=OFF
cmake --build build-release --target docs-check
cmake --build build-release --target format-check
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
cmake --install build-release --prefix build-stage
cmake -S examples/install_consumer -B build-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build-stage"
cmake --build build-consumer --parallel
cmake -DTOOLX_STAGE_PREFIX=build-stage -P cmake/release_smoke.cmake
cpack --config build-release/CPackConfig.cmake
cmake -DPACKAGE_DIR=build-release/packages \
  -P cmake/release_archive_smoke.cmake
cpack --config build-release/CPackSourceConfig.cmake
```

不要把旧构建目录作为发布证据；候选版本应使用独立、可删除的构建和 stage
目录。

## Windows/MSVC

- 使用 Visual Studio Developer PowerShell，或让 Visual Studio generator
  选择工具链。
- 不要将可能已删除的 MSVC `bin` 路径写入全局 `Path`。
- 需要固定本机工具链时使用 `TOOLX_MSVC_ROOT`，但具体安装路径不是仓库
  兼容承诺。
- Ninja 构建前必须加载 MSVC developer environment；普通 PowerShell 中
  `cl` 不存在时，CMake 无法发现编译器。

## CI 职责

- `.github/workflows/ci.yml`：分支/PR 的格式、lint、构建、测试、安装消费、
  发布 smoke、归档验证、coverage、文档检查与 showcase。
- 独立 OpenSSL job：本机证书、信任 `localhost`、拒绝 hostname mismatch。
- `.github/workflows/release.yml`：tag 驱动的重建、复验、归档、SHA256 和
  GitHub Release 发布。

GitHub Actions 必须固定到不可变 revision；依赖下载必须固定版本与校验值。

## CLI 合同维护

- 六个 CLI 各自拥有 exit code 与 JSON envelope；不能用库级 `Status` 推导。
- 新增 JSON 字段必须 additive-only，并同步修改 CLI reference 与黑盒合同。
- 修改文件副作用、manifest precedence、默认代理、dry-run 或错误分类时，
  必须同步更新 [`docs/cli/matrix.md`](../cli/matrix.md)。
- 新 CLI 必须先满足[产品准入清单](cli-productization.md)，不能仅因已有库
  模块就自动进入发布集合。

## 模块边界维护

- 核心库不能依赖 `tools/`。
- `schemax` 可以依赖 `cfgx`，反向依赖禁止。
- `resultx` 保持 adapter 层，不能吞掉各模块的完整错误语义。
- 工具内部重复代码在未经多产品复用验证前保持私有，不增加公共 `clix`、
  `packx` 或 `inspectx`。
- `tuix` 保持实验性；`toolx-inspect` 的稳定不等于框架稳定。

## TLS 维护矩阵

```bash
cmake -S . -B build-ossl -DHTTPX_ENABLE_OPENSSL=ON
cmake -S . -B build-mbedtls -DHTTPX_ENABLE_MBEDTLS=ON \
  -DMBEDTLS_ROOT=/path/to/mbedtls/install
```

两个 backend 互斥。默认发布包均关闭；改变默认值属于依赖和发行策略变更，
不能作为普通实现修复提交。

## 发布检查表

- 版本在 `CMakeLists.txt`、README、CHANGELOG 与 candidate release note 中一致。
- 所有公开规范通过 `docs-check`，不存在孤儿文档和断链。
- 六个 CLI 同时存在于 install tree 与归档 `bin/`。
- standalone consumer 能通过 `find_package(ToolX CONFIG REQUIRED)` 构建。
- release smoke 与 unpacked archive smoke 通过。
- Linux GCC/Clang、Windows MSVC、macOS Clang 与 OpenSSL 专项 CI 通过。
- release notes 明确稳定/实验边界、依赖影响、迁移与已知限制。
- 不在候选加固阶段扩张产品/API 范围。

## Security and release gates

The normal CTest suite includes security_regression_tests, toolx_pack_security_contracts
and release_preflight_tests. Linux must execute symbolic-link regressions; Windows
also checks junctions without requiring symbolic-link privileges. HTTP loopback
startup failures and clang-tidy findings fail the gate.

TOOLX_ENABLE_SANITIZERS enables ASan/UBSan; TOOLX_BUILD_FUZZERS requires POSIX Clang.
CI runs the full sanitizer suite and bounded cfgx/httpx/fsx fuzz smoke tests seeded
from tests/fuzz/corpus. Mutated corpora belong in the build directory. Longer fuzz
campaigns are separate from the bounded PR gate.

Release builds run cmake/release_preflight.cmake with TOOLX_RELEASE_TAG (or the
GITHUB_REF_NAME environment variable), checking CMake version, note heading/metadata
and changelog. Formal publication requires an exact `## [MAJOR.MINOR.PATCH]`
changelog heading and `> Status: Released` in the notes. Keep candidate notes marked
as candidates until the release is approved; their formal preflight must fail.
Missing notes fail; publication never substitutes the template. The docs check
accepts a consistent candidate state or a consistent released state. Released
documentation must pass the same preflight and remove candidate wording from the
README. `release_docs_contracts` exercises both states and rejects incomplete
transitions and unknown status metadata.
Generated source archives are verified with cmake/source_archive_check.cmake and
TOOLX_SOURCE_ARCHIVE. Local references, dependency archives, build/stage/temp
directories and root logs are excluded.

The GCC CI coverage gate requires 70% lines and 40% branches. The local GCC 15
security-hardening baseline measured 72.3% lines and 43.6% branches across src/include;
the margin accounts for toolchain differences. Revisit thresholds from measured
reports instead of lowering them to hide regressions.

All enabled clang-tidy findings are errors. The easily-swappable-parameters design
heuristic is excluded because the stable 0.3.x API deliberately accepts adjacent
source/destination and min/max parameters. Narrow, commented suppressions preserve
public by-value signatures and document intentional exception handling, bitmask
enums and std::function ownership modeling. Other findings are fixed rather than
suppressed globally.

Compile-time clang-tidy gates production libraries and installed CLIs. The explicit
lint-check target additionally checks its listed contracts and showcase server.
Other tests/examples are built and exercised normally and under sanitizers; their
GTest assertion macros are not treated as production static-analysis evidence.

Coverage emits separate XML and detailed HTML reports. The target verifies both
artifacts, and Codecov receives only the explicit XML report with fallback discovery
disabled. Missing artifacts or upload failures fail CI.
