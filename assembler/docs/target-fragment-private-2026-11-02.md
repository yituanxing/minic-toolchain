# MiniAS — Target/Fragment private boundary (2026-11-02)

The preceding stages hid mutable Common Section/Symbol records and then
replaced the Measure/Relax/Encode `MiniAsStmt` input with a const MCInst
projection. Three target callbacks still received the entire retained
`MiniAsStmt`: parse, per-source-site state capture, and executable code
alignment. As a result the Target-visible header still declared every
variant of the owned Common Fragment payload, despite only the parser
needing to construct a single MCInst.

This batch removes those callbacks' direct Fragment dependency **for all
four architectures, including the separate RISC-V encoder**. It does not
create a second statement, instruction, or ELF object graph.

## Exact boundaries

| Call | Target input now | Lifetime and allowed mutation |
| --- | --- | --- |
| `parse_mcinst` | `MiniAsParseInstruction *` | Read-only borrowed `op`/`args`, source `line`, value-copied target site; **one writable owned `inst` output** |
| `capture_site_state` | `const MiniAsSourceSiteView *` | Value source kind, const-borrowed operation name, alignment value; no Fragment/payload/offset |
| `append_code_alignment` | `const MiniAsCodeAlignmentView *` | Value lexical site state and line, plus byte count; no Fragment or MCInst |
| `measure_mcinst`, relaxation, `encode` | `const MiniAsInstructionView *` | Previous read-only MCInst projection; unchanged |

`common_lowering.c` alone builds the parse request from its retained
Fragment. The request copies the target site snapshot and borrows owned
lexical strings. The Target constructs instruction operands and installs
the result in `request.inst`. Common **always** moves that pointer into
`fragment.payload.instruction.inst` after the call, even on parser failure,
so Common's existing single Fragment destructor can unwind a partial
construction. No Target callback can reassign the Common Fragment,
Section, placement, or source site.

The special x86 `lock`, `notrack` and `rep bsf/bsr` recursive mnemonic
aliases now copy the *small parse request* rather than the complete
Fragment. They clear their temporary `inst`, parse the alias, retain the
original lexical line/site, and return the parsed MCInst through the
outer request. The original ISA-specific instruction/operand/encoding
implementation is unchanged.

The six Fragment variant structs, discriminated payload union and
`MiniAsStmt` have exactly **one definition** in the new
`minias_fragment_internal.h`. Only Common and SourceGraph's current
replay implementation include this **private** header. The five Target
translation units cannot include that header and the public
`minias_target_api.h` no longer contains a Fragment or payload type.
The separate SourceGraph replay dependency is not silently labeled
closed; eliminating that raw Fragment-array borrow is P05 work.

## Verification and limits

- Required Target CI runs a new structural gate with **ten negative** and
  **two positive** C compiler cases: it rejects public Fragment records,
  mutable lexical source characters, fake payload access and old callback
  signatures, and accepts only private Common/Source and new callback
  shape. The guard checks all five Target translation units.
- The existing Fragment-union, Target-boundary and read-only-MCInst
  guards now check the single private definition and the actual Common
  transfer, not the obsolete public header layout.
- The four-architecture parser failure-unwind probe adds three x86
  recursive-alias failures; all nine cases must exit without a signal,
  preserve the original lexical line and return the ISA diagnostic.
- Independent old/new fixture parity, GNU/linked-ELF probes, strict
  `-Werror` A0 and sanitizers protect behavior. Missing local RISC-V,
  LoongArch and AArch64 GNU executables remain remote-CI dependencies;
  the full optional GNU corpus and Linux/QEMU P1 are separate acceptance.

The public Target API still intentionally declares the parse-only
`MiniAsMCInst` builder type. This is not permission to mutate any retained
MCInst in a read phase: none of those callbacks receives its mutable
pointer. The next meaningful boundary is SourceGraph's replay view, not
another compatibility wrapper around this deleted `MiniAsStmt` API.
