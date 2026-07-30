# ToolX Module Guides

> Audience: C++ consumers and maintainers
> Status: Canonical module index
> Applies to: `v0.3.2` release candidate
> Source of truth for: module guide ownership and comparison

Each exported target is independently consumable through `find_package(ToolX)`.
The table distinguishes direct CMake dependencies from conceptual relationships.

| Target | Stability | Direct ToolX dependency | I/O or process side effects | Cookbook |
| --- | --- | --- | --- | --- |
| [`argtool`](argtool.md) | Stable core | None | None unless callbacks perform I/O | `argtool_cookbook` |
| [`asyncx`](asyncx.md) | Stable core | `sysx` | Starts worker/scheduler threads | `asyncx_cookbook` |
| [`cfgx`](cfgx.md) | Stable core | None | Optional files, environment, remote callback and polling | `cfgx_cookbook` |
| [`fsx`](fsx.md) | Stable core | `utils` | Filesystem mutation, archives, links and polling | `fsx_cookbook` |
| [`hashx`](hashx.md) | Stable support | `utils` | None | `hashx_cookbook` |
| [`httpx`](httpx.md) | Bounded stable | `utils` | Network and optional file transfer | `httpx_cookbook` |
| [`logsys`](logsys.md) | Stable core | None | Console, files, debugger or UDP depending on sinks | `logsys_cookbook` |
| [`resultx`](resultx.md) | Stable support | `sysx` | None | `resultx_cookbook` |
| [`schemax`](schemax.md) | Experimental MVP | `cfgx` | None | `schemax_cookbook` |
| [`sysx`](sysx.md) | Stable support | System libraries on Windows | Threads, sleeping and native error inspection | `sysx_cookbook` |
| [`textcodec`](textcodec.md) | Stable support | `utils` | None | `textcodec_cookbook` |
| [`tuix`](tuix.md) | Experimental foundation | None | Terminal input/output and console state | `tuix_cookbook` |
| [`utils`](utils.md) | Stable support | None | `ensure_parent_dir` creates directories | `utils_cookbook` |

For cross-module relationships, see [Architecture](../architecture.md). Public
headers remain the compile-time authority; these guides define intended use and
compatibility boundaries.
