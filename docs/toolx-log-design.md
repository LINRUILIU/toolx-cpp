# `toolx-log` Design Notes

`toolx-log` is intentionally an offline diagnostic CLI. The first product use
case is release and deployment triage: read logs that already exist, summarize
severity counts, expose parse failures, and fail deterministically when a gate
condition is met.

The MVP does not follow files, watch rotations, or behave like `tail -f`.
Real-time behavior would introduce ordering, lifecycle, and platform-specific
watch semantics that are not needed for the first product chain. Existing `fsx`
watcher capabilities can be revisited later if a concrete operator workflow
needs live inspection.

The parser is kept inside `tools/toolx_log.cpp` rather than added to `logsys`.
That keeps the public library surface stable while the CLI proves which parsing
contract is actually useful. A future public parser API should only be added if
multiple tools or applications need the same log ingestion behavior.

Supported input is deliberately narrow: logsys text formatter lines and logsys
JSON formatter lines. Generic arbitrary-log heuristics are out of scope because
they are difficult to test deterministically and can turn diagnostics into false
confidence.
