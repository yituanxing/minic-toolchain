# Dedicated source expression translation units — 2026-10-21

This is a follow-on structural step for the global architecture audit and
semantic-family refactor in PR #549. The five semantically intact helpers
previously defined within the giant `core_lower.c` now have explicit
compilation-unit ownership:

- `core_lower_expr_flow.c`: conditional CFG (pruning, C/GNU ternary, void
  and omitted-middle expressions), comma operator and short-circuit
  logical materialization.
- `core_lower_expr_scalar.c`: unary integer builtins, integer/floating
  comparisons, pointer difference and scaled pointer arithmetic.
- `core_lower.c`: source-to-Core root, lvalue and memory lowering, CFG
  statement machinery, private scalar building primitives and block owners.
- `core_lower_internal.h`: explicitly **private**, namespaced declarations
  for the five helpers and twelve existing Core construction primitives.

Exactly five method bodies have moved unchanged except removing their
file-static linkage and assigning unique symbol names. Their prior
handled/unhandled flow preserves both fail-closed returns and original
fallthrough. The existing twelve helper implementations have the same
logic and access permissions but an intentionally namespaced private
cross-TU identity. No instruction kind, TargetInfo or machine backend
changed. Makefile builds the two new units for every ISA; no
architecture-specific `#ifdef` or separate build logic was added.

Qualification: exact-head -Werror build/link, original GNU/QEMU
four-target real workloads, and separately original RV64 Linux
2064-TU and performance gates before any broad equivalence claim.

Do not extend this private header into public plugin/backends;
new Machine IR and ABI lowering belong below Core, not here.
