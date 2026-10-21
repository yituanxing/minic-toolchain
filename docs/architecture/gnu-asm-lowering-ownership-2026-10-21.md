# GNU inline assembly lowering: ordered ownership split (2026-10-21)

This is a single coherent restructuring of the former ~2,641-line
`minic_core_lower_inline_asm` dispatcher. No ABI, target feature,
instruction kind, or GNU constraint acceptance rule is being added.

The original ordered implementation had seven natural groups. Each now
has its own same-TU `core_asm_try_*` helper:
1. RISC-V CSR immediate output specialization (early, before generic).
2. Canonical structured register/memory roles and single-label asm-goto.
3. Fixed-register and scalar-register forms.
4. Immediate, barriers, atomic structured operand families.
5. Mixed register/memory output shapes.
6. Register read/write and immediate output specialization.
7. Empty/tied/opaque barriers and scalar input forms.

Each candidate returns both the original status and whether an existing
return path was reached. An unclaimed candidate returns OK with handled=false;
the next candidate is then evaluated in original source order. This avoids
mistaking the original UNSUPPORTED return for a fallthrough. All original
return expressions and mutation paths are preserved by a mechanical
return-site wrapping with the matched marker.

Original optional batch-trace output happens before any shape admission,
and unclaimed diagnostics happen only after all candidates declined.
The tail fail-closed/opaque-inline-asm fallback remains in the dispatcher.
Preflight of generic structured operand roles still runs before mutation.

All seven candidate sections remain in one translation unit so the helper
functions can continue to use existing private immediate/constraint/operand
functions without widening the API surface or leaking target details.

Qualification: compile and link with -Werror, GNU/QEMU four-backend real
projects including cJSON and BusyBox, RV64 downstream regressions, and
optional diagnostics byte-for-byte comparison. Focused CI is insufficient
to claim a full 2064 TU Linux or performance certification.
