# fsx

> Audience: C++ tools that need planned filesystem mutation
> Status: Stable core module guide
> Applies to: `v0.3.2` release candidate
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
| Recovery | `RecoverFromJournal`, `RecoverOptions` | Supports retained FSXJ1/2 and synchronized FSXJ3 records |
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
- When `journal_path` is configured, FSXJ3 writes and synchronizes undo records
  before each destructive primitive mutation and persists `COMMIT` before normal
  cleanup.
- FSXJ3 recovery refuses to overwrite a destination that appeared after its
  journal entry. It reports a conflict and keeps evidence for inspection.
- This ordering is recoverable, but it is not a universal claim about drive
  caches, every filesystem, cross-device rename, or sudden hardware loss.
- Archive extraction rejects rooted/escaping paths, including escape through
  pre-existing symlinks where the platform exposes enough information.

## Stability, compatibility, examples, and version changes

Batch/run/recovery, directory operations, deterministic tar and polling watcher
basics are stable. Archive format support is intentionally tar-only.

`v0.3.2` changes configured journal behavior to synchronized FSXJ3 write-ahead
undo records with conflict-aware recovery. FSXJ1/FSXJ2 recovery remains
supported; the public plan API is unchanged.

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

Copy-file transactions create missing parents through staging directories and record
staging REMOVE plus inverse MOVE entries using the existing FSXJ3 format. Both
rollback and crash recovery remove those directories only when empty, allowing an
old file replaced by a directory to be restored. A conflicting path preserves the
journal for inspection; ordinary parent paths are never recorded as REMOVE entries.
