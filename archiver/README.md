# MiniAR archiver component

Public tool identity: `minic-ar`.

Ownership: archive-file construction and archive-member metadata. Linker policy
remains outside this component.

## A0 boundary

The first native implementation intentionally targets the archive surface used by
Linux Kbuild and the later MiniLD integration:

- GNU/System V archive headers;
- deterministic `D` metadata;
- long-name tables;
- GNU archive symbol indexes for ELF32/ELF64 little-endian objects;
- thin archives (`T`) with paths stored relative to the archive;
- `P`, `s`, and `S` modifiers used by Kbuild;
- empty-archive behavior compatible with GNU `ar`.

The A0 oracle is GNU `ar`. Deterministic normal archives, long-name archives,
thin archives without an index, and thin archives with an index are compared
byte-for-byte. Host archives are also consumed by the system linker, and CI
repeats the indexed-thin comparison with RISC-V ELF objects.

A0 does not yet implement archive mutation operations such as delete/extract or
incremental replacement of an existing archive. Linux Kbuild removes archive
outputs before its `r` invocations, so those operations are outside the first
production boundary.
