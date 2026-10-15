# Core-only Linux + Runtime R0 checkpoint

Status: exact-head validation checkpoint for the Core-only production backend.

## Frozen evidence entering this checkpoint

The branch previously demonstrated Linux coverage batch-by-batch, but those batches
were not all replayed on one exact production head. The permanent
`Linux Core All3352 Exact` workflow exposed five cross-batch initializer
regressions. They were repaired by restoring the ordinary matching-record copy
route alongside the existing brace-elided integer-zero shorthand.

Exact head `dbbc214b860b5a340d5ff06ad3924dd7da907458` then passed:

- Linux frozen corpus: 3352 selected / 3352 PASS
- unsupported: 0
- ERROR: 0
- preprocess_missing: 0
- focused cross-shard regressions: 5 / 5 PASS

## Runtime R0

Runtime R0 restores executable validation beyond "MiniC emitted assembly".
The gate builds RV64 executables and runs them through QEMU, including
differential checks where the existing harness provides them.

Exact head `d2e395ebaa6289999b598f8ec6af27644fd0473c` passed every Runtime R0 lane:

- micro runtime suite
- tiny-AES
- cJSON
- SDS
- Parson
- linenoise

The runtime work also closed Core-only gaps that Linux compilation did not
exercise directly:

- binary64 add/subtract/multiply/divide
- binary64 unary negate
- ordered binary64 comparisons with NaN-safe semantics
- integer-to-double and double-to-integer conversion
- transported float-to-double widening
- direct double return through RV64 `fa0`
- fixed double arguments through `fa0..fa7`
- fixed double arguments on indirect calls
- variadic double arguments according to psABI placement (integer register/stack bank)
- Core-owned va_start/va_arg/va_copy/va_end test shims

## Freeze rule

A production freeze requires Linux All3352, Runtime R0, and the focused-five
regression gate to pass on the same exact head. This checkpoint commit exists
only to trigger that joint replay; it does not change compiler semantics.

After the joint exact-head freeze, the next validation phase is the recovered
Python-era broader runtime replay followed by stage0 -> stage1 -> stage2
compiler bootstrap comparison.
