#ifndef MINIC_TARGET_AARCH64_MACHINE_FRAME_SELECT_H
#define MINIC_TARGET_AARCH64_MACHINE_FRAME_SELECT_H

#include "target/aarch64/frame_emit.h"
#include "target/common/machine_builder.h"
#include "target/common/machine_lp64_frame.h"

/* Physical AAPCS64 frame selection. All operations append typed selected
 * instructions to an already-positioned MachineBuilder: no textual GNU
 * assembly or C/Core IR references are admitted by this interface. */
bool minic_a64_machine_emit_frame_load(MinicMachineBuilder *,
    const MinicA64FrameView *, MinicA64FrameRef,
    MinicA64FrameRegister, MinicA64ScratchPolicy);
bool minic_a64_machine_emit_frame_store(MinicMachineBuilder *,
    const MinicA64FrameView *, MinicA64FrameRef,
    MinicA64FrameRegister, MinicA64ScratchPolicy);
bool minic_a64_machine_emit_frame_address(MinicMachineBuilder *,
    const MinicA64FrameView *, MinicA64FrameRef,
    unsigned int target, MinicA64ScratchPolicy);
bool minic_a64_machine_emit_prologue(MinicMachineBuilder *,
    const MinicMachineLP64FrameContract *);
bool minic_a64_machine_emit_epilogue(MinicMachineBuilder *,
    const MinicMachineLP64FrameContract *);

#endif
