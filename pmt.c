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
#include <sys/sbuf.h>
#include <sys/mman.h>
#include <sys/module.h>

#include "pmt.h"
#include "tests.h"

#define PMT_TSC         // Use time stamp counter
//#define PMT_BEFORE
//#define PMT_AFTER

extern uint64_t tsc_freq;

static unsigned int pmt_pri = PRI_MIN_KERN;
static unsigned int pmt_verbosity = 1;
static unsigned int pmt_samples_step = CACHE_LINE_SIZE;
static unsigned int pmt_samples = 3;
static unsigned int pmt_iters = 4 * 1000 * 1000;
static uint64_t pmt_roundup = 2 * 1024 * 1024;
static uint64_t pmt_align = MAP_ALIGNED_SUPER;
static char pmt_results[2048];
static char pmt_tests[1024];

static char pmt_cpustr[CPUSETBUFSIZ];
static cpuset_t pmt_cpuset;


typedef struct {
    pmt_test_cb_t   *every;     // Func to call on every iteration
    pmt_test_cb_t   *before;    // Func to call once before every()
    pmt_test_cb_t   *after;     // Func to call once after every()
    const char      *help;
    const char      *name;
} pmt_test_t;

typedef struct {
    unsigned long delta;        // Sample time (stop - start) in cycles or nsecs.
    unsigned long iters;        // Sample iterations
} pmt_sample_t;


static int pmt_run(pmt_test_t *ptest, void *mem, size_t memsz,
                   int nsamples, pmt_sample_t *psamples);

static int pmt_kthread_create(void (*func)(void *), void *arg, const char *name);


MALLOC_DEFINE(M_PMT, "pmt", "perf measurement tool");


SYSCTL_NODE(_debug, OID_AUTO, pmt,
            CTLFLAG_RW,
            0, "pmt perf measurement tool");

SYSCTL_UINT(_debug_pmt, OID_AUTO, pri,
            CTLFLAG_RW,
            &pmt_pri, 0,
            "Priority at which to run each kernel thread");

SYSCTL_UINT(_debug_pmt, OID_AUTO, verbosity,
            CTLFLAG_RW,
            &pmt_verbosity, 0,
            "Show intermediate results on console");

SYSCTL_UINT(_debug_pmt, OID_AUTO, samples_step,
            CTLFLAG_RW,
            &pmt_samples_step, 0,
            "Number of bytes to step forward in memory on each test sample iteration");

SYSCTL_UINT(_debug_pmt, OID_AUTO, iter,
            CTLFLAG_RW,
            &pmt_iters, 0,
            "Number of iterations per thread per test loop iteration");

SYSCTL_UINT(_debug_pmt, OID_AUTO, samples,
            CTLFLAG_RW,
            &pmt_samples, 0,
            "Number of samples per test");

SYSCTL_U64(_debug_pmt, OID_AUTO, roundup,
           CTLFLAG_RW,
           &pmt_roundup, 0,
           "Round up test memory allocation to this size");

SYSCTL_U64(_debug_pmt, OID_AUTO, align,
           CTLFLAG_RW,
           &pmt_align, 0,
           "Test memory allocation alignmentment");


static pmt_test_t tests[] = {
    { .name = "null",
      .help = "pmt framework overhead",
    },

    { .name = "empty",
      .help = "call function that does nothing",
      .every = pmt_empty_every,
    },

    { .name = "atomic_add_long",
      .help = "use atomic_add_long() to increment a shared counter",
      .every = pmt_atomic_add_long_every,
    },

    { .name = "atomic_fetchadd_long",
      .help = "use atomic_fetchadd_long() to increment a shared counter",
      .every = pmt_atomic_fetchadd_long_every,
    },

    { .name = "atomic_cmpset_long",
      .help = "use atomic_cmpset_long() to increment a counter",
      .every = pmt_atomic_cmpset_long_every,
    },

    { .name = "rm_rlock",
      .help = "use a shared rm read lock to do nothing",
      .every = pmt_rm_rlock_every,
    },

    { .name = "rm_wlock",
      .help = "use a shared rm write lock to increment a counter",
      .every = pmt_rm_wlock_every,
    },

    { .name = "sx_slock",
      .help = "use a shared sx shared lock to do nothing",
      .every = pmt_sx_slock_every,
    },

    { .name = "sx_xlock",
      .help = "use a shared sx lock to increment a counter",
      .every = pmt_sx_xlock_every,
    },

    { .name = "mutex",
      .help = "use a shared mutex to increment a shared counter",
      .every = pmt_mtx_every,
    },

    { .name = "spin",
      .help = "use a shared spin mutex to increment a shared counter",
      .every = pmt_mtx_spin_every,
    },

    { .name = "rw_rlock",
      .help = "use a shared rw read lock to increment a private counter",
      .every = pmt_rw_rlock_every,
    },

    { .name = "rw_wlock",
      .help = "use a shared rw write lock to increment a shared counter",
      .every = pmt_rw_wlock_every,
    },

    { .name = "rw_rlock+atomic_add_long",
      .help = "use a shared rw read lock to increment an atomic counter",
      .every = pmt_rw_rlock_atomic_add_every,
    },

    { .name = "getnanotime",
      .help = "call getnanotime",
      .every = pmt_getnanotime_every,
    },

    { .name = "nanotime",
      .help = "call nanotime",
      .every = pmt_nanotime_every,
    },

    { .name = NULL }
};

