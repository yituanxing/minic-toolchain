# Canonical named test suites

These are editable, plain-text test definitions, not binary archives.
Each case section has its original independent translation unit or shell
body. Driver suites factor out exact common command blocks. Different
assertions and GNU/ABI/QEMU checks remain independent. No test is removed.

Run `make prepare-tests` after checkout before directly invoking legacy
test paths. Make check targets automatically expand the inputs. Edit
the canonical .cases source rather than generated files; use
`python3 tests/suites/materialize.py --refresh` after editing.
`--list` lists the individual named cases.

The compatibility paths are generated and ignored by Git. Historical
CI that inspects the Git tree, rather than the filesystem, requires a
separate adaptation. This branch is a candidate until that is verified.
