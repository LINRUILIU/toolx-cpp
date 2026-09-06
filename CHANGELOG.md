# Changelog

All notable user-visible changes to ToolX are recorded here. Detailed release
narratives remain under [`docs/releases`](docs/releases/).

## [Unreleased]

Target: `v0.3.2` release candidate. The latest tagged release is `v0.3.1`.

### Security hardening

- Prevent overlapping pack roots and preserve replacements during remove-extra staging.
- Restore type conflicts on rollback, including newly created copy parent directories.
- Strip HTTP fragments, reject blank Host values and bound multipart collision work.
- Preserve native POSIX tree filenames while retaining portable tar restrictions.
- Reject candidate releases at publication and verify explicit coverage report artifacts.

- Copy scheduler deadlines before unlocking to prevent use-after-free on task removal.

- Reject linked filesystem descendants and unsafe relative archive/tree paths.
- Validate outgoing HTTP metadata and reject ambiguous request body framing.
- Preserve pack artifacts whose names resemble backups; fail incomplete enumeration.
- Check release tag/version/notes and generated source archive contents.
- Fail clang-tidy and loopback startup errors; add sanitizer and bounded fuzz gates.

### Added

- `toolx-http check --no-proxy-from-env` and manifest-level
  `use_proxy_from_environment` control.
- `toolx-sync --no-proxy-from-env` for optional remote configuration requests.
- Dedicated Linux/OpenSSL loopback TLS coverage for trusted localhost and
  hostname-mismatch rejection.
- Layered documentation portal, module/CLI guides, dependency compatibility
  reference, and reproducible product-chain showcase.

### Changed

- Configured `fsx` journals now use synchronized FSXJ3 write-ahead undo records
  before destructive primitive mutations.
- FSXJ3 recovery preserves evidence and reports a conflict when a destination
  appears after the journal entry.
- `toolx-http` and `toolx-sync` JSON data add the additive
  `proxy_from_environment` field.

### Compatibility

- No public C++ API or CLI envelope is removed or redefined.
- FSXJ1 and FSXJ2 recovery compatibility remains available.
- Default builds and release packages still select no TLS backend.

## [0.3.1]

### Fixed

- Corrected filesystem journal undo retention, completion handling and rollback
  order for overwritten targets.
- Hardened tar extraction against rooted paths and paths escaping through
  existing symlinks.
- Recreated HTTP download temporary files for each retry.
- Treated no-argument log messages as text rather than dynamic format strings.
- Hardened MSVC environment handling, GitHub Action pinning and GoogleTest
  archive verification.

## [0.3.0]

### Added

- Finalized the six-CLI product chain: `toolx-config`, `toolx-sync`,
  `toolx-pack`, `toolx-http`, `toolx-log`, and `toolx-inspect`.
- Added install-tree and packaged-archive smoke verification for the product
  set.

### Compatibility

- Stable modules and tested CLI envelopes entered the documented `0.3.x`
  additive source-compatibility line.

## [0.2.0]

### Added

- Experimental `schemax` schema compilation and validation.
- Module cookbooks, richer `tuix`, cancellation/task groups, filesystem sync
  and tar support, HTTP retry/circuit/download/upload helpers, and structured
  logging context/spans/metrics.

## [0.1.0]

### Added

- First productized library set and stable `toolx-config` CLI contract.
- Initial bounded `httpx`, experimental `tuix`, and scenario-oriented
  `toolx-sync` surface.

[Unreleased]: https://github.com/LINRUILIU/toolx-cpp/compare/v0.3.1...HEAD
[0.3.1]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.3.1
[0.3.0]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.3.0
[0.2.0]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.2.0
[0.1.0]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.1.0
