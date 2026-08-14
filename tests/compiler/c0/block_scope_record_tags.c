struct shadow_tag {
    int outer;
};

int block_scope_record_tags(void) {
    struct shadow_tag before;
    before.outer = 1;

    {
        struct shadow_tag {
            long inner;
        };
        struct shadow_tag inner_value;
        inner_value.inner = 2;
        if (inner_value.inner != 2)
            return 1;
    }

    {
        struct same_scope;
        struct same_scope {
            int value;
        };
        struct same_scope local_value;
        local_value.value = 3;
        if (local_value.value != 3)
            return 2;
    }

    union cpumask_rcuhead {
        int cpumask;
        long rcu;
    };
    union cpumask_rcuhead mask;
    mask.cpumask = 4;
    if (mask.cpumask != 4)
        return 3;

    {
        struct with_declarator {
            int value;
        } object;
        object.value = 5;
        if (object.value != 5)
            return 4;
    }

    {
        struct shadow_tag after;
        after.outer = 6;
        if (after.outer != 6)
            return 5;
    }
    return before.outer == 1 ? 0 : 6;
}

int main(void) {
    return block_scope_record_tags();
}
