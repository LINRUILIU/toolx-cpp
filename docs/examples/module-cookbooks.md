# Module Cookbook Report

> Audience: C++ consumers and reviewers
> Status: Verified example report
> Applies to: `v0.3.2` release candidate
> Source of truth for: cookbook scenario inventory and observed sample output

The 13 `examples/*_cookbook.cpp` programs contain 59 annotated scenarios. They
are executable teaching programs rather than a substitute for the test suite:
each exits nonzero when a demonstrated contract fails and prints a compact
summary of the observed path.

## Inventory and execution behavior

| Cookbook | Scenarios | Topics | Side effects | Network | Platform note |
| --- | ---: | --- | --- | --- | --- |
| `argtool` | 4 | typed options; repeated/count flags; relations; traces/JSON diagnostics | none | none | portable |
| `asyncx` | 5 | futures; priority posts; cancellation; task groups; wait helpers | worker threads | none | portable |
| `cfgx` | 5 | path writes; parse/merge; validation; layered composition; runtime patch | in-memory only | none | portable |
| `fsx` | 5 | batch plan; walk; diff/sync; tar; capability query | temporary files/directories | none | archive capability is reported at runtime |
| `hashx` | 4 | one-shot; byte input; streaming; reset | none | none | portable |
| `httpx` | 5 | custom transport; retry; circuit breaker; safe download; multipart upload | temporary download/upload fixtures | none in this cookbook | custom transport keeps the example offline |
| `logsys` | 5 | setup; context; spans; metrics; V2 routing | captured log sink | none | debugger/UDP sinks are not enabled |
| `resultx` | 4 | cfgx; asyncx; httpx; typed propagation | none | none | portable normalization example |
| `schemax` | 5 | compile; valid input; issue details; fail-fast; cfgx conversion | none | none | portable MVP subset |
| `sysx` | 4 | platform/compiler; errors; clocks/sleep; thread wrapper | sleeps briefly and creates a thread | none | output intentionally reports the host |
| `textcodec` | 4 | hex; buffer sizing; Base64URL; URL form mode | none | none | portable |
| `tuix` | 5 | styled cells; layout; panel; scripted input/list; diff rendering | ANSI bytes captured in memory | none | no interactive terminal required |
| `utils` | 4 | strings; parsing; UTF-8 width; path helpers | creates a temporary parent directory | none | path rendering is host-specific |
| **Total** | **59** |  |  |  |  |

## Scenario catalogue

The annotations in source are the readable scenario names:

- **argtool (4):** typed defaults/ranges/choices; repeatable options and
  counted flags; option relationship rules; parser trace, logger callbacks and
  machine-readable diagnostics.
- **asyncx (5):** value-producing submit; priority fire-and-forget posts;
  cooperative cancellation; grouped outcomes; multi-future wait helpers.
- **cfgx (5):** nested path writes; JSON parse and overlay merge; accumulated
  validation; environment/local/runtime composition with attribution;
  runtime-patch materialization.
- **fsx (5):** batch plans and rollback metadata; relative directory walk;
  diff/sync with removal; tar round trip; capability flags.
- **hashx (4):** one-shot text hashes; explicit bytes; streaming state; reset
  and reuse.
- **httpx (5):** offline custom transport; transient retry; circuit breaker;
  temp-file download replacement; multipart file upload.
- **logsys (5):** minimal logger; inherited structured context; scoped trace
  duration; metrics snapshots; explicit V2 routing/formatting.
- **resultx (4):** cfgx normalization; asyncx error metadata; httpx network and
  native codes; propagation into a typed result.
- **schemax (5):** reusable compiled schema; empty valid result; structured
  invalid issues; fail-fast; conversion to cfgx-style issues.
- **sysx (4):** platform/compiler detection; normalized errors; steady/system
  time helpers; movable thread wrapper.
- **textcodec (4):** validated hex; capacity error reporting; configurable
  Base64URL padding; explicit plus/space URL behavior.
- **tuix (5):** styled cells; layout weights; framed panels; direct scripted
  events; stream-targeted terminal diffs.
- **utils (4):** trimming/case/splitting; non-throwing parsing; display width
  versus byte length; predictable path helpers.

## Captured output

Command used from the configured build directory:

```powershell
Get-ChildItem *_cookbook.exe | ForEach-Object { & $_.FullName }
```

Captured on Windows with Ninja + Clang 21.1.0; line order is deterministic for
these examples, while path, hardware-concurrency and platform fields can vary.

```text
argtool: typed ok=1 threads=8 input=in.txt | includes=2 verbose=2 | relations ok=1 | trace=2 json-bytes=244
asyncx: submit=42 | posted=1 | post-with-token=1 | group submitted=2 completed=2
cfgx: port=8080 | validation-ok=0 issues=1 | composed-ok=1 trace=3 | patch-kind=object
fsx: batch-ok=1 steps=2 | walk-ok=1 entries=2 | diff=4 sync-ok=1 | archive-text=alpha | tar=1 zip=0
hashx: fnv32=fa4636ad crc32=7d2807ff | adler-bytes=3eb019f | stream-fnv64=2a5257d3bd29a8d | reset-fnv64=2a5257d3bd29a8d
httpx: get-body=method=GET | retry=retry-ok attempts=2 | circuit-open=1 | upload-parts=1
logsys: accepted=3 emitted=3
resultx: cfgx=system:not_found missing key | asyncx=system:interrupted cancelled by caller | httpx=network:timed_out gateway timeout (native=504) | propagated-ok=0
schemax: valid-issues=0 | svc.port maximum numeric value is above maximum | extra additionalProperties additional property is not allowed | fail-fast=1 | cfgx-issues=2
sysx: os=windows compiler=msvc | status=0 kind=not_found | slept=1 | thread-value=7 hw=32
textcodec: hex=546F6F6C58 ok=1 | buffer-ok=0 code=OutOfRange | b64-url=YS9iPw roundtrip=a/b? | url=hello+world decoded=hello world
tuix: cell-bold=1 | input=toolx! selected=1 | ansi-bytes=380
utils: trimmed=ToolX parts=2 lower=http | int=42 bool-ok=0 | utf8-bytes=1 width=1 | parent-ok=1 path=58
```

Each module guide under [`docs/modules`](../modules/) maps these scenarios to
the public API categories and calls out behavior that is not demonstrated here.
