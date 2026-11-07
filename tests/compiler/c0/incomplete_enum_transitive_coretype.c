/* Linux-style forward enums in an *unused* externally declared function
 * pointer's transitive record type have no object layout or known integer
 * ABI. C->Core must preserve their distinct nominal identities rather than
 * trying to import an undefined compatible integer representation.
 *
 * These paths match the original Linux 6.6.143 fwnode_operations and consw
 * headers, but the contract is language-wide and applies to every target. */
enum dma_attr;
enum vc_intensity;
struct device;

struct fwnode_operations {
    enum dma_attr (*device_get_dma_attr)(const struct device *);
    unsigned int (*con_build_attr)(enum vc_intensity);
};
struct fwnode_handle { const struct fwnode_operations *ops; };
struct device { struct fwnode_handle *fwnode; unsigned long flags; };

extern int (*platform_notify)(struct device *);
extern const struct fwnode_operations *conswitchp;

/* Exercise both imported external symbol identities from reachable machine
 * code. Do not call the opaque-enum callbacks: without a definition, their
 * by-value ABI is deliberately unknown. */
int (*platform_notify)(struct device *) = 0;
const struct fwnode_operations *conswitchp = 0;

/* Complete enum uses keep their actual C integer ABI and never become
 * opaque. The linked native oracle exercises this independent control. */
enum complete_direction { DIRECTION_UP = 7, DIRECTION_DOWN = 9 };
static int direction_value(enum complete_direction direction) {
    return (int)direction;
}
int main(void) {
    return direction_value(DIRECTION_DOWN) == 9 &&
                   platform_notify == 0 && conswitchp == 0 ? 0 : 1;
}
