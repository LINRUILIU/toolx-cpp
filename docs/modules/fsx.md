# fsx

> Audience: C++ tools that need planned filesystem mutation
> Status: Stable core module guide
> Applies to: `v0.3.3` release
> Source of truth for: intended `fsx` use and recovery boundaries

## Role and boundary

`fsx` plans and executes file operations with explicit conflict and rollback
policy. It also provides directory walk/diff/sync, deterministic tar archives,
links, capability reporting and a polling watcher. It is not a database
transaction engine, package manager, universal power-loss-atomic layer, or
native event watcher abstraction.

- CMake target: `toolx::fsx`
- Direct dependency: `toolx::utils`; transitive ToolX dependencies: none
- Stability: stable core
- Header: [`include/fsx.h`](../../include/fsx.h)

## Quick start

```cpp
fsx::BatchPlan plan;
plan.AddAtomicWrite("out/config.json", "{\"ok\":true}\n");

fsx::RunOptions options;
options.conflict_policy = fsx::ConflictPolicy::Overwrite;
options.journal_path = "out/config.journal";
const auto result = fsx::Run(plan, options);
```

## Capability and API matrix

| Area | Public API | Notes |
| --- | --- | --- |
| Planning | `BatchPlan`, `OpType`, fluent add-operation methods | Plan creation itself does not mutate files |
| Execution | `Run`, `RunOptions`, `RunResult`, `StepReport` | Conflict and rollback policy are explicit |
| Recovery | `RecoverFromJournal`, `RecoverOptions` | Reads FSXJ1–4; new runs use synchronized FSXJ4 records |
| Directory inspection | `WalkDirectory`, `WalkOptions`, `WalkResult` | Returns relative entries according to options |
| Diff/sync | `BuildDirectoryDiff`, `DirectoryDiff`, `BuildSyncPlan` | Sync plan may include removal of extra destinations |
| Archives | `CreateArchive`, `ExtractArchive`, `ArchiveOptions` | Deterministic tar MVP; ZIP capability is false |
| Links | `CreateLink`, `LinkType` | Platform capability and permission dependent |
| Watching | `IFileWatcher`, `CreateFileWatcher`, `WatchEvent` | Polling implementation and bounded event model |
| Capabilities | `QueryCapabilities`, `CapabilityInfo` | Call before assuming archive/link behavior |

## Mutation and recovery semantics

- `Run` may create, replace, move, copy or remove files described by its plan.
- `ConflictPolicy` determines what happens when targets already exist;
  `RollbackMode` determines failure recovery effort.
- When `journal_path` is configured, FSXJ4 writes and synchronizes undo records
  before each destructive primitive mutation and persists `COMMIT` before normal
  cleanup.
- FSXJ3/4 MOVE recovery refuses to overwrite a destination that appeared after its
  journal entry. It reports a conflict and keeps evidence for inspection.
- This ordering is recoverable, but it is not a universal claim about drive
  caches, every filesystem, cross-device rename, or sudden hardware loss.
- Archive extraction rejects rooted/escaping paths, including escape through
  pre-existing symlinks where the platform exposes enough information.

The journal must be separate from every batch source, destination, selected
source/destination tree, and retained backup path. `Run` checks this before
writing the header or executing any step. The check resolves existing path
components, compares ancestor relationships, detects existing hard-link aliases,
and uses Windows Unicode case-insensitive comparison on Windows. These are
static checks; they do not provide containment against another process changing
paths after validation. The caller must keep these paths under exclusive control.

On Windows, a configured journal also makes explicit stream paths unsupported in
the journal, every batch source/destination, and each selected retained backup.
`Run` rejects these paths before writing the header or executing a step, including
default streams such as `file::$DATA`, named streams, streams in path components,
and paths whose files do not yet exist. A stream shares its host file's replacement
lifetime; accepting a different stream name cannot establish independent recovery
storage. This check belongs to `fsx` and applies independently of logging.
Ordinary drive roots and recognized extended-drive roots such as
`\\?\C:\path\file` retain their designator colon. No-journal behavior and POSIX
literal-colon filenames keep their existing boundary; this repair adds no stream
support to either mode. Previously accepted journaled Windows stream paths now
fail before mutation. Migrate the journal, batch paths and selected backups to
ordinary, separate files rather than omitting an unrelated log option.

New FSXJ4 undo records contain absolute paths, including private witnesses and
restoration staging paths. Recovery can run from a different working directory;
relative paths in FSXJ4 records are rejected before mutation. Keep the recorded
targets, witnesses and staging trees at their original locations until recovery
finishes: absolute paths do not remap a relocated tree. FSXJ1–3 remain readable,
but their historical relative records have no stored origin directory. Recover
those journals from the original working directory; supplying an absolute path
to the journal alone does not resolve this ambiguity.

