/* test_sleeplog.c - F4 sleep log ring buffer, weekly view, debt, F5 reference samples. */
#include "sg_test.h"
#include <string.h>

#define TZ_BA "America/Argentina/Buenos_Aires"

#define D_WED 20261007   /* Wednesday */
#define D_THU 20261008   /* Thursday  */
#define D_FRI 20261009   /* Friday    */
#define D_SAT 20261010   /* Saturday  */
#define D_SUN 20261011   /* Sunday    */

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

/* v2: sg_log_open_bed_for = bed_ms of the open newest record whose wake_ms == T (T > 0). */
static void test_log_open_bed_for(void) {
    SgState s;
    int64_t T = sg_at(20261009, 420);
    int64_t bed = sg_at(20261008, 1380);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    SG_CHECK(sg_log_open_bed_for(&s, T) == 0);                      /* empty log */
    SG_CHECK(sg_log_open_bed_for(NULL, T) == 0);
    (void)sg_log_bed_tap(&s, bed, T);
    SG_CHECK(sg_log_open_bed_for(&s, T) == bed);                    /* open, wake == T */
    SG_CHECK(sg_log_open_bed_for(&s, T + SG_MIN_TO_MS(30)) == 0);   /* another alarm */
    SG_CHECK(sg_log_open_bed_for(&s, 0) == 0);                      /* T = 0 never matches */
    SG_CHECK(sg_log_close_if_due(&s, T) == 1);
    SG_CHECK(sg_log_open_bed_for(&s, T) == 0);                      /* closed */
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, bed, 0);                               /* wake unknown */
    SG_CHECK(sg_log_open_bed_for(&s, 0) == 0);
    SG_CHECK(sg_log_open_bed_for(&s, T) == 0);
}

/* v2: sg_log_bed_retap = "Actualizar" (same night, bed moves forward, never appends);
 * otherwise exactly the v1 sg_log_bed_tap. */
static void test_log_bed_retap(void) {
    SgState s;
    int64_t bed = sg_at(20261008, 1380);                 /* 23:00 Thu */
    int64_t T = sg_at(20261009, 420);
    int64_t T2 = sg_at(20261009, 480);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    SG_CHECK(sg_log_bed_retap(NULL, bed, T) == 0);
    SG_CHECK(sg_log_bed_retap(&s, 0, T) == 0);           /* now_ms <= 0 */
    SG_CHECK(sg_log_bed_retap(&s, bed, T) == 1);         /* no record: append */
    SG_CHECK(s.night_count == 1);
    /* same night, 90 min later: updated, not appended (v1 dedup would append) */
    SG_CHECK(sg_log_bed_retap(&s, bed + SG_MIN_TO_MS(90), T) == 2);
    SG_CHECK(s.night_count == 1 && bed_at(&s, 0) == bed + SG_MIN_TO_MS(90));
    /* an earlier time never moves bed_ms backwards */
    SG_CHECK(sg_log_bed_retap(&s, bed + SG_MIN_TO_MS(10), T) == 0);
    SG_CHECK(s.night_count == 1 && bed_at(&s, 0) == bed + SG_MIN_TO_MS(90));
    SG_CHECK(sg_log_at(&s, 0)->wake_ms == T);
    /* different alarm: v1 rule applies (inside 30 min -> update, wake follows) */
    SG_CHECK(sg_log_bed_retap(&s, bed + SG_MIN_TO_MS(100), T2) == 2);
    SG_CHECK(s.night_count == 1 && sg_log_at(&s, 0)->wake_ms == T2);
    /* same night (now wake == T2), 200 min later: updated at any distance, never appended */
    SG_CHECK(sg_log_bed_retap(&s, bed + SG_MIN_TO_MS(300), T2) == 2);
    SG_CHECK(s.night_count == 1 && bed_at(&s, 0) == bed + SG_MIN_TO_MS(300));
    /* a third alarm 200 min after the bed: no open night for it, v1 rule appends */
    SG_CHECK(sg_log_bed_retap(&s, bed + SG_MIN_TO_MS(600), T2 + SG_MIN_TO_MS(30)) == 1);
    SG_CHECK(s.night_count == 2);
    /* closed record is never updated: a new one is appended */
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, bed, T);
    SG_CHECK(sg_log_close_if_due(&s, T) == 1);
    SG_CHECK(sg_log_bed_retap(&s, bed + SG_MIN_TO_MS(60), T) == 1);
    SG_CHECK(s.night_count == 2 && sg_log_at(&s, 0)->closed == 1);
    SG_CHECK(bed_at(&s, 0) == bed);
}

