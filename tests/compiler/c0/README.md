# C0 compiler regression gates

The C0 directory exercises the current compiler frontend, Core IR and target backends. Keep independent semantic inputs as C sources; share execution code instead of duplicating shell drivers.

## Maintained native matrix

```sh
make -j4 MODE=release build/release/bin/minic-cc build/release/bin/minic
export MINIC_CC="$PWD/build/release/bin/minic-cc"
export MINIC="$PWD/build/release/bin/minic"
sh tests/compiler/c0/run-native-matrix.sh --list
sh tests/compiler/c0/run-native-matrix.sh run-tag-namespace-scopes
```

`native-matrix.json` defines named cases, their source files, dialect and ABI options, target sets, assembly checks, rejection diagnostics and runtime oracles. The Python executor owns the actual subprocess calls, temporary directories, exit checking, GNU comparison and host sanitizer builds. Each negative macro variant is compiled separately; a successful compile or a crash cannot count as a rejection. New cases using the same execution mechanism should add case data, not a new shell driver.

Some cases accept a compiler or host CC argument, as their original caller did. Existing Make targets and review gates preserve those calling conventions.

## Runtime boundary

Native GNU comparison and cross-target assembly are distinct from execution on a non-host ISA. `run-runtime.sh` and the target-specific QEMU gates cover cross-assembly, linking and actual execution where required. Do not label a compile-only case a runtime pass.
