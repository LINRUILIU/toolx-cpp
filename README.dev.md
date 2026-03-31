# logsys

A compact logging library with a single public header and single source implementation.

## Consumer footprint

- Include only: `include/logsys.h`
- Link only: `logsys.lib` (or `logsys.dll` if you switch target type)

## Quick start for small projects

```cpp
#include "logsys.h"

int main() {
	logsys::DefaultLoggerOptions opt;
	opt.level = logsys::LogLevel::Debug;
	opt.enable_console = true;
	opt.enable_file = true;
	opt.file_path = "game.log";

	auto& logger = logsys::Logger::Instance();
	logger.ConfigureDefaultLogger(opt);
	logger.SetDefaultOrigin(logsys::ErrorSource::Business,
							logsys::ModuleId::BusinessCommon,
							logsys::ErrorCategory::Business);

	LOGI("boot scene=%s", "menu");
	LOGE_STREAM().SetField("scene", "battle") << "hp invalid: " << -1;
}
```

### Ultra-minimal usage

```cpp
#include "logsys.h"

int main(int argc, const char* argv[]) {
	auto& logger = logsys::Logger::Instance();

	// Default: record >= INFO, output >= FATAL, console output,
	// fields only: level + message.
	logger.ConfigureSimpleLogger();

	// Optional: parse runtime level from args: --log-level=debug or --log-level debug
	logger.SetLevelFromArgs(argc, argv);

	log_info("the program is going to print %d", 7);
	log_debug << "value=" << 7 << std::endl;
}
```

### Level semantics

- Record level: controls what enters the logger pipeline.
- Output level: controls what is emitted to sinks.
- Simple mode defaults: record `INFO`, output `FATAL`.

## Recommended usage tiers

- Small personal projects: use `ConfigureDefaultLogger` + `LOGI/LOGE/LOG*_STREAM`.
- Team projects: use explicit `LOG_INFO(code, category, ...)` for stable error governance.
- Mixed mode: keep simple macros for non-critical paths, explicit code/category for critical modules.

## Runtime switches (debug-friendly)

```cpp
auto& logger = logsys::Logger::Instance();

// Toggle outputs at runtime.
logger.EnableFileOutput(false);      // close file output
logger.EnableConsoleOutput(true);    // keep console output
logger.EnableDebuggerOutput(false);  // disable debugger output

// Hide selected text fields (keep only what you need during quick debugging).
logger.SetTextFieldEnabled(logsys::TextField::Timestamp, false);
logger.SetTextFieldEnabled(logsys::TextField::Code, false);

// If a manually built event misses metadata, auto-fill placeholders/defaults.
logger.SetAutoFillMissingMetadata(true);
```

## V2 features (implemented)

- Async enqueue pipeline is enabled by default; call `logger.Flush()` when tests need deterministic assertions.
- Log rolling by size + retention count (`rolling.max_file_size_bytes`, `rolling.keep_recent_files`).
- Time-based rolling (`rolling.time_mode`: `none` / `day` / `hour`).
- Periodic flush scheduling (`schedule.periodic_flush_enabled`, `schedule.flush_interval`).
- Remote forwarding: UDP syslog (`remote.enable_udp_syslog`, `remote.udp_host`, `remote.udp_port`, `remote.syslog_facility`, `remote.syslog_app_name`, `remote.syslog_hostname`).
- Output strategy:
	- `ByTimeMixed`: emit immediately by event arrival time.
	- `ByLevelGrouped`: buffer and flush by level order (`TRACE -> DEBUG -> INFO -> WARNING -> ERROR -> FATAL -> CRITICAL`).
- Multi-source registration strategy (profile-based):
	- module-based profile (`module`)
	- file-path profile (`file_path_pattern`)
	- precedence: file profile overrides module profile.
- Console color render scheme:
	- controlled by `render.enable_color`
	- level-based ANSI color output on console sink.
- Backpressure strategy:
	- controlled by `backpressure.drop_low_level_when_full`
	- drop low-level logs when pending count exceeds `queue_high_watermark`.

## Load V2 config from JSON

```cpp
auto& logger = logsys::Logger::Instance();
logger.LoadConfigV2FromJsonFile("logsys.v2.json");
```

Example:

```json
{
	"global_record_level": "debug",
	"global_output_level": "error",
	"global_enable_console": true,
	"global_enable_file": true,
	"file_path": "app.log",
	"output_order": "by_level_grouped",
	"rolling": {
		"enabled": true,
		"max_file_size_bytes": 10485760,
		"keep_recent_files": 5,
		"time_mode": "hour"
	},
	"render": {
		"enable_color": true,
		"light_theme_only": true
	},
	"backpressure": {
		"drop_low_level_when_full": true,
		"drop_below_level": "info",
		"queue_high_watermark": 10000
	},
	"remote": {
		"enable_udp_syslog": true,
		"udp_host": "127.0.0.1",
		"udp_port": 514,
		"syslog_facility": 1,
		"syslog_app_name": "logsys",
		"syslog_hostname": "-"
	},
	"profiles": [
		{
			"name": "module-business",
			"module": "business_common",
			"output_level": "warning"
		},
		{
			"name": "battle-file-override",
			"file_path_pattern": "*battle.cpp",
			"output_level": "debug"
		}
	]
}
```

## Build with Visual Studio 2026 (MSBuild solution)

1. Generate solution files:

```powershell
cmake -S . -B build-vs-win32 -G "Visual Studio 18 2026" -A Win32
```

