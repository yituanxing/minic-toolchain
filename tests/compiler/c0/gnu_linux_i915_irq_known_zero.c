/* Fixed Linux 6.6.143 intel_gt_irq.c: gen11_gt_irq_postinstall's essential
 * BUILD_BUG_ON(irqs & 0xffff0000) shape. This is a model of the source,
 * NOT an authenticated frozen .i proof for the remaining x86 GNU LD owner.
 * The difference from an expression-only mask proof is a conditional update
 * to a local object, followed by a control-flow merge and then a read. */
typedef unsigned int u32;

extern void __compiletime_assert_461(void)
    __attribute__((__error__("i915 interrupt bits must remain low16")));

static volatile unsigned int observed_conditions;

static int intel_uc_wants_guc_submission(u32 configured)
{
    ++observed_conditions;
    return configured != 0U;
}

static u32 gen11_gt_irq_postinstall(u32 configured)
{
    u32 irqs = 0x00000100U;
    if (!intel_uc_wants_guc_submission(configured))
        irqs |= 0x00000004U | 0x00000010U | 0x00000020U;

    if (irqs & 0xffff0000U)
        __compiletime_assert_461();
    return irqs;
}

int main(int argc, char **argv)
{
    u32 a, b;
    (void)argv;
    a = gen11_gt_irq_postinstall((u32)(argc - 1));
    b = gen11_gt_irq_postinstall((u32)argc);
    return a == 0x00000134U && b == 0x00000100U &&
        observed_conditions == 2U ? 42 : 1;
}

#ifdef MINIC_NEGATIVE_CASES
/* These MUST retain the external call. A possible-one-bits dataflow fact
 * cannot survive unknown compound OR, escaped locals or volatile accesses. */
extern void runtime_dynamic_highbits_probe(void);
extern void runtime_escaped_highbits_probe(void);
extern void runtime_volatile_highbits_probe(void);
extern void runtime_constant_highbits_probe(void);
extern void mutate_irqs(u32 *);

u32 dynamic_highbits(u32 incoming, int enabled)
{
    u32 irqs = 0x00000100U;
    if (enabled)
        irqs |= incoming;
    if (irqs & 0xffff0000U)
        runtime_dynamic_highbits_probe();
    return irqs;
}

u32 constant_highbits(int enabled)
{
    u32 irqs = 0x00000100U;
    if (enabled)
        irqs |= 0x80000000U;
    if (irqs & 0xffff0000U)
        runtime_constant_highbits_probe();
    return irqs;
}

u32 escaped_local_highbits(u32 incoming)
{
    u32 irqs = incoming & 0xffffU;
    mutate_irqs(&irqs);
    if (irqs & 0xffff0000U)
        runtime_escaped_highbits_probe();
    return irqs;
}

u32 volatile_local_highbits(u32 incoming)
{
    volatile u32 irqs = incoming & 0xffffU;
    if (incoming & 1U)
        irqs |= 0x20U;
    if (irqs & 0xffff0000U)
        runtime_volatile_highbits_probe();
    return irqs;
}
#endif
