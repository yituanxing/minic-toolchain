struct minic_zoned_queue {
    int model;
};

struct minic_zoned_disk {
    struct minic_zoned_queue *queue;
};

extern volatile int minic_zoned_getter_visits;
extern int minic_disabled_zoned_entry(struct minic_zoned_disk *disk);
extern int minic_dynamic_zoned_entry(struct minic_zoned_disk *disk);
extern int minic_zoned_getter_effect_entry(struct minic_zoned_disk *disk);

int minic_dynamic_zoned_must_resolve(void)
{
    return 97;
}

int main(void)
{
    struct minic_zoned_queue queue = {1};
    struct minic_zoned_disk disk = {&queue};
    if (minic_disabled_zoned_entry(&disk) != 41)
        return 1;
    if (minic_dynamic_zoned_entry(&disk) != 97)
        return 2;
    if (minic_zoned_getter_effect_entry(&disk) != 2 ||
        minic_zoned_getter_visits != 1)
        return 3;
    return 0;
}
