/* Final-link relocation intent shared between link modes.  Relocation
 * numbers, instruction encoding and dynamic ELF relocation types belong to
 * the target backend; Common only reasons about these semantic categories. */
typedef enum MiniLdTargetRelocClass {
    MINILD_TARGET_RELOC_NOOP,
    MINILD_TARGET_RELOC_ALIGN,
    MINILD_TARGET_RELOC_ABS32,
    MINILD_TARGET_RELOC_ABS64,
    MINILD_TARGET_RELOC_ARITHMETIC,
    MINILD_TARGET_RELOC_FINAL,
    MINILD_TARGET_RELOC_PCREL_LO,
    MINILD_TARGET_RELOC_CALL,
    MINILD_TARGET_RELOC_GOT,
    MINILD_TARGET_RELOC_TLS_IE,
    MINILD_TARGET_RELOC_TLS_LE,
    MINILD_TARGET_RELOC_TLS_GD,
    MINILD_TARGET_RELOC_UNSUPPORTED
} MiniLdTargetRelocClass;

/* The dynamic-link planner asks for loader operations by meaning; the
 * selected ELF/psABI target maps each one to its actual relocation number. */
typedef enum MiniLdDynamicRelocSemantic {
    MINILD_DYNAMIC_RELOC_RELATIVE,
    MINILD_DYNAMIC_RELOC_ABSOLUTE,
    MINILD_DYNAMIC_RELOC_TLS_MODULE,
    MINILD_DYNAMIC_RELOC_TLS_OFFSET,
    MINILD_DYNAMIC_RELOC_JUMP_SLOT
} MiniLdDynamicRelocSemantic;
