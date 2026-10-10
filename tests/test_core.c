/* test_core.c - sg_core.h decision engine: F1, F2, F3, F4 actions, F5, settings, UI model,
 * command list helpers. All local-time expectations are built with sg_at() under an
 * explicit TZ (America/Argentina/Buenos_Aires, no DST). */
#include "sg_test.h"
#include <string.h>

#define TZ_BA "America/Argentina/Buenos_Aires"

#define D_THU 20261008   /* Thursday */
#define D_FRI 20261009   /* Friday   */
#define D_SAT 20261010   /* Saturday */
#define D_SUN 20261011   /* Sunday   */
#define D_MON 20261012   /* Monday   */
#define D_WED 20261007   /* Wednesday */

#define DEBOUNCE_MS ((int64_t)SG_DEBOUNCE_S * SG_MS_PER_S)

static SgObs obs(int64_t now, int64_t next, uint8_t creator) {
    SgObs o;
    o.now_ms = now;
    o.next_alarm_ms = next;
    o.creator = creator;
    o.exact_allowed = 1;
    o.notif_allowed = 1;
    return o;
}

static const SgCmd *find(const SgCmdList *l, int64_t type, int64_t a0) {
    int32_t i;
    for (i = 0; i < l->count; i++) {
        if (l->cmd[i].type == type && l->cmd[i].a[0] == a0) {
            return &l->cmd[i];
        }
    }
    return NULL;
}

static int32_t count_type(const SgCmdList *l, int64_t type) {
    int32_t i, n = 0;
    for (i = 0; i < l->count; i++) {
        if (l->cmd[i].type == type) {
            n++;
        }
    }
    return n;
}

static void set_flag(SgState *s, unsigned f, int on) {
    s->flags = (uint8_t)(on ? (s->flags | f) : (s->flags & ~f));
}

static void add_ref(SgState *s) {
    sg_ref_add(s, 430);
    sg_ref_add(s, 430);
    sg_ref_add(s, 430);
}

/* Remove the F5 no-alarm alarm so that only the always-on alarms remain. */
static void no_noalarm(SgState *s) {
    set_flag(s, SG_FLAG_JETLAG_NOALARM, 0);
}

static void test_core_f1_schedules_across_midnight(void) {
    SgState s;
    SgCmdList out;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_THU, 1200);
    SgObs o;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_f1_at(&s, T) == T - SG_MIN_TO_MS(570));
    SG_CHECK(sg_core_f1_at(&s, T) == sg_at(D_THU, 1290));
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_OK);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == sg_at(D_THU, 1290) && c->a[2] == SG_SCHED_EXACT_IDLE);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_RECHECK);
    SG_CHECK(c != NULL && c->a[1] == now + SG_MIN_TO_MS(SG_RECHECK_MIN));
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);
    SG_CHECK(s.last_seen_T == T);
}

static void test_core_window_and_horizon(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T, now;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    no_noalarm(&s);
    /* 23:00 alarm: outside 04:00..12:00 */
    T = sg_at(D_FRI, 1380);
    now = sg_at(D_THU, 1200);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_WINDOW);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_type(&out, SG_CMD_SCHEDULE) == 1);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_RECHECK) != NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    /* 40 h away: F1 still scheduled at T - lead, not left to RECHECK (K3) */
    T = sg_at(D_FRI, 420);
    now = T - SG_MIN_TO_MS(2400);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_HORIZON);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == sg_core_f1_at(&s, T) && c->a[2] == SG_SCHED_EXACT_IDLE);
    /* exactly 36 h: accepted; 36 h + 1 min: horizon */
    now = T - SG_MIN_TO_MS(SG_MAX_HORIZON_MIN);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_OK);
    now = T - SG_MIN_TO_MS(SG_MAX_HORIZON_MIN + 1);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_HORIZON);
}

static void test_core_none_cancels(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_THU, 1200);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    set_flag(&s, SG_FLAG_F1_POSTED, 1);     /* F1 notification believed visible */
    /* alarm removed */
    o = obs(now + SG_MIN_TO_MS(1), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_NONE);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F2) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F5_HINT) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F2) != NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(s.last_seen_T == 0);
    /* an alarm already in the past is also REL_NONE */
    o = obs(T + SG_MIN_TO_MS(1), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_NONE);
}

static void test_core_other_app_only_clock(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_THU, 1200);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_OTHER);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_OTHER_APP);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    /* only_clock off: any package accepted */
    set_flag(&s, SG_FLAG_ONLY_CLOCK, 0);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_OK);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    /* null creator (fail open) is accepted even with only_clock on */
    set_flag(&s, SG_FLAG_ONLY_CLOCK, 1);
    o = obs(now, T, SG_CREATOR_NONE);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_OK);
}

static void test_core_disabled_cancels_all(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int id;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    set_flag(&s, SG_FLAG_ENABLED, 0);
    o = obs(sg_at(D_THU, 1200), sg_at(D_FRI, 420), SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_SETTING, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_type(&out, SG_CMD_SCHEDULE) == 0);
    for (id = 1; id <= SG_ALARM_ID_MAX; id++) {
        SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, id) != NULL);
    }
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F2) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F3) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F5) != NULL);
}

static void test_core_late_debounce_f3(void) {
    SgState s;
    SgCmdList out;
    const SgCmd *c;
    SgObs o;
    int64_t T = sg_at(D_FRI, 360);     /* 06:00 */
    int64_t now = sg_at(D_THU, 1380);  /* 23:00, F1_at was 21:30 */
    int64_t due = now + DEBOUNCE_MS;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE);
    SG_CHECK(c != NULL && c->a[1] == due && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(s.debounce_due_ms == due && s.debounce_T == T);
    /* fire at the due time: 6 h 20 min available after latency, floored to 390 */
    o = obs(due, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_avail_min(T, due) == 390);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F3);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F3_LATE);
    SG_CHECK(c != NULL && c->a[2] == 360 && c->a[3] == 390);
    SG_CHECK(c != NULL && c->a[5] == SG_ACT_SLEEP && c->a[6] == SG_F3_TIMEOUT_MIN);
    SG_CHECK(s.debounce_due_ms == 0);
    SG_CHECK(s.last_notified_ms == due);
    /* late_on off: no debounce at all */
    sg_state_defaults(&s);
    set_flag(&s, SG_FLAG_LATE_ON, 0);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
}

static void test_core_f3_cooldown_and_threshold(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T1 = sg_at(D_FRI, 360);
    int64_t now1 = sg_at(D_THU, 1380);
    int64_t due1 = now1 + DEBOUNCE_MS;
    int64_t T2 = sg_at(D_FRI, 390);          /* 06:30 */
    int64_t now2 = due1 + SG_MIN_TO_MS(10);
    int64_t due2 = now2 + DEBOUNCE_MS;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now1, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(due1, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) != NULL);
    /* alarm edited 10 min later: inside the 180 min cooldown -> silent */
    o = obs(now2, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) != NULL);
    o = obs(due2, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) == NULL);
    /* avail below 240 min: no F3 */
    sg_state_defaults(&s);
    T1 = sg_at(D_FRI, 360);
    now1 = T1 - SG_MIN_TO_MS(200);          /* 02:40 */
    due1 = now1 + DEBOUNCE_MS;
    o = obs(now1, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(due1, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) == NULL);
}

static void test_core_f3_variant_ok_and_f1_future(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_THU, 1340);       /* 22:20: 8 h 40 min before alarm */
    int64_t due = now + DEBOUNCE_MS;
    sg_tz_set(TZ_BA);
    /* avail 495 >= target 480 and F1 already passed: F3 with the OK text */
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(due, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F3);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F3_OK && c->a[3] == 495);
    /* F1 still in the future and avail >= target: no F3 (plain F1 territory) */
    sg_state_defaults(&s);
    now = T - SG_MIN_TO_MS(600);            /* 21:00, F1_at 21:30 is still ahead */
    due = now + DEBOUNCE_MS;
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    s.debounce_due_ms = due;               /* pending evaluation injected directly */
    s.debounce_T = T;
    o = obs(due, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) == NULL);
}

static void test_core_debounce_rules(void) {
    SgState s;
    SgCmdList out1, out2, out3, out;
    SgObs o;
    const SgCmd *c1, *c2, *c3;
    int64_t T1 = sg_at(D_FRI, 360);
    int64_t T2 = T1 + SG_MIN_TO_MS(1);
    int64_t T3 = T1 + SG_MIN_TO_MS(2);
    int64_t now0 = sg_at(D_THU, 1380);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* three edits 10 s apart: each re-arms the debounce with a later due time */
    o = obs(now0, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out1) == SG_OK);
    o = obs(now0 + 10000, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out2) == SG_OK);
    o = obs(now0 + 20000, T3, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out3) == SG_OK);
    c1 = find(&out1, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE);
    c2 = find(&out2, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE);
    c3 = find(&out3, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE);
    SG_CHECK(c1 != NULL && c2 != NULL && c3 != NULL);
    SG_CHECK(c1 != NULL && c2 != NULL && c1->a[1] < c2->a[1]);
    SG_CHECK(c2 != NULL && c3 != NULL && c2->a[1] < c3->a[1]);
    /* firing the first (stale) due time does nothing */
    if (c1 != NULL) {
        o = obs(c1->a[1], T3, SG_CREATOR_ALLOWED);
        SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
        SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) == NULL);
    }
    /* the last due time evaluates the final T once */
    if (c3 != NULL) {
        o = obs(c3->a[1], T3, SG_CREATOR_ALLOWED);
        SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
        SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) != NULL);
        SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) != NULL &&
                 find(&out, SG_CMD_NOTIFY, SG_NK_F3)->a[2] == 362);
    }
    /* debounce_T mismatch with the observed alarm: nothing */
    sg_state_defaults(&s);
    o = obs(now0, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    s.debounce_T = T2;
    o = obs(now0 + DEBOUNCE_MS, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F3) == NULL);
}

static void test_core_f1_fires_once(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t f1 = sg_at(D_THU, 1290);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(f1, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F1_NORMAL);
    SG_CHECK(c != NULL && c->a[2] == 420 && c->a[3] == 480);
    SG_CHECK(c != NULL && c->a[4] == 1360);  /* bed 22:40, minute-of-day */
    SG_CHECK(c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE) && c->a[6] == SG_F1_TIMEOUT_MIN);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F2) != NULL);
    SG_CHECK(s.last_f1_for_T == T);
    /* duplicate fire for the same T, no snooze in between: no second post */
    o = obs(f1 + SG_MIN_TO_MS(1), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F1) == NULL);
}

