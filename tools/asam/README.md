# ASAM — permanent mission + forgettable execution prototype

**Purpose:** a fresh AI conversation or agent must resume the **same whole-compiler mission**
without inheriting any previous conversation. This is a deterministic Supervisor
experiment, not yet an autonomous multi-agent engine.

## Sources of authority (read in this order)

1. tools/asam/control/policy.json — **L0 locked mission**: whole MiniC compiler,
   breadth-first coverage, original constraints, S0–S5 audit stages, agent roles.
   The Supervisor pins its complete SHA-256; changes require an explicit review.
2. Existing canonical MiniC architecture / issues / function-contract / refactor
   records linked in policy.json. They alone own technical facts, established
   problems and exclusions. ASAM must NOT duplicate the issue register.
3. tools/asam/control/state.json — **scheduling and coverage cursor only**.
   C01–C09 are planning-level functional regions; all start as PLANNED.
   No region has been claimed verified by this experiment.
4. Actual versioned source, CI logs, test matrix and external runtime oracles
   determine code correctness. An AI assertion is not runtime evidence.

This portable prototype lives in a dedicated unmerged MiniC branch because
the available GitHub connector does not support creating a separate repository.
Do not merge it into production without independent design review.

## Restart in a fresh process — no past chat required

    python3 -I tools/asam/supervisor.py --root . check
    python3 -I tools/asam/supervisor.py --root . resume
    python3 -I tools/asam/supervisor.py --root . status
    python3 -I tools/asam/selftest_supervisor.py

The resume packet contains permanent global mission, canonical doc pointers,
all mandatory constraints, source snapshot, progress for all regions, a breadth
frontier and the next task. It contains **no prior model conversation or
expanded source bodies**.

## Session protocol

1. Close the old agent session and its huge expanded source context.
2. Supervisor check must PASS; a failure blocks the task.
3. Resume from durable records in a new process, reload the canonical
   architecture map/issues/contracts, and then inspect live source.
4. Breadth-map ALL unfinished functional regions before substituting isolated
   file cleanup for the whole-project goal. Form complete root-cause clusters.
5. Keep real ISA/ABI differences; never delete an optimization for LOC.
6. Record bounded versioned evidence, independent challenge and required tests.
7. End the task-local model context; retain only durable facts, evidence pointers,
   reviewed exclusions and unfinished scope. Select the next global task.

## Enforced state transitions

A proposed transition has exactly these fields:
schema, mission_id, expected_epoch, slice_id, to_status, actor_role,
actor_id, evidence.

Allowed stages: PLANNED -> MAPPED -> INVESTIGATING -> CONFIRMED ->
FIXED_UNVERIFIED -> VERIFIED, plus limited direct verified exits with full
evidence for a completely reviewed slice with no fix.

Functional mapping requires producers, consumers and full path evidence.
Confirmation requires a separate challenger. Fixes need exact commits.
Verification requires a separate verifier and tested commit, real
input/oracle matrix and negative evidence. **Terminal VERIFIED is currently
DISABLED** until external CI evidence can be authenticated by a trusted
integration: model-generated links/names could otherwise forge "success".
An unverified change is NEVER marked verified.

Run a reviewed transition locally with:

    python3 -I tools/asam/supervisor.py --root . apply --proposal path/to.json

The Supervisor updates only a **local atomic file**. Committing back to GitHub
requires authorized access and a review. Concurrent stale epochs are refused.
A worker cannot close the global mission, even with all regions complete.

## Active forgetfulness acceptance

selftest_supervisor.py uses independent Python processes with no history,
mutates temporary queue files, starts another process, checks that the original
global goal, source baseline and remaining scope survive, and tries forbidden
goal replacement, false completion, fabricated verification, dropped coverage
and policy tampering. Failure to reject any case fails CI.

## Limitations

- This is not yet a full source-level functional and ownership graph.
- Not yet an authenticated GitHub-hosted persistent scheduler.
- No independently instantiated LLM subagents or unattended API-driven work
  exists; ChatGPT currently serves as interactive controller / reasoner.
- Checks of evidence links are structural, not external truth validation.
- Tests do not certify a semantic fix or validate unrun architectures.
- Policy is mechanically pinned, but the experiment branch is not an
  independently protected trust anchor.

The historical merged LoongArch scalar fix PR #711 is a successful **local
subtask**, not evidence that the whole Target region or full MiniC was audited.
