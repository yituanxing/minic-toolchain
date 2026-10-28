# MiniC feature-batch granularity and evidence

The default development target is **800–2000 effective changed lines per
coherent feature batch**. This is a delivery cadence target, not an excuse to
duplicate implementations or include unrelated cleanup. A major architecture
transition can be larger; a security or correctness hotfix can be smaller.

Run before committing a local batch:

```bash
python3 tools/ci/minic-batch-granularity.py --base HEAD --worktree --strict
```

Run after a commit (or over an explicit Git range):

```bash
python3 tools/ci/minic-batch-granularity.py --base HEAD^ --head HEAD --strict
python3 tools/ci/minic-batch-granularity.py --base <base-sha> --head <tip-sha>
```

The reporter counts additions **and** deletions. Implementation in `src/`,
`include/`, `tools/` and executable verification in `tests/` count toward
the effective line total; CI YAML, prose documentation and non-code files
are reported separately and cannot pad it. It also counts untracked new code
in `--worktree` mode, handles Git rename records and ignores binary files.

`--strict` exits nonzero outside the 800–2000 band. Native GitHub Actions
executes regression tests for the auditor and **reports** the current batch
without making the size a hard CI gate. Genuine hotfixes remain possible
without mixing in unrelated changes merely to reach a numeric quota.

## Architectural checks required for each batch

1. Identify the original GNU/Linux instruction template and its C types,
   register constraints, labels, memory clobbers and ordering semantics.
2. Qualify it in the target-only ASM contract; transport **typed roles**
   through Common Core without copying target machine instructions there.
3. Emit real target ISA instructions and review both admissible variants and
   negative template/constraint mutations. Never claim an opaque ASM pass.
4. Run strict native compilation, all affected source-only probes, and the
   foreign GNU assembler / QEMU probes on CI before counting runtime success.
5. Distinguish kernel corpus `C->.o` pass counts from whole image linking,
   guest boot and runtime parity. They are separate evidence levels.

The batch report is deliberately **not** a substitute for any of these
tests. It only prevents the progress update from hiding the magnitude of
the actual changes behind documentation or generated files.