static void test_core_snooze_limits(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t t = sg_at(D_THU, 1290);
    int64_t t1, t2, t3;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(t, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    /* snooze 1 */
    t1 = t + SG_MIN_TO_MS(1);
    o = obs(t1, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == t1 + SG_MIN_TO_MS(SG_SNOOZE_MIN));
    SG_CHECK(s.snooze_count == 1);
    t = t1 + SG_MIN_TO_MS(SG_SNOOZE_MIN);
    o = obs(t, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE));
    /* snooze 2 */
    t2 = t + SG_MIN_TO_MS(1);
    o = obs(t2, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    SG_CHECK(s.snooze_count == 2);
    t = t2 + SG_MIN_TO_MS(SG_SNOOZE_MIN);
    o = obs(t, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(c != NULL && c->a[5] == SG_ACT_SLEEP);      /* snooze hidden after max */
    /* snooze 3: refused, no reschedule */
    t3 = t + SG_MIN_TO_MS(1);
    o = obs(t3, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
    SG_CHECK(count_type(&out, SG_CMD_SCHEDULE) == 0);
    SG_CHECK(s.snooze_count == 2);
}

static void test_core_grace_boundaries(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t f1 = sg_at(D_THU, 1290);
    const SgCmd *c;
    sg_tz_set(TZ_BA);
    /* +10 min: immediate F1 */
    sg_state_defaults(&s);
    o = obs(f1 + SG_MIN_TO_MS(10), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == f1 + SG_MIN_TO_MS(10));
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    /* +30 min: still inside grace (inclusive) */
    sg_state_defaults(&s);
    o = obs(f1 + SG_MIN_TO_MS(SG_F1_GRACE_MIN), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    /* +31 min: beyond grace, routed to F3 debounce */
    sg_state_defaults(&s);
    o = obs(f1 + SG_MIN_TO_MS(31), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) != NULL);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c == NULL || c->a[1] != f1 + SG_MIN_TO_MS(31));
}

static void test_core_f2_schedule(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t f1 = sg_at(D_THU, 1290);
    int64_t now = sg_at(D_THU, 1200);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);      /* off by default */
    SG_CHECK(sg_core_set(&s, &o, SG_SET_WINDDOWN_ON, 1, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2);
    SG_CHECK(c != NULL && c->a[1] == f1 - SG_MIN_TO_MS(30));
    SG_CHECK(sg_core_set(&s, &o, SG_SET_WINDDOWN_MIN, 60, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2);
    SG_CHECK(c != NULL && c->a[1] == f1 - SG_MIN_TO_MS(60));
    /* now already within the 30-min pre-window: F2 not scheduled */
    sg_state_defaults(&s);
    set_flag(&s, SG_FLAG_WINDDOWN_ON, 1);
    o = obs(f1 - SG_MIN_TO_MS(20), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);
}

static void test_core_sleep_action_window(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgNight *n;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_FRI, 120);   /* 02:00 Friday, inside the window */
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(s.night_count == 1);
    n = sg_log_at(&s, 0);
    SG_CHECK(n != NULL && n->bed_ms == now && n->wake_ms == T && n->closed == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F3) != NULL);
    SG_CHECK(s.ref_count == 1);                 /* Friday wake = weekday F5 sample */
    /* window [T-690, T-60]: edges accepted, one minute outside rejected */
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(60), T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    SG_CHECK(s.night_count == 1);
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(59), T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    SG_CHECK(s.night_count == 0);
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(690), T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    SG_CHECK(s.night_count == 1);
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(691), T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    SG_CHECK(s.night_count == 0);
    /* 30 min before the alarm: ignored, but the notification is still removed */
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(30), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(s.night_count == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    /* duplicate tap within 30 min: one record, bed moved forward */
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    o = obs(now + SG_MIN_TO_MS(10), T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    SG_CHECK(s.night_count == 1);
    SG_CHECK(sg_log_at(&s, 0) != NULL && sg_log_at(&s, 0)->bed_ms == now + SG_MIN_TO_MS(10));
}

static void test_core_record_follows_and_closes(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T1 = sg_at(D_FRI, 420);
    int64_t T2 = sg_at(D_FRI, 480);
    int64_t now = sg_at(D_FRI, 120);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    /* alarm moved later: open record follows, housekeeping armed at wake + 1 min */
    o = obs(now + SG_MIN_TO_MS(10), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(sg_log_at(&s, 0) != NULL && sg_log_at(&s, 0)->wake_ms == T2);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_HOUSEKEEP);
    SG_CHECK(c != NULL && c->a[1] == T2 + SG_MIN_TO_MS(1));
    /* after wake: the first evaluation closes the record */
    o = obs(T2 + SG_MIN_TO_MS(1), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_RECHECK, &out) == SG_OK);
    SG_CHECK(sg_log_at(&s, 0) != NULL && sg_log_at(&s, 0)->closed == 1);
}

static void test_core_f5_weekday_samples(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    static const int days[3] = {20261005, 20261006, 20261007};   /* Mon, Tue, Wed */
    static const int mods[3] = {420, 430, 440};
    int i;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    for (i = 0; i < 3; i++) {
        int64_t T = sg_at(days[i], mods[i]);
        int64_t f1 = sg_core_f1_at(&s, T);
        o = obs(f1 - SG_MIN_TO_MS(60), T, SG_CREATOR_ALLOWED);
        SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
        o = obs(f1, T, SG_CREATOR_ALLOWED);
        SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
        SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F1) != NULL);
    }
    SG_CHECK(s.ref_count == 3);
    SG_CHECK(sg_ref_median(&s) == 430);
}

static void test_core_f5_saturday_hint(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_SAT, 550);            /* Saturday 09:10 */
    int64_t f1 = sg_at(D_FRI, 1420);          /* 23:40 Friday = T - 9h30 */
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    add_ref(&s);                              /* reference 07:10 */
    SG_CHECK(sg_core_f1_at(&s, T) == f1);
    o = obs(f1 - SG_MIN_TO_MS(60), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(f1, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT);
    SG_CHECK(c != NULL && c->a[1] == f1 + SG_MIN_TO_MS(1));
    o = obs(f1 + SG_MIN_TO_MS(1), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_HINT, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F5);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F5_ALARM);
    SG_CHECK(c != NULL && c->a[2] == 550 && c->a[3] == 120);
    SG_CHECK(c != NULL && c->a[4] == 495);   /* ref 430 + 60 = 490, rounded to 15 */
    /* duplicate F1 for the same T: no second hint scheduled */
    o = obs(f1 + SG_MIN_TO_MS(2), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT) == NULL);
    /* Saturday wakes do not add reference samples */
    SG_CHECK(s.ref_count == 3);
}

static void test_core_f5_monday_and_tz(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_MON, 430);           /* Monday 07:10, alarm set on Sunday night */
    int64_t f1;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    add_ref(&s);
    f1 = sg_core_f1_at(&s, T);
    o = obs(f1 - SG_MIN_TO_MS(60), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(f1, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT) == NULL);
    /* timezone change clears the weekday reference */
    o = obs(f1 - SG_MIN_TO_MS(60), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_TZ_CHANGED, &out) == SG_OK);
    SG_CHECK(s.ref_count == 0);
    SG_CHECK(sg_ref_median(&s) == -1);
}

static void test_core_f5_noalarm(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    add_ref(&s);
    /* Friday 21:30, no alarm: one hint suggesting 495 */
    o = obs(sg_at(D_FRI, 1290), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_NOALARM, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F5);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F5_NOALARM && c->a[2] == 495);
    SG_CHECK(s.last_f5_date == D_FRI);
    /* once per date */
    o = obs(sg_at(D_FRI, 1291), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_NOALARM, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F5) == NULL);
    /* Thursday: no hint */
    sg_state_defaults(&s);
    add_ref(&s);
    o = obs(sg_at(D_THU, 1290), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_NOALARM, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F5) == NULL);
    /* reference not valid yet: silent */
    sg_state_defaults(&s);
    o = obs(sg_at(D_FRI, 1290), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_NOALARM, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_NOTIFY, SG_NK_F5) == NULL);
}

static void test_core_settings_clamp(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), sg_at(D_FRI, 420), SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_LEAD_MIN, 700, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_LEAD_MIN) == 660);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_LEAD_MIN, 500, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_LEAD_MIN) == 495);   /* v - (v - min) % step */
    SG_CHECK(sg_core_set(&s, &o, SG_SET_LEAD_MIN, 100, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_LEAD_MIN) == 480);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_TARGET_MIN, 1000, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_TARGET_MIN) == 540);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_TARGET_MIN, 470, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_TARGET_MIN) == 465);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_WINDDOWN_MIN, 100, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_WINDDOWN_MIN) == 60);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_WINDDOWN_MIN, 40, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_WINDDOWN_MIN) == 30);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_LATE_ON, 0, &out) == SG_OK);
    SG_CHECK((s.flags & SG_FLAG_LATE_ON) == 0);
    SG_CHECK(sg_core_get(&s, SG_SET_LATE_ON) == 0);
    SG_CHECK(sg_core_set(&s, &o, SG_SET_ENABLED, 5, &out) == SG_OK);
    SG_CHECK(sg_core_get(&s, SG_SET_ENABLED) == 1);
    SG_CHECK(sg_core_get(&s, 99) == -1);
    SG_CHECK(sg_core_set(&s, &o, 99, 1, &out) == SG_E_ARG);
}

static void test_core_ui_fields(void) {
    SgState s;
    SgUiModel m;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_THU, 1200);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    add_ref(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_ui(&s, &o, &m) == SG_OK);
    SG_CHECK(m.alarm_rel == SG_REL_OK);
    SG_CHECK(m.next_alarm_ms == T && m.alarm_mod == 420 && m.alarm_date == D_FRI);
    SG_CHECK(m.reminder_kind == 1 && m.next_reminder_ms == sg_at(D_THU, 1290));
    SG_CHECK(m.bed_suggest_mod == 1360);            /* 07:00 - 8 h 20 min */
    SG_CHECK(m.jetlag_status == 1);
    SG_CHECK(m.weekday_ref_mod == 430);
    SG_CHECK(m.settings[SG_SET_LEAD_MIN] == 570 && m.settings[SG_SET_TARGET_MIN] == 480);
    SG_CHECK(m.week.night[6].date == D_THU);        /* today = local date of now */
    SG_CHECK(m.week.logged_count == 0);
}

