# `toolx-log`

`toolx-log` is a bounded-stable CLI for offline runtime log diagnosis. It reads
existing log files, summarizes matched records by severity, reports parse
failures, and can fail a release or deployment gate when configured thresholds
are exceeded.

The MVP supports logsys text output and logsys JSON-lines output. It is not a
real-time tailer, monitoring daemon, alerting system, metrics exporter, or
generic arbitrary-log parser.

## Commands

```bash
toolx-log summarize --file FILE [--file FILE...] [options]
toolx-log summarize --manifest FILE [options]
```

Input options:

- `--file FILE`: log file to read. Repeatable.
- `--manifest FILE`: JSON manifest with files and defaults.
- `--format auto|logsys-text|jsonl`: input format. `auto` selects JSONL when the
  first non-blank line starts with `{`, otherwise logsys text.

Filter options:

- `--level LEVEL`: exact level filter. Repeatable.
- `--min-level LEVEL`: inclusive severity threshold.
- `--contains TEXT`: raw-line substring filter.
- `--since "YYYY-MM-DD HH:MM:SS.mmm"`: include records at or after local time.
- `--until "YYYY-MM-DD HH:MM:SS.mmm"`: include records at or before local time.

Diagnostic options:

- `--max-samples N`: cap JSON samples. Default: `20`.
- `--fail-on-level LEVEL`: exit `4` if any matched record is at or above `LEVEL`.
- `--max-parse-errors N`: exit `4` if parse failures exceed `N`.

Output/audit options:

- `--json`: emit stable JSON envelope.
- `--log-file FILE`: write audit logs through `logsys`.

Levels are `trace`, `debug`, `info`, `warning`, `error`, `fatal`, and
`critical`, case-insensitive.

## Manifest

Manifest top-level fields:

```json
{
  "files": ["app.log", "worker.jsonl"],
  "format": "auto",
  "level": ["error", "fatal"],
  "min_level": "warning",
  "contains": "request=abc123",
  "since": "2026-06-04 10:00:00.000",
  "until": "2026-06-04 11:00:00.000",
  "max_samples": 20,
  "fail_on_level": "error",
  "max_parse_errors": 0
}
```

`files` is required for a manifest. Unknown top-level fields are rejected through
`schemax`. CLI options override manifest fields. `--level` and `--min-level` are
mutually exclusive after overrides are applied.

## Output

Plain output is stable key-value lines:

```text
files=1
lines=42
matched=3
parse_failures=0
warnings=1
errors=2
fatals=0
critical=0
```

JSON output uses:

```json
{
  "schema": "toolx.log.result",
  "schema_version": 1,
  "ok": true,
  "code": 0,
  "message": "summarized",
  "issues": [],
  "data": {
    "command": "summarize",
    "manifest": "",
    "files": ["app.log"],
    "file_count": 1,
    "format": "auto",
    "filters": {},
    "lines_read": 42,
    "blank_lines": 0,
    "parsed": 42,
    "matched": 3,
    "parse_failures": 0,
    "time_missing": 0,
    "by_level": {
      "trace": 0,
      "debug": 0,
      "info": 0,
      "warning": 1,
      "error": 2,
      "fatal": 0,
      "critical": 0
    },
    "first_time": "2026-06-04 10:00:00.000",
    "last_time": "2026-06-04 10:05:00.000",
    "samples": [],
    "capabilities": {},
    "warnings": []
  }
}
```

Fields may be added, but existing fields are additive-only within this bounded
CLI contract.

## Exit Codes

| Code | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Runtime/read error |
| `2` | Usage or parse error |
| `3` | File or manifest not found |
| `4` | Manifest validation failed or configured log gate failed |

## Examples

Summarize a text log:

```bash
toolx-log summarize --file app.log --format logsys-text
```

Fail a gate when matched errors are present:

```bash
toolx-log summarize --file app.log --min-level warning --fail-on-level error --json
```

Analyze logsys JSON-lines output:

```bash
toolx-log summarize --file app.jsonl --format jsonl --since "2026-06-04 10:00:00.000" --json
```
