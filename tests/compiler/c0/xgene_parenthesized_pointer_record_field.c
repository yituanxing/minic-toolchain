/* Linux xgene_enet_ring.h: `struct sk_buff *(*rx_skb);` is a pointer-to-
 * pointer object field, not a function pointer (no parameter suffix follows). */
struct Packet { int bytes; };
struct Ring {
    struct Packet *(*rx_skb);
    struct Packet *(*const owner);
    struct Packet *(*deliver)(void);
    struct Packet *(*buffers)[3];
};

static struct Packet packet = { 17 };
static struct Packet *head = &packet;
static struct Packet *buffer_rows[3] = { &packet, 0, &packet };
static struct Packet *next_packet(void) { return &packet; }
static struct Ring ring = {
    .rx_skb = &head,
    .owner = &head,
    .deliver = next_packet,
    .buffers = &buffer_rows,
};
int main(void) {
    return ring.rx_skb == &head && *ring.rx_skb == &packet &&
           (*ring.owner)->bytes == 17 && ring.deliver() == &packet &&
           (*ring.buffers)[2] == &packet ? 0 : 1;
}