2. Build Debug|Win32:

```powershell
cmake --build build-vs-win32 --config Debug -- /p:Platform=Win32
```

3. Open solution in Visual Studio:

- `build-vs-win32/logsys.slnx`

## Notes

- Default library type is static to keep deployment simple for small projects.
- Core API supports both macro-format and macro-stream styles.

## 编码

- 仓库源文件与注释使用 UTF-8（无 BOM）。
- 为避免在 Windows 上被误识别为 GBK，已在仓库根目录添加 `.editorconfig` 强制 `charset = utf-8`。


## argtool (V1/V1.1/V1.2)

`argtool` is a lightweight argument parser library for small C++ projects.

Implemented scope:

- Positional arguments, including multiple positionals with a variadic tail.
- Options and flags with mixed ordering.
- `--` switch to force all following tokens as positionals.
- Merged boolean flags only (example: `-vvv`).
- Required argument checks and structured parse errors.
- Declarative fluent API (`Option/Flag/Positional` builders) with safer defaults.
- `Done()` validation for each declarative item (type/default/range/choice sanity checks).
- One-shot template style registration (`AddOptionTemplate` / `AddPositionalTemplate`).
- `-h/--help` generation with table columns (`Options/Type/Required/Default/Repeat/Constraints/Description`).

V1.1/V1.2 additions:

- Mutex constraints (mutually exclusive options).
- Dependency constraints (option A requires option B).
- Optional parse logger interface (`IParseLogger`) for errors/warnings.
- Range and choice constraints:
	- default range strategy is fail
	- optional fallback-to-default + warning strategy
	- choices are case-insensitive

	Iteration 1/2 additions:

	- Constraint pipeline abstraction:
		- built-in relation rules (mutex/dependency) are executed via a unified rule pipeline
		- custom rules can be registered with group/priority and `fail_fast` behavior
	- Converter system:
		- global converters by value type (`SetGlobalConverter`)
		- local per-option/per-positional override (`ConvertWith`)
		- converter-first normalization in parse flow
	- Built-in unit parsing for numeric values:
		- time units: `ms/s/m/h`
		- size units: `kb/mb/gb`
	- Explicit cardinality semantics:
		- `OptionalValue()`
		- `ListValue()`

	Iteration 3/4/5 additions:

	- Two-level subcommand tree:
		- `AddSubcommandRoot(...)`
		- `AddSubcommandLeaf(...)`
		- parse result captures `subcommand.root` / `subcommand.leaf` / `subcommand.path`
	- Lightweight trace stream:
		- `EnableTrace(true)` to collect per-token and decision events
		- trace events are exposed in `ParseResult::trace`
	- Machine-readable JSON output:
		- `ResultToJson(result, include_trace)` emits stable fields for `ok/help/exit/error/values/subcommand/trace`
		- stable contract header fields: `schema=argtool.parse.result`, `schema_version=1`
		- `values` keys are emitted in deterministic lexicographic order
	- Dual help layout:
		- `HelpLayout::Fixed` (table layout)
		- `HelpLayout::Compact` (compact list layout)
		- `SetHelpLayout(...)` and `HelpText(layout)`
	- Legacy bridge profile:
		- `EnableLegacyProfile(true)` enables compatibility mode
		- supports legacy `-?` help trigger and compact-oriented migration output

Behavior notes:

- Unknown options fail immediately unless custom unknown-option handler is provided.
- Exit code convention: `0` for help/success, `2` for parse errors.
- Short option values use `-o value` only (`-ovalue` is intentionally not supported in V1).
- Bool options are treated as flags in V1 (no explicit `--flag=false` parsing).
- Bool flag mode defaults to `switch`; optional modes are `count` and `toggle`.
- Default repeat strategy for value options is `override` and is shown in help text.
- Invalid configurations are rejected early (for example: int range beyond int32 bounds, default value outside range/choices).

Constraint and extension APIs:

- `AddMutexGroup({{"json", "plain"}, "Use either --json or --plain."})`
- `AddDependency({"server", "port", "--server requires --port."})`
- `SetUnknownOptionHandler(...)` for custom unknown-token handling.
- `SetLogger(...)` for structured error/warning hooks.
- `BoolMode(argtool::BoolFlagMode::Switch/Count/Toggle)` for bool flag behavior.
- `AddOptionTemplate(...)` / `AddPositionalTemplate(...)` for dense option sets.
- `Alias(...)` / `ShortAlias(...)` for option aliases.
- `AddConstraintRule(...)` for custom semantic/relation checks.
- `SetGlobalConverter(...)` + `ConvertWith(...)` for converter override chain.
- `OptionalValue(...)` / `ListValue(...)` for explicit optional/list semantics.
- `AddSubcommandRoot(...)` / `AddSubcommandLeaf(...)` for 2-layer command routing metadata.
- `EnableTrace(...)` + `ResultToJson(...)` for observability.
- `EnableLegacyProfile(...)` for migration compatibility bridge.

Help output notes:

- Help now separates `Flags`, `Value Options`, and `Positional Arguments` into independent tables.
- Relationship rules are listed in a separate `Relations` section (`Mutex Groups` + `Dependencies`).
- `Repeat`/`Behavior` columns use explicit words (`override`, `append`, `count`, `switch`, `toggle`) instead of `-`.

### argtool quick start

