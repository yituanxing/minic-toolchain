struct attribute_group {
    int value;
};

const struct attribute_group *probe(const struct attribute_group *group)
{
    return group;
}

int consume(const struct attribute_group **groups)
{
    return groups[0] != ((void *)0) && groups[1] == ((void *)0);
}

int linux_shape(const struct attribute_group *grp)
{
    const struct attribute_group *groups[] = { probe(grp), ((void *)0) };

    return sizeof(groups) == 2 * sizeof(groups[0]) ? consume(groups) : 0;
}

int fixed_tail_zero(void)
{
    int values[3] = { 7, 9 };

    return values[0] + values[1] + values[2];
}

int main(void)
{
    return fixed_tail_zero() == 16 ? 0 : 1;
}
