/*
 * Copyright (c) 2013,2016-2017 Greg Becker.  All rights reserved.
 *
 * Performance test module.
 */

#ifndef PMT_H
#define PMT_H

struct pmt_priv_s;
struct pmt_share_s;

typedef int pmt_test_cb_t(struct pmt_share_s *shr, struct pmt_priv_s *priv);


/* Per-worker thread private data
 */
typedef struct pmt_priv_s {
    struct pmt_share_s *shr;
    cpuset_t vcpu_mask;
    int vcpu;

    pmt_test_cb_t *before;      // Func to call just once before every()
    pmt_test_cb_t *every;       // Func to call on every iteration
    pmt_test_cb_t *after;       // Func to call just once after every()

    unsigned long count;
} pmt_priv_t;


/* Data shared amongst all threads.
 */
typedef struct pmt_share_s {
    unsigned long count;

    __aligned(64)
    struct mtx    mtx;
    u_long        mtx_count;

    __aligned(64)
    struct mtx    spin;
    u_long        spin_count;

    __aligned(64)
    struct sx     sx;
    u_long        sx_count;

    __aligned(64)
    struct rmlock rm;
    u_long        rm_count;

    __aligned(64)
    struct rwlock rw;
    u_long        rw_count;

    __aligned(64)
    struct cv cv;           // Used for worker thread synchronization
    uint64_t stop;          // Stop time in cycles or nanoseconds
    uint64_t start;         // Start time in cycles or nanoseconds
    uint64_t sync;          // Used to synchronize test worker threads
    u_int nactive;          // Number of worker threads waiting to start

    __aligned(64)
    pmt_priv_t priv[MAXCPU];// Array of per-worker thread private data
} pmt_share_t;

#endif /* PMT_H */
