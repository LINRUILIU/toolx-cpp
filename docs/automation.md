# Using ToolX in automation

> Audience: AI coding agents and project-script authors
> Status: Bounded usage guide; development changes are marked explicitly
> Applies to: v0.3.4 release
> Source of truth for: choosing ToolX responsibilities and validating config candidates

Start with the project's existing build, validator and release script. Use ToolX
when one of its bounded operations removes work from that task. A machine-readable
success is evidence of that operation, not evidence that the whole task passed.

## Choose the smallest useful operation

| Need | Use |
| --- | --- |
| Project-native merge already has the required semantics | The project's merge command |
| Ordered generic layers, later scalar wins, recursive objects | `toolx-sync`, or an existing mature merge library |
| Arrays replace | Default ToolX composition |
| Arrays append in input order | Explicit `--append-arrays`; empty arrays do not clear earlier arrays |
| Full project types, references, regex and runtime initialization | Project-native validation and actual behavior checks |
| Full JSON Schema | A standard validator; schemax supports a [bounded subset](modules/schemax.md) |
| Exact file-level origin or publication of previously checked bytes | Keep these responsibilities in the project recipe; current sync does not promise them |

Check actual `toolx-sync --help` and the [CLI reference](cli/toolx-sync.md).
There is no need to learn all thirteen libraries or six CLIs for one operation.
Source traces name layers (`base`, `remote`, `local`), not overlay filenames.

## Configuration candidate recipe

1. Freeze the selected input files in a private attempt directory. Preserve the
   required file order and array policy explicitly.
2. Compose a private candidate, for example:

   ```sh
   toolx-sync --json --base work/base.json --overlay work/local.json \
     --append-arrays --out work/candidate.json
   ```

3. Run the project's own validator on that candidate. If validation does not
   initialize the required runtime behavior, start an isolated instance and test
   that behavior. Bind evidence to that new instance, not an old running service.
4. Let the existing project/OS publication mechanism consume those exact accepted
   bytes. Check candidate/input/target identity as required by the project. A
   separate sync invocation re-reads and re-serializes its input; it is not a
   checked-bytes publication API.
5. On failure, preserve the last accepted target and enough candidate/diagnostic
   evidence to repair the cause. Do not count a previous candidate or output as
   this attempt's success. Skip unchanged publication when the recipe requires it.

The recipe is project-owned orchestration, not a new ToolX runner. A hash check
before replacement is not an atomic compare-and-swap. Multi-file writes and
service reloads require their own acceptance and failure semantics.

## Interpret failures without guessing

All sync JSON envelopes contain `ok`, `code`, `message`, `issues` and `data`.
Use exit codes and fields; do not classify an error by matching English prose.
For v0.3.2, a publish failure can have empty `data`, so independent file checks
are necessary to determine what was restored.

The v0.3.3 maintenance release retains composed context on validation failures and adds
`validation_passed`, `publish_attempted`, `stage` and `execution` where applicable.
Execution facts distinguish forward completion from rollback. Read
`rollback_attempted`, `rollback_complete` and per-step errors separately. These
facts do not establish project-native acceptance or current service state.

`--snapshot` is a second copy of the new resolved configuration, not a backup.
Dry-run still reads inputs and explicitly requested remote data; it does not
prove output writability or bind the next invocation. The v0.3.3 atomic writes
reject existing directories and symlinks; do not use an empty directory as a
portable failure fixture. See [filesystem recovery boundaries](modules/fsx.md).

This guide describes existing operations; it does not establish an AI adoption or
cost advantage. Release compatibility and recovery-version migration are in
the [v0.3.3 notes](releases/v0.3.3.md). A future `v0.4` product needs repeated real
tasks and a material held-out benefit over mature tools or reusable scripts.