static unsigned long
pmt_cycles2nsecs(unsigned long cycles)
{
    if (((cycles << 30) >> 30) == cycles)
        return (cycles * 1000000000ul) / tsc_freq;

    if (((cycles << 20) >> 20) == cycles)
        return (((cycles * 1000000ul) / tsc_freq) * 1000);

    if (((cycles << 10) >> 10) == cycles)
        return (((cycles * 1000ul) / tsc_freq) * 1000000);

    return ((cycles / tsc_freq) * 1000000000ul);
}

static void
pmt_tests_reset(void)
{
    pmt_test_t *test;
    char *dst, *sep;
    size_t len;

    dst = pmt_tests;
    *dst = '\000';
    sep = "";

    for (test = tests; test->name; ++test) {
        len = strlen(test->name) + strlen(sep);
        if (dst + len >= pmt_tests + sizeof(pmt_tests))
            break;

        strcat(dst, sep);
        strcat(dst, test->name);
        dst += len;
        sep = " ";
    }
}

static int
pmt_tests_sysctl(SYSCTL_HANDLER_ARGS)
{
    if (pmt_tests[0] == '\000' || 0 == strcmp(pmt_tests, "all"))
        pmt_tests_reset();

    return sysctl_handle_string(oidp, pmt_tests, sizeof(pmt_tests), req);
}

SYSCTL_PROC(_debug_pmt, OID_AUTO, tests,
            CTLTYPE_STRING | CTLFLAG_RW,
            NULL, 0, pmt_tests_sysctl, "A",
            "List of tests to run");

