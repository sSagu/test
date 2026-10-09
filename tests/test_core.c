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
    /* 40 h away: stored but not scheduled */
    T = sg_at(D_FRI, 420);
    now = T - SG_MIN_TO_MS(2400);
    o = obs(now, T, SG_CREATOR_ALLOWED);
    SG_CHECK(sg_core_relevant(&s, &o) == SG_REL_HORIZON);
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_F1) == NULL);
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
    int id, kind;
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
    kind = 0;
    (void)kind;
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
    SG_CHECK(c != NULL && c->a[1] == due && c->a[2] == SG_SCHED_EXACT);
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
    SG_CHECK(c != NULL && c->a[4] == sg_at(D_THU, 1360));  /* bed 22:40 */
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
    /* window edges: T-60 accepted, T-61 rejected, T-690 accepted, T-691 rejected */
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(60), T, SG_CREATOR_ALLOWED);
    (void)sg_core_action(&s, &o, SG_ACTION_SLEEP, &out);
    SG_CHECK(s.night_count == 1);
    sg_state_defaults(&s);
    o = obs(T - SG_MIN_TO_MS(61), T, SG_CREATOR_ALLOWED);
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
    int64_t f1 = sg_core_f1_at(&s, T);
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
    _Static_assert(SG_UI_LEN == 66, "UI layout size");
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
    o = obs(now, T_new, SG_CREATOR_ALLOWED);                            /* T changed, late */
    SG_CHECK(sg_core_sync(&s, &o, SG_REASON_BROADCAST, &out) == SG_OK);
    SG_CHECK(out.dropped == 0);
    SG_CHECK(out.count <= SG_MAX_CMDS);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_DEBOUNCE) != NULL);
    SG_CHECK(find(&out, SG_CMD_SCHEDULE, SG_ALARM_HOUSEKEEP) != NULL);
    SG_CHECK(find(&out, SG_CMD_CANCEL_NOTIFY, SG_NK_F1) != NULL);
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

void run_core_tests(void) {
    SG_RUN(test_core_f1_schedules_across_midnight);
    SG_RUN(test_core_window_and_horizon);
    SG_RUN(test_core_none_cancels);
    SG_RUN(test_core_other_app_only_clock);
    SG_RUN(test_core_disabled_cancels_all);
    SG_RUN(test_core_late_debounce_f3);
    SG_RUN(test_core_f3_cooldown_and_threshold);
    SG_RUN(test_core_f3_variant_ok_and_f1_future);
    SG_RUN(test_core_debounce_rules);
    SG_RUN(test_core_f1_fires_once);
    SG_RUN(test_core_snooze_limits);
    SG_RUN(test_core_grace_boundaries);
    SG_RUN(test_core_f2_schedule);
    SG_RUN(test_core_sleep_action_window);
    SG_RUN(test_core_record_follows_and_closes);
    SG_RUN(test_core_f5_weekday_samples);
    SG_RUN(test_core_f5_saturday_hint);
    SG_RUN(test_core_f5_monday_and_tz);
    SG_RUN(test_core_f5_noalarm);
    SG_RUN(test_core_settings_clamp);
    SG_RUN(test_core_ui_fields);
    SG_RUN(test_core_ui_none_and_flatten);
    SG_RUN(test_core_command_budget);
    SG_RUN(test_core_api_args);
    SG_RUN(test_cmd_helpers_flatten);
    SG_RUN(test_core_alarm_changed_resets);
    SG_RUN(test_core_avail_min);
}