```cpp
#include "argtool.h"

int main(int argc, const char* const argv[]) {
	argtool::Parser parser;
	parser.SetProgramName("app.exe")
		  .SetDescription("demo")
		  .SetUsageExample("app.exe -vv --output app.log input.txt -- --literal")
		  .Flag("verbose", 'v').Description("Enable verbose output.").Done()
		  .Option("output", 'o').String().ValueName("FILE").Default("app.log").Description("Output file path.").Done()
		  .Option("level", 'l').Int().Range(0, 5).Default("3").Description("Log level in [0,5].").Done()
		  .Positional("input").String().Required(true).Description("Input file.").Done()
		  .Positional("extras").String().Required(false).Variadic(true).Description("Extra args.").Done();

	const argtool::ParseResult result = parser.Parse(argc, argv);
	if (result.help_requested) {
		std::cout << parser.HelpText();
		return result.exit_code;
	}
	if (!result.ok) {
		std::cerr << result.error->message << '\n';
		return result.exit_code;
	}
	return 0;
}
```

### Build argtool example (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target argtool_example -- /p:Platform=Win32
```

### Run all tests (including argtool)

```powershell
ctest --test-dir build-vs-win32 -C Debug --output-on-failure
```

### Release candidate gate (stable convergence)

Before calling a build "stable", verify all items below:

1. Build and test gate:
	- build `argtool_example` and `argtool_tests` in Win32/Debug
	- run full `ctest` with `--output-on-failure`
2. JSON contract gate:
	- `ResultToJson(...)` includes `schema=argtool.parse.result` and `schema_version=1`
	- `values` keys are deterministic (lexicographic)
	- `include_trace=false` excludes trace field
3. Legacy bridge gate:
	- with `EnableLegacyProfile(true)`, `-?` triggers help
	- with `EnableLegacyProfile(false)`, `-?` remains invalid option
4. Help layout gate:
	- `HelpText(HelpLayout::Fixed)` keeps table headers
	- `HelpText(HelpLayout::Compact)` keeps compact sections
	- default `HelpText()` follows `SetHelpLayout(...)`

## utils (M3)

`utils` is a compact helper library for small tools with a single public header and
single source implementation.

Consumer footprint:

- Include only: `include/utils.h`
- Link only: `utils.lib`

Current scope:

- `utils::str`: `trim/ltrim/rtrim`, `split`, `to_lower_ascii`, `iequals`, `starts_with/ends_with`
- `utils::time`: `format_local_timestamp_ms`, `now_system_ms`, `steady_elapsed_ms`
- `utils::parse`: `parse_int32`, `parse_double`, `parse_bool` (status + error text)
- `utils::err`: `join_context(scope, key, reason)` and unified error formatting
- `utils::path`: `normalize_slash`, `file_exists`, `ensure_parent_dir`
- `utils::hash`: `fnv1a32/fnv1a64/crc32/adler32` + streaming state APIs
- `utils::str` text metrics for hot paths:
	- UTF-8: `measure_text_utf8_strlen`, `measure_text_utf8_codepoints`, `measure_text_utf8_display_width`
	- GBK: `measure_text_gbk_strlen`, `measure_text_gbk_codepoints`, `measure_text_gbk_display_width`

Out of scope in M3:

- Large cross-encoding convert/save engine (UTF-8/UTF-16/GBK full converter)
- Complex file transaction/batch processing
- Pixel-level graphics API
- Terminal widget/layout framework

### utils quick start

```cpp
#include "utils.h"

int main() {
	auto int_v = utils::parse::parse_int32("42");
	if (!int_v.ok) {
		return 2;
	}

	const auto len_m = utils::str::measure_text_utf8_strlen("ab\xE4\xB8\xAD");
	const auto cp = utils::str::measure_text_utf8_codepoints("ab\xE4\xB8\xAD");
	const auto width = utils::str::measure_text_utf8_display_width("ab\xE4\xB8\xAD");
	const auto gbk_len = utils::str::measure_text_gbk_strlen("A\xD6\xD0B");
	const auto gbk_cp = utils::str::measure_text_gbk_codepoints("A\xD6\xD0B");
	const auto gbk_width = utils::str::measure_text_gbk_display_width("A\xD6\xD0B");
	const auto h = utils::hash::fnv1a32("hello");

	const auto now = utils::time::now_system_ms();
	const auto ts = utils::time::format_local_timestamp_ms(now);
	const auto parts = utils::str::split("a,,b", ',', false);
	(void)len_m;
	(void)cp;
	(void)width;
	(void)gbk_len;
	(void)gbk_cp;
	(void)gbk_width;
	(void)h;
	(void)ts;
	(void)parts;
	return 0;
}
```

### Build utils example (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target utils_example -- /p:Platform=Win32
```

### Run tests (including utils)

```powershell
ctest --test-dir build-vs-win32 -C Debug --output-on-failure
```

## fsx (M1)

`fsx` is the file transaction and batch operation module.

M1 scope:

- `atomic_write(path, data)` behavior via batch plan action.
- `safe_replace(src, dst)` with default overwrite behavior.
- `batch_rename(src, dst)` in plan execution.
- Run mode: fail-fast + best-effort rollback.

M1 report fields per step:

- `step`
- `op`
- `src`
- `dst`
- `error`
- `rolled_back`

M1 constraints:

- Single-process execution model.
- Best-effort rollback for completed steps.
- No crash-recovery/journal guarantee in M1.

M2 additions:

- Conflict policy: `Fail` / `Overwrite` / `Skip`.
- Rollback mode: `BestEffort` / `Strict`.
- Optional run journal (`RunOptions::journal_path`).
- Recovery entry: `RecoverFromJournal(...)`.
- Step report fields now include `recovery_source` and `skipped`.

