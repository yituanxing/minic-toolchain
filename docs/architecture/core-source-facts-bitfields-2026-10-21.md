# Core source facts and bit-field lowering ownership
Integrated refactor of Core AST/CFG conservative fact proof and two large
bit-field expression cases. Base commit 2daeec70ca3a8f737a6370a899665551e00113a2, focused CI #39567628638
SUCCESS.

* core_lower_const_facts.c owns immutable source/CFG zero checks, local
  constants and null-pointer facts, boolean-range checks, and the local
  integer constant evaluator. Original fact/decision bodies are unchanged.
* core_lower.c owns CFG creation and the external-reentry oracle. Four
  namespaced private helpers bridge the new translation-unit boundary.
* The two bulky bit-field expression cases are localized as same-TU helpers:
  scalar bit-field read (~200 lines) and compound read/modify/write (~570).
  A matched flag distinguishes exclusive handling from original fallthrough.
  All matched-path Core mutations and status returns retain their order.
* The IR opcodes, TargetInfo, ABI, backend registry and machine emission
  remain unchanged.

Require -Werror build, four-ISA GNU/QEMU real BusyBox and cJSON gate,
x86/RV focused tests, and separate RV64 Linux + throughput checks.
