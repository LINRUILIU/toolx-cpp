# CLI Productization Checklist

Use this checklist before adding a new ToolX CLI to the public release matrix.

## Release Admission Bar

A new CLI must have all of the following before it is shipped in GitHub Release
artifacts:

- explicit stability level in documentation
- installable binary target wired through `TOOLX_BUILD_TOOLS`
- black-box contract test that exercises help text, exit codes, and core success and failure flows
- install-tree smoke coverage
- archive-level smoke coverage after unpacking a `CPack` artifact
- standalone CLI reference under `docs/`
- at least one user-facing example in `README.md`, `docs/`, or `examples/`

## Maintainer Template

Use this as the minimum productization template for each new CLI:

1. Product boundary
- Decide whether the tool is stable, bounded stable, or experimental.
- State whether the CLI is a primary contract like `cfgtool` or a scenario tool like `toolx-sync`.

2. Build and install
- Add the executable under `TOOLX_BUILD_TOOLS`.
- Ensure `cmake --install` places it in `bin/`.
- Ensure `CPack` binary archives include it without adding dev-only files.

3. Contract testing
- Add a CTest black-box script similar to `cfgtool_cli_contracts.cmake` or `toolx_sync_cli_scenario.cmake`.
- Cover help output, plain-text output, JSON output if present, and at least one non-zero exit path.

4. Smoke verification
- Extend `cmake/release_smoke.cmake` if the tool is part of the shipped install tree promise.
- Extend `cmake/release_archive_smoke.cmake` if the tool becomes part of the packaged archive verification contract.

5. Documentation
- Add a dedicated `docs/<tool>.md` reference page.
- Add a concise README example.
- Update `docs/stability.md` if the new CLI changes the public boundary.

## Default Decision Rule

If a new CLI does not yet need a long-lived compatibility promise, keep it out
of the public release matrix until the checklist above is complete.