static int
pmt_run_sysctl(SYSCTL_HANDLER_ARGS)
{
    unsigned long baseline_cycles, baseline_nsecs;
    char cpustr[CPUSETBUFSIZ];
    pmt_sample_t *samplesv;
    struct cpuset *set;
    struct thread *td;
    struct proc *proc;
    pmt_test_t *test;
    struct sbuf *sb;
    cpuset_t cpuset;
    int samplesc;
    size_t memsz;
    void *mem;
    int rc;

    cpusetobj_strprint(cpustr, &pmt_cpuset);

    rc = sysctl_handle_string(oidp, cpustr, sizeof(cpustr), req);
    if (rc || !req->newptr)
        return rc;

    rc = cpusetobj_strscan(&cpuset, cpustr);
    if (rc)
        return EINVAL;

    rc = cpuset_which(CPU_WHICH_CPUSET, -1, &proc, &td, &set);
    if (rc)
        return rc;

    CPU_AND(&cpuset, &set->cs_mask);
    cpuset_rel(set);

    if (CPU_EMPTY(&cpuset))
        return EINVAL;

    cpusetobj_strprint(pmt_cpustr, &cpuset);
    CPU_COPY(&cpuset, &pmt_cpuset);

    //sb = sbuf_new(NULL, NULL, 1024 * 1024, SBUF_AUTOEXTEND | SBUF_INCLUDENUL);
    sb = sbuf_new_auto();
    if (!sb)
        return ENOMEM;

    sbuf_clear(sb);

    /* Ensure roundup and align are of an integral page size.
     */
    pmt_roundup = roundup(pmt_roundup, PAGE_SIZE);
    pmt_align = roundup(pmt_align, PAGE_SIZE);

    samplesc = pmt_samples + 1;
    if (samplesc < 2)
        samplesc = 2;
    else if (samplesc > 128)
        samplesc = 128;

    /* Determine how much memory we need to run the test.
     */
    memsz = sizeof(pmt_share_t) * samplesc * pmt_samples_step;
    memsz = roundup(memsz, pmt_roundup);

    mem = contigmalloc(memsz, M_PMT, M_NOWAIT, 0, ~(vm_paddr_t)0, pmt_align, 0);
    if (!mem) {
        printf("%s: unable to malloc %lu contiguous bytes\n", __func__, memsz);
        return ENOMEM;
    }

    samplesv = malloc(sizeof(*samplesv) * samplesc, M_PMT, M_NOWAIT);
    if (!samplesv) {
        printf("%s: unable to malloc %lu bytes for samplesv\n",
               __func__, sizeof(*samplesv) * samplesc);
        contigfree(mem, memsz, M_PMT);
        return ENOMEM;
    }

    sbuf_printf(sb, "\n%16s %3s %12s %12s %8s %12s %8s  %s\n",
                "vCPUMASK", "TDS", "CALLS/s",
                "ns", "ns/CALL", "CYCLES", "CY/CALL", "NAME");

    baseline_cycles = baseline_nsecs = 0;
    rc = 0;

    /* Run each test listed in pmt_tests[].
     */
    for (test = tests; test->name; ++test) {
        unsigned long cycles_avg, nsecs_avg, iters_avg;
        int i;

        if (!strstr(pmt_tests, test->name))
            continue;

        rc = pmt_run(test, mem, memsz, samplesc, samplesv);
        if (rc) {
            sbuf_printf(sb, "%s interrupted %d\n",
                        test->name, rc);
            break;
        }

        /* Discard the first smaple and average the rest.
         */
        nsecs_avg = iters_avg = cycles_avg = 0;

        for (i = 1; i < samplesc; ++i) {
            nsecs_avg += samplesv[i].delta;
            iters_avg += samplesv[i].iters;
        }

        nsecs_avg /= (samplesc - 1);
        iters_avg /= (samplesc - 1);

#ifdef PMT_TSC
        cycles_avg = nsecs_avg;
        nsecs_avg = pmt_cycles2nsecs(cycles_avg);
#endif

        if (nsecs_avg <= baseline_nsecs || iters_avg < 1)
            continue;

        /* Subtract the pmt framework overhead.
         */
        if (test->every) {
            cycles_avg -= baseline_cycles;
            nsecs_avg -= baseline_nsecs;
        }

        /* (Re)compute the baseline.
         *
         * TODO: Unconditionally run the "null" and "empty" tests.
         */
        if (!test->every || test->every == pmt_empty_every) {
            baseline_cycles = cycles_avg;
            baseline_nsecs = nsecs_avg;
        }

        sbuf_printf(sb, "%016lx %3u %12lu %12lu %8lu %12lu %8lu  %s\n",
                    pmt_cpuset.__bits[0],                   // vCPUMASK
                    CPU_COUNT(&cpuset),                     // TDS
                    (iters_avg * 1000000000ul) / nsecs_avg, // CALLS/s
                    nsecs_avg,                              // ns
                    nsecs_avg / iters_avg,                  // ns/CALL
                    cycles_avg,                             // CYCLES
                    cycles_avg / iters_avg,                 // CY/CALL
                    test->name);
    }

    sbuf_finish(sb);
    strlcpy(pmt_results, sbuf_data(sb), sizeof(pmt_results));
    sbuf_delete(sb);

    contigfree(mem, memsz, M_PMT);
    free(samplesv, M_PMT);

    return rc;
}

SYSCTL_PROC(_debug_pmt, OID_AUTO, run,
            CTLTYPE_STRING | CTLFLAG_RW,
            NULL, 0, pmt_run_sysctl, "",
            "Run the pmt test loop");


static int
pmt_results_sysctl(SYSCTL_HANDLER_ARGS)
{
    return sysctl_handle_string(oidp, pmt_results, strlen(pmt_results) + 1, req);
}

SYSCTL_PROC(_debug_pmt, OID_AUTO, results,
            CTLTYPE_STRING | CTLFLAG_RD,
            NULL, 0, pmt_results_sysctl, "A",
            "Show pmt run results");


