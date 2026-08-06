struct Box {
    int value;
};

int read_box(struct Box *box)
{
    return box->missing;
}

int main(void)
{
    struct Box box;

    return read_box(&box);
}