static void test_core_ui_none_and_flatten(void) {
    SgState s;
    SgUiModel m;
    SgObs o;
    int64_t buf[SG_UI_LEN];
    int64_t T = sg_at(D_FRI, 420);
    sg_tz_set(TZ_BA);
    _Static_assert(SG_UI_LEN == 103, "UI layout size");
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_ui(&s, &o, &m) == SG_OK);
    SG_CHECK(m.alarm_rel == SG_REL_NONE && m.next_alarm_ms == 0);
    SG_CHECK(m.alarm_mod == -1 && m.alarm_date == 0);
    SG_CHECK(m.next_reminder_ms == 0 && m.reminder_kind == 0);
    SG_CHECK(m.bed_suggest_mod == -1 && m.weekday_ref_mod == -1);
    SG_CHECK(m.jetlag_status == 0);
    set_flag(&s, SG_FLAG_JETLAG_ON, 0);
    SG_CHECK(sg_core_ui(&s, &o, &m) == SG_OK);
    SG_CHECK(m.jetlag_status == 2);
    /* flatten with an alarm present */
    add_ref(&s);
    set_flag(&s, SG_FLAG_JETLAG_ON, 1);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_ui(&s, &o, &m) == SG_OK);
    sg_core_ui_flatten(&m, buf);
    SG_CHECK(buf[SG_UI_ALARM_REL] == SG_REL_OK);
    SG_CHECK(buf[SG_UI_NEXT_ALARM_MS] == T);
    SG_CHECK(buf[SG_UI_ALARM_MOD] == 420);
    SG_CHECK(buf[SG_UI_REMINDER_KIND] == 1);
    SG_CHECK(buf[SG_UI_WEEKDAY_REF_MOD] == 430);
    SG_CHECK(buf[SG_UI_DEBT_MIN] == m.week.debt_min);
    SG_CHECK(buf[SG_UI_SETTINGS + SG_SET_LEAD_MIN] == 570);
    SG_CHECK(buf[SG_UI_NIGHTS + 6 * SG_UI_NIGHT_WORDS] == D_THU);
    SG_CHECK(buf[SG_UI_NIGHTS + 6 * SG_UI_NIGHT_WORDS + 2] == 0);
}

static void test_core_command_budget(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T_old = sg_at(D_FRI, 420);
    int64_t T_new = sg_at(D_FRI, 360);
    int64_t now = sg_at(D_FRI, 120);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    set_flag(&s, SG_FLAG_WINDDOWN_ON, 1);
    set_flag(&s, SG_FLAG_JETLAG_NOALARM, 1);
    set_flag(&s, SG_FLAG_F1_POSTED, 1);
    s.last_seen_T = T_old;
    o = obs(now - SG_MIN_TO_MS(60), T_old, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);   /* open record */
    set_flag(&s, SG_FLAG_F1_POSTED, 1);                                 /* SLEEP cleared it */
    o = obs(now, T_new, SG_CREATOR_ALLOWED);                            /* T changed, late */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(out.count <= SG_MAX_CMDS);
    /* v2 rule L: the moved alarm is still logged (record followed), so nothing is scheduled
     * for it except the housekeeping of the open record */
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_HOUSEKEEP) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(s.last_f1_for_T == T_new);
}

static void test_core_api_args(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), sg_at(D_FRI, 420), SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, 99, &out) == SG_E_ARG);
    SG_CHECK(sg_core_alarm_fired(&s, &o, 0, &out) == SG_E_ARG);
    SG_CHECK(sg_core_sync(NULL, &o, SG_REASON_BROADCAST, &out) == SG_E_ARG);
    SG_CHECK(sg_core_sync(&s, NULL, SG_REASON_BROADCAST, &out) == SG_E_ARG);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, NULL) == SG_E_ARG);
    SG_CHECK(sg_core_set(&s, &o, 0, 1, &out) == SG_E_ARG);
}

static void test_cmd_helpers_flatten(void) {
    SgCmdList l;
    int64_t out[SG_CMD_WORDS * SG_MAX_CMDS];
    int32_t n, i;
    sg_cmd_init(&l);
    SG_CHECK(l.count == 0 && l.dropped == 0);
    for (i = 0; i < SG_MAX_CMDS; i++) {
        SG_CHECK(sg_cmd_cancel_alarm(&l, SG_ALARM_F1) == 0);
    }
    SG_CHECK(sg_cmd_cancel_alarm(&l, SG_ALARM_F1) == -1);   /* full: dropped */
    SG_CHECK(l.count == SG_MAX_CMDS && l.dropped == 1);
    n = sg_cmd_flatten(&l, out, (int32_t)(SG_CMD_WORDS * SG_MAX_CMDS));
    SG_CHECK(n == SG_MAX_CMDS * SG_CMD_WORDS);
    SG_CHECK(out[0] == SG_CMD_CANCEL_ALARM && out[1] == SG_ALARM_F1);
    SG_CHECK(out[SG_CMD_WORDS] == SG_CMD_CANCEL_ALARM);

    sg_cmd_init(&l);
    SG_CHECK(sg_cmd_notify(&l, SG_NK_F1, SG_TXT_F1_NORMAL, 420, 480, 1360,
                           SG_ACT_SLEEP | SG_ACT_SNOOZE, 180) == 0);
    SG_CHECK(sg_cmd_cancel_notify(&l, SG_NK_F2) == 0);
    n = sg_cmd_flatten(&l, out, (int32_t)(SG_CMD_WORDS * SG_MAX_CMDS));
    SG_CHECK(n == 2 * SG_CMD_WORDS);
    SG_CHECK(out[0] == SG_CMD_NOTIFY && out[1] == SG_NK_F1 && out[2] == SG_TXT_F1_NORMAL);
    SG_CHECK(out[3] == 420 && out[4] == 480 && out[5] == 1360);
    SG_CHECK(out[6] == (SG_ACT_SLEEP | SG_ACT_SNOOZE) && out[7] == 180);
    SG_CHECK(out[SG_CMD_WORDS] == SG_CMD_CANCEL_NOTIFY && out[SG_CMD_WORDS + 1] == SG_NK_F2);
}

static void test_core_alarm_changed_resets(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T1 = sg_at(D_FRI, 420);
    int64_t T2 = sg_at(D_FRI, 480);
    int64_t f1_2 = T2 - SG_MIN_TO_MS(570);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1290), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(s.last_f1_for_T == T1);
    /* alarm moved to 08:00 before the new F1_at (22:30): silent reschedule */
    o = obs(sg_at(D_THU, 1305), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == f1_2);
    SG_CHECK(s.last_f1_for_T == 0);
    SG_CHECK(s.snooze_count == 0);
}

static void test_core_avail_min(void) {
    int64_t now = sg_at(D_THU, 1200);
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_core_avail_min(now + SG_MIN_TO_MS(420), now) == 390);  /* 400 floored */
    SG_CHECK(sg_core_avail_min(now + SG_MIN_TO_MS(500), now) == 480);
    SG_CHECK(sg_core_avail_min(now + SG_MIN_TO_MS(35), now) == 15);
    SG_CHECK(sg_core_avail_min(now + SG_MIN_TO_MS(34), now) == 0);
    SG_CHECK(sg_core_avail_min(now, now + SG_MIN_TO_MS(60)) == 0);     /* never negative */
}

/* Every core test computes local times with sg_at(): set the zone before each one. */
/* Number of NOTIFY commands of one kind in the list. */
static int32_t count_notify(const SgCmdList *l, int kind) {
    int32_t i, n = 0;
    for (i = 0; i < l->count; i++) {
        if (l->cmd[i].type == SG_CMD_NOTIFY && l->cmd[i].a[0] == kind) {
            n++;
        }
    }
    return n;
}

/* K1: reboot 15 min after F1_at with T unchanged -> F1 scheduled now, then posted. */
static void test_core_reboot_within_grace_f1_now(void) {
    SgState s;
    SgCmdList out;
    const SgCmd *c;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t boot = sg_at(D_THU, 1305);     /* 21:45 = F1_at + 15 min */
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 900), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    /* reboot wipes the alarms; the persisted state still holds T */
    o = obs(boot, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == boot && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    /* the recovered F1 alarm posts the reminder */
    o = obs(boot, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
    SG_CHECK(s.last_f1_for_T == T);
}

/* K1: reboot 50 min after F1_at with T unchanged -> one debounce, exactly one F3,
 * and later RECHECKs for the same T stay silent. */
static void test_core_reboot_late_one_f3(void) {
    SgState s;
    SgCmdList out;
    const SgCmd *c;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t boot = sg_at(D_THU, 1340);     /* 22:20 = F1_at + 50 min */
    int64_t due = boot + DEBOUNCE_MS;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 900), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(boot, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE);
    SG_CHECK(c != NULL && c->a[1] == due && c->a[2] == SG_SCHED_EXACT_IDLE);
    /* debounce fires: exactly one F3 */
    o = obs(due, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_DEBOUNCE, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F3) == 1);
    SG_CHECK(s.last_f1_for_T == T);
    /* hourly RECHECKs for the same T: no new debounce, no further F3 */
    o = obs(sg_at(D_THU, 1400), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_RECHECK, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    SG_CHECK(count_notify(&out, SG_NK_F3) == 0);
    o = obs(sg_at(D_FRI, 60), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_RECHECK, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    SG_CHECK(count_notify(&out, SG_NK_F3) == 0);
}

/* K2: the F1 alarm fires for a T the user edited without a broadcast. The stale
 * alarm must re-plan (F1 at the new T - lead), not be dropped silently. */
static void test_core_stale_f1_replans(void) {
    SgState s;
    SgCmdList out;
    const SgCmd *c;
    SgObs o;
    int64_t T1 = sg_at(D_FRI, 420);
    int64_t T2 = sg_at(D_FRI, 440);        /* edited to 07:20, no broadcast */
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1260), T1, SG_CREATOR_ALLOWED);      /* 21:00 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    /* the old F1 alarm fires at 21:30 and reports T2 */
    o = obs(sg_at(D_THU, 1290), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == sg_at(D_THU, 1310) && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK(s.last_seen_T == T2);
    /* the re-planned F1 (21:50) posts the reminder */
    o = obs(sg_at(D_THU, 1310), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
}

/* F1 + two snoozes = three F1 posts for one T, and no F3 (replacement for C3-3). */
static void test_core_snooze_three_posts(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    int64_t t = sg_at(D_THU, 1290);
    int32_t posts = 0;
    int i;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    for (i = 0; i < 3; i++) {
        o = obs(t, T, SG_CREATOR_ALLOWED);
        SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
        posts += count_notify(&out, SG_NK_F1);
        if (i < 2) {
            o = obs(t + SG_MIN_TO_MS(1), T, SG_CREATOR_ALLOWED);
            SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
            t = t + SG_MIN_TO_MS(1 + SG_SNOOZE_MIN);
        }
    }
    SG_CHECK(posts == 3);
    SG_CHECK(s.snooze_count == 2);
    SG_CHECK(s.last_f1_for_T == T);
    SG_CHECK(count_notify(&out, SG_NK_F3) == 0);
}

/* ---- v2 (docs/ADVICE-v2.md): in-app bedtime, reboot re-post, UI model v2 ---- */

static void ui_at(const SgState *s, const SgObs *o, SgUiModel *m) {
    SG_CHECK(sg_core_ui(s, o, m) == SG_OK);
}

/* Thu 20:00 sync for T = Fri 07:00, then F1 fires at 21:30: F1 posted, last_notified_ms
 * = 21:30, F1_POSTED set. */
static void v2_posted_f1(SgState *s, int64_t T) {
    SgObs o;
    SgCmdList out;
    sg_state_defaults(s);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
    (void)sg_core_sync(s, &o, SG_REASON_BROADCAST, &out);
    o = obs(sg_at(D_THU, 1290), T, SG_CREATOR_ALLOWED);
    (void)sg_core_alarm_fired(s, &o, SG_ALARM_F1, &out);
}

/* Bedtime tapped in the app at 20:30 (inside the window, before F1): every F1/F2/F3 path
 * is closed and a late F1 alarm posts nothing. */
static void test_v2_sleep_marks_handled(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1140), T, SG_CREATOR_ALLOWED);           /* 19:00, before the window */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    o = obs(sg_at(D_THU, 1230), T, SG_CREATOR_ALLOWED);           /* 20:30 tap */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F2) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_DEBOUNCE) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F2) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F3) != NULL);
    SG_CHECK(s.last_f1_for_T == T);
    SG_CHECK(s.night_count == 1);
    /* RECHECK while logged: neither F1 nor the debounce is scheduled */
    o = obs(sg_at(D_THU, 1235), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_RECHECK, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    /* the F1 alarm that was still queued fires at 21:30: nothing is posted */
    o = obs(sg_at(D_THU, 1290), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
}

/* Two taps for the same alarm, 90 min apart: one night, bed moved forward, and the
 * weekday reference sample is taken only by the first tap. */
static void test_v2_retap_updates(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgNight *n;
    int64_t T = sg_at(D_FRI, 420);
    int32_t refs;
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1380), T, SG_CREATOR_ALLOWED);           /* 23:00 */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(s.night_count == 1);
    SG_CHECK(s.ref_count == 1);                                   /* Friday wake: weekday sample */
    refs = s.ref_count;
    o = obs(sg_at(D_FRI, 30), T, SG_CREATOR_ALLOWED);             /* 00:30 */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(s.night_count == 1);
    n = sg_log_at(&s, 0);
    SG_CHECK(n != NULL && n->bed_ms == sg_at(D_FRI, 30) && n->wake_ms == T && n->closed == 0);
    SG_CHECK(s.ref_count == refs);
}

