# `toolx-http` CLI Reference

`toolx-http` is the bounded-stable ToolX CLI for runtime endpoint preflight. It
checks one or more HTTP endpoints against explicit expectations and returns a
stable result envelope for release smoke, deployment gates, and local diagnosis.

The MVP is not a `curl` replacement. It does not add a public C++ API and does
not implement load testing, OAuth, download/upload workflows, or complex body
assertion DSLs.

## Commands

```bash
toolx-http check --url URL [options]
toolx-http check --manifest FILE [options]
```

`--url` creates one check. `--manifest` loads a batch of checks. Use one or the
other, not both.

## Options

| Option | Meaning |
| --- | --- |
| `--method METHOD` | HTTP method: `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`, or `OPTIONS`. Defaults to `GET`. |
| `--header KEY:VALUE` | Request header. Repeatable. |
| `--body TEXT` | Request body text. |
| `--body-file FILE` | Request body loaded from a file. Mutually exclusive with `--body`. |
| `--expect-status N` | Exact expected status. |
| `--expect-status MIN:MAX` | Inclusive expected status range. Defaults to `200:299`. |
| `--expect-body-contains TEXT` | Require a response body substring. Not allowed for `HEAD`. |
| `--timeout-ms N` | Total request timeout. |
| `--connect-timeout-ms N` | Connection timeout. |
| `--retry N` | Retry attempts for retryable transport errors. |
| `--retry-delay-ms N` | Delay between retry attempts. |
| `--follow-redirects` | Follow redirects through `httpx`. |
| `--manifest FILE` | Batch preflight manifest. |
| `--log-file FILE` | Optional audit log. |
| `--json` | Emit the stable JSON envelope. |

CLI request/expectation options override manifest check fields when used with
`--manifest`; runtime options override manifest defaults. Repeated CLI
`--header` replaces manifest top-level headers and is then combined with each
check's own headers.

## Manifest

Supported top-level fields:

```json
{
  "timeout_ms": 1000,
  "connect_timeout_ms": 500,
  "retry": 1,
  "retry_delay_ms": 50,
  "follow_redirects": true,
  "headers": ["User-Agent: toolx-http"],
  "checks": [
    {
      "name": "health",
      "url": "http://127.0.0.1:8080/health",
      "method": "GET",
      "expect_status": "200",
      "expect_body_contains": "ready"
    }
  ]
}
```

Check fields are `name`, `url`, `method`, `headers`, `body`, `body_file`,
`expect_status`, and `expect_body_contains`. Unknown top-level or check fields
are rejected through `schemax` and return exit code `4`.

Manifest headers use the same `KEY:VALUE` format as CLI `--header`.

## JSON Output

`--json` always uses this envelope:

```json
{
  "schema": "toolx.http.result",
  "schema_version": 1,
  "ok": true,
  "code": 0,
  "message": "checks passed",
  "issues": [],
  "data": {}
}
```

`data` contains at least:

- `command`, `manifest`
- `checked`, `passed`, `failed`, `duration_ms`
- `checks`
- `warnings`

Each `checks[]` entry contains at least `name`, `url`, `method`, `ok`, `status`,
`duration_ms`, `error_kind`, `message`, `expect_status`, and `body_matched`.
Sensitive query values are redacted in reported URLs.

## Exit Codes

| Code | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Transport or runtime failure |
| `2` | Usage or parse error |
| `3` | Manifest or body-file not found |
| `4` | Preflight validation failed, including status/body expectation mismatch |

For batch checks, any transport/runtime error makes the overall exit code `1`.
If all requests complete but any expectation fails, the overall exit code is
`4`.

## Examples

Check a local health endpoint:

```bash
toolx-http check --url http://127.0.0.1:8080/health \
  --expect-status 200 --expect-body-contains ready --json
```

POST a small body:

```bash
toolx-http check --url http://127.0.0.1:8080/validate \
  --method POST --header "Content-Type: application/json" \
  --body '{"probe":true}' --expect-status 200 --json
```

Run a batch manifest:

```bash
toolx-http check --manifest http-preflight.json --timeout-ms 1000 --json
```

## Scope

The MVP depends on existing `httpx` transport behavior. HTTPS works only when
the build enables an `httpx` TLS backend; otherwise HTTPS failures are reported
as runtime errors. Release smoke does not depend on external network access;
network behavior is covered by loopback CLI contract tests.