/* ---- v3 manual night (docs/ADVICE-v3.md section 2): sg_log_attr_date, sg_log_put_manual ---- */

/* est_sleep_min of the i-th record, or -2 if out of range */
static int32_t est_at(const SgState *s, int i) {
    const SgNight *n = sg_log_at(s, i);
    return n == NULL ? -2 : sg_log_est_sleep_min(n);
}

/* wake_ms of the i-th record, or -1 if out of range */
static int64_t wake_at(const SgState *s, int i) {
    const SgNight *n = sg_log_at(s, i);
    return n == NULL ? -1 : n->wake_ms;
}

/* Byte-identical persisted image (sg_store_encode of both states). */
static int same_image(const SgState *a, const SgState *b) {
    uint8_t ia[SG_STORE_SIZE];
    uint8_t ib[SG_STORE_SIZE];
    sg_store_encode(a, ia);
    sg_store_encode(b, ib);
    return memcmp(ia, ib, SG_STORE_SIZE) == 0;
}

static void test_manual_attr_date(void) {
    SgNight n;
    sg_tz_set(TZ_BA);
    n.bed_ms = sg_at(D_FRI, 1380);
    n.wake_ms = sg_at(D_SAT, 420);
    n.closed = 1;
    SG_CHECK(sg_log_attr_date(&n) == D_SAT);            /* wake date */
    n.wake_ms = 0;
    SG_CHECK(sg_log_attr_date(&n) == D_SAT);            /* bed + 12 h = Sat 11:00 */
    n.bed_ms = sg_at(D_THU, 1380);
    SG_CHECK(sg_log_attr_date(&n) == D_FRI);            /* bed + 12 h = Fri 11:00 */
    SG_CHECK(sg_log_attr_date(NULL) == 0);
}

static void test_manual_insert(void) {
    SgState s;
    SgWeek w;
    const SgNight *n;
    int64_t wake;
    sg_tz_set(TZ_BA);
    wake = sg_at(D_SAT, 420);
    sg_state_defaults(&s);
    sg_ref_add(&s, 430);
    sg_ref_add(&s, 430);
    sg_ref_add(&s, 430);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 360) == 1);
    SG_CHECK(s.night_count == 1);
    n = sg_log_at(&s, 0);
    SG_CHECK(n != NULL && n->wake_ms == wake);
    SG_CHECK(n != NULL && n->bed_ms == wake - SG_MIN_TO_MS(380));   /* 360 + SG_LATENCY_MIN */
    SG_CHECK(n != NULL && n->closed == 1);
    SG_CHECK(est_at(&s, 0) == 360);
    SG_CHECK(s.ref_count == 3 && sg_ref_median(&s) == 430);         /* F5 untouched */
    sg_log_week(&s, D_SAT, &w);
    SG_CHECK(w.night[6].date == D_SAT && w.night[6].status == 1);
    SG_CHECK(w.night[6].est_sleep_min == 360);
    SG_CHECK(w.debt_min == 120 && w.logged_count == 1);             /* target 480 - 360 */
}

static void test_manual_args(void) {
    SgState s;
    SgState snap;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(D_THU, 1380), sg_at(D_FRI, 420));
    snap = s;
    SG_CHECK(sg_log_put_manual(NULL, D_SAT, 360) == -1);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 59) == -1);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 841) == -1);
    SG_CHECK(sg_log_put_manual(&s, 20261332, 360) == -1);
    SG_CHECK(sg_log_put_manual(&s, 0, 360) == -1);
    SG_CHECK(s.night_count == snap.night_count);
    SG_CHECK(same_image(&s, &snap));
}

static void test_manual_bounds(void) {
    SgState s;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 60) == 1);
    SG_CHECK(est_at(&s, 0) == 60);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 840) == 2);
    SG_CHECK(s.night_count == 1 && est_at(&s, 0) == 840);
}

