struct hlist_node {
    int value;
};

int __cpuhp_setup_state(int (*startup)(unsigned int cpu),
                        int (*teardown)(unsigned int cpu),
                        int multi_instance)
{
    (void)startup;
    (void)teardown;
    return multi_instance;
}

int cpuhp_setup_state_multi(int (*startup)(unsigned int cpu,
                                          struct hlist_node *node),
                            int (*teardown)(unsigned int cpu,
                                           struct hlist_node *node))
{
    return __cpuhp_setup_state((void *)startup, (void *)teardown, 1);
}

int startup_multi(unsigned int cpu, struct hlist_node *node)
{
    return (int)cpu + node->value;
}

int teardown_multi(unsigned int cpu, struct hlist_node *node)
{
    return (int)cpu - node->value;
}

int main(void)
{
    return cpuhp_setup_state_multi(startup_multi, teardown_multi) == 1 ? 0 : 1;
}
