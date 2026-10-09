/* test_sleeplog.c - F4 sleep log ring buffer, weekly view, debt, F5 reference samples. */
#include "sg_test.h"

#define TZ_BA "America/Argentina/Buenos_Aires"

/* bed_ms of the i-th record, or -1 if out of range */
static int64_t bed_at(const SgState *s, int i) {
    const SgNight *n = sg_log_at(s, i);
    return n == NULL ? -1 : n->bed_ms;
}

static void test_log_bed_tap_adds(void) {
    SgState s;
    const SgNight *n;
    int64_t now, T;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    now = sg_at(20261008, 1380);
    T = sg_at(20261009, 420);
    SG_CHECK(sg_log_bed_tap(&s, now, T) == 1);
    SG_CHECK(s.night_count == 1);
    n = sg_log_at(&s, 0);
    SG_CHECK(n != NULL && n->bed_ms == now && n->wake_ms == T && n->closed == 0);
    SG_CHECK(sg_log_at(&s, 1) == NULL);
    SG_CHECK(sg_log_at(&s, -1) == NULL);
}

static void test_log_dedup_window(void) {
    SgState s;
    int64_t now, T;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    now = sg_at(20261008, 1380);
    T = sg_at(20261009, 420);
    SG_CHECK(sg_log_bed_tap(&s, now, T) == 1);
    /* within 30 min of an open record: updates bed_ms, no new record */
    SG_CHECK(sg_log_bed_tap(&s, now + SG_MIN_TO_MS(10), T) == 2);
    SG_CHECK(s.night_count == 1);
    SG_CHECK(bed_at(&s, 0) == now + SG_MIN_TO_MS(10));
    /* an earlier tap never moves bed_ms backwards */
    (void)sg_log_bed_tap(&s, now + SG_MIN_TO_MS(5), T);
    SG_CHECK(s.night_count == 1);
    SG_CHECK(bed_at(&s, 0) == now + SG_MIN_TO_MS(10));
    /* 40 min later: outside the dedup window, new record */
    SG_CHECK(sg_log_bed_tap(&s, now + SG_MIN_TO_MS(50), T) == 1);
    SG_CHECK(s.night_count == 2);
}

static void test_log_follow_and_close(void) {
    SgState s;
    int64_t now, T1, T2;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    now = sg_at(20261008, 1380);
    T1 = sg_at(20261009, 420);
    T2 = sg_at(20261009, 480);
    (void)sg_log_bed_tap(&s, now, T1);
    sg_log_follow_alarm(&s, T2);
    SG_CHECK(sg_log_at(&s, 0)->wake_ms == T2);
    SG_CHECK(sg_log_close_if_due(&s, T2 - 1) == 0);
    SG_CHECK(sg_log_close_if_due(&s, T2) == 1);
    SG_CHECK(sg_log_at(&s, 0)->closed == 1);
    /* a closed record is never deduplicated */
    SG_CHECK(sg_log_bed_tap(&s, now + SG_MIN_TO_MS(5), T2) == 1);
    SG_CHECK(s.night_count == 2);
    /* alarm removed: wake unknown, record cannot be closed */
    sg_log_follow_alarm(&s, 0);
    SG_CHECK(sg_log_at(&s, 1)->wake_ms == 0);
    SG_CHECK(sg_log_close_if_due(&s, T2 + SG_MIN_TO_MS(600)) == 0);
}

static void test_log_est_sleep(void) {
    SgNight n;
    sg_tz_set(TZ_BA);
    n.closed = 1;
    n.bed_ms = sg_at(20261008, 1380);
    n.wake_ms = sg_at(20261009, 420);
    SG_CHECK(sg_log_est_sleep_min(&n) == 460);          /* 8 h - 20 min */
    n.bed_ms = sg_at(20261008, 1200);
    n.wake_ms = sg_at(20261009, 780);
    SG_CHECK(sg_log_est_sleep_min(&n) == 940);          /* 17 h clamped to 16 h, -20 */
    n.bed_ms = sg_at(20261009, 420);
    n.wake_ms = sg_at(20261009, 430);
    SG_CHECK(sg_log_est_sleep_min(&n) == 0);            /* 10 min opportunity */
    n.wake_ms = 0;
    SG_CHECK(sg_log_est_sleep_min(&n) == -1);           /* unknown wake */
}

