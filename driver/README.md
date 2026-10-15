# MiniC toolchain driver

Future public tool identity: `minic`.

The unified driver will orchestrate independently executable tool components through
their command/file contracts. It must not become a mega-library that links every
tool's private implementation together.

During M0, `build/.../bin/minic` remains a compatibility compiler entrypoint so
existing compiler tests and historical workflows do not break. The real compiler
identity introduced by M0 is `minic-cc`. The compatibility name will be retired or
converted into this driver only in a later explicit milestone.
