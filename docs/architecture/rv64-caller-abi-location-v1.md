# RV64 caller ABI location ownership v1

This slice makes the checked-in RV64 call emitter consume the canonical
`MinicRiscv64AbiCursor` / `MinicRiscv64AbiArgumentLocation` result once per
call instead of independently recomputing integer-register, floating-register,
and stack placement in two backend passes.

The C evaluation contract is intentionally unchanged: arguments are still
evaluated in source order and staged before physical placement. The slice only
changes ownership of the later ABI placement decision.

Formal v1 remains deliberately bounded. It covers the already-frozen scalar,
fixed floating-point, and fixed 8/16-byte integer-aggregate cases. Zero-sized
aggregates, sub-XLEN aggregate extensions, variadic aggregate placement, and
greater-than-16-byte indirect aggregate passing remain outside this formal slice
until their psABI semantics are promoted deliberately.

Frozen Linux 6.6.143 `init/main.i` was revalidated with a hybrid pressure
harness: formal-v1-compatible calls use the new location consumer, while
discovery-only ABI shapes retain the already-proven discovery emission. The
exact hybrid run for this checked-in fold is `31776243097` and completed
with the 90,928-line full-TU gate.

This preserves the future Core IR seam: target-neutral call semantics stay above
the seam; TargetABI owns abstract locations; the RV64 emitter alone maps those
locations to textual registers and stack operations.