static void test_log_ring_wrap_90(void) {
    SgState s;
    int64_t base;
    int i, ok = 1, inorder = 1;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    base = sg_at(20261001, 0);
    for (i = 0; i < 95; i++) {
        int64_t bed = base + SG_MIN_TO_MS(120 * i);
        if (sg_log_bed_tap(&s, bed, bed + SG_MIN_TO_MS(540)) != 1) {
            ok = 0;
        }
    }
    SG_CHECK(ok);
    SG_CHECK(s.night_count == SG_LOG_RETAIN_NIGHTS);
    SG_CHECK(bed_at(&s, 0) == base + SG_MIN_TO_MS(120 * 5));
    SG_CHECK(bed_at(&s, 89) == base + SG_MIN_TO_MS(120 * 94));
    SG_CHECK(sg_log_at(&s, 90) == NULL);
    for (i = 1; i < SG_LOG_RETAIN_NIGHTS; i++) {
        if (bed_at(&s, i) <= bed_at(&s, i - 1)) {
            inorder = 0;
        }
    }
    SG_CHECK(inorder);
}

static void test_log_week_attribution(void) {
    SgState s;
    SgWeek w;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* wake 2026-10-02: outside the 7-night window ending 10-09, ignored */
    (void)sg_log_bed_tap(&s, sg_at(20261001, 1380), sg_at(20261002, 420));
    /* wake Wed 10-07 (index 4), 8 h in bed -> est 460 */
    (void)sg_log_bed_tap(&s, sg_at(20261006, 1410), sg_at(20261007, 450));
    /* wake Fri 10-09 = today (index 6), 8 h in bed -> est 460 */
    (void)sg_log_bed_tap(&s, sg_at(20261008, 1380), sg_at(20261009, 420));
    sg_log_week(&s, 20261009, &w);
    SG_CHECK(w.night[0].date == 20261003 && w.night[6].date == 20261009);
    SG_CHECK(w.night[6].wday == 5);
    SG_CHECK(w.night[4].status == 1 && w.night[4].bed_mod == 1410);
    SG_CHECK(w.night[4].wake_mod == 450 && w.night[4].est_sleep_min == 460);
    SG_CHECK(w.night[6].status == 1 && w.night[6].bed_mod == 1380);
    SG_CHECK(w.night[6].wake_mod == 420 && w.night[6].est_sleep_min == 460);
    SG_CHECK(w.night[0].status == 0 && w.night[0].bed_mod == -1);
    SG_CHECK(w.night[0].est_sleep_min == -1);
    SG_CHECK(w.logged_count == 2);
    SG_CHECK(w.debt_min == 40);
}

static void test_log_week_debt_surplus(void) {
    SgState s;
    SgWeek w;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* est 540 (surplus of 60 over target 480) */
    (void)sg_log_bed_tap(&s, sg_at(20261006, 1320), sg_at(20261007, 440));
    /* est 400 (deficit of 80) */
    (void)sg_log_bed_tap(&s, sg_at(20261008, 0), sg_at(20261008, 420));
    sg_log_week(&s, 20261009, &w);
    SG_CHECK(w.logged_count == 2);
    SG_CHECK(w.debt_min == 20);     /* surplus offsets deficit: (-60) + 80 */
}

static void test_log_week_debt_clamped(void) {
    SgState s;
    SgWeek w;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(20261006, 1320), sg_at(20261007, 440));
    (void)sg_log_bed_tap(&s, sg_at(20261007, 1320), sg_at(20261008, 440));
    sg_log_week(&s, 20261009, &w);
    SG_CHECK(w.logged_count == 2);
    SG_CHECK(w.debt_min == 0);      /* only surplus: clamped at zero */
}

static void test_log_week_open_night(void) {
    SgState s;
    SgWeek w;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* no wake known: attributed to bed + 12 h = 10-09 11:00 */
    (void)sg_log_bed_tap(&s, sg_at(20261008, 1380), 0);
    sg_log_week(&s, 20261009, &w);
    SG_CHECK(w.night[6].status == 2 && w.night[6].bed_mod == 1380);
    SG_CHECK(w.night[6].wake_mod == -1 && w.night[6].est_sleep_min == -1);
    SG_CHECK(w.logged_count == 0 && w.debt_min == 0);
}

