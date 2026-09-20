# ToolX Product CLI References

> Audience: CLI users, operators, and automation authors
> Status: Canonical product reference index
> Applies to: `v0.3.2` release
> Source of truth for: CLI reference ownership

| CLI | Stability | Workflow | JSON schema |
| --- | --- | --- | --- |
| [`toolx-config`](toolx-config.md) | Primary stable contract | Config authoring, review and snapshots | `toolx.config.result` |
| [`toolx-sync`](toolx-sync.md) | Bounded stable | Layer composition and atomic publish | `toolx.sync.result` |
| [`toolx-pack`](toolx-pack.md) | Bounded stable | Local staging and deterministic tar | `toolx.pack.result` |
| [`toolx-http`](toolx-http.md) | Bounded stable | HTTP endpoint preflight | `toolx.http.result` |
| [`toolx-log`](toolx-log.md) | Bounded stable | Offline log diagnosis and gates | `toolx.log.result` |
| [`toolx-inspect`](toolx-inspect.md) | Bounded stable | Config/schema report and terminal rendering | `toolx.inspect.result` |

Use the [cross-CLI matrix](matrix.md) to compare exit codes, precedence,
network behavior, file writes and dry-run guarantees.

Library `Status`/`Result` types do not define CLI exit codes. Each product owns
its envelope and tested output contract.

## Windows path encoding

Builds from this source tree embed a UTF-8 process manifest in all six CLIs.
On Windows 10 version 1903 and later (including Windows 11), command-line
arguments, narrow filesystem paths and JSON output use UTF-8, including Chinese
and supplementary characters in paths. UTF-8 configuration and log content is
preserved without additional transcoding. Changing the console code page with
`chcp` is not required for redirected JSON output; interactive display still
depends on the terminal's decoding and font.

Windows versions before 1903 ignore this setting and are not covered by this
Unicode path guarantee. The manifest is private to the CLI executables; linking
a ToolX library does not change the host application's code page. See Microsoft's
[process code page documentation](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page).

Windows CTest builds with tools enabled run `toolx_windows_utf8_contracts`. This
captures raw process output, strictly decodes UTF-8, compares the exact paths and
payloads, and checks file writes and the HTTP timeout envelope. The same script
can verify installed binaries with `-BinDir <install>/bin -WorkDir <test-dir>`.
