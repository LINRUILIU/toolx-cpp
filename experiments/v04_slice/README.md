# ToolX v0.4 task validation slice

This experiment compares mature project scripts (A), the same scripts with one
verified ToolX packaging operation (B), and the same recipe/operation in a minimal
sequential runner (C). It is a deterministic engineering slice, not an agent-token
benchmark or a public runner implementation.

The independent project is TinyXML2 10.0.0, pinned to
`321ea883b7190d4e85cae5512a12e5eaa8f8731f`. Its existing CMake/CTest/install and
consumer tests supply the project knowledge equally to all arms. A package is
accepted only after relocated SDK consumption succeeds.

The selected environment is Windows, Python 3.13, CMake 4.2 and Visual Studio 18
2026. No cross-platform claim is made. Existing ToolX binaries are identified by
SHA-256; the experiment does not rebuild or certify a ToolX release.

```powershell
git clone --depth 1 --branch 10.0.0 https://github.com/leethomason/tinyxml2.git temp/tinyxml2-slice
python experiments/v04_slice/check_primitives.py
python experiments/v04_slice/experiment.py `
  --upstream temp/tinyxml2-slice `
  --output temp/new-exclusive-comparison `
  --toolx-pack build/utf8-msvc-20260920/Release/toolx-pack.exe
python experiments/v04_slice/summarize.py temp/new-exclusive-comparison
```

The output directory must not exist. No prior comparison is deleted or reused.
Each arm has the same source inputs, project recipe, build-cache policy, archive
verification, failure gates, and independent outcome oracle. Order rotates by
scenario. Consumer sources are isolated from the upstream header so that quote
includes cannot silently bypass the installed SDK.

The nine scenarios are normal completion, real upstream test regression, the
same failure with an old accepted archive, actual fixture repair, actual archive
I/O failure after staging, retry to a fresh output, corrupted relocated header,
retry with a fresh extraction, and warm repetition. Every scenario is run in all
three arms. The old archive is retained on failure and is not labeled current.

Archive/consumer retries use fresh attempts; they do not repair the previous
failed directory. No resume, crash recovery, process-tree containment, hostile
input isolation, or model trust/cost claim is made. The runner's ledger is an
incremental trace, not a crash-consistency guarantee.

The report distinguishes upstream facilities, shared recipe/verification work,
the ToolX backend, and the runner. Raw commands, outputs, input identities,
receipts, real SDK archives, independent consumer executables and metrics are
retained. `summarize.py` bundles the portable evidence without build caches.