M3 additions:

- Directory walk API (`WalkDirectory`) with recursive/relative options.
- Polling watcher abstraction (`CreateFileWatcher`) now supports file and directory paths.
- Directory watcher emits child-level `Created/Modified/Removed` events with relative paths.
- Link API (`CreateLink`) for hard/symbolic link operations.
- Capability query (`QueryCapabilities`) and archive placeholder (`ArchivePlaceholder`).

### fsx quick start

```cpp
#include "fsx.h"

int main() {
	fsx::BatchPlan plan;
	plan.AddAtomicWrite("temp/a.txt", "A")
		.AddRename("temp/a.txt", "temp/b.txt");

	const fsx::RunResult result = fsx::Run(plan);
	return result.ok ? 0 : 2;
}
```

### Build fsx example (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target fsx_example -- /p:Platform=Win32
```

### Run fsx tests

```powershell
ctest --test-dir build-vs-win32 -C Debug --output-on-failure -R fsx_tests
```

## asyncx (V1/V1.1/V1.2/V2)

`asyncx` is the lightweight thread-pool and async task utility module.

V1 scope:

- fixed-size worker pool with bounded queue
- blocking submission by default (`Post` / `Submit`)
- timeout-based submission (`PostFor` / `SubmitFor`)
- graceful stop and drain (`Stop(Drain)` + `Join`)
- optional fast stop with pending-task cancel (`Stop(CancelPending)`)
- basic runtime stats (`submitted/completed/rejected/timed_out`)

V1.1 additions:

- non-blocking submission (`TryPost` / `TrySubmit`)
- absolute-deadline submission (`PostUntil` / `SubmitUntil`)
- idle observation and waiting (`IsIdle`, `ActiveWorkerCount`, `WaitForIdle`, `WaitForIdleFor`)

V1.2 additions:

- absolute-deadline idle wait (`WaitForIdleUntil`)
- one-shot shutdown helper (`StopAndJoin`)
- runtime counter reset API (`ResetStats`)

V2 additions (batch 1):

- delayed one-shot tasks (`PostDelayedFor` / `PostDelayedUntil`)
- periodic scheduling (`ScheduleEvery`) with explicit cancel (`CancelScheduled`)
- scheduled queue observability (`ScheduledCount`)

V2 additions (batch 2):

- batch convergence helpers for futures:
	- `WaitAll` / `WaitAllFor`
	- `WaitAny` / `WaitAnyFor` / `WaitAnyUntil`

V2 additions (batch 3):

- configurable queue pressure strategy (`BackpressurePolicy::Block/Reject`)
- runtime policy control (`GetBackpressurePolicy` / `SetBackpressurePolicy`)
- layered metrics snapshot (`GetMetricsSnapshot`) with scheduler and execution counters

V2 additions (batch 4):

- priority queue support (`TaskPriority::High/Normal/Low`)
- priority-aware enqueue APIs (`PostWithPriority` / `TryPostWithPriority` / `PostWithPriorityUntil`)
- priority-aware future APIs (`SubmitWithPriority` / `TrySubmitWithPriority` / `SubmitWithPriorityUntil`)
- weighted dispatch slots to reduce low-priority starvation under sustained high-priority load

V1 constraints:

- no mandatory third-party dependency
- no coroutine runtime in V1
- no direct in-place refactor of `logsys/fsx/httpx` in V1

### asyncx quick start

```cpp
#include <chrono>
#include "asyncx.h"

int main() {
	asyncx::PoolOptions opt;
	opt.backpressure_policy = asyncx::BackpressurePolicy::Reject;
	asyncx::ThreadPool pool(opt);

	auto r = pool.Submit([] { return 6 * 7; });
	if (!r.ok) {
		return 2;
	}

	const int value = r.value.get();
	(void)value;

	auto s = pool.TryPost([] {});
	if (!s.ok && s.error.kind == asyncx::ErrorKind::QueueFull) {
		// queue is full, caller can fallback or retry later
	}

	auto hp = pool.PostWithPriority(asyncx::TaskPriority::High, [] {});
	if (!hp.ok) {
		return 2;
	}

	auto delayed = pool.PostDelayedFor(std::chrono::milliseconds(50), [] {});
	if (!delayed.ok) {
		return 2;
	}

	auto periodic = pool.ScheduleEvery(std::chrono::milliseconds(100), [] {}, true);
	if (periodic.ok) {
		pool.CancelScheduled(periodic.value);
	}

	std::vector<std::future<int>> futures;
	futures.push_back(std::move(r.value));
	asyncx::WaitAllFor(futures, std::chrono::milliseconds(200));

	const auto snapshot = pool.GetMetricsSnapshot();
	(void)snapshot;

	pool.WaitForIdleUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
	const auto stats = pool.ResetStats();
	(void)stats;

	pool.StopAndJoin(asyncx::StopMode::Drain);
	return 0;
}
```

### Build asyncx example/tests (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target asyncx_example asyncx_tests -- /p:Platform=Win32
ctest --test-dir build-vs-win32 -C Debug --output-on-failure -R asyncx_tests
```

### Build and run asyncx benchmark (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target asyncx_benchmark -- /p:Platform=Win32
.\build-vs-win32\Debug\asyncx_benchmark.exe

