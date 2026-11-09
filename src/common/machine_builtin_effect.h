#ifndef MINIC_COMMON_MACHINE_BUILTIN_EFFECT_H
#define MINIC_COMMON_MACHINE_BUILTIN_EFFECT_H

/* Sealed Core CALL side effects for target-owned, non-call instructions.
 * No C builtin spelling or architecture reaches the generic Core model. */
typedef enum MinicMachineBuiltinEffect {
    MINIC_MACHINE_BUILTIN_EFFECT_NONE = 0,
    MINIC_MACHINE_BUILTIN_EFFECT_READ,
    MINIC_MACHINE_BUILTIN_EFFECT_WRITE
} MinicMachineBuiltinEffect;

#endif
