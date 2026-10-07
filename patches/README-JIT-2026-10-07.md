# ARM32 JIT follow-up patches (2026-10-07)

These files preserve the JIT investigation independently of unrelated, uncommitted UI and port work. They are not a new complete replacement for `wk-winuwp.patch`.

- `2026-10-07-arm32-putbyval-r9.patch`: engine fix, applied from the WebKit checkout root. It changes only `Source/JavaScriptCore/jit/BaselineJITRegisters.h`. The local upstream base is webkitgtk-2.54.0, commit 5220e80b97. The patched JSC built successfully with `ninja -j4 JavaScriptCore`; device validation of the fix is still pending.
- `2026-10-07-jit-diagnostic-driver.patch`: this investigation's driver changes relative to the local pre-investigation 0.2.5.11 driver, including cold-start mode pinning and bounded crash evidence capture. **Requires that local driver's existing JIT gating/pool diagnostics; it is not standalone against the repository HEAD.** The pre-investigation copy remains in `crash/jit-analysis-20261007/WebCoreDriver.before.cpp` on the build machine.
- `2026-10-07-jit-diagnostic-host.patch`: move crash/config initialization before profile initialization and advance the local 0.2.5.11 manifest to 0.2.5.14. Applied from the Apotheosis root against that local diagnostic baseline.

All three patches are already applied to the build machine's working files. Do not apply them again there. The two diagnostic patches preserve only this session's increment rather than importing unrelated unfinished changes. The repository source files have not been replaced by the local pre-investigation state.

Read [root-cause analysis](../docs/JIT-ARM32-R9-ROOT-CAUSE-2026-10-07.md) and [handoff](../docs/HANDOFF-JIT-2026-10-07.md) before resuming. Raw device evidence and matching EXE/PDB files stay local under the ignored `crash/` directory; the analysis contains the relevant instruction sequence and register values.
