# toolx-http Product Design

This document records the design rationale for the bounded-stable
`toolx-http` CLI. The shipped operator contract is documented in
[toolx-http.md](toolx-http.md).

## Product Role

`toolx-http` is the fourth CLI in the ToolX product chain. Its job is runtime
endpoint preflight: after config composition and packaging, operators need a
small installed command that can verify health, readiness, and simple HTTP
contracts in release smoke or deployment gates.

It should stay narrower than `curl`. ToolX already exposes richer HTTP behavior
through the `httpx` C++ API; the CLI should provide stable automation output,
not every transport feature.

## Scenarios

The MVP should support:

- checking a single local or remote endpoint
- checking a batch of endpoints from a manifest
- asserting status code or inclusive status range
- asserting a simple response body substring
- sending small request bodies and headers
- controlling timeout, retry, and redirect behavior
- emitting stable JSON for CI and release smoke
- writing an optional audit log

## Module Composition

`toolx-http` composes existing modules:

| Module | Use |
| --- | --- |
| `argtool` | CLI parsing, help, usage errors |
| `cfgx` | Manifest loading and JSON envelope assembly |
| `schemax` | Manifest validation with unknown-field rejection |
| `httpx` | Request execution, retry, redirect, timeout, redaction |
| `logsys` | Optional audit logging |

No new public C++ module API is required for the MVP.

## Non-Goals

The MVP should not implement:

- load testing or benchmarking
- OAuth or credential flows
- generic download/upload workflows
- JSONPath, regex, schema, or DSL body assertions
- certificate management beyond existing `httpx` TLS options
- external-network-dependent release smoke

## Admission Checklist

`toolx-http` enters the public product chain only with:

- installed target under `TOOLX_BUILD_TOOLS`
- black-box loopback CLI contract tests
- install-tree and archive smoke coverage
- standalone `docs/toolx-http.md` reference
- README workflow example
- stability entry marking it bounded stable
