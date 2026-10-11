# Independent MiniC driver validation

The unified `minic` executable routes distinct frontend and ELF tool
stages; passing its integration test does not imply that the compiler,
preprocessor, assembler, archiver or linker passes the full Linux runtime
contract. Avoid silently counting one combined smoke as independently
certifying its components.

- Maintained test owner: `.github/workflows/minic-driver-v0.yml`.
- Current opt-in push gate: `[minic-driver-v0]`; this directory is on its
  declared trigger path list.
- Its current integration uses a pinned external GNU RV64 compiler to build
  musl, runs the existing driver-routing and dynamic-library tests, and
  preserves the exact tested HEAD and trace.
- Treat a green driver run as an orchestration/interface proof at precisely
  its tested scope; follow standalone GNU-baseline tool-specific contracts
  before native components can be promoted together.
- Self-hosting and missing MiniObjdump implementation are intentionally out
  of scope for this validation stage.

Do not add another GitHub Actions workflow for this owner.