static void test_manual_replace(void) {
    SgState s;
    SgWeek w;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* closed tap record waking Fri 07:00 */
    (void)sg_log_bed_tap(&s, sg_at(D_THU, 1380), sg_at(D_FRI, 420));
    SG_CHECK(sg_log_close_if_due(&s, sg_at(D_FRI, 420)) == 1);
    SG_CHECK(sg_log_put_manual(&s, D_FRI, 420) == 2);
    SG_CHECK(s.night_count == 1 && est_at(&s, 0) == 420);
    SG_CHECK(s.night_count == 1 && sg_log_at(&s, 0)->closed == 1);
    /* a second closed record for Fri (wake 07:10): only the NEWEST Fri record is overwritten */
    (void)sg_log_bed_tap(&s, sg_at(D_THU, 1200), sg_at(D_FRI, 430));
    SG_CHECK(sg_log_close_if_due(&s, sg_at(D_FRI, 430)) == 1);
    SG_CHECK(s.night_count == 2);
    SG_CHECK(sg_log_put_manual(&s, D_FRI, 300) == 2);
    SG_CHECK(s.night_count == 2 && est_at(&s, 0) == 420 && est_at(&s, 1) == 300);
    /* open record without wake (attributed to Sat 11:00 via bed + 12 h): overwritten, status 1 */
    (void)sg_log_bed_tap(&s, sg_at(D_FRI, 1380), 0);
    SG_CHECK(s.night_count == 3);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 360) == 2);
    SG_CHECK(s.night_count == 3);
    sg_log_week(&s, D_SAT, &w);
    SG_CHECK(w.night[6].status == 1 && w.night[6].est_sleep_min == 360);
}

static void test_manual_order(void) {
    SgState s;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(D_WED, 1380), sg_at(D_THU, 420));
    SG_CHECK(sg_log_close_if_due(&s, sg_at(D_THU, 420)) == 1);
    (void)sg_log_bed_tap(&s, sg_at(D_FRI, 1380), sg_at(D_SAT, 420));
    SG_CHECK(sg_log_close_if_due(&s, sg_at(D_SAT, 420)) == 1);
    SG_CHECK(sg_log_put_manual(&s, D_FRI, 400) == 1);
    SG_CHECK(s.night_count == 3);
    SG_CHECK(wake_at(&s, 0) == sg_at(D_THU, 420));
    SG_CHECK(wake_at(&s, 1) == sg_at(D_FRI, 420));
    SG_CHECK(wake_at(&s, 2) == sg_at(D_SAT, 420));
    SG_CHECK(est_at(&s, 1) == 400);
}

/* An open record (tonight's bed, waking Sun) stays the newest after a manual insert. */
static void test_manual_keeps_open_newest(void) {
    SgState s;
    int64_t T;
    int64_t bed;
    int64_t T2;
    sg_tz_set(TZ_BA);
    T = sg_at(D_SUN, 420);
    bed = sg_at(D_SAT, 1380);
    T2 = T + SG_MIN_TO_MS(30);
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, bed, T);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 360) == 1);
    SG_CHECK(s.night_count == 2);
    SG_CHECK(sg_log_open_bed_for(&s, T) == bed);
    SG_CHECK(s.night_count == 2 && sg_log_at(&s, 1)->closed == 0);
    sg_log_follow_alarm(&s, T2);
    SG_CHECK(sg_log_open_bed_for(&s, T2) == bed);
    SG_CHECK(wake_at(&s, 1) == T2);
    /* the manual record is untouched by the follow */
    SG_CHECK(wake_at(&s, 0) == sg_at(D_SAT, 420) && est_at(&s, 0) == 360);
    SG_CHECK(s.night_count == 2 && sg_log_at(&s, 0)->closed == 1);
}

/* An open record waking on the same date is overwritten and closed (the user chose it). */
static void test_manual_replace_open_same_date(void) {
    SgState s;
    int64_t T;
    sg_tz_set(TZ_BA);
    T = sg_at(D_SAT, 450);           /* open record waking Sat 07:30 */
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(D_FRI, 1380), T);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 360) == 2);
    SG_CHECK(s.night_count == 1 && sg_log_at(&s, 0)->closed == 1);
    SG_CHECK(est_at(&s, 0) == 360 && wake_at(&s, 0) == sg_at(D_SAT, 420));
    SG_CHECK(sg_log_open_bed_for(&s, T) == 0);
}

