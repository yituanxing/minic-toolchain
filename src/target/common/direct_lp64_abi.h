#ifndef MINIC_TARGET_COMMON_DIRECT_LP64_ABI_H
#define MINIC_TARGET_COMMON_DIRECT_LP64_ABI_H

/* Target ABI policy for the two direct LP64 emitters. This deliberately
 * does not describe RV64's psABI or x86-64's SysV calling convention. */
typedef enum MinicLP64DirectABI {
    MINIC_LP64_ABI_AAPCS64,
    MINIC_LP64_ABI_LOONGARCH64_LP64D
} MinicLP64DirectABI;

#endif
