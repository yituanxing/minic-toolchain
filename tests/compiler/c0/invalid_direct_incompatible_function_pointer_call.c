struct hlist_node {
    int value;
};

int sink(int (*callback)(unsigned int cpu))
{
    (void)callback;
    return 0;
}

int multi(unsigned int cpu, struct hlist_node *node)
{
    return (int)cpu + node->value;
}

int main(void)
{
    return sink(multi);
}