/* This is the "main" routine for each thread created by pmt_run().
 */
static void
pmt_run_main(void *arg)
{
    struct thread *td = curthread;
    pmt_priv_t *priv = arg;
    unsigned int nrunning;
    pmt_test_cb_t *every;
    unsigned int iters;
    pmt_share_t *shr;
    int rc;

    if (!priv || !priv->shr) {
        printf("%s: priv or priv->shr is nil priv=%p priv->shr=%p\n",
               __func__, priv, priv->shr);
        kthread_exit();
    }

    /* Affine this thread to the given vCPU.
     */
    rc = cpuset_setthread(td->td_tid, &priv->vcpu_mask);
    if (rc) {
        printf("%s: cpuset_setthread() failed: rc=%d vcpu=%d\n",
               __func__, rc, priv->vcpu);
        kthread_exit();
    }

    every = priv->every;
    iters = pmt_iters;
    shr = priv->shr;
    nrunning = 0;

    /* Wait here for pmt_run() to signal us, which won't happen until
     * all worker threads have arrived at this point and called cv_wait().
     */
    mtx_lock(&shr->mtx);
    ++shr->nactive;
    cv_wait_unlock(&shr->cv, &shr->mtx);

    /* Busy wait to synchronize all threads that have awakened.
     */
#ifdef PMT_TSC
    while (rdtsc() < shr->sync)
        ;
#else
    while (getnanotime() < shr->sync)
        ;
#endif

    /* First thread to increment nrunning records the start time.
     */
    if (0 == atomic_fetchadd_int(&nrunning, 1)) {
#ifdef PMT_TSC
        shr->start = rdtsc();
#else
        struct timespec ts_start;

        getnanotime(&ts_start);
        shr->start = ts_start.tv_sec * 1000000000L + ts_start.tv_nsec;
#endif
    }

#ifdef PMT_BEFORE
    if (priv->before) {
        priv->before(shr, priv);
    }
#endif

    /* Run the test iteration.
     *
     * Note:  In our attempt to measure the cost of the framework
     * we want to run the loop even if 'every' is NULL.
     */
    while (iters-- > 0) {
        if (every) {
            every(shr, priv);
        }
    }

#ifdef PMT_AFTER
    if (priv->after) {
        priv->after(shr, priv);
    }
#endif

    /* Last thread out records the stop time and signals the master
     * thread waiting in pmt_run().
     */
    if (1 == atomic_fetchadd_int(&shr->nactive, -1)) {
#ifdef PMT_TSC
        shr->stop = rdtsc();
#else
        struct timespec ts_stop;

        getnanotime(&ts_stop);
        shr->stop = ts_stop.tv_sec * 1000000000L + ts_stop.tv_nsec;
#endif

        mtx_lock(&shr->mtx);
        cv_broadcast(&shr->cv);
        mtx_unlock(&shr->mtx);
    }

    kthread_exit();
}


/* This function orchestrates running the give test concurrently
 * across all the vCPUs specified by pmt_cpuset.
 */
