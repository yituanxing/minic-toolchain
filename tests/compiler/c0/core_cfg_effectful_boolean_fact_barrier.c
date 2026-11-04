/* A CFG fact describes a local BEFORE an expression runs. Do not reuse it
 * for a later read in that expression if a prior conditional effect can
 * write the local. This protects call-clone selection, bounded compares,
 * and bitmask false-only guards with the same C evaluation semantics. */
extern void __compiletime_assert_impossible(void)
    __attribute__((error("static masked range is never out of bounds")));

int runtime_guard_hits;
int mutation_calls;
int sibling_calls;

void __attribute__((noinline)) runtime_guard(void)
{
    ++runtime_guard_hits;
}

static inline __attribute__((always_inline)) unsigned
encode(unsigned long flags)
{
    if (flags > 3ul) runtime_guard();
    return (unsigned)flags;
}

static inline __attribute__((always_inline)) unsigned
remove_flags(unsigned flags)
{
    return encode(flags);
}

void __attribute__((noinline)) mutate(unsigned *flags)
{
    *flags = 4u;
    ++mutation_calls;
}

int __attribute__((noinline)) mutate_condition(unsigned *flags)
{
    mutate(flags);
    return 1;
}

int __attribute__((noinline)) mutate_one_condition(unsigned *flags)
{
    *flags = 1u;
    ++mutation_calls;
    return 1;
}

int __attribute__((noinline)) sibling_effect(void)
{
    ++sibling_calls;
    return 2;
}

static inline __attribute__((always_inline)) unsigned
remove_pair(unsigned extra, unsigned flags)
{
    return extra + encode(flags);
}

/* The dynamic _Bool seed creates a range-only clone. The following runtime
 * ternary must NOT select it from the stale pre-mutation `flags=0` fact. */
unsigned condition_argument(int dynamic_bool)
{
    unsigned flags = 0;
    unsigned seed = remove_flags((_Bool)dynamic_bool);
    unsigned observed = remove_flags(
        mutate_condition(&flags) ? flags : flags);
    return seed + observed;
}

/* The condition executes and turns the old 0 into 4 before the comparison. */
unsigned effectful_greater(void)
{
    unsigned flags = 0;
    if ((mutate_condition(&flags) ? flags : flags) > 3u)
        runtime_guard();
    return flags;
}

/* The same stale fact previously erased this true bitmask branch. */
unsigned effectful_bitmask(void)
{
    unsigned flags = 0;
    if ((mutate_condition(&flags) ? flags : flags) & 4u)
        runtime_guard();
    return flags;
}

/* Subtraction's right operand is evaluated after a condition that mutates
 * it. Its previous exact value must not be read as a constant RHS offset. */
unsigned effectful_subtract_rhs(void)
{
    unsigned flags = 4u;
    if (((mutate_one_condition(&flags) ? 4u : 4u) - flags) > 2u)
        runtime_guard();
    return flags;
}

/* Condition and switch entry own their real source effects. A pre-eval
 * constant oracle is not an excuse to skip lowering those expressions. */
unsigned effectful_if_condition(void)
{
    unsigned flags = 0;
    if ((mutate_condition(&flags), flags) == 4u)
        runtime_guard();
    return flags;
}

unsigned effectful_switch_selector(void)
{
    unsigned flags = 0;
    switch ((mutate_condition(&flags), flags)) {
    case 4u:
        runtime_guard();
        break;
    default:
        break;
    }
    return flags;
}

/* A GNU statement expression may assign its own fresh temporaries, but a
 * write to an already-live outer local is observable and must not be erased
 * by the constant interpreter's pre-condition local facts. */
unsigned effectful_outer_statement_expression(void)
{
    unsigned flags = 0;
    if (({ flags = 4u; flags; }) == 4u)
        runtime_guard();
    return flags;
}

/* Comma and assignments remain ordinary effect barriers as well. */
unsigned effectful_comma(void)
{
    unsigned flags = 0;
    return remove_flags((mutate_condition(&flags), flags));
}

unsigned effectful_assignment(void)
{
    unsigned flags = 0;
    return remove_flags((flags = 4u));
}

/* A side effect in ANY sibling argument disables pre-call mutable-local
 * facts for the entire argument group, without dropping the sibling call. */
unsigned effectful_sibling(int dynamic_bool)
{
    unsigned flags = 0;
    unsigned seed = remove_pair(1u, (_Bool)dynamic_bool);
    unsigned observed = remove_pair((unsigned)sibling_effect(), flags);
    return seed + observed;
}

/* The effects in the condition are retained, but literal arms remain
 * statically in [1,2] and cannot set bit 2. These two diagnostic calls must
 * disappear in GNU and MiniC without banning all effectful ternaries. */
unsigned effectful_static_arms(void)
{
    unsigned flags = 0;
    if ((mutate_condition(&flags) ? 1u : 2u) > 3u)
        __compiletime_assert_impossible();
    if ((mutate_condition(&flags) ? 1u : 2u) & 4u)
        __compiletime_assert_impossible();
    return flags;
}

int main(void)
{
    if (condition_argument(1) != 5u ||
        effectful_greater() != 4u ||
        effectful_bitmask() != 4u ||
        effectful_subtract_rhs() != 1u ||
        effectful_if_condition() != 4u ||
        effectful_switch_selector() != 4u ||
        effectful_outer_statement_expression() != 4u ||
        effectful_comma() != 4u ||
        effectful_assignment() != 4u ||
        effectful_sibling(1) != 4u ||
        effectful_static_arms() != 4u)
        return 1;
    return runtime_guard_hits == 9 && mutation_calls == 9 &&
           sibling_calls == 1 ? 42 : 2;
}
