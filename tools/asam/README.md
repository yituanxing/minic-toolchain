# ASAM v1 — Runner feasibility trial

This folder is a **read-only trial**, not an automatic dead-code remover.

## Purpose

Determine whether a GitHub Actions runner can reproducibly inspect the actual
MiniC compiler without using a persistent ChatGPT conversation or local PC.

## Trial

1. A push to the dedicated experiment branch starts the GitHub Actions workflow.
2. A standard Ubuntu runner installs Universal Ctags and runs a synthetic selftest.
3. The scanner inventories C function definitions within src/target/x86_64.
4. It searches token references across compiler C/H files, creates conservative
   static-function candidates and exact lexical-duplicate candidates.
5. It writes versioned JSON, symbol records and Markdown reports as CI artifacts.

No compiler code is edited and no source candidates are deleted automatically.

## Explicit limitations

- This is **not** a semantic call graph or a reliable dead-code proof.
- The AST/type-aware, multi-config compilation database, AI Scout, Fixer, and
  independent test gates are **not yet implemented**.
- Runner model inference requires a separately configured model provider;
  the retired GitHub Models API must not be used.
- The trial is intentionally isolated; it does not alter existing CI acceptance.

## Next acceptance gates

- Real GitHub Actions run with downloadable report and clean selftest.
- Scout triage of actual candidates with direct source and caller evidence.
- Explicit contract and model credentials before autonomous Fixer is enabled.
- Source changes only in an isolated branch and independently verified PR.

The scanner can run locally as well:

    python3 tools/asam/scan.py --repo . --scope src/target/x86_64 --output asam-out

Requires Universal Ctags installed in PATH. Reports are ephemeral; an
authoritative finding must be recorded separately before a source edit.