static int
pmt_run(pmt_test_t *ptest, void *mem, size_t memsz, int nsamples, pmt_sample_t *psamples)
{
    uint64_t samples_step = pmt_samples_step;
    int signaled = 0;
    int rc;
    int n;

    if (pmt_verbosity > 0) {
        printf("\n%s:\n", ptest->name);

        printf("%4s %16s %12s %12s %8s %12s %9s\n",
               "LOOP", "vCPUMASK", "CALLS/s",
               "ns", "ns/CALL", "CYCLES", "CY/CALL");
    }

    for (n = 0; n < nsamples; ++n, ++psamples) {
        unsigned long cycles = 0;
        unsigned long nsecs = 0;
        unsigned int iters = 0;
        int nworkers = 0;
        pmt_share_t *shr;
        int i;

        shr = (pmt_share_t *)((uintptr_t)mem + (n * samples_step));

        if ((char *)shr > (char *)mem + memsz) {
            printf("%s: pmt_samples or pmt_samples_step changed...\n", __func__);
            return EINVAL;
        }

        memset(shr, 0, sizeof(*shr));

        mtx_init(&shr->mtx, "pmtmtx", (char *)0, MTX_DEF);
        mtx_init(&shr->spin, "pmtspin", (char *)0, MTX_SPIN);
        sx_init(&shr->sx, "pmtsx");
        rw_init(&shr->rw, "pmtrw");
        rm_init(&shr->rm, "pmtrm");
        cv_init(&shr->cv, "pmtcv");

        /* Start a worker thread for each vCPU in the set.
         */
        for (i = 0; i < MAXCPU; ++i) {
            if (CPU_ISSET(i, &pmt_cpuset)) {
                pmt_priv_t *priv = &shr->priv[i];

                priv->shr = shr;
                priv->before = ptest->before;
                priv->after = ptest->after;
                priv->every = ptest->every;
                priv->vcpu = i;

                CPU_ZERO(&priv->vcpu_mask);
                CPU_SET(i, &priv->vcpu_mask);

                rc = pmt_kthread_create(pmt_run_main, priv, "pmt");
                if (rc) {
                    printf("%s: kthread create failed: %d\n", __func__, rc);
                    continue;
                }

                iters += pmt_iters;
                ++nworkers;
            }
        }

        shr->count = 0;

        /* Wait/poll for all worker threads to get to the rendezvous point.
         */
        mtx_lock(&shr->mtx);
        while (shr->nactive < nworkers) {
            msleep(shr, &shr->mtx, 0, "wait", hz / 10);
        }

        /* Give all worker threads ~33ms to wake up and reach the rendezvous
         * point (in an attempt to arrange that they are all scheduled and
         * running and hence start the test at approximately the same time).
         */
#ifdef PMT_TSC
        shr->sync = rdtsc() + tsc_freq / 33;
#else
        shr->sync = getnanotime() + 33 * 1000000;
#endif

        /* Signal all the worker threads to start running, then wait for
         * them all to finish.  The last thread to finish will wake us up.
         */
        cv_broadcast(&shr->cv);

        while (shr->nactive > 0) {
            if (signaled) {
                cv_wait_sig(&shr->cv, &shr->mtx);
                continue;
            }

            signaled = cv_wait_sig(&shr->cv, &shr->mtx);
        }
        mtx_unlock(&shr->mtx);


        /* Test is done, record results then clean up.
         */
        psamples->delta = shr->stop - shr->start;
        psamples->iters = iters;

#ifdef PMT_TSC
        cycles = (shr->stop - shr->start);
        nsecs = pmt_cycles2nsecs(cycles);
#else
        nsecs = psamples->delta;
#endif

        if (nsecs == 0) {
            nsecs = 1;
        }
        if (iters == 0) {
            iters = 1;
        }

        if (pmt_verbosity > 0) {
            printf("%4d %016lx %12lu %12lu %8lu %12lu %9lu\n",
                   n, pmt_cpuset.__bits[0],
                   (iters * 1000000000ul) / nsecs,          // CALLS/s
                   nsecs,                                   // ns
                   nsecs / iters,                           // ns/CALL
                   cycles,                                  // CYCLES
                   cycles / iters);                         // CY/CALL
        }

        cv_destroy(&shr->cv);
        rm_destroy(&shr->rm);
        rw_destroy(&shr->rw);
        sx_destroy(&shr->sx);
        mtx_destroy(&shr->spin);
        mtx_destroy(&shr->mtx);

        if (signaled)
            break;
    }

    return signaled;
}


static int
pmt_kthread_create(void (*func)(void *), void *arg, const char *name)
{
    struct thread *td;
    int rc;

    rc = kthread_add(func, arg, (void *)0, &td, RFSTOPPED, 0, "%s", name);
    if (rc) {
        printf("%s: kthread_add: rc=%d\n", __func__, rc);
        return rc;
    }

    thread_lock(td);
    sched_prio(td, pmt_pri);
    sched_add(td, SRQ_BORING);
    thread_unlock(td);

    return 0;
}



static int
pmt_modevent(module_t mod, int cmd, void *data)
{
    int rc = 0;

    switch (cmd) {
    case MOD_LOAD:
        pmt_tests_reset();
        break;

    case MOD_UNLOAD:
        break;

    default:
        rc = EOPNOTSUPP;
        break;
    }

    return rc;
}


static moduledata_t pmt_mod = {
    "pmt",
    pmt_modevent,
    NULL,
};


DECLARE_MODULE(pmt, pmt_mod, SI_SUB_EXEC, SI_ORDER_ANY);
MODULE_VERSION(pmt, 1);
