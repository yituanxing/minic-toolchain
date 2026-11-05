/* RV32/RV64 relaxation is a whole-image fixed-point phase.  Per-section
 * placement cannot decide CALL/CALL_PLT reachability to later sections, and
 * mutating an ALIGN fragment here would make subsequent layout passes see
 * already shortened input.  The RISC-V static target's post-layout hook owns
 * all source->final offset projections as a single transaction. */
static bool minild_riscv_relax_section(MiniLdState *state,
                                      size_t section_index,
                                      uint64_t base_addr) {
    (void)state;
    (void)section_index;
    (void)base_addr;
    return true;
}