# optional args: task_count work_per_task
.\build-vs-win32\Debug\asyncx_benchmark.exe 200000 128
```

### asyncx integration bridges (non-invasive)

These examples show integration-layer orchestration with existing modules
without modifying their internals:

- `asyncx_logsys_bridge_example`: async log fan-in for `logsys`
- `asyncx_fsx_bridge_example`: concurrent batch execution for `fsx`
- `asyncx_httpx_bridge_example`: fan-out/fan-in request orchestration for `httpx`

Build and run:

```powershell
cmake --build build-vs-win32 --config Debug --target asyncx_logsys_bridge_example asyncx_fsx_bridge_example asyncx_httpx_bridge_example -- /p:Platform=Win32

.\build-vs-win32\Debug\asyncx_logsys_bridge_example.exe
.\build-vs-win32\Debug\asyncx_fsx_bridge_example.exe
.\build-vs-win32\Debug\asyncx_httpx_bridge_example.exe
```

## hashx (M3 compatibility facade)

`hashx` remains available as a compatibility facade.
Canonical implementations now live in `utils::hash`.

Current facade scope:

- one-shot APIs: `fnv1a32/fnv1a64/crc32/adler32`
- bytes APIs for binary input
- state aliases: `Fnv1a32State/Fnv1a64State/Crc32State/Adler32State`

Compatibility notes:

- Existing hashx call sites remain source-compatible.
- `hashx` result values must remain identical to `utils::hash`.
- Non-cryptographic use only.

### hashx quick start

```cpp
#include "hashx.h"

int main() {
	const auto h32 = hashx::fnv1a32("hello");
	const auto u32 = utils::hash::fnv1a32("hello");
	return (h32 == u32) ? 0 : 2;
	(void)h32;
	(void)u32;
	return 0;
}
```

### Build hashx example (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target hashx_example -- /p:Platform=Win32
```

### Run hashx tests

```powershell
ctest --test-dir build-vs-win32 -C Debug --output-on-failure -R hashx_tests
```

## textcodec (M1)

`textcodec` is the encoding helper module.

M1 scope:

- `hex_encode` / `hex_decode`
- `base64_encode` / `base64_decode`
- `url_encode` / `url_decode`

Decode error model:

- Error text + `DecodeError` enum (`InvalidCharacter`, `InvalidPadding`, `TruncatedInput`, `InvalidPercentEncoding`, ...)

M1 constraints:

- One-shot APIs only.
- URL mode follows RFC3986 style (`space -> %20`, no `+` decoding rule).
- UTF-8/UTF-16/GBK large conversion set is out of scope.

M2 additions:

- bytes-oriented API: `hex_encode_bytes` and `hex_decode_to_buffer`.
- base64 strategy options: standard/url-safe and optional padding.
- URL strategy options: `%20` vs `+` for spaces, `+` decode policy control.

### textcodec quick start

```cpp
#include "textcodec.h"

int main() {
	const auto b64 = textcodec::base64_encode("hello");
	const auto decoded = textcodec::base64_decode(b64);
	return decoded.ok ? 0 : 2;
}
```

### Build textcodec example (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target textcodec_example -- /p:Platform=Win32
```

### Run textcodec tests

```powershell
ctest --test-dir build-vs-win32 -C Debug --output-on-failure -R textcodec_tests
```

## tuix (M3)

`tuix` is the terminal UI foundation module.

Current scope:

- terminal core (`clear/move/set cursor/color/print/clear-line/clear-rect/title`)
- runtime backend reporting (`Terminal::CurrentBackend`)
- poll-only input model (`InputSource::Poll`, non-blocking by default)
- input consume strategy (`ExclusiveConsume` / `PeekOnly` / `TeeBack`)
- frame diff rendering (`FrameBuffer` + `RenderFrameDiff`)
- minimal widget/layout/application framework (`Widget/Layout/Application`)
- basic controls (`Label`, `Button`) for text render and Enter-triggered click

Platform strategy in M3:

- On Windows console handle output, prefer Win32-enhanced backend by default.
- If console handle is unavailable (pipe/redirect/test host), fallback to ANSI backend.
- Windows input source reads low-level console events (`PeekConsoleInput` / `ReadConsoleInput`).
- Basic layout engine is available (vertical/horizontal split), but no advanced focus manager or pixel graphics API.

### tuix quick start

```cpp
#include "tuix.h"
#include <iostream>

int main() {
	tuix::Terminal term(std::cout, true);
	term.ClearScreen();
	term.MoveTo(0, 0);
	term.SetColor(tuix::Color::BrightCyan, tuix::Color::Default);
	term.Print("tuix\n");
	term.ResetStyle();
	term.Flush();
	return 0;
}
```

### Build tuix example (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target tuix_example -- /p:Platform=Win32
```

### Build tuix showcase (Win32/Debug)

```powershell
cmake --build build-vs-win32 --config Debug --target tuix_showcase -- /p:Platform=Win32
```

### Run tuix showcase

```powershell
.\build-vs-win32\Debug\tuix_showcase.exe
```

Showcase controls:

- arrow keys or `W/A/S/D`: move cursor marker
- mouse move/click: move marker (when console mouse events are enabled)
- `Tab`: cycle consume mode (`ExclusiveConsume` -> `PeekOnly` -> `TeeBack`)
- `Q` or `Esc`: exit

### Run tuix tests

```powershell
ctest --test-dir build-vs-win32 -C Debug --output-on-failure -R tuix_tests
```

If `ctest` prints `No tests were found!!!` in a stale cache/build folder, run test binary directly:

```powershell
.\build-vs-win32\Debug\tuix_tests.exe
```

