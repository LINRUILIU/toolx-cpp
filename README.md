# ToolX C++ Toolkit

ToolX is a practical C++20 toolkit for beginners and small-to-medium projects.
ToolX 是一套面向新手和中小项目开发者的 C++20 工具集，目标是快速上手、渐进扩展。

## Why ToolX / 为什么选 ToolX

- Ready-to-use modules for common engineering tasks.
- Practical examples and tests in one repository.
- Clean CMake targets, easy to integrate module by module.
- 提供开箱即用的基础模块，避免重复造轮子。
- 示例和测试齐全，便于理解和回归。
- 按模块接入，不需要一次性“全家桶”。

## Modules / 模块总览

| Module      | Purpose                         | Example Target                  | Test Target       |
| ----------- | ------------------------------- | ------------------------------- | ----------------- |
| `logsys`    | Logging                         | `logsys_example`                | `logsys_tests`    |
| `argtool`   | CLI argument parsing            | `argtool_example`               | `argtool_tests`   |
| `cfgx`      | Config loading/compose/reload   | `cfgx_example`, `cfgtool`       | `cfgx_tests`      |
| `fsx`       | File operations and batch tasks | `fsx_example`                   | `fsx_tests`       |
| `hashx`     | Hash helpers                    | `hashx_example`                 | `hashx_tests`     |
| `httpx`     | HTTP client utilities           | `httpx_example`                 | `httpx_tests`     |
| `asyncx`    | Thread pool and scheduling      | `asyncx_example`                | `asyncx_tests`    |
| `utils`     | String/time/path/common helpers | `utils_example`                 | `utils_tests`     |
| `sysx`      | System/network primitives       | `sysx_example`                  | `sysx_tests`      |
| `resultx`   | Unified result/status adapters  | -                               | `resultx_tests`   |
| `textcodec` | Text encoding utilities         | `textcodec_example`             | `textcodec_tests` |
| `tuix`      | Terminal UI building blocks     | `tuix_example`, `tuix_showcase` | `tuix_tests`      |

## Quick Start / 快速开始

### 1) Requirements / 环境要求

- CMake >= 3.20
- A C++20 compiler (MSVC 2022+ / Clang 14+ / GCC 9+; GCC 11+ recommended for CI parity)
- Git

### 2) Clone / 克隆

```bash
git clone https://github.com/LINRUILIU/toolx-cpp.git
cd toolx-cpp
```

### 3) Configure / 配置

Default options build tests and examples.
默认会构建测试与示例。

```bash
cmake -S . -B build -DCOPILOT_BUILD_TESTS=ON -DCOPILOT_BUILD_EXAMPLES=ON -DCOPILOT_BUILD_BENCHMARKS=ON
```

Or use presets:
也可以直接使用预设：

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

### 4) Build / 编译

Single-config generators (Ninja/Makefiles):

```bash
cmake --build build -j
```

Multi-config generators (Visual Studio):

```powershell
cmake --build build --config Debug
```

### 5) Run Tests / 运行测试

```bash
ctest --test-dir build --output-on-failure
```

For Visual Studio generators:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## Build Profiles / 构建配置

### Minimal local build / 最小本地构建

```bash
cmake -S . -B build-min -DCOPILOT_BUILD_TESTS=OFF -DCOPILOT_BUILD_EXAMPLES=ON -DCOPILOT_BUILD_BENCHMARKS=OFF
cmake --build build-min
```

### Full CI-like build / 全量构建（接近 CI）

```bash
cmake -S . -B build-ci -DCOPILOT_BUILD_TESTS=ON -DCOPILOT_BUILD_EXAMPLES=ON -DCOPILOT_BUILD_BENCHMARKS=ON
cmake --build build-ci
ctest --test-dir build-ci --output-on-failure
```

## Stable Capability Notes / 稳定能力边界

- `cfgx` stable formats are JSON and INI/CFG. YAML and TOML are practical subset parsers for simple map/list/scalar configuration.
- `fsx` does not expose archive creation as a stable feature yet; `QueryCapabilities()` reports archive support as unavailable.
- `cfgx` encrypted persistence is lightweight local protection, not authenticated encryption for production secrets or credentials.
- `logsys` does not terminate the host process on fatal logs by default; opt into abort behavior with `FatalPolicy::AbortAfterFlush`.

## Quality Gates / 质量门禁

Configure with optional static analysis:
使用可选静态分析进行配置：

```bash
cmake -S . -B build-ci -DCOPILOT_BUILD_TESTS=ON -DCOPILOT_ENABLE_CLANG_TIDY=ON
```

Formatting and lint entrypoints:
格式与静态检查入口：

```bash
cmake --build build-ci --target format-check
cmake --build build-ci --target lint-check
```

Coverage baseline on GCC/Clang-like toolchains:
在 GCC/Clang 类工具链上生成覆盖率基线：

```bash
cmake -S . -B build-cov -DCOPILOT_BUILD_TESTS=ON -DCOPILOT_ENABLE_COVERAGE=ON
cmake --build build-cov --target coverage
```

## Optional TLS Backends For `httpx` / `httpx` 可选 TLS 后端

`httpx` supports one TLS backend at a time.
`httpx` 一次只能启用一个 TLS 后端。

### OpenSSL

```bash
cmake -S . -B build-ossl -DHTTPX_ENABLE_OPENSSL=ON
```

### mbedTLS

```bash
cmake -S . -B build-mbedtls -DHTTPX_ENABLE_MBEDTLS=ON -DMBEDTLS_ROOT=/path/to/mbedtls/install
```

## Integration Example / 集成示例

Use as a subdirectory in your own project:
在你的项目中通过 `add_subdirectory` 接入：

```cmake
add_subdirectory(external/toolx-cpp)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE logsys cfgx httpx)
```

Use the installed package export:
使用安装后的包导出：

```cmake
find_package(ToolX CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE toolx::logsys toolx::cfgx)
```

Minimal `logsys` usage:

```cpp
#include "logsys.h"

int main() {
    auto& logger = logsys::Logger::Instance();
    logger.ConfigureSimpleLogger();
    LOGI("hello from toolx");
    logger.Flush();
    return 0;
}
```

## Repository Layout / 目录结构

- `include/`: public headers
- `src/`: module implementations
- `examples/`: runnable demos
- `tests/`: unit tests

## Developer Guide / 开发者说明

Maintainer-focused notes and local development workflow are in:
面向维护者的本地开发规则与详细说明见：

- `README.dev.md`

## License / 许可证

This repository currently includes a `LICENSE` file.
仓库已包含 `LICENSE` 文件，请按其中条款使用。