/* F1 posted, snoozed once, then tapped in the app: the snooze is cancelled and the
 * alarm that was queued behind it posts nothing. */
static void test_v2_snooze_then_sleep(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t f1 = sg_at(D_THU, 1290);
    int64_t snz = sg_at(D_THU, 1291);                             /* 21:31 */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1260), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(f1, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
    o = obs(snz, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == snz + SG_MIN_TO_MS(SG_SNOOZE_MIN));
    o = obs(sg_at(D_THU, 1295), T, SG_CREATOR_ALLOWED);           /* 21:35, tap in the app */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(s.last_f1_for_T == T);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) == 0);
    /* the snoozed alarm would have fired at 21:46: nothing is posted */
    o = obs(snz + SG_MIN_TO_MS(SG_SNOOZE_MIN), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
}

/* Logged for T1, then the alarm moves to T2 = T1 + 30 min: the night follows T2, nothing
 * is scheduled for F1/F2/debounce, and the screen stays LOGGED. */
static void test_v2_alarm_moved_after_logging(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    SgUiModel m;
    int64_t T1 = sg_at(D_FRI, 420);
    int64_t T2 = sg_at(D_FRI, 450);
    int64_t now = sg_at(D_THU, 1390);                             /* 23:10 */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1380), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    o = obs(now, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    SG_CHECK(sg_log_at(&s, 0) != NULL && sg_log_at(&s, 0)->wake_ms == T2);
    SG_CHECK(s.last_f1_for_T == T2);
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_LOGGED && m.bed_mod == 1380 && m.bed_can_update == 1);
    SG_CHECK(m.hero == SG_HERO_ALARM && m.alarm_mod == 450);
}

/* Bedtime control states (§2): BEFORE, AVAILABLE, LOGGED, CLOSED and HIDDEN. */
static void test_v2_ui_bed_states(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    SgUiModel m;
    int64_t T = sg_at(D_FRI, 420);                                /* lo 19:30 Thu, hi 06:00 Fri */
    int64_t T_w = sg_at(D_FRI, 1380);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1169), T, SG_CREATOR_ALLOWED);           /* 19:29 */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_BEFORE && m.bed_mod == 1170 && m.bed_can_update == 0);
    o = obs(sg_at(D_THU, 1170), T, SG_CREATOR_ALLOWED);           /* 19:30: window opens */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_AVAILABLE && m.bed_mod == 360 && m.bed_can_update == 0);
    o = obs(sg_at(D_FRI, 360), T, SG_CREATOR_ALLOWED);            /* 06:00: last minute */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_AVAILABLE && m.bed_mod == 360);
    o = obs(sg_at(D_FRI, 361), T, SG_CREATOR_ALLOWED);            /* 06:01 */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_CLOSED && m.bed_mod == 360 && m.bed_can_update == 0);
    /* tapped at 23:00: LOGGED with the tap time, Actualizar while the window is open */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1380), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_LOGGED && m.bed_mod == 1380 && m.bed_can_update == 1);
    o = obs(sg_at(D_FRI, 361), T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_LOGGED && m.bed_mod == 1380 && m.bed_can_update == 0);
    /* HIDDEN: master toggle off, no alarm, outside 04:00..12:00, other app, > 36 h away */
    sg_state_defaults(&s);
    set_flag(&s, SG_FLAG_ENABLED, 0);
    o = obs(sg_at(D_THU, 1170), T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_HIDDEN && m.bed_mod == -1 && m.bed_can_update == 0);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1170), 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_HIDDEN && m.bed_mod == -1);
    o = obs(sg_at(D_THU, 1200), T_w, SG_CREATOR_ALLOWED);         /* REL_WINDOW */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_HIDDEN && m.bed_mod == -1);
    o = obs(T - SG_MIN_TO_MS(2400), T, SG_CREATOR_ALLOWED);       /* REL_HORIZON */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_HIDDEN && m.bed_mod == -1);
    o = obs(sg_at(D_THU, 1170), T, SG_CREATOR_OTHER);             /* REL_OTHER_APP */
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_HIDDEN && m.bed_mod == -1);
}

/* Hero card selection (§3, field 66) and the weekday of the alarm (field 67). */
static void test_v2_ui_hero(void) {
    SgState s;
    SgObs o;
    SgUiModel m;
    int64_t T = sg_at(D_FRI, 420);
    int64_t now = sg_at(D_THU, 1200);
    sg_state_defaults(&s);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_ALARM && m.alarm_wday == 5 && m.alarm_mod == 420);
    o = obs(now, T, SG_CREATOR_OTHER);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_OTHER_APP && m.alarm_mod == 420);
    o = obs(now, sg_at(D_FRI, 1380), SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_OUT_OF_WINDOW);
    o = obs(T - SG_MIN_TO_MS(2400), T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_FAR);
    o = obs(now, 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_NO_ALARM && m.alarm_wday == -1);
    o = obs(T + SG_MIN_TO_MS(1), T, SG_CREATOR_ALLOWED);          /* T already passed */
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_NO_ALARM && m.alarm_wday == -1);
    /* the master toggle wins over every alarm state */
    set_flag(&s, SG_FLAG_ENABLED, 0);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_DISABLED);
    o = obs(now, sg_at(D_FRI, 1380), SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_DISABLED);
    o = obs(now, 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    SG_CHECK(m.hero == SG_HERO_DISABLED);
}

/* Reminder row (§3, fields 68-69): UPCOMING, SENT, NONE and HIDDEN. */
static void test_v2_ui_remind(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    SgUiModel m;
    int64_t T = sg_at(D_FRI, 420);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);           /* 20:00 */
    ui_at(&s, &o, &m);
    SG_CHECK(m.remind_state == SG_REMIND_UPCOMING && m.remind_mod == 1290);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1295), T, SG_CREATOR_ALLOWED);           /* F1 fires at 21:35 */
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
    o = obs(sg_at(D_THU, 1296), T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.remind_state == SG_REMIND_SENT && m.remind_mod == 1295);
    /* alarm set after F1 + grace (04:00 for 07:00): no reminder row */
    sg_state_defaults(&s);
    o = obs(sg_at(D_FRI, 240), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) != NULL);
    ui_at(&s, &o, &m);
    SG_CHECK(m.remind_state == SG_REMIND_NONE && m.remind_mod == -1);
    /* tapped before F1: the night is handled, no reminder row */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1230), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1231), T, SG_CREATOR_ALLOWED);
    ui_at(&s, &o, &m);
    SG_CHECK(m.remind_state == SG_REMIND_NONE && m.remind_mod == -1);
    /* no alarm: hidden */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    SG_CHECK(m.remind_state == SG_REMIND_HIDDEN && m.remind_mod == -1);
}

/* Week bars (fields 77-84), label, debt state. Wed 20:00 -> Thu 07:00 is est 640 (full bar),
 * Thu 23:10 -> Fri 07:00 is est 450 (750 permille); the other five nights are dashes. */
