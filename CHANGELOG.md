# Changelog

All notable user-visible changes to ToolX are recorded here. Detailed release
narratives remain under [`docs/releases`](docs/releases/).

> Status: Released
> Applies to: v0.3.4

## [0.3.4] - 2026-10-22

### Fixed

- Preserve JSON Unicode escapes and surrogate pairs without data loss; escape
  every string control character and reject literal controls and malformed surrogates.
- Reject archive outputs that alias source data, including dangling paths; honor
  extraction's `Skip` policy while consuming skipped payload and padding.
- Reject pack/inspect audit paths, rotation destinations and archive/journal
  conflicts before writes. Plan and dry-run also validate output separation.
- Exclusively create download temporary siblings and preserve existing files on
  failure. Only the final followed response reaches bodies, stream callbacks and
  progress; apply the effective per-request redirect policy on every hop.
- Handle bodyless 204/304 responses, empty header blocks and up to 16 informational
  responses; reject protocol switching.
- Enforce `additionalProperties:false` without a `properties` block and reject
  negative or unrepresentable string-length bounds during schema compilation.
- Dynamically join TaskGroup/task cancellation, classify accepted skipped or
  discarded work after waiting, and flush all logging routes once per sink.
- Match multi-config build/test presets at Debug; document matching Release overrides.

### Compatibility and migration

- Public source signatures, thirteen library targets, six CLI products, JSON
  schemas and default dependency/TLS choices remain unchanged. Rebuild C++ consumers:
  `CancellationToken` has a changed private layout; ABI compatibility is not promised.
- Use separate ordinary input, log, archive and journal paths. Existing temporary
  download files must be inspected before removal or replaced by an unused sibling
  suffix. Directory components in the suffix and Windows stream paths are rejected.
- Review corrected Unicode values, stricter JSON/schema acceptance, final-response
  callback expectations and wait-before-statistics ordering. Running cancellation
  remains cooperative; a normally returning callback counts as completed.
- FSXJ4 format and the v0.3.3 recovery migration remain unchanged. Preserve the
  producing version, journals and dependent witnesses until recovery finishes.
  See the [v0.3.4 migration notes](docs/releases/v0.3.4.md#migration).

## [0.3.3] - 2026-10-20

### Fixed

- Correct configuration traces, overlay ordering, empty-key paths, finite
  JSON/range checks, index overflow and finite-double output.
- Preserve sync failure context and resultx failed fsx reports; report actual
  rollback attempts, completeness, errors and restored business-step counts.
- Replace regular targets with one native operation; use FSXJ4 checked recovery
  with retryable restoration and private content witnesses.
- Reject journal/target aliases and conflicting main/rotation log paths before
  mutation, unresolved logging-boundary links, missing-parent traversal and
  Windows explicit streams; preserve recognized extended-drive roots.
- Reject unsupported Windows streams in configured journals, batch paths and
  selected retained backups before mutation, including when no log is configured.

### Changed

- Add sync validation/execution facts and improve the short automation guide.
  No new product CLI or runner is introduced.
- Treat this work as maintenance. Future `v0.4` needs repeated real duties, a
  mature comparison and held-out net benefit; observed AI-cost savings remain
  unestablished.

### Compatibility and packaging

- New writers use FSXJ4; readers retain J1–3 support, but older tools reject J4.
  Keep the producer and dependent witnesses at their recorded locations until
  recovery completes; recover legacy relative J1–3 records from the original cwd.
- Witnesses add disk/I/O cost. Windows replacement requires system/filesystem
  support and reader delete-sharing; failure does not use a two-rename fallback.
- Previously accepted journaled Windows stream paths now fail before writes.
  Migrate them to ordinary separate files; no-journal and POSIX filename boundaries
  remain unchanged.
- JSON additions remain additive under `schema_version=1`. Corrected trace,
  overlay, numeric spelling and rollback facts and stricter paths are explicit
  correctness exceptions, not zero-migration changes.
- Default build/dependency/TLS choices remain unchanged.
  See the [v0.3.3 release notes](docs/releases/v0.3.3.md) for migration and limits.
- Exclude generated runtime/test-fixture roots and Python bytecode/caches from
  source archives while retaining experiment recipes and legitimate log fixtures.
  CI retains current package/test evidence and executes the installed consumer.

## [0.3.2] - 2026-09-14

### Security hardening

- Reject linked filesystem descendants and paths escaping archive/tree roots.
- Restore file/directory type changes on rollback and recover newly created copy
  parents after interruption using valid FSXJ3 staging records.
- Reject overlapping pack roots, preserve replacements during remove-extra staging,
  retain legitimate backup-like filenames, and fail incomplete enumeration.
- Preserve native POSIX filenames in tree operations and explicit pack include/exclude
  selections while retaining portable tar member restrictions.
- Validate outgoing HTTP targets, headers and multipart metadata; reject blank Host
  values and ambiguous request body framing.
- Strip fragments from wire targets and resolve relative redirects with the full
  base path, correct query inheritance and path-only dot-segment removal.
- Bound multipart collision work and exercise collision/exhaustion fuzz branches.
- Copy scheduler deadlines before unlocking to prevent use-after-free on task removal.
- Require consistent Released metadata across README, CHANGELOG and release notes;
  check release tag/version agreement and generated source archive contents.
- Verify explicit coverage XML/HTML artifacts and authenticate Codecov uploads with
  OIDC. Retain the 70% line and 40% branch coverage floors.
- Fail clang-tidy findings and loopback startup errors; add sanitizer and bounded
  cfgx/httpx/fsx fuzz gates.

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

[0.3.3]: https://github.com/LINRUILIU/toolx-cpp/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/LINRUILIU/toolx-cpp/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.3.1
[0.3.0]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.3.0
[0.2.0]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.2.0
[0.1.0]: https://github.com/LINRUILIU/toolx-cpp/releases/tag/v0.1.0