## M3 Closeout Contract

This section defines the current stable baseline after the M3 re-scope.

### Compatibility

- Existing M1/V2 call sites remain callable.
- `hashx` remains source-compatible as a facade and forwards to `utils::hash`.
- New M3 capability is additive (`utils::hash`, `tuix::Terminal::CurrentBackend`, string text metrics helpers in `utils::str`).

### Thread-Safety

- `utils::hash` one-shot functions are thread-safe; each state object is not safe for concurrent mutation.
- `textcodec` one-shot helpers are thread-safe; decode-to-buffer APIs are safe if caller buffers do not overlap.
- `tuix::Terminal` and `tuix::FrameBuffer` are not thread-safe for concurrent writes.
- `fsx::Run` / `fsx::RecoverFromJournal` are re-entrant, but not safe on the same file set/journal path without external locking.

### Platform Semantics

- `tuix` prefers Win32-enhanced backend on Windows console handles and falls back to ANSI on redirected/piped outputs.
- `fsx` recovery/journal cleanup follows OS file locking rules (Windows requires closed handles before delete).
- `utils::hash/hashx` and `textcodec` remain deterministic for identical byte input.

### Text Metric Boundaries

- `utils::str` text measurement helpers focus on fast runtime metrics for loops and rendering paths.
- They do not solve source/compiler/terminal charset mismatch configuration.

### Known Limits (M3)

- Hash algorithms are non-cryptographic only; no SHA-family guarantees.
- `textcodec` does not provide a full UTF-8/UTF-16/GBK conversion suite.
- `tuix` currently provides only basic widget/layout primitives (`Label/Button/Vertical/Horizontal`) and no advanced retained UI system.
- `fsx` recovery is best-effort and is not a full ACID transaction system.

### fsx Usage Boundary

- Prefer plain `std::filesystem` operations for single-step local file edits.
- Use `fsx` when you need batch intent, rollback visibility, and optional journal recovery.
- Use `RecoverFromJournal(...)` only for interrupted/failed transactional batches.

### M3 Release Gate

Before treating M3 as stable, verify all items below:

1. Build gate:
	- build module examples (`utils/fsx/hashx/textcodec/tuix`) in Win32/Debug.
2. Test gate:
	- run full `ctest --output-on-failure` and ensure no failures.
3. Contract gate:
	- README statements match tests and public headers.
4. Regression gate:
	- existing M1/V2 tests remain green.

## Phase E Final Closeout (B/C/D)

This repository now treats Phase B/C/D as integrated and stable under Phase E closure.

Closeout status:

- Phase B (`logsys`): async queue backend, time rotation, UDP syslog sink, dynamic level apply, and regression tests are all landed.
- Phase C (`cfgx`/`cfgtool`): TOML + remote reload + encrypted persistence + snapshot/audit + `snapshot-export`/`snapshot-restore` CLI landed.
- Phase D (`fsx`/`tuix`): directory watcher semantics + link/capability API + basic widget/layout/control framework landed.

Migration notes (non-breaking):

- Existing APIs remain callable; new functionality is additive.
- `fsx::CreateFileWatcher` now accepts directory path and emits per-child relative paths for directory events.
- `tuix` adds `Label`/`Button` and `Application` loop usage without changing existing terminal/input API contracts.
- For deterministic test assertions after async logging in `logsys`, call `Logger::Flush()` before checking sink output.

### CMake Tools configure failure (VS Code) troubleshooting

In some environments (typically network/cache state), VS Code CMake Tools may report
"failed to configure project" while command-line CMake still works.

Recommended fallback:

```powershell
cmake -S . -B build-vs-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build build-vs-win32 --config Debug -- /p:Platform=Win32
ctest --test-dir build-vs-win32 -C Debug --output-on-failure
```

If `ctest` index is stale and reports `No tests were found!!!`, run test executables directly from `build-vs-win32/Debug/` (for example, `logsys_tests.exe`, `cfgx_tests.exe`, `fsx_tests.exe`, `tuix_tests.exe`).

If cache is stale/corrupted, clean build artifacts and reconfigure:

```powershell
Remove-Item -Recurse -Force build-vs-win32
```

Notes:

- Build folders (`build/`, `build-vs/`, `build-vs-win32/`) are generated artifacts and can be removed safely.
- Keep source folders (`include/`, `src/`, `tests/`, `examples/`) untouched.

## cfgx + cfgtool (V1 stable)

`cfgx` is the unified configuration core library, and `cfgtool` is its thin CLI facade.

### V1 implemented scope

- formats: `json`, `ini/cfg`, `yaml` subset (`map/list/scalar`), `toml` (basic table/scalar)
- unified data model: `Node(Object/Array/Scalar)`
- path API: `Get/Set/Remove/Exists`
- node convenience API:
	- object: `Get/Set/Erase`
	- array: `At/Push/SetAt/EraseAt`
- unified empty check API: `Node::IsEmpty` / `IsEmptyValue`
- path grammar: `dot + [index]`, with escape support for special key chars
- numeric compatibility:
	- `AsDouble` accepts int64
	- `AsInt` accepts integral doubles (example: `8080.0`)
- merge: recursive object merge, array override by default, optional append mode
- remote pull entrypoint:
	- `SetRemoteFetcher(...)` + `LoadFromRemote(...)`
	- recommended to plug `httpx::Client` in the callback
- encrypted persistence:
	- `SaveEncryptedToFile(...)` + `LoadEncryptedFromFile(...)`
	- static key based encryption for at-rest config protection