static void test_v2_ui_week(void) {
    SgState s;
    SgObs o;
    SgUiModel m;
    int i;
    int64_t now = sg_at(D_FRI, 720);
    sg_state_defaults(&s);
    o = obs(now, 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    for (i = 0; i < SG_DEBT_WINDOW_NIGHTS; i++) {
        SG_CHECK(m.bar_permille[i] == -1);
    }
    SG_CHECK(m.label_night == -1 && m.debt_state == SG_DEBT_EMPTY);
    SG_CHECK(m.target_permille == 800);
    (void)sg_log_bed_tap(&s, sg_at(20261007, 1200), sg_at(D_THU, 420));
    (void)sg_log_bed_tap(&s, sg_at(D_THU, 1390), sg_at(D_FRI, 420));
    ui_at(&s, &o, &m);
    for (i = 0; i < 5; i++) {
        SG_CHECK(m.bar_permille[i] == -1);
    }
    SG_CHECK(m.bar_permille[5] == 1000 && m.bar_permille[6] == 750);
    SG_CHECK(m.label_night == 6);
    SG_CHECK(m.debt_state == SG_DEBT_ZERO && m.week.debt_min == 0);  /* surplus offsets deficit */
    /* two nights of est 450: debt 60 */
    sg_state_defaults(&s);
    (void)sg_log_bed_tap(&s, sg_at(20261007, 1390), sg_at(D_THU, 420));
    (void)sg_log_bed_tap(&s, sg_at(D_THU, 1390), sg_at(D_FRI, 420));
    ui_at(&s, &o, &m);
    SG_CHECK(m.bar_permille[5] == 750 && m.bar_permille[6] == 750);
    SG_CHECK(m.debt_state == SG_DEBT_SOME && m.week.debt_min == 60);
    SG_CHECK(m.label_night == 6);
}

/* Banner and permission prompt flags (fields 74-75): enabled && !allowed, !allowed && !asked. */
static void test_v2_ui_notif_flags(void) {
    SgState s;
    SgObs o;
    SgUiModel m;
    /* enabled, notif_allowed, prompted, banner, ask */
    static const int rows[6][5] = {
        {1, 0, 0, 1, 1},
        {1, 0, 1, 1, 0},
        {1, 1, 0, 0, 0},
        {1, 1, 1, 0, 0},
        {0, 0, 0, 0, 1},
        {0, 1, 1, 0, 0}
    };
    int r;
    int64_t T = sg_at(D_FRI, 420);
    for (r = 0; r < 6; r++) {
        sg_state_defaults(&s);
        set_flag(&s, SG_FLAG_ENABLED, rows[r][0]);
        set_flag(&s, SG_FLAG_NOTIF_PROMPTED, rows[r][2]);
        o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
        o.notif_allowed = (uint8_t)rows[r][1];
        ui_at(&s, &o, &m);
        SG_CHECK(m.notif_banner == rows[r][3]);
        SG_CHECK(m.ask_notif == rows[r][4]);
    }
}

/* sg_core_ui_flatten writes the v2 words (66..84) and the v1 words stay in place. */
static void test_v2_ui_flatten(void) {
    SgState s;
    SgObs o;
    SgUiModel m;
    int64_t buf[SG_UI_LEN];
    int i;
    int64_t T = sg_at(D_FRI, 420);
    _Static_assert(SG_UI_BARS + SG_DEBT_WINDOW_NIGHTS == SG_UI_MANUAL_OK, "bars end the v2 block");
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);           /* 20:00 */
    ui_at(&s, &o, &m);
    sg_core_ui_flatten(&m, buf);
    SG_CHECK(buf[SG_UI_HERO] == SG_HERO_ALARM);
    SG_CHECK(buf[SG_UI_ALARM_WDAY] == 5);
    SG_CHECK(buf[SG_UI_REMIND_STATE] == SG_REMIND_UPCOMING && buf[SG_UI_REMIND_MOD] == 1290);
    SG_CHECK(buf[SG_UI_BED_STATE] == SG_BED_AVAILABLE && buf[SG_UI_BED_MOD] == 360);
    SG_CHECK(buf[SG_UI_BED_CAN_UPDATE] == 0);
    SG_CHECK(buf[SG_UI_DEBT_STATE] == SG_DEBT_EMPTY);
    SG_CHECK(buf[SG_UI_NOTIF_BANNER] == 0 && buf[SG_UI_ASK_NOTIF] == 0);
    SG_CHECK(buf[SG_UI_TARGET_PERMILLE] == 800 && buf[SG_UI_LABEL_NIGHT] == -1);
    for (i = 0; i < SG_DEBT_WINDOW_NIGHTS; i++) {
        SG_CHECK(buf[SG_UI_BARS + i] == m.bar_permille[i]);
        SG_CHECK(buf[SG_UI_BARS + i] == -1);
    }
    SG_CHECK(buf[SG_UI_ALARM_REL] == SG_REL_OK && buf[SG_UI_DEBT_MIN] == m.week.debt_min);
}

/* Reboot re-post (§4): one silent NOTIFY with the remaining timeout; other reasons never
 * re-post. */
static void test_v2_reboot_repost(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int64_t last;
    v2_posted_f1(&s, T);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) != 0);
    SG_CHECK(s.last_notified_ms == sg_at(D_THU, 1290));
    last = s.last_notified_ms;
    o = obs(sg_at(D_THU, 1380), T, SG_CREATOR_ALLOWED);           /* 23:00, 90 min after F1 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE | SG_NOTIFY_SILENT));
    SG_CHECK(c != NULL && c->a[6] == 90);
    SG_CHECK(s.last_notified_ms == last);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) != 0);
    /* every other reason never re-posts (no loops) */
    o = obs(sg_at(D_THU, 1381), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_RECHECK, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    o = obs(sg_at(D_THU, 1382), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    o = obs(sg_at(D_THU, 1383), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    /* boot at 00:20 Friday: 170 min since the post, 10 min of timeout left */
    o = obs(sg_at(D_FRI, 20), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1 && c != NULL && c->a[6] == 10);
    /* boot at 00:30: 180 min, the notification has timed out */
    o = obs(sg_at(D_FRI, 30), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    /* a package update behaves like a boot */
    v2_posted_f1(&s, T);
    o = obs(sg_at(D_THU, 1380), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_PKG_REPLACED, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
}

/* Reboot without re-post (§4): logged nights, out of the window, grace path, snooze limit. */
static void test_v2_reboot_no_repost(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T1 = sg_at(D_FRI, 420);
    int64_t T2 = sg_at(D_FRI, 450);
    /* tapped before the reboot and the alarm moved across it: logged, no re-post */
    v2_posted_f1(&s, T1);
    o = obs(sg_at(D_THU, 1380), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1385), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    /* boot at 06:01: outside the bed window (and past the 3 h lifetime) */
    v2_posted_f1(&s, T1);
    o = obs(sg_at(D_FRI, 361), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    /* boot inside the grace period, F1 never posted: v1 path, no SILENT bit */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 900), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1305), T1, SG_CREATOR_ALLOWED);          /* 21:45 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(c != NULL && (c->a[5] & SG_NOTIFY_SILENT) == 0);
    /* snooze limit reached, notification gone, snooze alarm lost: no silent post; the
     * snooze is re-armed 15 min after the boot (ADVICE-v2 section 4, C2) */
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1200), T1, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    s.snooze_count = 2;
    set_flag(&s, SG_FLAG_F1_POSTED, 0);
    s.last_f1_for_T = T1;
    s.last_notified_ms = sg_at(D_THU, 1310);                      /* 21:50 */
    o = obs(sg_at(D_THU, 1320), T1, SG_CREATOR_ALLOWED);          /* 22:00 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == sg_at(D_THU, 1335) && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) == 0);
}

/* C2: a snooze is pending (F1 posted 21:30, SNOOZE at 21:35 -> F1 due 21:50). A reboot or
 * an update must keep it audible: no silent post, the lost alarm is re-armed 15 min after
 * `now`, and that alarm posts a normal (non-silent) F1 with the SNOOZE action. */
static void test_v2_reboot_snooze_pending(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int reason;
    int i;

    for (i = 0; i < 2; i++) {
        reason = (i == 0) ? SG_REASON_BOOT : SG_REASON_PKG_REPLACED;
        v2_posted_f1(&s, T);                                      /* F1 posted 21:30 */
        o = obs(sg_at(D_THU, 1295), T, SG_CREATOR_ALLOWED);       /* 21:35 */
        SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
        SG_CHECK(s.snooze_count == 1 && (s.flags & SG_FLAG_F1_POSTED) == 0);
        o = obs(sg_at(D_THU, 1297), T, SG_CREATOR_ALLOWED);       /* 21:37 */
        SG_CHECK(sg_core_sync(&s, &o, reason, &out) == SG_OK);
        SG_CHECK(out.dropped == 0);
        SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
        c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
        SG_CHECK(c != NULL && c->a[1] == sg_at(D_THU, 1312) && c->a[2] == SG_SCHED_EXACT_IDLE);
        /* the re-armed alarm posts an audible snoozed F1 */
        o = obs(sg_at(D_THU, 1312), T, SG_CREATOR_ALLOWED);
        SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F1, &out) == SG_OK);
        c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
        SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
        SG_CHECK(c != NULL && (c->a[5] & SG_NOTIFY_SILENT) == 0);
        SG_CHECK(c != NULL && (c->a[5] & SG_ACT_SNOOZE) != 0);
        SG_CHECK((s.flags & SG_FLAG_F1_POSTED) != 0);
    }
}

/* C3: BOOT_COMPLETED arrives before the clock app re-registers its alarm (no next alarm).
 * The unanswered F1 is still owed: the BROADCAST that follows must re-post it, silently,
 * with the remaining lifetime, and only once. */
static void test_v2_boot_unseen_then_broadcast(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);

    v2_posted_f1(&s, T);                                          /* F1 posted 21:30 */
    o = obs(sg_at(D_THU, 1380), 0, SG_CREATOR_NONE);              /* 23:00, alarm list empty */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    SG_CHECK(s.boot_unseen == 1);
    SG_CHECK(s.last_seen_T == T);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) != 0);
    /* the clock app re-registers: broadcast 5 s later, same T */
    o = obs(sg_at(D_THU, 1380) + 5000, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
    SG_CHECK(c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE | SG_NOTIFY_SILENT));
    SG_CHECK(c != NULL && c->a[6] == 90);
    SG_CHECK(s.boot_unseen == 0);
    /* settled: a later broadcast does not repeat the re-post */
    o = obs(sg_at(D_THU, 1381), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
}

/* C3: the owed-repost flag is in memory only; it is never written to the state file. */
static void test_v2_boot_unseen_not_persisted(void) {
    SgState a;
    SgState b;
    uint8_t buf[SG_STORE_SIZE];
    SgObs o;
    SgCmdList out;
    int64_t T = sg_at(D_FRI, 420);

    v2_posted_f1(&a, T);
    o = obs(sg_at(D_THU, 1380), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_sync(&a, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(a.boot_unseen == 1);
    sg_store_encode(&a, buf);
    sg_state_defaults(&b);
    SG_CHECK(sg_store_decode(buf, &b) == SG_STORE_OK);
    SG_CHECK(b.boot_unseen == 0);
    SG_CHECK(b.last_seen_T == T);
}

/* R2-C2: an in-app SLEEP before F1 closes F1 for T, but the weekend hint that F1 would have
 * armed must still fire. Ref 07:00 x3, T Sat 10:30, F1 Sat 01:00, window opens Fri 23:00. */
static void test_v2_early_sleep_keeps_f5_hint(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_SAT, 630);
    int64_t tap = sg_at(D_FRI, 1410);                     /* Friday 23:30 */
    int i;

    sg_state_defaults(&s);
    for (i = 0; i < 3; i++) {
        sg_ref_add(&s, 420);
    }
    o = obs(sg_at(D_FRI, 1200), T, SG_CREATOR_ALLOWED);   /* 20:00, before the window */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT) == NULL);
    o = obs(tap, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(s.last_f1_for_T == T);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F1) != NULL);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT);
    SG_CHECK(c != NULL && c->a[1] == tap + SG_MIN_TO_MS(1) && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK(out.count <= SG_MAX_CMDS);
    o = obs(tap + SG_MIN_TO_MS(1), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_HINT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F5) == 1);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F5);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F5_ALARM);
    SG_CHECK(c != NULL && c->a[2] == 630 && c->a[3] == 210);
    SG_CHECK(c != NULL && c->a[4] == 480);                /* ref 420 + 60, rounded to 15 */
    SG_CHECK(s.last_f5_for_T == T);
    /* a weekday wake has no drift: an early tap arms no hint (Fri 07:00, tap Thu 23:30) */
    sg_state_defaults(&s);
    for (i = 0; i < 3; i++) {
        sg_ref_add(&s, 420);
    }
    T = sg_at(D_FRI, 420);
    o = obs(sg_at(D_THU, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1410), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT) == NULL);
}

