/* Linux 6.6 xen/interface/io/ring.h RING_COPY_RESPONSE shape. */
typedef unsigned long long u64;
typedef unsigned short u16;
typedef unsigned char u8;
typedef short s16;

struct blkif_response {
    u64 id;
    u8 operation;
    s16 status;
};
struct xen_netif_tx_response {
    u16 id;
    s16 status;
};

struct blkif_slot { struct blkif_response rsp; };
struct netif_slot { struct xen_netif_tx_response rsp; };
struct blkif_sring { struct blkif_slot ring[4]; };
struct netif_sring { struct netif_slot ring[4]; };
struct blkif_front_ring { struct blkif_sring *sring; unsigned nr_ents; };
struct netif_front_ring { struct netif_sring *sring; unsigned nr_ents; };

#define RING_GET_RESPONSE(r, i) \
    (&(r)->sring->ring[((i) & ((r)->nr_ents - 1))].rsp)
#define RING_COPY_RESPONSE(r, i, dest) do { \
    *(dest) = *(volatile typeof(dest))RING_GET_RESPONSE(r, i); \
} while (0)

void xen_blkif_response_copy(struct blkif_front_ring *ring, unsigned index,
                             struct blkif_response *result) {
    RING_COPY_RESPONSE(ring, index, result);
}

void xen_netif_response_copy(struct netif_front_ring *ring, unsigned index,
                             struct xen_netif_tx_response *result) {
    RING_COPY_RESPONSE(ring, index, result);
}

int main(void) {
    struct blkif_sring blk_sring = {0};
    struct netif_sring net_sring = {0};
    struct blkif_front_ring blk = {&blk_sring, 4};
    struct netif_front_ring net = {&net_sring, 4};
    struct blkif_response block;
    struct xen_netif_tx_response network;
    blk_sring.ring[2].rsp.id = 0x1234;
    blk_sring.ring[2].rsp.operation = 7;
    blk_sring.ring[2].rsp.status = -2;
    net_sring.ring[3].rsp.id = 17;
    net_sring.ring[3].rsp.status = -1;
    xen_blkif_response_copy(&blk, 2, &block);
    xen_netif_response_copy(&net, 3, &network);
    return block.id == 0x1234 && block.operation == 7 && block.status == -2 &&
           network.id == 17 && network.status == -1 ? 0 : 1;
}
