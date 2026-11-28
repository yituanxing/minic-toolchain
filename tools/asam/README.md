# ASAM v1.1 — tool-driven repository triage trial

Status: Read-only trial using a real GitHub Actions runner in Draft PR #710.

This is NOT an autonomous refactoring pipeline and certifies no function as dead
or semantically duplicated.

## Current execution

Workflow: .github/workflows/asam-v1-readonly.yml

1. GitHub push/PR starts an isolated Ubuntu Runner.
2. scan.py uses Universal Ctags for C symbol definitions, source positions and
   conservative static token-reference candidates.
3. near_duplicates.py separates structural similarity from semantic anchors:
   called functions, field names, constants and literal values. Merely sharing
   a control-flow template is reported in a separate lower-priority category.
4. review_packets.py builds up to 12 bounded evidence packets with source
   excerpts, exact revision, source digest and lexical reference candidates.
5. review_dispositions.json stores already reviewed distinct cases; unchanged
   source digests suppress repeat triage, changes invalidate the suppression.
6. selftest.py and selftest_structural.py verify these deterministic behaviors.
7. scout_api.py produces DRY_RUN without a credential. It cannot approve a
   source change even when operator-initiated model triage is enabled.
8. JSON/Markdown/symbol index artifacts are uploaded for inspection.

No production compiler source file is modified by this workflow.

## Local test commands

Requires Python 3.10+ and Universal Ctags:

    python3 tools/asam/selftest.py
    python3 tools/asam/selftest_structural.py
    python3 tools/asam/scan.py --scope src/target/x86_64 --output asam-out
    python3 tools/asam/near_duplicates.py
    python3 tools/asam/review_packets.py
    python3 tools/asam/scout_api.py

An explicit opt-in for at most 3 packets (not enabled in CI):

    export ASAM_OPENAI_API_KEY=<credential; never commit>
    export ASAM_SCOUT_MODEL=<supported Responses API model>
    python3 tools/asam/scout_api.py --execute --max-packets 2

The adapter uses a strict JSON response schema, supplies no tools, never
changes source and classifies evidence as DISTINCT_KEEP, POSSIBLE_OVERLAP or
INSUFFICIENT_EVIDENCE. No paid model call occurs in the default workflow.
Do not expose inference credentials to untrusted PR code.

## Generated reports

- asam-out/report.json: source inventory
- asam-out/symbols.json: C symbol definitions and source ranges
- asam-out/near-duplicates.json: ranked hypotheses and pattern-only examples
- asam-out/scout-packets.json: bounded review evidence and source digests
- asam-out/scout-queue.md: compact review queue
- asam-out/scout-model-status.json: explicit inference status

## Limitations and next gates

This is not an exhaustive call graph, AST or data-flow analysis.
Indirect calls, feature flags, inline assembly, linker roots and ABI contracts
must be checked independently. Similarity is not proof of duplicate semantics.

MiniC's existing issue and architecture registers remain authoritative.
Nothing in this folder automatically retires a function, changes production
logic or redefines existing test acceptance.

Before automatic Fixer work can be enabled: connect an authorized model
provider; establish verifiable call/data-flow evidence; use an isolated
worktree; require an independent Verifier and relevant build/runtime/performance
gates; preserve the existing production CI.
