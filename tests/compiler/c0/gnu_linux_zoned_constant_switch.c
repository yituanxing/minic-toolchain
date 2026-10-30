/* CONFIG_BLK_DEV_ZONED=n: a dead `return q->limits.zoned` must not keep a
 * runtime zoned callback alive through an inline switch/helper/getter chain.
 * The companion dynamic case exercises the opposite live-reference edge. */
struct minic_zoned_queue {
    int model;
};

struct minic_zoned_disk {
    struct minic_zoned_queue *queue;
};

extern int minic_disabled_zoned_must_not_resolve(void);
extern int minic_dynamic_zoned_must_resolve(void);

static volatile int minic_dynamic_zoned_enabled = 1;
volatile int minic_zoned_getter_visits;

static inline int minic_disabled_zoned_model(struct minic_zoned_queue *queue)
{
    if (0)
        return queue->model;
    return 0;
}

static inline int minic_dynamic_zoned_model(struct minic_zoned_queue *queue)
{
    if (minic_dynamic_zoned_enabled)
        return queue->model;
    return 0;
}

static inline int minic_disabled_queue_is_zoned(struct minic_zoned_queue *queue)
{
    switch (minic_disabled_zoned_model(queue)) {
    case 1:
    case 2:
        return 1;
    default:
        return 0;
    }
}

static inline int minic_dynamic_queue_is_zoned(struct minic_zoned_queue *queue)
{
    switch (minic_dynamic_zoned_model(queue)) {
    case 1:
    case 2:
        return 1;
    default:
        return 0;
    }
}

static inline struct minic_zoned_queue *
minic_disk_queue(struct minic_zoned_disk *disk)
{
    return disk->queue;
}

int minic_disabled_zoned_entry(struct minic_zoned_disk *disk)
{
    if (minic_disabled_queue_is_zoned(minic_disk_queue(disk)))
        return minic_disabled_zoned_must_not_resolve();
    return 41;
}

int minic_dynamic_zoned_entry(struct minic_zoned_disk *disk)
{
    if (minic_dynamic_queue_is_zoned(minic_disk_queue(disk)))
        return minic_dynamic_zoned_must_resolve();
    return 43;
}

static inline struct minic_zoned_queue *
minic_disk_queue_with_effect(struct minic_zoned_disk *disk)
{
    ++minic_zoned_getter_visits;
    return disk->queue;
}

int minic_zoned_getter_effect_entry(struct minic_zoned_disk *disk)
{
    struct minic_zoned_queue *queue = minic_disk_queue_with_effect(disk);
    return queue->model + minic_zoned_getter_visits;
}
