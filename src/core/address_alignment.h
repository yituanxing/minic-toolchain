#ifndef MINIC_CORE_ADDRESS_ALIGNMENT_H
#define MINIC_CORE_ADDRESS_ALIGNMENT_H

#include "core/core_ir.h"

/* A target-neutral, conservative proof of the minimum alignment of a Core
 * pointer value.  The callback resolves only OBJECT_ADDRESS/GLOBAL_ADDRESS
 * roots using the caller's DataLayout and actual object-storage contract.
 * It must return 1 when a root's alignment cannot be established.  Neither
 * the pointee type nor a cast of an arbitrary pointer proves its alignment:
 * GNU packed fields can flow through ordinary typed C pointers.
 *
 * A value of 1 means unknown, NOT known byte-only access.  Targets may
 * dispatch at runtime to a fast naturally aligned access and bytewise
 * fallback, or always select bytewise transport.  No target ISA or ABI
 * policy belongs in the proof itself. */
typedef size_t (*MinicCoreAddressRootAlignment)(
    const MinicCoreFunction *function,
    const MinicCoreInstruction *root,
    void *context);

size_t minic_core_address_known_alignment(
    const MinicCoreFunction *function,
    MinicCoreValueId address,
    MinicCoreAddressRootAlignment root_alignment,
    void *context);

/* The default visitor has a 64-edge recursion guard. A caller that already
 * enforces a stricter historical bound can preserve its exact proof/fallback
 * decisions without maintaining a second recursive address algorithm.
 * A larger requested bound never bypasses the Common 64-edge safety cap. */
size_t minic_core_address_known_alignment_bounded(
    const MinicCoreFunction *function,
    MinicCoreValueId address,
    MinicCoreAddressRootAlignment root_alignment,
    void *context,
    unsigned int max_depth);

#endif
