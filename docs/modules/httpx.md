# httpx

> Audience: C++ clients requiring bounded HTTP workflows
> Status: Bounded-stable module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: supported HTTP behavior and backend boundary

## Role and boundary

`httpx` provides a synchronous HTTP client with request limits, redirects,
proxy routing, retry policy, circuit breaking, redacted diagnostics, safe file
download, multipart file upload, and optional TLS. It is not a browser stack,
HTTP/2 framework, asynchronous reactor, OAuth client, or general `curl`
replacement.

- CMake target: `toolx::httpx`
- Direct dependency: `toolx::utils`; transitive ToolX dependencies: none;
  Windows also links `ws2_32`
- Optional public dependencies: OpenSSL or mbedTLS
- Stability: bounded stable
- Header: [`include/httpx.h`](../../include/httpx.h)

## Quick start

```cpp
httpx::ClientOptions options;
options.timeout.total_ms = 2000;
options.use_proxy_from_environment = false;
httpx::Client client(options);

const auto response = client.Get("http://127.0.0.1:8080/health");
if (!response.ok) return 1;
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Requests | `Request`, `Response`, `HttpMethod`, `HeaderList`, `Client::Send` | Per-request redirect and progress callbacks are available |
| Convenience methods | `Get`, `Post`, `Put`, `Patch`, `Delete`, `Head`, `Options`, `Connect`, `Trace` | Synchronous wrappers over `Send` |
| Client policy | `ClientOptions`, timeout/redirect/proxy/pool/TLS/limit structs | Client owns a copy of options |
| Retry | `RetryPolicy`, legacy retry field, `should_retry`, attempt callback | Retryability combines error metadata and configured policy |
| Circuit breaker | `CircuitBreakerOptions`, `CircuitSnapshot` | Shared per client instance and lock-protected |
| Transfers | `DownloadFile`, `DownloadOptions`, `UploadFile`, `MultipartPart` | Download publishes from a temp file; upload reads the file into memory |
| Configuration | `FlatConfigEntry`, `ParseOptionsFromFlatConfig`, `ApplyFlatConfig` | Small integration surface, not a general config parser |
| Diagnostics | `LogEvent`, `FailureStats`, logger callback, `RedactUrl`, `RedactHeaderValue` | Sensitive values are redacted by default |
| Errors | `ErrorKind`, `Error`, `Status`, `Result<T>`, `ToString` | Preserves retryability and optional HTTP status |

## Network, proxy and TLS behavior

- Requests are synchronous and may block until configured timeouts expire.
- Environment proxy handling is enabled by default and can be disabled through
  `ClientOptions::use_proxy_from_environment`. Explicit proxy options remain a
  separate policy input.
- Applications should disable environment proxy inheritance for deterministic
  loopback checks when host policy requires it.
- HTTPS is available only when the library is built with one TLS backend.
  OpenSSL and mbedTLS are mutually exclusive; default packages select neither.
- `verify_peer` and `verify_host` default to true. Disabling verification weakens
  transport security and is not recommended for production.
- Limits cap response header/body accumulation. Large streaming bodies and
  multipart streaming are outside the current boundary.

## Threading, files and failure behavior

- Failure statistics and circuit snapshots are protected for concurrent query;
  callers must still coordinate lifecycle and mutable external callbacks.
- Custom transport and logger callbacks run synchronously and must outlive the
  request that invokes them.
- `DownloadFile` writes a temporary sibling and replaces the destination only
  after a complete successful response. Every retry recreates the temp file.
- URL/header redaction is a diagnostic safeguard, not a substitute for avoiding
  secrets in URLs.

## Stability, compatibility, examples, and version changes

HTTP/1-style request/response behavior and the documented policy surface are
bounded stable. Backend availability, external server interoperability and
network environment remain outside source compatibility.

`v0.3.2` adds `ClientOptions::use_proxy_from_environment`. Existing request
behavior remains unchanged when callers leave it enabled.

- [Annotated offline cookbook](../../examples/httpx_cookbook.cpp)
- [Network example](../../examples/httpx_example.cpp)
- [Remote cfgx bridge](../../examples/cfgx_httpx_remote_example.cpp)
- [Behavior and loopback tests](../../tests/httpx_tests.cpp)
- [Dependencies and TLS](../dependencies.md)

## Outgoing HTTP validation

Before each transport call (including redirects), raw URL whitespace/control
characters, invalid header names and header value controls are rejected. Horizontal
tab remains allowed in header values. Multipart names, filenames and content types
reject all controls. Binary bodies and multipart data remain byte-preserving.

Request `Transfer-Encoding` is unsupported. A supplied `Content-Length` must be a
single decimal value matching the body size; duplicate lengths are rejected. For
multipart requests the client owns `Content-Length` and `Content-Type` (including
the boundary), so caller overrides are rejected. Invalid metadata returns
`InvalidArgument`; invalid URLs return `InvalidUrl`. Validation also applies to
custom transports. Multipart boundaries are checked against part data.