static void test_log_week_newest_wins(void) {
    SgState s;
    SgWeek w;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(20261008, 1380), sg_at(20261009, 420));
    /* later record, same wake date: must replace the earlier one in the view */
    (void)sg_log_bed_tap(&s, sg_at(20261009, 180), sg_at(20261009, 480));
    sg_log_week(&s, 20261009, &w);
    SG_CHECK(w.night[6].status == 1 && w.night[6].bed_mod == 180);
    SG_CHECK(w.night[6].est_sleep_min == 280);
    SG_CHECK(w.logged_count == 1);
    SG_CHECK(w.debt_min == 200);
}

static void test_log_clear(void) {
    SgState s;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(20261008, 1380), sg_at(20261009, 420));
    sg_ref_add(&s, 430);
    sg_log_clear(&s);
    SG_CHECK(s.night_count == 0);
    SG_CHECK(s.ref_count == 0);
}

static void test_ref_median_lower(void) {
    SgState s;
    sg_state_defaults(&s);
    SG_CHECK(sg_ref_median(&s) == -1);
    sg_ref_add(&s, 600);
    sg_ref_add(&s, 500);
    SG_CHECK(sg_ref_median(&s) == -1);      /* below SG_REF_SAMPLES_MIN */
    sg_ref_add(&s, 700);
    SG_CHECK(sg_ref_median(&s) == 600);
    sg_ref_add(&s, 800);
    SG_CHECK(sg_ref_median(&s) == 600);     /* lower median of 500,600,700,800 */
}

static void test_ref_cap_ten(void) {
    SgState s;
    int v;
    sg_state_defaults(&s);
    for (v = 0; v <= 10; v++) {
        sg_ref_add(&s, (int16_t)v);       /* 11 samples: oldest (0) evicted */
    }
    SG_CHECK(s.ref_count == SG_REF_SAMPLES_MAX);
    SG_CHECK(sg_ref_median(&s) == 5);     /* median of 1..10, lower = 5 */
}

static void test_ref_clear(void) {
    SgState s;
    sg_state_defaults(&s);
    sg_ref_add(&s, 400);
    sg_ref_add(&s, 410);
    sg_ref_add(&s, 420);
    SG_CHECK(sg_ref_median(&s) == 410);
    sg_ref_clear(&s);
    SG_CHECK(s.ref_count == 0);
    SG_CHECK(sg_ref_median(&s) == -1);
}

/* K5: an early alarm dismissal (T jumps a day) keeps last night's wake time and date. */
static void test_log_early_dismiss_keeps_night(void) {
    SgState s;
    SgWeek w;
    int k;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* Sun 22:40 bed, alarm Mon 07:00 */
    (void)sg_log_bed_tap(&s, sg_at(20261011, 1360), sg_at(20261012, 420));
    /* Mon 06:50 the user dismisses: T moves to Tue 07:00, 24 h after bed */
    sg_log_follow_alarm(&s, sg_at(20261013, 420));
    SG_CHECK(sg_log_at(&s, 0)->wake_ms == sg_at(20261012, 420));
    /* Mon 22:40 tap: a new record, not an overwrite of Sunday's */
    SG_CHECK(sg_log_bed_tap(&s, sg_at(20261012, 1360), sg_at(20261013, 420)) == 1);
    sg_log_week(&s, 20261013, &w);
    SG_CHECK(w.logged_count == 2);
    for (k = 0; k < SG_DEBT_WINDOW_NIGHTS; k++) {
        if (w.night[k].date == 20261012) {
            SG_CHECK(w.night[k].status == 1 && w.night[k].est_sleep_min == 480);
        }
        if (w.night[k].date == 20261013) {
            SG_CHECK(w.night[k].status == 1 && w.night[k].est_sleep_min == 480);
        }
    }
}

void run_sleeplog_tests(void) {
    SG_RUN(test_log_bed_tap_adds);
    SG_RUN(test_log_dedup_window);
    SG_RUN(test_log_follow_and_close);
    SG_RUN(test_log_early_dismiss_keeps_night);
    SG_RUN(test_log_est_sleep);
    SG_RUN(test_log_ring_wrap_90);
    SG_RUN(test_log_week_attribution);
    SG_RUN(test_log_week_debt_surplus);
    SG_RUN(test_log_week_debt_clamped);
    SG_RUN(test_log_week_open_night);
    SG_RUN(test_log_week_newest_wins);
    SG_RUN(test_log_clear);
    SG_RUN(test_ref_median_lower);
    SG_RUN(test_ref_cap_ten);
    SG_RUN(test_ref_clear);
}
