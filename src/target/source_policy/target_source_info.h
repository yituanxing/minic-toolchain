#ifndef MINIC_TARGET_SOURCE_POLICY_TARGET_SOURCE_INFO_H
#define MINIC_TARGET_SOURCE_POLICY_TARGET_SOURCE_INFO_H

#include "target/target_catalog.h"

/* Typed, opaque handoff from SourcePolicy's immutable canonical registry.
 * Machine TargetInfo never includes a C type or C admission header. */
struct MinicTargetSourceInfo;

const struct MinicTargetSourceInfo *minic_target_source_info_for_architecture(
    MinicTargetArchitecture architecture);

#endif
