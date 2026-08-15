struct CpuMask {
    unsigned long bits[1];
};
typedef struct CpuMask OneMask[1];
typedef struct CpuMask TwoMasks[2];
struct WrongMaskHolder {
    TwoMasks scratch_mask;
};
static int accept_one(OneMask *mask) {
    return (int)sizeof(**mask);
}
int reject_wrong_bound(struct WrongMaskHolder *rq) {
    return accept_one(&rq->scratch_mask);
}