/* Ring full (90): the oldest record is evicted and the new one is found. */
static void test_manual_ring_full(void) {
    SgState s;
    SgWeek w;
    int64_t base;
    int64_t bed = 0;
    int64_t T = 0;
    int i;
    sg_tz_set(TZ_BA);
    base = sg_at(20261001, 0);
    /* newest record closed */
    sg_state_defaults(&s);
    for (i = 0; i < SG_LOG_RETAIN_NIGHTS; i++) {
        bed = base + SG_MIN_TO_MS(120 * i);
        T = bed + SG_MIN_TO_MS(540);
        (void)sg_log_bed_tap(&s, bed, T);
        (void)sg_log_close_if_due(&s, T);
    }
    SG_CHECK(s.night_count == SG_LOG_RETAIN_NIGHTS);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 360) == 1);
    SG_CHECK(s.night_count == SG_LOG_RETAIN_NIGHTS);
    SG_CHECK(bed_at(&s, 0) == base + SG_MIN_TO_MS(120));          /* i = 0 evicted */
    SG_CHECK(wake_at(&s, SG_LOG_RETAIN_NIGHTS - 1) == sg_at(D_SAT, 420));
    SG_CHECK(est_at(&s, SG_LOG_RETAIN_NIGHTS - 1) == 360);
    sg_log_week(&s, D_SAT, &w);
    SG_CHECK(w.night[6].status == 1 && w.night[6].est_sleep_min == 360);

    /* newest record open (the last bed, waking at T): it stays the newest */
    sg_state_defaults(&s);
    for (i = 0; i < SG_LOG_RETAIN_NIGHTS; i++) {
        bed = base + SG_MIN_TO_MS(120 * i);
        T = bed + SG_MIN_TO_MS(540);
        (void)sg_log_bed_tap(&s, bed, T);
        if (i < SG_LOG_RETAIN_NIGHTS - 1) {
            (void)sg_log_close_if_due(&s, T);
        }
    }
    SG_CHECK(s.night_count == SG_LOG_RETAIN_NIGHTS);
    SG_CHECK(sg_log_put_manual(&s, D_SAT, 360) == 1);
    SG_CHECK(s.night_count == SG_LOG_RETAIN_NIGHTS);
    SG_CHECK(bed_at(&s, 0) == base + SG_MIN_TO_MS(120));
    SG_CHECK(sg_log_open_bed_for(&s, T) == bed);                  /* open record still newest */
    SG_CHECK(s.night_count == SG_LOG_RETAIN_NIGHTS && sg_log_at(&s, SG_LOG_RETAIN_NIGHTS - 1)->closed == 0);
    SG_CHECK(est_at(&s, SG_LOG_RETAIN_NIGHTS - 2) == 360);        /* manual sits just before it */
    SG_CHECK(wake_at(&s, SG_LOG_RETAIN_NIGHTS - 2) == sg_at(D_SAT, 420));
}

/* Madrid DST: the night of a fall-back and of a spring-forward date is still exactly N min. */
static void dst_case(int32_t date) {
    SgState s;
    SgWeek w;
    sg_state_defaults(&s);
    SG_CHECK(sg_log_put_manual(&s, date, 360) == 1);
    SG_CHECK(s.night_count == 1 && est_at(&s, 0) == 360);
    SG_CHECK(s.night_count == 1 && sg_log_attr_date(sg_log_at(&s, 0)) == date);
    SG_CHECK(s.night_count == 1 &&
             sg_log_at(&s, 0)->bed_ms == sg_log_at(&s, 0)->wake_ms - SG_MIN_TO_MS(380));
    sg_log_week(&s, date, &w);
    SG_CHECK(w.night[6].date == date && w.night[6].est_sleep_min == 360);
}

static void test_manual_dst(void) {
    sg_tz_set("Europe/Madrid");
    dst_case(20261025);      /* fall back: 03:00 CEST -> 02:00 CET */
    dst_case(20270328);      /* spring forward: 02:00 CET -> 03:00 CEST */
}

void run_sleeplog_tests(void) {
    SG_RUN(test_manual_attr_date);
    SG_RUN(test_manual_insert);
    SG_RUN(test_manual_args);
    SG_RUN(test_manual_bounds);
    SG_RUN(test_manual_replace);
    SG_RUN(test_manual_order);
    SG_RUN(test_manual_keeps_open_newest);
    SG_RUN(test_manual_replace_open_same_date);
    SG_RUN(test_manual_ring_full);
    SG_RUN(test_manual_dst);
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
    SG_RUN(test_log_open_bed_for);
    SG_RUN(test_log_bed_retap);
}
