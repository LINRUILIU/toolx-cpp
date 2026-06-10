This release ships the current ToolX install set and uses GitHub Release
archives produced from `cmake --install` output.

- Stable core modules remain the boundary described in `docs/stability.md`.
- Shipped product CLIs keep additive JSON envelope compatibility within the
  current minor line.
- New CLI products must satisfy `docs/cli_productization.md` before entering the
  public release matrix.
- ToolX does not promise ABI compatibility across compiler, standard library,
  or build configuration boundaries.
- HTTPS behavior depends on the selected `httpx` TLS backend at build time.