/* V2-F5-1: an early in-app tap arms the weekend hint (R2-C2). If the alarm then moves while
 * still on the weekend, the open record follows T (rule L) and F1 never fires for the new T,
 * so the T-changed branch must re-arm the hint. Ref 07:00 x3, T Sat 10:30 -> Sat 10:45. */
static void test_v2_moved_alarm_keeps_f5_hint(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_SAT, 630);
    int64_t T2 = sg_at(D_SAT, 645);
    int64_t tap = sg_at(D_FRI, 1410);                     /* Friday 23:30 */
    int64_t moved = tap + 20 * SG_MS_PER_S;               /* the BROADCAST 20 s later */
    int i;

    sg_state_defaults(&s);
    for (i = 0; i < 3; i++) {
        sg_ref_add(&s, 420);
    }
    o = obs(sg_at(D_FRI, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
    o = obs(tap, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT) != NULL);
    SG_CHECK(s.last_f5_for_T == 0);

    /* the alarm moves on the weekend: the hint must be re-armed for T2 */
    o = obs(moved, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(out.count <= SG_MAX_CMDS);
    SG_CHECK(s.last_seen_T == T2);
    SG_CHECK(s.last_f1_for_T == T2 && s.last_f2_for_T == T2);
    SG_CHECK(find(&out, SG_CMD_CANCEL_ALARM, SG_ALARM_F5_HINT) != NULL);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT);
    SG_CHECK(c != NULL && c->a[1] == moved + SG_MIN_TO_MS(1) && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);

    /* the re-armed hint fires for T2 once, with the drift from the same reference */
    o = obs(moved + SG_MIN_TO_MS(1), T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_alarm_fired(&s, &o, SG_ALARM_F5_HINT, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F5) == 1);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F5);
    SG_CHECK(c != NULL && c->a[1] == SG_TXT_F5_ALARM && c->a[2] == 645);
    SG_CHECK(s.last_f5_for_T == T2);

    /* without an early tap the move arms nothing: F1 still owns T2 */
    sg_state_defaults(&s);
    for (i = 0; i < 3; i++) {
        sg_ref_add(&s, 420);
    }
    o = obs(sg_at(D_FRI, 1200), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(moved, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F5_HINT) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) != NULL);
}

/* Mirror of the JNI glue's mutate() adopt policy (sg_jni.c is device-only, so the host cannot
 * link it). `adopt_always` 1 is the fixed glue; 0 is the old one that adopted only when the
 * encoded image changed. Returns whether the image changed (i.e. whether the file is written). */
static int mirror_mutate(SgState *g, const SgState *work, int adopt_always) {
    uint8_t ea[SG_STORE_SIZE];
    uint8_t eb[SG_STORE_SIZE];
    int changed;

    sg_store_encode(g, ea);
    sg_store_encode(work, eb);
    changed = memcmp(ea, eb, SG_STORE_SIZE) != 0;
    if (adopt_always || changed) {
        *g = *work;
    }
    return changed;
}

/* R2-C1: BOOT with an empty alarm list sets boot_unseen only, an in-memory field that the file
 * image does not show. The glue must adopt it anyway, or the BROADCAST that follows never
 * re-posts the owed F1. The old policy (adopt only on an image change) loses it, which the
 * second pass shows. */
static void test_v2_boot_unseen_adopted_by_glue(void) {
    SgState g;
    SgState work;
    SgObs o;
    SgCmdList out;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int adopt;

    for (adopt = 0; adopt <= 1; adopt++) {
        v2_posted_f1(&g, T);                                          /* F1 posted 21:30 */
        o = obs(sg_at(D_THU, 1380), 0, SG_CREATOR_NONE);              /* BOOT 23:00 */
        work = g;
        SG_CHECK(sg_core_sync(&work, &o, SG_REASON_BOOT, &out) == SG_OK);
        SG_CHECK(mirror_mutate(&g, &work, adopt) == 0);               /* image unchanged */
        SG_CHECK(g.boot_unseen == (uint8_t)adopt);
        o = obs(sg_at(D_THU, 1380) + 5000, T, SG_CREATOR_ALLOWED);    /* BROADCAST */
        work = g;
        SG_CHECK(sg_core_sync(&work, &o, SG_REASON_BROADCAST, &out) == SG_OK);
        mirror_mutate(&g, &work, adopt);
        SG_CHECK(count_notify(&out, SG_NK_F1) == adopt);
        c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
        SG_CHECK(adopt == 0 ||
                 (c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE | SG_NOTIFY_SILENT)));
    }
}

/* V2-R1: BOOT with an empty alarm list, then APP_OPEN (REASON_APP_OPEN, list still empty), then
 * the BROADCAST with T. The owed re-post must survive the APP_OPEN: no F1 is cancelled there, and
 * the BROADCAST posts exactly one silent F1 (once; a later broadcast posts nothing). */
static void test_v2_boot_unseen_app_open_null(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);

    v2_posted_f1(&s, T);                                          /* F1 posted 21:30 */
    o = obs(sg_at(D_THU, 1380), 0, SG_CREATOR_NONE);              /* BOOT 23:00 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(s.boot_unseen == 1);
    o = obs(sg_at(D_THU, 1381), 0, SG_CREATOR_NONE);              /* APP_OPEN 23:01, list empty */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) == NULL);
    SG_CHECK(s.boot_unseen == 1);
    SG_CHECK(s.last_seen_T == T);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) != 0);
    o = obs(sg_at(D_THU, 1382), T, SG_CREATOR_ALLOWED);           /* BROADCAST 23:02 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 1);
    c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
    SG_CHECK(c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE | SG_NOTIFY_SILENT));
    SG_CHECK(c != NULL && c->a[6] == SG_F1_TIMEOUT_MIN - 92);     /* 92 min since 21:30 */
    SG_CHECK(s.boot_unseen == 0);
    o = obs(sg_at(D_THU, 1383), T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
}

/* V2-R1, snooze variant: SNOOZE at 21:35 (F1 due 21:50), reboot 21:37 with an empty list, an
 * APP_OPEN at 21:38 still with an empty list, then the BROADCAST at 21:39. The lost snooze must
 * be re-armed 15 min after that broadcast (21:54), not dropped. */
static void test_v2_boot_unseen_app_open_snooze(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);

    v2_posted_f1(&s, T);                                          /* F1 posted 21:30 */
    o = obs(sg_at(D_THU, 1295), T, SG_CREATOR_ALLOWED);           /* SNOOZE 21:35 */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SNOOZE, &out) == SG_OK);
    SG_CHECK(s.snooze_count == 1 && (s.flags & SG_FLAG_F1_POSTED) == 0);
    o = obs(sg_at(D_THU, 1297), 0, SG_CREATOR_NONE);              /* BOOT 21:37 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(s.boot_unseen == 1);
    o = obs(sg_at(D_THU, 1298), 0, SG_CREATOR_NONE);              /* APP_OPEN 21:38 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(s.boot_unseen == 1 && s.snooze_count == 1);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    o = obs(sg_at(D_THU, 1299), T, SG_CREATOR_ALLOWED);           /* BROADCAST 21:39 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(count_notify(&out, SG_NK_F1) == 0);
    c = find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1);
    SG_CHECK(c != NULL && c->a[1] == sg_at(D_THU, 1314) && c->a[2] == SG_SCHED_EXACT_IDLE);
    SG_CHECK(s.boot_unseen == 0);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) == 0);
}

/* V2-R1 guard: the owed state is bounded by T. Once T has passed, a null-list sync drops it
 * like any other "no relevant alarm" sync: F1 is cancelled, last_seen_T and the flag clear. */
static void test_v2_boot_unseen_after_t_dropped(void) {
    SgState s;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);

    v2_posted_f1(&s, T);
    o = obs(sg_at(D_THU, 1380), 0, SG_CREATOR_NONE);              /* BOOT 23:00 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BOOT, &out) == SG_OK);
    SG_CHECK(s.boot_unseen == 1 && s.last_seen_T == T);
    o = obs(T + SG_MIN_TO_MS(30), 0, SG_CREATOR_NONE);            /* APP_OPEN 07:30, T passed */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
    SG_CHECK(s.boot_unseen == 0);
    SG_CHECK(s.last_seen_T == 0);
    SG_CHECK((s.flags & SG_FLAG_F1_POSTED) == 0);
}

/* V2-R1 through the glue: BOOT (empty list), APP_OPEN (empty list), BROADCAST, each step run
 * through mirror_mutate. adopt 1 is the fixed glue: one silent re-post. adopt 0 (old policy)
 * drops the in-memory flag at BOOT because the image does not change, so nothing is posted. */