FSXJ4 witness stamps and private temporary names serialize integer fields with
the classic locale, independently of the application's global locale. Recovery
and committed-journal reuse therefore do not require the producing locale.
Malformed localized stamps from an earlier development candidate remain
invalid; recovery does not guess their intended witness identities.

`AtomicWrite` replaces an existing regular file with one same-directory native
replacement operation. The public target stays available with its old or new
contents; this does not make a multi-file batch atomic. Existing directories and
symbolic links are rejected. Windows uses `FileRenameInfoEx` with POSIX replacement
semantics; the running Windows system and filesystem must support that information
class and its flags. Unsupported systems return failure without a multi-rename fallback.
Readers must allow delete sharing for a
replacement to succeed; a sharing violation returns failure while preserving the
old target. This applies equally with and without a journal.

For overwrite rollback, FSXJ4 retains private old/new witness copies, verifies
their native file identity, size and write/change timestamps, and compares target
bytes exactly against those witnesses. An old-equivalent target needs no rewrite;
a new-equivalent target is restored by one native replacement. A missing,
non-regular or different target, or a modified witness, reports a conflict and
keeps the journal and dependent witnesses. Same-content replacements are
equivalent within this contract. The caller owns these paths exclusively;
this is not a compare-and-swap API against concurrent writers or a defense against
deliberate witness/metadata tampering.

The new `UNDO|REPLACE` record references old/new witnesses and a private restoration
staging path. A synchronized `RESTORED` record follows a successfully checked
undo before witness cleanup. Recovery can therefore retry after either restoration
or partial cleanup. Unknown/malformed records and duplicate or out-of-order
`RESTORED` references are rejected before mutation. FSXJ1–3 remain readable;
older ToolX versions reject the FSXJ4 header. Keep the producing version available
until recovery finishes. Overwrites temporarily use two witness copies in addition
to the staged candidate; content restoration does not promise complete ACL or
platform metadata preservation.

`RunResult.rollback_attempted` and `rollback_complete` report whether undo ran and
whether all recorded undo work succeeded. A failure preserves its original error
and adds `rollback_error`; each `StepReport` carries its own attempt/error facts.
`rolled_back` is true only when the report's complete undo range succeeded and
changed a path. This can include a failed forward step whose partial changes were
restored; a skipped forward step is never rolled back. `Run.rolled_back_steps`
counts restored business steps; recovery reports/counts individual undo records.
Best-effort in-process rollback continues independent paths after a conflict but
retains dependent evidence. Strict rollback stops immediately. FSXJ3/4 recovery
stops on a conflict; legacy best-effort recovery may continue independent undo
work but still returns failure and retains its journal if any undo fails.

## Stability, compatibility, examples, and version changes

Batch/run/recovery, directory operations, deterministic tar and polling watcher
basics are stable. Archive format support is intentionally tar-only.

`v0.3.2` changes configured journal behavior to synchronized FSXJ3 write-ahead
undo records with conflict-aware recovery. FSXJ1/FSXJ2 recovery remains
supported; the public plan API is unchanged.

The `v0.3.3` maintenance release adds FSXJ4 checked single-file replacement and
truthful rollback reports. It keeps the public planning API and legacy recovery
support. Older recovery tools reject J4; retain the producer and original witness
locations until recovery completes. The added witnesses and synchronized records
increase disk and I/O use. These are explicit correctness/compatibility changes,
not a new orchestration product. See the [release migration notes](../releases/v0.3.3.md).
A retained committed journal remains audit-only and cannot be recovered.

- [Annotated cookbook](../../examples/fsx_cookbook.cpp)
- [Basic example](../../examples/fsx_example.cpp)
- [Behavior tests](../../tests/fsx_tests.cpp)
- [Crash-recovery tests](../../tests/fsx_journal_failpoint_tests.cpp)

## Tree operation boundary

Directory walks, directory diff/sync, tree copies and archive creation reject
symbolic links (including dangling links), Windows junctions/reparse points and
unsupported file types below the selected root. Relative entry names are lexical
and cannot contain parent traversal or absolute roots. Native POSIX tree operations
preserve literal colon and backslash filenames. Tar member names additionally reject
colon and backslash so archives stay unambiguous across platforms.
Enumeration and metadata failures return errors instead of partial success.

Roots are caller-selected locations. These checks do not provide a sandbox
against another process replacing directories between validation and use. Keep
source and destination trees under exclusive control while planning and executing
operations. Direct `BatchPlan` file operations and recovery journals remain trusted
caller inputs; they do not infer a containment root.

Copy-file and atomic-write transactions create missing parents through staging directories and record
staging REMOVE plus inverse MOVE entries using the existing FSXJ3 format. Both
rollback and crash recovery remove those directories only when empty, allowing an
old file replaced by a directory to be restored. A conflicting path preserves the
journal for inspection; ordinary parent paths are never recorded as REMOVE entries.
