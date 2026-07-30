# toolx-log CLI Reference

> Audience: operators diagnosing existing ToolX logs
> Status: Bounded-stable CLI specification
> Applies to: the `0.3.x` line
> Source of truth for: supported log inputs, filters, gates and output

`toolx-log` reads existing logsys text or JSON-lines files, summarizes matched
records and can fail lightweight gates. It is not a live tailer, monitoring
daemon, exporter, alerting system or arbitrary-log parser.

## Invocation and input

```bash
toolx-log summarize --file FILE [--file FILE...] [options]
toolx-log summarize --manifest FILE [options]
```

`--format` accepts `auto`, `logsys-text`, or `jsonl`. Auto mode treats the
first non-blank line beginning with `{` as JSONL and otherwise selects logsys
text.

## Filters and gates

| Area | Options |
| --- | --- |
| Level | repeated `--level LEVEL` or `--min-level LEVEL` |
| Content/time | `--contains`, `--since`, `--until` |
| Samples | `--max-samples N` |
| Gates | `--fail-on-level LEVEL`, `--max-parse-errors N` |
| Output | `--json`, optional `--log-file` audit output |

Levels are `trace`, `debug`, `info`, `warning`, `error`, `fatal`, and
`critical`, case-insensitive. Exact `--level` and `--min-level` remain mutually
exclusive after CLI-over-manifest overrides.

Manifest fields are `files`, `format`, `level`, `min_level`, `contains`,
`since`, `until`, `max_samples`, `fail_on_level`, and `max_parse_errors`.
`files` is required and unknown fields fail schema validation.

## Contract

| Exit | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Runtime/read failure |
| `2` | Usage or parse error |
| `3` | File or manifest not found |
| `4` | Manifest validation or configured log gate failure |

JSON uses `schema=toolx.log.result`, `schema_version=1`. Data includes command,
manifest, files/count, selected format/filters, line/blank/parsed/matched counts,
parse failures, missing timestamps, per-level counts, first/last time, samples,
capabilities and warnings.

Plain output remains stable key/value lines for files, lines, matched,
parse-failure and severity counts.

## Examples

```bash
toolx-log summarize --file app.log --format logsys-text

toolx-log summarize --file app.log --min-level warning \
  --fail-on-level error --json

toolx-log summarize --file app.jsonl --format jsonl \
  --since "2026-06-04 10:00:00.000" --json
```

Contract coverage lives in
[`cmake/toolx_log_cli_contracts.cmake`](../../cmake/toolx_log_cli_contracts.cmake).
