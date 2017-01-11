/*
 * Copyright (c) 2013,2016-2017 Greg Becker.  All rights reserved.
 *
 * Performance test module.
 */

#include <sys/param.h>
#include <sys/limits.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/sched.h>
#include <vm/uma.h>
#include <sys/unistd.h>
#include <machine/stdarg.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
#include <sys/cpuset.h>
#include <sys/module.h>

#include "pmt.h"
#include "tests.h"


/* The "empty" functions are used to measure the overhead
 * of the test framework when calling a test callback.
 */
int
pmt_empty_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    return 0;
}


/* Increment a shared counter (no synchronization).
 */
int
pmt_inc_shared_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    ++shr->count;

    return 0;
}


/* Increment a per-cpu counter.
 */
int
pmt_inc_pcpu_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    ++priv->count;

    return 0;
}


/* Use a mutex to increment a shared counter.
 */
int
pmt_mtx_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    mtx_lock_flags(&shr->mtx, MTX_QUIET);
    ++shr->mtx_count;
    mtx_unlock(&shr->mtx);

    return 0;
}


/* Use a spin mutex to increment a shared counter.
 */
int
pmt_mtx_spin_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    mtx_lock_spin_flags(&shr->spin, MTX_QUIET);
    ++shr->spin_count;
    mtx_unlock_spin(&shr->spin);

    return 0;
}


/* Use an sx shared lock to increment a private counter
 */
int
pmt_sx_slock_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    sx_slock(&shr->sx);
    ++priv->count;
    sx_sunlock(&shr->sx);

    return 0;
}


/* Use an sx exclusive lock to increment a shared counter.
 */
int
pmt_sx_xlock_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    sx_xlock(&shr->sx);
    ++shr->sx_count;
    sx_xunlock(&shr->sx);

    return 0;
}


/* Use an rw read lock to do increment a private counter.
 */
int
pmt_rw_rlock_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    rw_rlock(&shr->rw);
    ++priv->count;
    rw_runlock(&shr->rw);

    return 0;
}


/* Use an rw write lock to increment a shared counter.
 */
int
pmt_rw_wlock_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    rw_wlock(&shr->rw);
    ++shr->rw_count;
    rw_wunlock(&shr->rw);

    return 0;
}


/* Use an rm read lock to increment a private counter.
 */
int
pmt_rm_rlock_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    struct rm_priotracker tracker;

    rm_rlock(&shr->rm, &tracker);
    ++priv->count;
    rm_runlock(&shr->rm, &tracker);

    return 0;
}


/* Use an rm write lock to increment a shared counter.
 */
int
pmt_rm_wlock_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    rm_wlock(&shr->rm);
    ++shr->rm_count;
    rm_wunlock(&shr->rm);

    return 0;
}


/* Use an rw read lock to increment an atomic shared variable.
 */
int
pmt_rw_rlock_atomic_add_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    rw_rlock(&shr->rw);
    atomic_add_long(&shr->rw_count, 1);
    rw_runlock(&shr->rw);

    return 0;
}


/* Use atomic_add_long() to increment a shared variable.
 */
int
pmt_atomic_add_long_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    atomic_add_long(&shr->count, 1);

    return 0;
}


/* Use atomic_fetchadd_long() to increment a shared variable.
 */
int
pmt_atomic_fetchadd_long_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    atomic_fetchadd_long(&shr->count, 1);

    return 0;
}


/* Use atomic_cmpset_long() to increment a shared variable.
 */
int
pmt_atomic_cmpset_long_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    int set;

    do {
        unsigned long old = shr->count;

        set = atomic_cmpset_long(&shr->count, old, old + 1);
    } while (!set);

    return 0;
}


/* Call nanotime().
 */
int
pmt_nanotime_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    struct timespec ts;

    nanotime(&ts);

    return 0;
}


/* Call getnanotime().
 */
int
pmt_getnanotime_every(pmt_share_t *shr, pmt_priv_t *priv)
{
    struct timespec ts;

    getnanotime(&ts);

    return 0;
}
