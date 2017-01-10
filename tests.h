/*
 * Copyright (c) 2013,2016-2017 Greg Becker.  All rights reserved.
 *
 * Performance test module.
 */

#ifndef PMT_TESTS_H
#define PMT_TESTS_H

extern pmt_test_cb_t pmt_empty_every;
extern pmt_test_cb_t pmt_mtx_every;
extern pmt_test_cb_t pmt_mtx_spin_every;
extern pmt_test_cb_t pmt_sx_slock_every;
extern pmt_test_cb_t pmt_sx_xlock_every;
extern pmt_test_cb_t pmt_rw_rlock_every;
extern pmt_test_cb_t pmt_rw_wlock_every;
extern pmt_test_cb_t pmt_rm_rlock_every;
extern pmt_test_cb_t pmt_rm_wlock_every;
extern pmt_test_cb_t pmt_rw_rlock_atomic_add_every;
extern pmt_test_cb_t pmt_nanotime_every;
extern pmt_test_cb_t pmt_getnanotime_every;
extern pmt_test_cb_t pmt_atomic_add_long_every;
extern pmt_test_cb_t pmt_atomic_fetchadd_long_every;
extern pmt_test_cb_t pmt_atomic_cmpset_long_every;
extern pmt_test_cb_t pmt_atomic_add_acq_long_every;
extern pmt_test_cb_t pmt_atomic_add_rel_long_every;
extern pmt_test_cb_t pmt_atomic_fetchadd_long_every;

#endif /* PMT_TESTS_H */
