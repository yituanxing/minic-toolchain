/* SPDX-License-Identifier: GPL-2.0 */
/* arch/x86/kernel/fpu/regset.c __convert_from_fxsr: a bare DS selector
 * snapshot is stored in a 32-bit legacy FPU save area, not a u16 local. */
struct linux_i387_env {
    unsigned int fos;
    unsigned short selector;
};

void linux_read_fpu_regset_ds(struct linux_i387_env *env) {
    asm("mov %%ds,%0" : "=r" (env->fos) : : "memory");
    asm("mov %%es,%0" : "=r" (env->selector) : : "memory");
    env->fos |= 0xffff0000U;
}
