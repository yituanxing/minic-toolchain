# ASAM — permanent mission + forgettable execution prototype

**Purpose:** a fresh AI conversation or agent must resume the **same whole-compiler mission**
without inheriting any previous conversation. This is a deterministic Supervisor
experiment, not yet an autonomous multi-agent engine.

## Sources of authority (read in this order)

1. tools/asam/control/policy.json — **L0 locked mission**: whole MiniC compiler,
   breadth-first coverage, original constraints, S0–S5 audit stages, agent roles.
   The Supervisor pins its complete SHA-256; changes require an explicit review.
   **This is NOT a cryptographic trust root**: the prototype code and policy
   live on the same mutable experimental branch.
2. Existing canonical MiniC architecture / issues / function-contract / refactor
   records linked in policy.json. They alone own technical facts, established
   problems and exclusions. ASAM must NOT duplicate the issue register.
3. tools/asam/control/state.json — **scheduling and coverage cursor only**.
   C01–C10 are planning-level functional regions. The actual checkpoint has
   **C01 MAPPING_PROPOSED**, nine others PLANNED, and zero verified.
   S0 v2 evidence-based scope refinement is pinned at epoch 3.
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

## Rebuild the whole-compiler Mapper handoff (CI-verified)

The S0 tracked-source inventory is **derived** and may be regenerated at
the pinned source revision; it never becomes a second authoritative issue
register or a declaration of semantic review completion.

    python3 -I tools/asam/selftest_topology.py
    python3 -I tools/asam/topology.py --root . --output asam-supervisor-out

The output contains three artifacts: breadth-inventory.json,
breadth-inventory.md and work-orders.json. The work-orders are **ten
independent, bounded, read-only Mapper task specifications**, not ten actual
running AI agents. Run the Supervisor resume command to recover the original
mission and selected slice before consuming the work orders.

At pinned MiniC main revision d3c66ce25b0c1f9e13b89e880a7b13b3804c68e8,
the entire repository has **1,983 tracked C/H files**. The initial scan
missed 237 paths. The S0 v2 migration explicitly adds **153 MiniC public
interfaces, thin driver integration entrypoints and additional test fixtures**:
1,899 tracked C/H inputs are now in the ten candidate regions and **84
C/H files belong to other tool implementations or third_party libraries**.
Every out-of-scope C/H path is required to match an explicit excluded root;
an unexplained new path fails the inventory and independent handoff checks.

These are repository C/H inventory figures, NOT semantic coverage,
NOT a complete build-script/assembler inventory, and NOT permission to
modify vendor or external-tool sources. 
Four historical anchor names did not occur in current production C/H scans;
their semantic replacements require S1 source review.

The latest verified execution and restart artifact can be reached through
the experiment PR #712 CI history. Do not assume the artifact persists forever.

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

The current executable transition gate only allows
**PLANNED -> MAPPING_PROPOSED**, which is UNTRUSTED and must be independently
challenged. MAPPED, INVESTIGATING, CONFIRMED, FIXED_UNVERIFIED and VERIFIED
are ALL DISABLED pending authenticated evidence and trusted controller rights.

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

## Adversarial recovery acceptance (mandatory)

The original global goal remains pinned, but worker statements are now split
into **MAPPING_PROPOSED** (untrusted, may be scheduled for independent review)
and **MAPPED** (trusted S1 completion, currently DISABLED). Similarly, all
terminal semantic stages remain disabled until authenticated independent
review evidence exists. A Mapper saying "producer/consumer" in a JSON field is
not evidence that the code has actually been audited.

The Supervisor uses exclusive local write locking plus state epoch/CAS.
All transitions persist replayable event proposals. On load it reconstructs
state from the original S0 bootstrap, validates history and checks the live
snapshot. It refuses forced active-task priority, changes to the ten planning
regions, altered historical evidence, altered mission instructions and
snapshot-only synthetic progress. A crash can leave a lock file; **do not
automatically steal or delete it** without operator review of the writer.

Test in an isolated checkout on GitHub Actions:

    python3 -I tools/asam/selftest_supervisor.py
    python3 -I tools/asam/selftest_adversarial.py
    python3 -I tools/asam/selftest_topology.py

The adversarial test runs 12 actual **two-process** races; exactly one write
may succeed in each. It also modifies and restores synthetic states to test
fabricated work, role drift, damaged event history and scope injection.

After emitting the resume, breadth inventory and Mapper orders, a separate
validator **does not import Supervisor or Topology**:

    python3 -I tools/asam/verify_handoff.py --root . \
      --resume /tmp/asam-resume.json \
      --inventory asam-supervisor-out/breadth-inventory.json \
      --orders asam-supervisor-out/work-orders.json
    python3 -I tools/asam/selftest_handoff.py

The validator independently re-checks all tracked C/H file bytes, line
counts, source-tree identity, live canonical document digests, all ten
functional scopes, the exact selected next task and all worker permissions.
The second test injects 36 malformed/stale/redirected work-order payloads
and checks that code modified **after** export is also rejected.

**What these tests cannot prove:** a fresh *model* instance's comprehension,
semantic correctness, genuine independent LLM verification, and a protected
remote trust root. Current testing uses separate programs/processes, NOT
separate AI subagents. A malicious actor allowed to modify both this
experiment's Supervisor code and its tests can forge the result. A model can
still submit false provisional Mapper data. Remote GitHub writes require
branch protection, code ownership and authenticated evidence verification
before accepting MAPPED or VERIFIED. Keep this Draft PR unmerged until those
permissions/trust decisions have been reviewed.

## No model-to-model instruction inheritance

Model-generated event fields and evidence URLs may contain malicious prompts
even when their JSON is structurally valid. Supervisor **must not copy** a
prior worker's raw prose into the next LLM prompt. The restart packet exports
only fixed-schema **SHA-256 event commitments and evidence-reference hashes**.
These commitments support provenance comparison, not semantic truth or
authentication. Original untrusted text remains available only by deliberate
review from the versioned event store.

    python3 -I tools/asam/selftest_context_isolation.py

This test writes nine adversarial proposed mappings into a temporary state,
including deliberate hostile prior-agent instructions, then starts a clean
Python process. Raw hostile text must not appear, and the report remains
bounded. When all ten mappings are proposed, the next task must be independent
counterevidence, not redoing C01 Mapper. It is **NOT** a fresh LLM instance.

Proposals are size-bounded, use a strict allowlist schema, and canonical
evidence links must exactly match this repo and source revision. Neither a
plausible link nor a worker identifier authenticates a reviewer. The
`trust_gate` field of every restart packet explicitly marks controller
protection, independent LLM restoration, worker authentication, semantic
certification and autonomous source edits **FALSE**. A successful CI on
this experiment branch cannot turn those flags true.

GitHub REST CI metadata is checked independently for repo, run, source SHA,
branch, job steps and artifact, but it is **only metadata**: it does not
prove semantic correctness, protected branch policy, or human reviewer
authorization.

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