- validation: rule pipeline + built-in factories
	- `RequirePathRule`
	- `ExpectKindRule`
	- `NumericRangeRule`
- write-back behavior:
	- INI/YAML save performs best-effort comment-preserving rewrite for existing scalar keys
	- unsupported structural rewrite falls back to canonical dump
- CLI subcommands:
	- `load`
	- `get`
	- `set`
	- `exists`
	- `merge`
	- `validate`
	- `reload-dryrun`
	- `snapshot-export`
	- `snapshot-restore`

### cfgx quick start

```cpp
#include "cfgx.h"

int main() {
		auto parsed = cfgx::ParseJson(R"({"svc":{"port":8080}})");
		if (!parsed.ok) {
				return 2;
		}

		cfgx::SetNode(parsed.value, "svc.host", cfgx::Node("127.0.0.1"));

		std::vector<cfgx::ValidationRule> rules;
		rules.push_back(cfgx::RequirePathRule("svc.host"));
		rules.push_back(cfgx::NumericRangeRule("svc.port", 1, 65535));

		const auto check = cfgx::Validate(parsed.value, rules);
		return check.ok ? 0 : 1;
}
```

### cfgtool quick start

```powershell
# get/set on JSON
.\build-vs-win32\Debug\cfgtool.exe set --file app.json --path svc.port --value 8080 --type int
.\build-vs-win32\Debug\cfgtool.exe get --file app.json --path svc.port

# inspect parser adapter state
.\build-vs-win32\Debug\cfgtool.exe adapters --json

# validate with rules
.\build-vs-win32\Debug\cfgtool.exe validate --file app.json --require svc.host --expect svc.port=int --range svc.port=1:65535

# dry-run reload check
.\build-vs-win32\Debug\cfgtool.exe reload-dryrun --current current.json --candidate candidate.json --range svc.port=1:65535
```

### Build and test (cfgx/cfgtool)

```powershell
cmake --build build-vs-win32 --config Debug --target cfgx_example cfgtool cfgx_tests -- /p:Platform=Win32
ctest --test-dir build-vs-win32 -C Debug --output-on-failure -R cfgx_tests
```

### Known limits in V1

- YAML is intentionally a subset parser in V1:
	- supported: map/list/scalar, indentation blocks, basic comments
	- not supported: anchors, tags, advanced schema semantics
- high-fidelity write-back currently focuses on existing scalar lines in INI/YAML.
	- complex structural rewrites (especially YAML list/object reshaping) may fall back to canonical dump.

### V2 recommendations

1. reload runtime layer:
	 - add polling reload manager (`mtime/hash` double-check, debounce, rollback to last-known-good snapshot).
2. diagnostics contract:
	 - add structured machine output for CLI (`--json`) and stable error codes.
3. schema system:
	 - add declarative schema DSL on top of rule pipeline, keep runtime callback rules as low-level escape hatch.
4. multi-source config composition:
	 - support `base -> env -> local -> runtime` layering and env var auto mapping (example: `APP_CFG_SERVER_HOST -> server.host`).
5. optional third-party parser adapters:
	 - CMake options for `nlohmann/json` and `yaml-cpp` as pluggable parser backends for complex scenarios.
6. config snapshot/rollback:
	 - snapshot save/load primitives to pair with reload rollback and incident recovery.

### V2 progress (single-header/single-lib)

- M1 landed:
	- layered compose: `base -> env -> local -> runtime`
	- env mapping: `APP_CFG_` + `__` segmentation
	- runtime overrides with last-write-wins
- M2 landed:
	- polling reloader (`Tick/ReloadNow`) with `mtime + hash` detection and debounce
	- rollback to last-known-good on parse/validation failure
	- callback payload includes `diff_paths + source_trace` (snapshots optional)
- M3 landed:
	- extended validation rules: `ChoiceRule`, `MutexRule`, `DependencyRule`, `StringLengthRule`
	- minimal in-memory snapshot API: `PollReloader::SnapshotCurrent/RestoreSnapshot`
	- cfgtool JSON diagnostics contract (`--json`) with stable envelope:
		- `schema`, `schema_version`, `ok`, `code`, `message`, `issues`, `data`
	- stable exit codes:
		- `0` success
		- `1` runtime error
		- `2` usage/parse error
		- `3` not found (exists/get style lookup miss)
		- `4` validation failed
- M4 landed:
	- pluggable parser adapter hooks (single-header/single-lib kept):
		- `RegisterParserAdapter`, `UnregisterParserAdapter`, `SetActiveParserAdapter`
		- `GetActiveParserAdapter`, `ListParserAdapters`, `HasParserAdapter`, `ClearParserAdapters`
	- `LoadFromFile` / `SaveToFile` now try active adapter first.
	- when adapter parse/dump fails, cfgx falls back to built-in JSON/INI/YAML/TOML path for resilience.
- M5 landed:
	- cfgtool adapter ops for runtime observability/control:
		- `adapters`: list current registry and active adapter
		- `adapter-activate --adapter NAME`: switch active adapter in current process
	- both commands support plain text and `--json` envelope output.

## httpx (incremental rollout)

`httpx` is the lightweight HTTP client module in this repository.

- standard convenience methods now include `Get/Delete/Head/Options/Connect/Trace/Post/Put/Patch`.

### Batch D contract (implemented)

- Redirect follow is opt-in (`redirects.follow=false` by default).
- Redirect chain guardrails:
	- max hops is enforced by `redirects.max_hops`.
	- redirect loop detection is enabled.
	- `Location` is required for redirect responses.
