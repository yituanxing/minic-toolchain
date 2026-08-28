# MiniCPP preprocessor component

Planned public tool identity: `minic-cpp`.

Ownership: C preprocessing from source/header tokens to the preprocessed translation
unit consumed by `minic-cc`.

This component is deliberately deferred until the downstream assembler/archive/link
boundaries have been replaced. The historical Python implementation remains a
reference oracle for later work.

Its eventual differential contract must compare more than text: normalized C token
streams, active pragmas, diagnostics, and downstream compilation through the same
reference compiler. Whitespace or line-marker differences alone are not semantic
failures.
