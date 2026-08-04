# MiniC Toolchain

MiniC is a software-driven, self-hosting compiler toolchain designed for practical use, extensibility, and compiler education.

The project is being rebuilt in ISO C11 from a proven Python implementation. Real workloads such as Linux, musl, BusyBox, SQLite, and Lua drive development priorities, while language standards, target ABIs, differential testing, and documented architectural rules define correctness.

## Project goals

- Build a compiler toolchain that can be used on real software.
- Keep the implementation readable enough for compiler education.
- Support staged self-hosting.
- Preserve clear frontend, IR, target, object-format, and host-platform boundaries.
- Add abstractions only when ownership, lifetime, platform variation, or multiple implementations justify them.
- Record temporary architectural deviations explicitly and remove them through verified follow-up work.

## Current status

Repository initialization is in progress. The validated Python implementation and the native C lexer migration will be imported in later, separately reviewable commits.

A Chinese introduction will be maintained in `README.zh-CN.md`.