static void test_v2_boot_unseen_app_open_glue(void) {
    SgState g;
    SgState work;
    SgObs o;
    SgCmdList out;
    const SgCmd *c;
    int64_t T = sg_at(D_FRI, 420);
    int adopt;

    for (adopt = 0; adopt <= 1; adopt++) {
        v2_posted_f1(&g, T);                                          /* F1 posted 21:30 */
        o = obs(sg_at(D_THU, 1380), 0, SG_CREATOR_NONE);              /* BOOT 23:00 */
        work = g;
        SG_CHECK(sg_core_sync(&work, &o, SG_REASON_BOOT, &out) == SG_OK);
        SG_CHECK(mirror_mutate(&g, &work, adopt) == 0);               /* image unchanged */
        o = obs(sg_at(D_THU, 1381), 0, SG_CREATOR_NONE);              /* APP_OPEN 23:01 */
        work = g;
        SG_CHECK(sg_core_sync(&work, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
        (void)mirror_mutate(&g, &work, adopt);
        SG_CHECK(adopt == 0 || g.boot_unseen == 1);
        SG_CHECK(adopt == 0 || (g.flags & SG_FLAG_F1_POSTED) != 0);
        o = obs(sg_at(D_THU, 1382), T, SG_CREATOR_ALLOWED);           /* BROADCAST 23:02 */
        work = g;
        SG_CHECK(sg_core_sync(&work, &o, SG_REASON_BROADCAST, &out) == SG_OK);
        (void)mirror_mutate(&g, &work, adopt);
        SG_CHECK(count_notify(&out, SG_NK_F1) == adopt);
        c = find(&out, SG_CMD_NOTIFY, SG_NK_F1);
        SG_CHECK(adopt == 0 ||
                 (c != NULL && c->a[5] == (SG_ACT_SLEEP | SG_ACT_SNOOZE | SG_NOTIFY_SILENT)));
        SG_CHECK(g.boot_unseen == 0);
    }
}

/* ---- v3 manual night (docs/ADVICE-v3.md sections 2, 3, 6): sg_core_log_night, UI v3 ---- */

/* sg_log_est_sleep_min of the i-th record, or -2 if out of range */
static int32_t est_of(const SgState *s, int i) {
    const SgNight *n = sg_log_at(s, i);
    return n == NULL ? -2 : sg_log_est_sleep_min(n);
}

/* wake_ms of the i-th record, or -1 if out of range */
static int64_t wake_of(const SgState *s, int i) {
    const SgNight *n = sg_log_at(s, i);
    return n == NULL ? -1 : n->wake_ms;
}

/* Byte-identical persisted image (sg_store_encode of both states). */
static int core_same_image(const SgState *a, const SgState *b) {
    uint8_t ia[SG_STORE_SIZE];
    uint8_t ib[SG_STORE_SIZE];
    sg_store_encode(a, ia);
    sg_store_encode(b, ib);
    return memcmp(ia, ib, SG_STORE_SIZE) == 0;
}

/* Every scheduler, flag, settings and F5 field that a manual night must leave alone. */
static int sched_same(const SgState *a, const SgState *b) {
    return a->flags == b->flags && a->snooze_count == b->snooze_count &&
           a->lead_min == b->lead_min && a->target_sleep_min == b->target_sleep_min &&
           a->winddown_min == b->winddown_min &&
           a->last_seen_T == b->last_seen_T && a->last_f1_for_T == b->last_f1_for_T &&
           a->last_f2_for_T == b->last_f2_for_T && a->last_f5_for_T == b->last_f5_for_T &&
           a->last_notified_ms == b->last_notified_ms &&
           a->debounce_due_ms == b->debounce_due_ms && a->debounce_T == b->debounce_T &&
           a->last_f5_date == b->last_f5_date && a->boot_unseen == b->boot_unseen &&
           a->ref_count == b->ref_count && a->ref_head == b->ref_head &&
           memcmp(a->ref_mod, b->ref_mod, sizeof a->ref_mod) == 0;
}

/* The owner's case (features section 1): Sat 2026-10-10 07:00, "hoy, sáb 10/10,
 * noche del vie al sáb: 6 h 00 min". Visible at once on the chart and in the debt. */
static void test_v3_user_scenario(void) {
    SgState s;
    SgUiModel m;
    SgObs o;
    SgCmdList out;
    int64_t buf[SG_UI_LEN];
    int i;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_SAT, 420), 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[6].date == D_SAT && m.week.night[6].wday == 6);
    SG_CHECK(m.manual_ok == 1);
    SG_CHECK(m.manual_default[6] == 360 && m.manual_prev_wday[6] == 5);
    SG_CHECK(m.manual_min == 60 && m.manual_max == 840 && m.manual_step == 15);
    SG_CHECK(sg_core_log_night(&s, &o, D_SAT, 360, &out) == SG_OK);
    SG_CHECK(out.count == 0 && out.dropped == 0);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[6].status == 1 && m.week.night[6].est_sleep_min == 360);
    SG_CHECK(m.bar_permille[6] == 600 && m.label_night == 6);
    SG_CHECK(m.week.debt_min == 120 && m.debt_state == SG_DEBT_SOME);
    SG_CHECK(m.week.logged_count == 1);
    sg_core_ui_flatten(&m, buf);
    SG_CHECK(buf[SG_UI_MANUAL_OK] == 1);
    SG_CHECK(buf[SG_UI_BARS + 6] == 600 && buf[SG_UI_LABEL_NIGHT] == 6);
    SG_CHECK(buf[SG_UI_DEBT_MIN] == 120 && buf[SG_UI_DEBT_STATE] == SG_DEBT_SOME);
    SG_CHECK(buf[SG_UI_LOGGED_COUNT] == 1);
    for (i = 0; i < SG_DEBT_WINDOW_NIGHTS; i++) {
        SG_CHECK(buf[SG_UI_MANUAL_DEFAULT + i] == m.manual_default[i]);
        SG_CHECK(buf[SG_UI_MANUAL_PREV_WDAY + i] == m.manual_prev_wday[i]);
    }
    SG_CHECK(buf[SG_UI_MANUAL_DEFAULT + 6] == 360 && buf[SG_UI_MANUAL_PREV_WDAY + 6] == 5);
    /* the same entry saved at 06:40 (wake still ahead): same result */
    sg_state_defaults(&s);
    o = obs(sg_at(D_SAT, 400), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_log_night(&s, &o, D_SAT, 360, &out) == SG_OK);
    SG_CHECK(out.count == 0);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[6].status == 1 && m.week.night[6].est_sleep_min == 360);
    SG_CHECK(m.week.debt_min == 120 && m.bar_permille[6] == 600);
}

/* Argument checks: refused calls leave the image unchanged and out empty; minutes are
 * clamped to [60, 840] and never rounded. */
static void test_v3_args(void) {
    SgState s;
    SgState snap;
    SgObs o;
    SgObs bad;
    SgCmdList out;
    int64_t now = sg_at(D_SAT, 420);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(now, 0, SG_CREATOR_NONE);
    bad = obs(-1, 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_log_night(NULL, &o, D_SAT, 360, &out) == SG_E_ARG);
    SG_CHECK(sg_core_log_night(&s, NULL, D_SAT, 360, &out) == SG_E_ARG);
    SG_CHECK(sg_core_log_night(&s, &o, D_SAT, 360, NULL) == SG_E_ARG);
    SG_CHECK(sg_core_log_night(&s, &bad, D_SAT, 360, &out) == SG_E_ARG);   /* now out of range */
    snap = s;
    out.count = 3;                                                        /* must be reset */
    SG_CHECK(sg_core_log_night(&s, &o, D_SUN, 360, &out) == SG_E_ARG);     /* tomorrow */
    SG_CHECK(out.count == 0);
    SG_CHECK(sg_core_log_night(&s, &o, sg_time_date_add(D_SAT, -7), 360, &out) == SG_E_ARG);
    SG_CHECK(sg_core_log_night(&s, &o, 0, 360, &out) == SG_E_ARG);
    SG_CHECK(sg_core_log_night(&s, &o, 20261332, 360, &out) == SG_E_ARG);
    SG_CHECK(core_same_image(&s, &snap));

    /* minutes: clamp to the range, then store as given (455 stays 455) */
    SG_CHECK(sg_core_log_night(&s, &o, D_SAT, 0, &out) == SG_OK);
    SG_CHECK(s.night_count == 1 && est_of(&s, 0) == 60);
    SG_CHECK(sg_core_log_night(&s, &o, D_SAT, 5000, &out) == SG_OK);
    SG_CHECK(s.night_count == 1 && est_of(&s, 0) == 840);
    SG_CHECK(sg_core_log_night(&s, &o, D_SAT, 455, &out) == SG_OK);
    SG_CHECK(s.night_count == 1 && est_of(&s, 0) == 455);
    SG_CHECK(out.count == 0 && out.dropped == 0);
}

/* A manual night never schedules, cancels, samples F5 or touches a scheduler field, also
 * with an alarm and a posted F1, and also with the master toggle off. */
static void test_v3_no_side_effects(void) {
    SgState s;
    SgState snap;
    SgCmdList out;
    SgObs o;
    int64_t T = sg_at(D_FRI, 420);
    sg_tz_set(TZ_BA);
    v2_posted_f1(&s, T);                                     /* F1 posted Thu 21:30 */
    add_ref(&s);
    snap = s;
    o = obs(sg_at(D_THU, 1295), T, SG_CREATOR_ALLOWED);      /* Thu 21:35 */
    SG_CHECK(sg_core_log_night(&s, &o, D_THU, 360, &out) == SG_OK);
    SG_CHECK(out.count == 0 && out.dropped == 0);
    SG_CHECK(s.night_count == snap.night_count + 1);
    SG_CHECK(sched_same(&s, &snap));

    set_flag(&s, SG_FLAG_ENABLED, 0);
    snap = s;
    SG_CHECK(sg_core_log_night(&s, &o, D_WED, 420, &out) == SG_OK);
    SG_CHECK(out.count == 0 && out.dropped == 0);
    SG_CHECK(s.night_count == snap.night_count + 1);
    SG_CHECK(sched_same(&s, &snap));
}

/* Tap-logged night (Thu 23:00, alarm Fri 07:00, synced Fri 08:00) is replaced in place. */
static void test_v3_replace(void) {
    SgState s;
    SgObs o;
    SgCmdList out;
    int64_t T = sg_at(D_FRI, 420);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1140), T, SG_CREATOR_ALLOWED);      /* Thu 19:00 sync */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1380), T, SG_CREATOR_ALLOWED);      /* Thu 23:00 tap */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    o = obs(sg_at(D_FRI, 480), T, SG_CREATOR_ALLOWED);       /* Fri 08:00 sync */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(s.night_count == 1 && sg_log_at(&s, 0)->closed == 1);
    SG_CHECK(sg_core_log_night(&s, &o, D_FRI, 300, &out) == SG_OK);
    SG_CHECK(s.night_count == 1 && est_of(&s, 0) == 300);
    SG_CHECK(sg_core_log_night(&s, &o, D_FRI, 315, &out) == SG_OK);
    SG_CHECK(s.night_count == 1 && est_of(&s, 0) == 315);
}

/* Open record (alarm Fri 07:00, tapped Thu 23:00) + manual nights for Thu and Wed:
 * BED_STATE LOGGED, APP_OPEN plans nothing, a BROADCAST moves only the open record. */
