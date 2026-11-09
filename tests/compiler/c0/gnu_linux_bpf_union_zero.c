/* Linux 6.6.143 kernel/bpf/helpers.c union spinlock zero test shape.
 * A reduced source model, NOT an authenticated frozen GNU Linux .i owner.
 * The same GNU-preprocessed file must pass both GNU -O2 and MiniC codegen. */
typedef unsigned int u32;
typedef struct { u32 value; } TinySpinlock;
typedef union { u32 word; TinySpinlock lock; } TinySpinWord;

extern void __compiletime_assert_758(void);

int bpf_union_zero_assert_model(u32 *spinword)
{
    /* Exact fixed-kernel ordering: __bpf_spin_lock declares its alias to
     * the incoming lock before the local designated-initialized union. */
    u32 *l = (void *)spinword;
    TinySpinWord u = { .lock = { .value = 0U } };
    if (u.word != 0U)
        __compiletime_assert_758();
    return (int)u.word + (l != (void *)0);
}

int main(void)
{
    u32 lock_word = 0;
    return bpf_union_zero_assert_model(&lock_word) == 1 ? 42 : 1;
}

#ifdef MINIC_NEGATIVE_CASES
/* Each case must retain all possible live writes and side effects.  An
 * initial zero union may not be used to prove a subsequently changed or
 * escaped object remains zero. Volatile union reads must stay observable. */
extern void runtime_nonzero_probe(void);
extern void mutate_spinword(TinySpinWord *);

u32 bpf_union_dynamic_write(u32 input)
{
    TinySpinWord u = { .lock = { .value = 0U } };
    u.word = input;
    if (u.word != 0U)
        runtime_nonzero_probe();
    return u.word;
}

u32 bpf_union_escaped_write(void)
{
    TinySpinWord u = { .lock = { .value = 0U } };
    mutate_spinword(&u);
    if (u.word != 0U)
        runtime_nonzero_probe();
    return u.word;
}

u32 bpf_union_volatile_read(u32 input)
{
    volatile TinySpinWord u = { .lock = { .value = 0U } };
    if (input != 0U)
        u.word = input;
    if (u.word != 0U)
        runtime_nonzero_probe();
    return u.word;
}

u32 bpf_union_nonzero_initializer(void)
{
    TinySpinWord u = { .lock = { .value = 1U } };
    if (u.word != 0U)
        runtime_nonzero_probe();
    return u.word;
}
#endif
