# toolx-http CLI Reference

> Audience: release/deployment preflight users
> Status: Bounded-stable CLI specification
> Applies to: the `0.3.x` line
> Source of truth for: endpoint checks, proxy precedence and output contract

`toolx-http` checks one URL or a manifest of URLs against status and simple body
expectations. It is not a `curl` replacement, load tester, OAuth client,
download/upload tool or body assertion language.

## Invocation and precedence

```bash
toolx-http check --url URL [options]
toolx-http check --manifest FILE [options]
```

`--url` and `--manifest` are mutually exclusive. Runtime CLI options override
manifest defaults; request/expectation options override each manifest check.
Repeated CLI headers replace manifest top-level headers and are combined with
per-check headers.

## Options

| Area | Options |
| --- | --- |
| Request | `--method`, repeated `--header`, `--body` or `--body-file` |
| Expectations | `--expect-status N|MIN:MAX`, `--expect-body-contains` |
| Runtime | `--timeout-ms`, `--connect-timeout-ms`, `--retry`, `--retry-delay-ms`, `--follow-redirects` |
| Proxy | `--no-proxy-from-env` |
| Input/output | `--manifest`, `--log-file`, `--json` |

Method defaults to `GET`; status defaults to `200:299`. Body expectations are
not valid for `HEAD`.

## Manifest

```json
{
  "timeout_ms": 1000,
  "connect_timeout_ms": 500,
  "retry": 1,
  "retry_delay_ms": 50,
  "follow_redirects": true,
  "use_proxy_from_environment": false,
  "headers": ["User-Agent: toolx-http"],
  "checks": [{
    "name": "health",
    "url": "http://127.0.0.1:8080/health",
    "method": "GET",
    "expect_status": "200",
    "expect_body_contains": "ready"
  }]
}
```

Check fields are `name`, `url`, `method`, `headers`, `body`, `body_file`,
`expect_status`, and `expect_body_contains`. Unknown fields fail schema
validation. Environment proxy use defaults to true; the CLI disable flag has
highest priority.

## Contract

| Exit | Meaning |
| --- | --- |
| `0` | Success or help |
| `1` | Transport or runtime failure |
| `2` | Usage or parse error |
| `3` | Manifest or body file not found |
| `4` | Manifest or expectation validation failure |

JSON uses `schema=toolx.http.result`, `schema_version=1`. Data includes
`command`, `manifest`, `proxy_from_environment`, `checked`, `passed`, `failed`,
`duration_ms`, `checks`, and `warnings`. Each check reports name, redacted URL,
method, success, status, duration, error kind/message, expected status and body
match. Any transport failure selects overall exit `1`; completed expectation
failures select `4`.

## Examples

```bash
toolx-http check --url http://127.0.0.1:8080/health \
  --no-proxy-from-env --expect-status 200 \
  --expect-body-contains ready --json

toolx-http check --manifest preflight.json --timeout-ms 1000 --json
```

HTTPS requires a selected `httpx` TLS backend. The reproducible showcase uses
loopback HTTP and no environment proxy. See [Dependencies](../dependencies.md)
and [`tests/toolx_http_cli_contracts.cpp`](../../tests/toolx_http_cli_contracts.cpp).