static void test_v3_open_record_safe(void) {
    SgState s;
    SgObs o;
    SgCmdList out;
    SgUiModel m;
    int64_t T = sg_at(D_FRI, 420);
    int64_t T2 = T + SG_MIN_TO_MS(30);
    int64_t now = sg_at(D_THU, 1380);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1140), T, SG_CREATOR_ALLOWED);      /* Thu 19:00 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(now, T, SG_CREATOR_ALLOWED);                     /* Thu 23:00 tap */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    SG_CHECK(sg_core_log_night(&s, &o, D_THU, 360, &out) == SG_OK);
    SG_CHECK(out.count == 0);
    SG_CHECK(sg_core_log_night(&s, &o, D_WED, 420, &out) == SG_OK);
    SG_CHECK(out.count == 0);
    ui_at(&s, &o, &m);
    SG_CHECK(m.bed_state == SG_BED_LOGGED);
    /* APP_OPEN: nothing is planned for T */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
    SG_CHECK(count_type(&out, SG_CMD_NOTIFY) == 0);
    /* BROADCAST: the alarm moves 30 min later; only the open record follows */
    o = obs(now + 5000, T2, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(s.night_count == 3 && wake_of(&s, 2) == T2 && sg_log_at(&s, 2)->closed == 0);
    SG_CHECK(wake_of(&s, 0) == sg_at(D_WED, 420) && est_of(&s, 0) == 420);
    SG_CHECK(wake_of(&s, 1) == sg_at(D_THU, 420) && est_of(&s, 1) == 360);
    SG_CHECK(s.night_count == 3 && sg_log_at(&s, 0)->closed == 1 && sg_log_at(&s, 1)->closed == 1);
}

/* Open record for Fri 07:30 and the user saves "hoy" (Fri) at 06:45: the open record is
 * overwritten and closed; APP_OPEN then plans nothing and BED_STATE is CLOSED. */
static void test_v3_replace_open_today(void) {
    SgState s;
    SgObs o;
    SgCmdList out;
    SgUiModel m;
    int64_t T = sg_at(D_FRI, 450);
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_THU, 1140), T, SG_CREATOR_ALLOWED);      /* Thu 19:00 */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    o = obs(sg_at(D_THU, 1380), T, SG_CREATOR_ALLOWED);      /* Thu 23:00 tap */
    SG_CHECK(sg_core_action(&s, &o, SG_ACTION_SLEEP, &out) == SG_OK);
    o = obs(sg_at(D_FRI, 405), T, SG_CREATOR_ALLOWED);       /* Fri 06:45 */
    SG_CHECK(sg_core_log_night(&s, &o, D_FRI, 360, &out) == SG_OK);
    SG_CHECK(out.count == 0);
    SG_CHECK(s.night_count == 1 && sg_log_at(&s, 0)->closed == 1 && est_of(&s, 0) == 360);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[6].status == 1 && m.week.night[6].est_sleep_min == 360);
    SG_CHECK(m.bed_state == SG_BED_CLOSED);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_APP_OPEN, &out) == SG_OK);
    SG_CHECK(count_type(&out, SG_CMD_NOTIFY) == 0);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F2) == NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) == NULL);
}

/* Picker defaults: logged nights show clamp(est, 60, 840); unlogged and status-2 nights
 * default to 360; PREV_WDAY is the weekday of the night's date minus one day. */
static void test_v3_ui_defaults(void) {
    SgState s;
    SgObs o;
    SgUiModel m;
    int i;
    int64_t now = sg_at(D_SAT, 720);                         /* Sat 12:00, no alarm */
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    /* Wed: est 900 (bed 920 min before the 07:00 wake) -> default 840 */
    (void)sg_log_bed_tap(&s, sg_at(D_WED, 420) - SG_MIN_TO_MS(920), sg_at(D_WED, 420));
    SG_CHECK(sg_log_close_if_due(&s, sg_at(D_WED, 420)) == 1);
    /* Thu: est 0 (bed 10 min before the wake) -> default 60 */
    (void)sg_log_bed_tap(&s, sg_at(D_THU, 410), sg_at(D_THU, 420));
    SG_CHECK(sg_log_close_if_due(&s, sg_at(D_THU, 420)) == 1);
    /* Fri: stored 475 -> default 475 */
    SG_CHECK(sg_log_put_manual(&s, D_FRI, 475) == 1);
    /* Sat: no wake known (bed Fri 23:00, open): status 2 -> default 360 */
    (void)sg_log_bed_tap(&s, sg_at(D_FRI, 1380), 0);
    o = obs(now, 0, SG_CREATOR_NONE);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[3].date == D_WED && m.week.night[3].est_sleep_min == 900);
    SG_CHECK(m.week.night[4].date == D_THU && m.week.night[4].est_sleep_min == 0);
    SG_CHECK(m.week.night[5].date == D_FRI && m.week.night[5].est_sleep_min == 475);
    SG_CHECK(m.week.night[6].status == 2);
    SG_CHECK(m.manual_default[3] == 840);
    SG_CHECK(m.manual_default[4] == 60);
    SG_CHECK(m.manual_default[5] == 475);
    SG_CHECK(m.manual_default[6] == 360);
    for (i = 0; i < SG_DEBT_WINDOW_NIGHTS; i++) {
        SG_CHECK(m.manual_prev_wday[i] == (m.week.night[i].wday + 6) % 7);
    }
    SG_CHECK(m.manual_default[0] == 360 && m.week.night[0].status == 0);
}

/* sg_core_log_night accepts exactly today-6..today; the window moves with the local date. */
static void test_v3_midnight(void) {
    SgState s;
    SgObs o;
    SgCmdList out;
    SgUiModel m;
    sg_tz_set(TZ_BA);
    sg_state_defaults(&s);
    o = obs(sg_at(D_SAT, 1), 0, SG_CREATOR_NONE);            /* Sat 00:01 */
    SG_CHECK(sg_core_log_night(&s, &o, D_FRI, 360, &out) == SG_OK);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[5].date == D_FRI && m.week.night[5].est_sleep_min == 360);
    SG_CHECK(sg_core_log_night(&s, &o, sg_time_date_add(D_SAT, -6), 360, &out) == SG_OK);
    SG_CHECK(sg_core_log_night(&s, &o, sg_time_date_add(D_FRI, -6), 360, &out) == SG_E_ARG);
    SG_CHECK(out.count == 0);
    SG_CHECK(sg_core_log_night(&s, &o, sg_time_date_add(D_SAT, 1), 360, &out) == SG_E_ARG);
}

/* Madrid fall-back day, saved at 08:00 local: the night is visible as today. */
static void test_v3_dst_night(void) {
    SgState s;
    SgObs o;
    SgCmdList out;
    SgUiModel m;
    sg_tz_set("Europe/Madrid");
    sg_state_defaults(&s);
    o = obs(sg_at(20261025, 480), 0, SG_CREATOR_NONE);
    SG_CHECK(sg_core_log_night(&s, &o, 20261025, 360, &out) == SG_OK);
    SG_CHECK(out.count == 0);
    ui_at(&s, &o, &m);
    SG_CHECK(m.week.night[6].date == 20261025 && m.week.night[6].est_sleep_min == 360);
    SG_CHECK(m.week.debt_min == 120);
}

#define CORE_RUN(fn)                                                         \
    do {                                                                     \
        sg_tz_set(TZ_BA);                                                    \
        SG_RUN(fn);                                                          \
    } while (0)

void run_core_tests(void) {
    CORE_RUN(test_core_f1_schedules_across_midnight);
    CORE_RUN(test_core_window_and_horizon);
    CORE_RUN(test_core_none_cancels);
    CORE_RUN(test_core_other_app_only_clock);
    CORE_RUN(test_core_disabled_cancels_all);
    CORE_RUN(test_core_late_debounce_f3);
    CORE_RUN(test_core_f3_cooldown_and_threshold);
    CORE_RUN(test_core_f3_variant_ok_and_f1_future);
    CORE_RUN(test_core_debounce_rules);
    CORE_RUN(test_core_f1_fires_once);
    CORE_RUN(test_core_snooze_limits);
    CORE_RUN(test_core_grace_boundaries);
    CORE_RUN(test_core_f2_schedule);
    CORE_RUN(test_core_sleep_action_window);
    CORE_RUN(test_core_record_follows_and_closes);
    CORE_RUN(test_core_f5_weekday_samples);
    CORE_RUN(test_core_f5_saturday_hint);
    CORE_RUN(test_core_f5_monday_and_tz);
    CORE_RUN(test_core_f5_noalarm);
    CORE_RUN(test_core_settings_clamp);
    CORE_RUN(test_core_ui_fields);
    CORE_RUN(test_core_ui_none_and_flatten);
    CORE_RUN(test_core_command_budget);
    CORE_RUN(test_core_api_args);
    CORE_RUN(test_cmd_helpers_flatten);
    CORE_RUN(test_core_alarm_changed_resets);
    CORE_RUN(test_core_avail_min);
    CORE_RUN(test_core_reboot_within_grace_f1_now);
    CORE_RUN(test_core_reboot_late_one_f3);
    CORE_RUN(test_core_stale_f1_replans);
    CORE_RUN(test_core_snooze_three_posts);
    CORE_RUN(test_v2_sleep_marks_handled);
    CORE_RUN(test_v2_retap_updates);
    CORE_RUN(test_v2_snooze_then_sleep);
    CORE_RUN(test_v2_alarm_moved_after_logging);
    CORE_RUN(test_v2_ui_bed_states);
    CORE_RUN(test_v2_ui_hero);
    CORE_RUN(test_v2_ui_remind);
    CORE_RUN(test_v2_ui_week);
    CORE_RUN(test_v2_ui_notif_flags);
    CORE_RUN(test_v2_ui_flatten);
    CORE_RUN(test_v2_reboot_repost);
    CORE_RUN(test_v2_reboot_no_repost);
    CORE_RUN(test_v2_reboot_snooze_pending);
    CORE_RUN(test_v2_boot_unseen_then_broadcast);
    CORE_RUN(test_v2_boot_unseen_not_persisted);
    CORE_RUN(test_v2_early_sleep_keeps_f5_hint);
    CORE_RUN(test_v2_moved_alarm_keeps_f5_hint);
    CORE_RUN(test_v2_boot_unseen_adopted_by_glue);
    CORE_RUN(test_v2_boot_unseen_app_open_null);
    CORE_RUN(test_v2_boot_unseen_app_open_snooze);
    CORE_RUN(test_v2_boot_unseen_after_t_dropped);
    CORE_RUN(test_v2_boot_unseen_app_open_glue);
    CORE_RUN(test_v3_user_scenario);
    CORE_RUN(test_v3_args);
    CORE_RUN(test_v3_no_side_effects);
    CORE_RUN(test_v3_replace);
    CORE_RUN(test_v3_open_record_safe);
    CORE_RUN(test_v3_replace_open_today);
    CORE_RUN(test_v3_ui_defaults);
    CORE_RUN(test_v3_midnight);
    CORE_RUN(test_v3_dst_night);
}