- Cross-origin safety:
	- when redirect target changes origin (`scheme/host/port`), `Authorization` and `Cookie` headers are stripped before the next hop.
- Cookie jar behavior:
	- in-memory only, scoped per `httpx::Client` instance.
	- domain/path matching follows standard host/path checks.
	- expiration is pruned on read/write; no disk persistence.
- Connection pooling behavior:
	- keep-alive reuse is scoped per `httpx::Client`.
	- per-host and total limits are enforced by `pool.per_host_limit` and `pool.total_limit`.
	- idle pooled sockets use TTL eviction (current implementation: 30s).

### Batch E status (started)

- HTTP forward proxy transport for `http://` requests is now being enabled.
- SOCKS5 no-auth proxy transport for `http://` requests is now available.
- Current Batch E target in this iteration:
	- route request through configured proxy endpoint.
	- use absolute-form request target when sending via proxy.
	- support `Proxy-Authorization: Basic ...` from `proxy.username/password`.
	- use no-auth SOCKS5 CONNECT tunnel when `proxy.scheme=socks5`.
	- current SOCKS5 boundary: no username/password auth method.
	- keep existing timeout/retry/pool semantics.

### Batch F status (started)

- TLS option contract landed:
	- `tls.verify_peer` / `tls.verify_host` (default strict: both true).
	- `tls.ca_file` for custom trust store path.
- HTTPS preflight checks now enforce:
	- invalid policy rejection (`verify_host=true` requires `verify_peer=true`).
	- custom CA file must be readable when configured.
- Current boundary:
	- without TLS backend macros, HTTPS returns clear `tls` error.
	- with backend macros enabled, HTTPS transport is wired through selected TLS backend.
	- current mbedtls limitation: when `verify_peer=true`, `tls.ca_file` is required.

TLS build prerequisites (CMake configure):

- OpenSSL path:
	- requires development package discoverable by CMake `find_package(OpenSSL)`.
	- if auto-discovery fails, set `OPENSSL_ROOT_DIR`.
- mbedtls path:
	- requires CMake package config discoverable by `find_package(MbedTLS)`.
	- if auto-discovery fails, set `MbedTLS_DIR` (or `CMAKE_PREFIX_PATH`).

### Batch G status (stabilization)

- Failure-injection tests added for:
	- redirect loop detection
	- malformed chunked response
	- proxy connect failure classification
- Soak baseline added:
	- local loopback sequential request rounds to catch crash/regression signals.
- Scope note:
	- these tests focus on behavior and stability signals, not leak-profiler-level memory accounting.

### Closure matrix commands (Windows)

1. HTTP-only baseline (Win32):

```powershell
cmake --build build-vs-win32 --config Debug --target httpx_tests
ctest --test-dir build-vs-win32 -C Debug -R httpx_tests --output-on-failure
```

2. OpenSSL backend (x64):

```powershell
cmake -S . -B build-vs-openssl-x64 -G "Visual Studio 18 2026" -A x64 -DHTTPX_ENABLE_OPENSSL=ON -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64"
cmake --build build-vs-openssl-x64 --config Debug --target httpx_tests
ctest --test-dir build-vs-openssl-x64 -C Debug -R httpx_tests --output-on-failure
```

3. mbedtls backend (x64, local prefix):

```powershell
cmake -S .third_party/mbedtls-3.6.2 -B .third_party/mbedtls-3.6.2/build-x64 -G "Visual Studio 18 2026" -A x64 -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_INSTALL_PREFIX=D:/copilot-tools/.third_party/mbedtls-install-x64
cmake --build .third_party/mbedtls-3.6.2/build-x64 --config Release --target install

cmake -S . -B build-vs-mbedtls-x64 -G "Visual Studio 18 2026" -A x64 -DHTTPX_ENABLE_MBEDTLS=ON -DMbedTLS_DIR=D:/copilot-tools/.third_party/mbedtls-install-x64/lib/cmake/MbedTLS
cmake --build build-vs-mbedtls-x64 --config Debug --target httpx_tests
ctest --test-dir build-vs-mbedtls-x64 -C Debug -R httpx_tests --output-on-failure
```

### Local loopback benchmark (offline)

```powershell
cmake --build build-vs-win32 --config Debug --target httpx_benchmark -- /p:Platform=Win32

# args: total_requests concurrency
.\build-vs-win32\Debug\httpx_benchmark.exe 20000 16
```

Output fields:

- `total/ok/fail`: completed request counts
- `handled_by_server`: loopback server handled count
- `qps`: achieved requests per second
- `p50_ms/p95_ms/p99_ms`: end-to-end latency percentiles

### Retained dependency policy

To keep TLS matrix reproducible without re-downloading dependencies each iteration,
retain the following workspace assets:

- `.third_party/mbedtls-3.6.2`
- `.third_party/mbedtls-framework-main`
- `.third_party/mbedtls-install-x64`
- local OpenSSL installation (default path: `C:/Program Files/OpenSSL-Win64`)

One-command closeout runner:

```powershell
powershell -ExecutionPolicy Bypass -File reference/httpx_closeout_windows.ps1
```

Optional flags:

- `-OpenSslRoot "C:/Program Files/OpenSSL-Win64"`
- `-MbedTlsInstallPrefix "D:/copilot-tools/.third_party/mbedtls-install-x64"`
- `-SkipMbedTlsInstall` (when local mbedtls install is already present)
