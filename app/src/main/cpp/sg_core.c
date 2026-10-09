/* sg_core.c - decision engine (card C3). Algorithm and contract: sg_core.h, sg_cmd.h.
 * Heap-free, no recursion, no VLAs, int64 ms arithmetic only.
 */
#include <string.h>

#include "sg_cmd.h"
#include "sg_config.h"
#include "sg_core.h"
#include "sg_sleeplog.h"
#include "sg_state.h"
#include "sg_time.h"

/* ------------------------------------------------------------------ helpers */

typedef struct {
    int     ok;
    int32_t date;
    int32_t mod;
    int     wday;
} Loc;

static int has_flag(const SgState *s, uint8_t f)
{
    return (s->flags & f) != 0;
}

static void put_flag(SgState *s, uint8_t f, int on)
{
    if (on) {
        s->flags = (uint8_t)(s->flags | f);
    } else {
        s->flags = (uint8_t)(s->flags & (uint8_t)~f);
    }
}

static int64_t min_ms(int64_t minutes)
{
    return minutes * SG_MS_PER_MIN;
}

static int in_range_ms(int64_t ms)
{
    return ms >= SG_TIME_MIN_MS && ms <= SG_TIME_MAX_MS;
}

static Loc loc_of(int64_t ms)
{
    Loc r;
    SgLocal l;

    r.ok = 0;
    r.date = 0;
    r.mod = -1;
    r.wday = -1;
    if (in_range_ms(ms) && sg_time_local(ms, &l) == 0) {
        r.ok = 1;
        r.date = l.date;
        r.mod = l.mod;
        r.wday = (int)l.wday;
    }
    return r;
}

static int is_weekday(int wd)
{
    return wd >= 1 && wd <= 5;
}

static int is_weekend(int wd)
{
    return wd == 0 || wd == 6;
}

/* F1 fire time: T - lead. */
static int64_t f1_of(const SgState *s, int64_t T)
{
    return T - min_ms((int64_t)s->lead_min);
}

/* F2 fire time: F1_at - winddown. */
static int64_t f2_of(const SgState *s, int64_t f1)
{
    return f1 - min_ms((int64_t)s->winddown_min);
}

/* Instant the bed suggestion refers to: T - (target + latency). */
static int64_t bed_of(const SgState *s, int64_t T)
{
    return T - min_ms((int64_t)s->target_sleep_min + SG_LATENCY_MIN);
}

/* Cooldown check: true when no F1/F3 was posted within SG_COOLDOWN_MIN.
 * A zero or future timestamp (never posted / clock moved back) does not block. */
static int cooldown_ok(const SgState *s, int64_t now)
{
    int64_t last = s->last_notified_ms;

    if (last <= 0 || last > now) {
        return 1;
    }
    return (now - last) >= min_ms(SG_COOLDOWN_MIN);
}

/* Weekend wake with a drift above the trigger. Sets *delta to minute-of-day(T) - ref. */
static int weekend_drift(const SgState *s, int64_t T, int32_t *delta)
{
    Loc l = loc_of(T);
    int32_t ref = sg_ref_median(s);

    *delta = 0;
    if (!l.ok || ref < 0 || !is_weekend(l.wday)) {
        return 0;
    }
    *delta = l.mod - ref;
    return *delta > SG_JETLAG_TRIGGER_MIN;
}

/* Add the F5 weekday sample for wake date of T when it is Monday..Friday. */
static void sample_if_weekday(SgState *s, int64_t T)
{
    Loc l = loc_of(T);

    if (l.ok && is_weekday(l.wday)) {
        sg_ref_add(s, (int16_t)l.mod);
    }
}

/* Next Friday/Saturday 21:30 local strictly after now, or 0. */
static int64_t next_noalarm_at(int64_t now, int32_t today_date)
{
    int d;

    for (d = 0; d <= 7; d++) {
        int32_t date = (d == 0) ? today_date : sg_time_date_add(today_date, (int32_t)d);
        int wd = sg_time_wday(date);

        if (wd == 5 || wd == 6) {
            int64_t at = sg_time_from_local(date, SG_NOALARM_HOUR_MIN);

            if (at > now) {
                return at;
            }
        }
    }
    return 0;
}

static void cancel_all_alarms(SgCmdList *out)
{
    int id;

    for (id = 1; id <= SG_ALARM_ID_MAX; id++) {
        sg_cmd_cancel_alarm(out, id);
    }
}

static void cancel_all_notifs(SgCmdList *out)
{
    sg_cmd_cancel_notify(out, SG_NK_F1);
    sg_cmd_cancel_notify(out, SG_NK_F2);
    sg_cmd_cancel_notify(out, SG_NK_F3);
    sg_cmd_cancel_notify(out, SG_NK_F5);
}

static void start_debounce(SgState *s, int64_t now, int64_t T, SgCmdList *out)
{
    int64_t due = now + (int64_t)SG_DEBOUNCE_S * SG_MS_PER_S;

    s->debounce_due_ms = due;
    s->debounce_T = T;
    sg_cmd_schedule(out, SG_ALARM_DEBOUNCE, due, SG_SCHED_EXACT_IDLE);
}

static void clear_debounce(SgState *s)
{
    s->debounce_due_ms = 0;
    s->debounce_T = 0;
}

/* Schedule F2 if it is still meaningful, otherwise cancel any stale F2 alarm. */
static void sched_f2_or_cancel(const SgState *s, int64_t f1, int64_t now, int64_t T,
                               SgCmdList *out)
{
    int64_t f2 = f2_of(s, f1);

    if (has_flag(s, SG_FLAG_WINDDOWN_ON) && now < f2 &&
        s->last_f1_for_T != T && s->last_f2_for_T != T) {
        sg_cmd_schedule(out, SG_ALARM_F2, f2, SG_SCHED_EXACT_IDLE);
    } else {
        sg_cmd_cancel_alarm(out, SG_ALARM_F2);
    }
}

/* Open record (newest) gets a housekeeping alarm right after its wake time. */
static void schedule_housekeep(const SgState *s, int64_t now, SgCmdList *out)
{
    const SgNight *n;

    if (s->night_count == 0) {
        return;
    }
    n = sg_log_at(s, (int)s->night_count - 1);
    if (n != NULL && n->closed == 0 && n->wake_ms > now &&
        n->wake_ms <= SG_TIME_MAX_MS) {
        sg_cmd_schedule(out, SG_ALARM_HOUSEKEEP, n->wake_ms + SG_MS_PER_MIN,
                        SG_SCHED_EXACT_IDLE);
    }
}

/* Common tail of every enabled sync: RECHECK, F5 no-alarm, housekeeping. */
static void sync_tail(const SgState *s, int64_t now, SgCmdList *out)
{
    sg_cmd_schedule(out, SG_ALARM_RECHECK, now + min_ms(SG_RECHECK_MIN),
                    SG_SCHED_INEXACT_IDLE);

    if (has_flag(s, SG_FLAG_JETLAG_ON) && has_flag(s, SG_FLAG_JETLAG_NOALARM)) {
        Loc today = loc_of(now);
        int64_t at = today.ok ? next_noalarm_at(now, today.date) : 0;

        if (at > 0) {
            sg_cmd_schedule(out, SG_ALARM_F5_NOALARM, at, SG_SCHED_EXACT_IDLE);
        } else {
            sg_cmd_cancel_alarm(out, SG_ALARM_F5_NOALARM);
        }
    } else {
        sg_cmd_cancel_alarm(out, SG_ALARM_F5_NOALARM);
    }

    schedule_housekeep(s, now, out);
}

static int32_t clamp_step(int32_t v, int32_t lo, int32_t hi, int32_t step)
{
    if (v < lo) {
        v = lo;
    }
    if (v > hi) {
        v = hi;
    }
    return v - (v - lo) % step;
}

/* ------------------------------------------------------- relevance, avail */

int sg_core_relevant(const SgState *s, const SgObs *o)
{
    int64_t T;
    Loc l;

    if (s == NULL || o == NULL) {
        return SG_REL_NONE;
    }
    if (!in_range_ms(o->now_ms)) {
        return SG_REL_NONE;
    }
    T = o->next_alarm_ms;
    if (T <= 0 || T <= o->now_ms) {
        return SG_REL_NONE;
    }
    if (o->creator == SG_CREATOR_OTHER && has_flag(s, SG_FLAG_ONLY_CLOCK)) {
        return SG_REL_OTHER_APP;
    }
    l = loc_of(T);
    if (!l.ok || l.mod < SG_WAKE_WIN_START_MIN || l.mod > SG_WAKE_WIN_END_MIN) {
        return SG_REL_WINDOW;
    }
    if (T - o->now_ms > min_ms(SG_MAX_HORIZON_MIN)) {
        return SG_REL_HORIZON;
    }
    return SG_REL_OK;
}

int64_t sg_core_f1_at(const SgState *s, int64_t T_ms)
{
    return f1_of(s, T_ms);
}

int32_t sg_core_avail_min(int64_t T_ms, int64_t now_ms)
{
    int64_t raw;

    if (!in_range_ms(T_ms) || !in_range_ms(now_ms) || T_ms <= now_ms) {
        return 0;
    }
    raw = (T_ms - now_ms) / SG_MS_PER_MIN - SG_LATENCY_MIN;
    if (raw <= 0) {
        return 0;
    }
    if (raw > INT32_MAX) {
        raw = INT32_MAX;
    }
    return sg_time_floor_min((int32_t)raw, SG_ROUND_STEP_MIN);
}

/* ------------------------------------------------------------------ sync */

int sg_core_sync(SgState *s, const SgObs *o, int reason, SgCmdList *out)
{
    int rel;
    int64_t T;
    int64_t now;
    int64_t f1;

    if (s == NULL || o == NULL || out == NULL) {
        return SG_E_ARG;
    }
    sg_cmd_init(out);
    if (!in_range_ms(o->now_ms)) {
        return SG_E_ARG;
    }
    now = o->now_ms;

    (void)sg_state_sanitize(s);
    (void)sg_log_close_if_due(s, now);
    if (reason == SG_REASON_TZ_CHANGED) {
        sg_ref_clear(s);
    }

    if (!has_flag(s, SG_FLAG_ENABLED)) {
        cancel_all_alarms(out);
        cancel_all_notifs(out);
        put_flag(s, SG_FLAG_F1_POSTED, 0);
        s->last_seen_T = 0;
        clear_debounce(s);
        return SG_OK;
    }

    rel = sg_core_relevant(s, o);
    T = o->next_alarm_ms;

    if (rel == SG_REL_OK) {
        f1 = f1_of(s, T);

        if (T != s->last_seen_T) {
            /* T changed: drop stale F1/F2/F5 state and re-plan. */
            if (has_flag(s, SG_FLAG_F1_POSTED)) {
                sg_cmd_cancel_notify(out, SG_NK_F1);
            }
            sg_cmd_cancel_notify(out, SG_NK_F2);
            sg_cmd_cancel_notify(out, SG_NK_F3);
            sg_cmd_cancel_alarm(out, SG_ALARM_F1);
            sg_cmd_cancel_alarm(out, SG_ALARM_F2);
            sg_cmd_cancel_alarm(out, SG_ALARM_F5_HINT);
            put_flag(s, SG_FLAG_F1_POSTED, 0);
            s->snooze_count = 0;
            s->last_seen_T = T;
            clear_debounce(s);
            sg_log_follow_alarm(s, T);

            if (now < f1) {
                s->last_f1_for_T = 0;
                s->last_f2_for_T = 0;
                sg_cmd_schedule(out, SG_ALARM_F1, f1, SG_SCHED_EXACT_IDLE);
                sched_f2_or_cancel(s, f1, now, T, out);
            } else if (s->last_f1_for_T == T) {
                /* F1 already handled for this T: nothing to do. */
            } else if (now <= f1 + min_ms(SG_F1_GRACE_MIN)) {
                sg_cmd_schedule(out, SG_ALARM_F1, now, SG_SCHED_EXACT_IDLE);
            } else if (has_flag(s, SG_FLAG_LATE_ON)) {
                start_debounce(s, now, T, out);
            }
        } else {
            /* T unchanged: idempotent re-plan of F1/F2. Also recovers an F1 alarm that
             * was lost (reboot, force-stop, update) or moved into the past (lead change):
             * within grace -> F1 now; later -> one F3 evaluation (fire_debounce then marks
             * T handled, so RECHECK cannot repeat it). */
            if (s->last_f1_for_T != T) {
                if (now < f1) {
                    sg_cmd_schedule(out, SG_ALARM_F1, f1, SG_SCHED_EXACT_IDLE);
                } else if (now <= f1 + min_ms(SG_F1_GRACE_MIN)) {
                    sg_cmd_schedule(out, SG_ALARM_F1, now, SG_SCHED_EXACT_IDLE);
                } else if (has_flag(s, SG_FLAG_LATE_ON) &&
                           (s->debounce_due_ms == 0 || s->debounce_T == T)) {
                    start_debounce(s, now, T, out);
                }
            }
            sched_f2_or_cancel(s, f1, now, T, out);
        }
    } else {
        /* No relevant alarm: cancel F1/F2/F5 hint and their notifications. */
        if (s->last_seen_T != 0) {
            sg_log_follow_alarm(s, 0);
        }
        s->last_seen_T = 0;
        if (has_flag(s, SG_FLAG_F1_POSTED)) {
            sg_cmd_cancel_notify(out, SG_NK_F1);
        }
        sg_cmd_cancel_notify(out, SG_NK_F2);
        sg_cmd_cancel_notify(out, SG_NK_F3);
        sg_cmd_cancel_notify(out, SG_NK_F5);
        put_flag(s, SG_FLAG_F1_POSTED, 0);
        if (rel == SG_REL_HORIZON) {
            /* Beyond 36 h: keep an exact wake-up at its F1 time so a starved RECHECK
             * cannot lose the reminder; alarm_fired(F1) then re-plans through sync. */
            sg_cmd_schedule(out, SG_ALARM_F1, f1_of(s, T), SG_SCHED_EXACT_IDLE);
        } else {
            sg_cmd_cancel_alarm(out, SG_ALARM_F1);
        }
        sg_cmd_cancel_alarm(out, SG_ALARM_F2);
        sg_cmd_cancel_alarm(out, SG_ALARM_F5_HINT);
        clear_debounce(s);
    }

    sync_tail(s, now, out);
    return SG_OK;
}

/* ---------------------------------------------------------- alarm fired */

static void fire_f1(SgState *s, const SgObs *o, int rel, int64_t T, SgCmdList *out)
{
    int64_t now = o->now_ms;
    int32_t target = (int32_t)s->target_sleep_min;
    Loc alarm = loc_of(T);
    int actions;
    int64_t bed_ms;
    Loc bed;
    int first;

    if (rel != SG_REL_OK || T != s->last_seen_T) {
        return;
    }
    /* Once per T; only a snoozed F1 (snooze_count > 0, notification withdrawn) re-posts. */
    if (s->last_f1_for_T == T &&
        (s->snooze_count == 0 || has_flag(s, SG_FLAG_F1_POSTED))) {
        return;
    }
    first = (s->last_f1_for_T != T);

    actions = SG_ACT_SLEEP;
    if (s->snooze_count < SG_SNOOZE_MAX) {
        actions |= SG_ACT_SNOOZE;
    }

    bed_ms = bed_of(s, T);
    bed = loc_of(bed_ms);
    if (bed_ms > now && bed.ok) {
        sg_cmd_notify(out, SG_NK_F1, SG_TXT_F1_NORMAL, alarm.mod, target, bed.mod,
                      actions, SG_F1_TIMEOUT_MIN);
    } else {
        sg_cmd_notify(out, SG_NK_F1, SG_TXT_F1_LATE, alarm.mod,
                      sg_core_avail_min(T, now), 0, actions, SG_F1_TIMEOUT_MIN);
    }

    s->last_f1_for_T = T;
    s->last_notified_ms = now;
    put_flag(s, SG_FLAG_F1_POSTED, 1);
    sg_cmd_cancel_notify(out, SG_NK_F2);

    if (first) {
        sample_if_weekday(s, T);
    }

    {
        int32_t delta = 0;

        if (has_flag(s, SG_FLAG_JETLAG_ON) && s->last_f5_for_T != T &&
            weekend_drift(s, T, &delta)) {
            sg_cmd_schedule(out, SG_ALARM_F5_HINT, now + SG_F5_AFTER_F1_MIN * SG_MS_PER_MIN,
                            SG_SCHED_EXACT_IDLE);
        }
    }
}

static void fire_f2(SgState *s, const SgObs *o, int rel, int64_t T, SgCmdList *out)
{
    int64_t now = o->now_ms;
    int64_t f1;
    Loc alarm;

    if (rel != SG_REL_OK || T != s->last_seen_T || !has_flag(s, SG_FLAG_WINDDOWN_ON) ||
        s->last_f2_for_T == T || s->last_f1_for_T == T) {
        return;
    }
    f1 = f1_of(s, T);
    if (now >= f1) {
        return;
    }
    alarm = loc_of(T);
    sg_cmd_notify(out, SG_NK_F2, SG_TXT_F2, alarm.mod, 0, 0, 0, SG_F2_TIMEOUT_MIN);
    s->last_f2_for_T = T;
}

static void fire_debounce(SgState *s, const SgObs *o, int rel, int64_t T, SgCmdList *out)
{
    int64_t now = o->now_ms;
    int32_t avail;
    int32_t target;
    int64_t f1;
    int notify;
    Loc alarm;

    if (s->debounce_due_ms == 0 || now < s->debounce_due_ms) {
        return;
    }
    if (rel != SG_REL_OK || T != s->debounce_T) {
        clear_debounce(s);
        return;
    }

    avail = sg_core_avail_min(T, now);
    target = (int32_t)s->target_sleep_min;
    f1 = f1_of(s, T);
    notify = avail >= SG_LATE_MIN_SLEEP_MIN &&
             cooldown_ok(s, now) &&
             !(avail >= target && f1 > now) &&
             s->last_f1_for_T != T;

    if (notify) {
        alarm = loc_of(T);
        sg_cmd_notify(out, SG_NK_F3, avail >= target ? SG_TXT_F3_OK : SG_TXT_F3_LATE,
                      alarm.mod, avail, 0, SG_ACT_SLEEP, SG_F3_TIMEOUT_MIN);
        s->last_notified_ms = now;
    }
    if (now > f1 + min_ms(SG_F1_GRACE_MIN)) {
        s->last_f1_for_T = T;   /* F1 time is over for this T: evaluate F3 once only */
    }
    clear_debounce(s);
}

static void fire_f5_hint(SgState *s, int rel, int64_t T, SgCmdList *out)
{
    Loc alarm;
    int32_t ref;
    int32_t delta = 0;
    int32_t suggest;

    if (rel != SG_REL_OK || T != s->last_seen_T || !has_flag(s, SG_FLAG_JETLAG_ON) ||
        s->last_f5_for_T == T) {
        return;
    }
    if (!weekend_drift(s, T, &delta)) {
        return;
    }
    ref = sg_ref_median(s);
    alarm = loc_of(T);
    suggest = sg_time_round_mod(ref + SG_JETLAG_SUGGEST_MIN, SG_ROUND_STEP_MIN);
    sg_cmd_notify(out, SG_NK_F5, SG_TXT_F5_ALARM, alarm.mod, delta, suggest, 0,
                  SG_F5_TIMEOUT_MIN);
    s->last_f5_for_T = T;
}

static void fire_f5_noalarm(SgState *s, const SgObs *o, int rel, int64_t T, SgCmdList *out)
{
    int64_t now = o->now_ms;
    Loc today = loc_of(now);
    int64_t tomorrow_noon;
    int32_t ref;
    int32_t suggest;

    if (!has_flag(s, SG_FLAG_JETLAG_ON) || !has_flag(s, SG_FLAG_JETLAG_NOALARM)) {
        return;
    }
    if (!today.ok || !(today.wday == 5 || today.wday == 6)) {
        return;
    }
    tomorrow_noon = sg_time_from_local(sg_time_date_add(today.date, 1), SG_WAKE_WIN_END_MIN);
    if (tomorrow_noon <= 0) {
        return;
    }
    if (rel == SG_REL_OK && T < tomorrow_noon) {
        return;
    }
    if (s->last_f5_date == today.date) {
        return;
    }
    ref = sg_ref_median(s);
    if (ref < 0) {
        return;
    }
    suggest = sg_time_round_mod(ref + SG_JETLAG_SUGGEST_MIN, SG_ROUND_STEP_MIN);
    sg_cmd_notify(out, SG_NK_F5, SG_TXT_F5_NOALARM, suggest, 0, 0, 0, SG_F5_TIMEOUT_MIN);
    s->last_f5_date = today.date;
}

int sg_core_alarm_fired(SgState *s, const SgObs *o, int alarm_id, SgCmdList *out)
{
    int rel;
    int64_t T;

    if (s == NULL || o == NULL || out == NULL) {
        return SG_E_ARG;
    }
    sg_cmd_init(out);
    if (!in_range_ms(o->now_ms)) {
        return SG_E_ARG;
    }
    if (alarm_id < 1 || alarm_id > SG_ALARM_ID_MAX) {
        return SG_E_ARG;
    }
    (void)sg_state_sanitize(s);

    if (alarm_id == SG_ALARM_RECHECK || alarm_id == SG_ALARM_HOUSEKEEP) {
        return sg_core_sync(s, o, SG_REASON_RECHECK, out);
    }
    if (!has_flag(s, SG_FLAG_ENABLED)) {
        return SG_OK;
    }

    rel = sg_core_relevant(s, o);
    T = o->next_alarm_ms;

    /* The alarm clock changed and no broadcast reached us: re-plan now instead of
     * silently dropping the reminder. */
    if ((alarm_id == SG_ALARM_F1 || alarm_id == SG_ALARM_F2 || alarm_id == SG_ALARM_F5_HINT) &&
        (rel == SG_REL_OK ? T != s->last_seen_T : s->last_seen_T != 0)) {
        return sg_core_sync(s, o, SG_REASON_RECHECK, out);
    }

    switch (alarm_id) {
    case SG_ALARM_F1:
        fire_f1(s, o, rel, T, out);
        break;
    case SG_ALARM_F2:
        fire_f2(s, o, rel, T, out);
        break;
    case SG_ALARM_DEBOUNCE:
        fire_debounce(s, o, rel, T, out);
        break;
    case SG_ALARM_F5_HINT:
        fire_f5_hint(s, rel, T, out);
        break;
    case SG_ALARM_F5_NOALARM:
        fire_f5_noalarm(s, o, rel, T, out);
        break;
    default:
        break;
    }
    return SG_OK;
}

/* -------------------------------------------------------------- actions */

int sg_core_action(SgState *s, const SgObs *o, int action_id, SgCmdList *out)
{
    int rel;
    int64_t T;
    int64_t now;

    if (s == NULL || o == NULL || out == NULL) {
        return SG_E_ARG;
    }
    sg_cmd_init(out);
    if (!in_range_ms(o->now_ms)) {
        return SG_E_ARG;
    }
    if (action_id != SG_ACTION_SLEEP && action_id != SG_ACTION_SNOOZE) {
        return SG_E_ARG;
    }
    (void)sg_state_sanitize(s);
    if (!has_flag(s, SG_FLAG_ENABLED)) {
        return SG_OK;
    }

    now = o->now_ms;
    rel = sg_core_relevant(s, o);
    T = o->next_alarm_ms;

    if (action_id == SG_ACTION_SLEEP) {
        if (rel == SG_REL_OK) {
            int64_t lo = f1_of(s, T) - min_ms(SG_BED_WINDOW_BEFORE_MIN);
            int64_t hi = T - min_ms(SG_BED_WINDOW_AFTER_MIN);

            if (now >= lo && now <= hi) {
                int r = sg_log_bed_tap(s, now, T);

                if (r == 1) {
                    sample_if_weekday(s, T);
                }
                if (r != 0) {
                    schedule_housekeep(s, now, out);
                }
            }
        }
        sg_cmd_cancel_notify(out, SG_NK_F1);
        sg_cmd_cancel_notify(out, SG_NK_F3);
        put_flag(s, SG_FLAG_F1_POSTED, 0);
    } else {
        if (s->snooze_count < SG_SNOOZE_MAX && has_flag(s, SG_FLAG_F1_POSTED)) {
            s->snooze_count = (uint8_t)(s->snooze_count + 1u);
            sg_cmd_cancel_notify(out, SG_NK_F1);
            put_flag(s, SG_FLAG_F1_POSTED, 0);
            /* last_f1_for_T stays T: fire_f1 lets a snoozed F1 re-post. */
            sg_cmd_schedule(out, SG_ALARM_F1, now + min_ms(SG_SNOOZE_MIN),
                            SG_SCHED_EXACT_IDLE);
        }
    }
    return SG_OK;
}

/* ------------------------------------------------------ settings get/set */

int sg_core_set(SgState *s, const SgObs *o, int key, int32_t value, SgCmdList *out)
{
    if (s == NULL || o == NULL || out == NULL) {
        return SG_E_ARG;
    }
    sg_cmd_init(out);

    switch (key) {
    case SG_SET_ENABLED:
        put_flag(s, SG_FLAG_ENABLED, value != 0);
        break;
    case SG_SET_LEAD_MIN:
        s->lead_min = (uint16_t)clamp_step(value, SG_LEAD_MIN_MIN, SG_LEAD_MAX_MIN,
                                           SG_LEAD_STEP_MIN);
        break;
    case SG_SET_TARGET_MIN:
        s->target_sleep_min = (uint16_t)clamp_step(value, SG_TARGET_SLEEP_MIN_MIN,
                                                   SG_TARGET_SLEEP_MAX_MIN,
                                                   SG_TARGET_SLEEP_STEP_MIN);
        break;
    case SG_SET_WINDDOWN_ON:
        put_flag(s, SG_FLAG_WINDDOWN_ON, value != 0);
        break;
    case SG_SET_WINDDOWN_MIN:
        s->winddown_min = (uint16_t)clamp_step(value, SG_WINDDOWN_MIN_MIN,
                                               SG_WINDDOWN_MAX_MIN, SG_WINDDOWN_STEP_MIN);
        break;
    case SG_SET_LATE_ON:
        put_flag(s, SG_FLAG_LATE_ON, value != 0);
        break;
    case SG_SET_JETLAG_ON:
        put_flag(s, SG_FLAG_JETLAG_ON, value != 0);
        break;
    case SG_SET_JETLAG_NOALARM:
        put_flag(s, SG_FLAG_JETLAG_NOALARM, value != 0);
        break;
    case SG_SET_ONLY_CLOCK:
        put_flag(s, SG_FLAG_ONLY_CLOCK, value != 0);
        break;
    case SG_SET_NOTIF_PROMPTED:
        put_flag(s, SG_FLAG_NOTIF_PROMPTED, value != 0);
        break;
    case SG_SET_CLEAR_LOG:
        sg_log_clear(s);
        break;
    default:
        return SG_E_ARG;
    }

    return sg_core_sync(s, o, SG_REASON_SETTING, out);
}

int32_t sg_core_get(const SgState *s, int key)
{
    if (s == NULL) {
        return -1;
    }
    switch (key) {
    case SG_SET_ENABLED:        return has_flag(s, SG_FLAG_ENABLED) ? 1 : 0;
    case SG_SET_LEAD_MIN:       return (int32_t)s->lead_min;
    case SG_SET_TARGET_MIN:     return (int32_t)s->target_sleep_min;
    case SG_SET_WINDDOWN_ON:    return has_flag(s, SG_FLAG_WINDDOWN_ON) ? 1 : 0;
    case SG_SET_WINDDOWN_MIN:   return (int32_t)s->winddown_min;
    case SG_SET_LATE_ON:        return has_flag(s, SG_FLAG_LATE_ON) ? 1 : 0;
    case SG_SET_JETLAG_ON:      return has_flag(s, SG_FLAG_JETLAG_ON) ? 1 : 0;
    case SG_SET_JETLAG_NOALARM: return has_flag(s, SG_FLAG_JETLAG_NOALARM) ? 1 : 0;
    case SG_SET_ONLY_CLOCK:     return has_flag(s, SG_FLAG_ONLY_CLOCK) ? 1 : 0;
    case SG_SET_NOTIF_PROMPTED: return has_flag(s, SG_FLAG_NOTIF_PROMPTED) ? 1 : 0;
    case SG_SET_CLEAR_LOG:      return 0;
    default:                    return -1;
    }
}

/* ----------------------------------------------------------- UI model */

int sg_core_ui(const SgState *s, const SgObs *o, SgUiModel *out)
{
    int rel;
    int64_t T;
    int64_t now;
    int enabled;
    Loc today;
    Loc tl;
    int32_t ref;
    int32_t delta = 0;
    int wd = -1;
    int key;

    if (s == NULL || o == NULL || out == NULL) {
        return SG_E_ARG;
    }
    if (!in_range_ms(o->now_ms)) {
        return SG_E_ARG;
    }
    memset(out, 0, sizeof *out);
    out->alarm_mod = -1;
    out->bed_suggest_mod = -1;
    out->weekday_ref_mod = -1;

    now = o->now_ms;
    T = o->next_alarm_ms;
    rel = sg_core_relevant(s, o);
    enabled = has_flag(s, SG_FLAG_ENABLED);
    today = loc_of(now);
    ref = sg_ref_median(s);

    out->alarm_rel = rel;
    out->weekday_ref_mod = ref;
    if (rel != SG_REL_NONE) {
        Loc al = loc_of(T);

        out->next_alarm_ms = T;
        if (al.ok) {
            out->alarm_mod = al.mod;
            out->alarm_date = al.date;
        }
    }

    if (enabled && rel == SG_REL_OK) {
        int64_t f1 = f1_of(s, T);
        int64_t f2 = f2_of(s, f1);

        if (has_flag(s, SG_FLAG_WINDDOWN_ON) && now < f2 &&
            s->last_f2_for_T != T && s->last_f1_for_T != T) {
            out->next_reminder_ms = f2;
            out->reminder_kind = 2;
        } else if (s->last_f1_for_T != T && now < f1) {
            out->next_reminder_ms = f1;
            out->reminder_kind = 1;
        }
        out->bed_suggest_mod = loc_of(bed_of(s, T)).mod;
    }

    if (rel == SG_REL_OK) {
        tl = loc_of(T);
        if (tl.ok && ref >= 0) {
            delta = tl.mod - ref;
            wd = tl.wday;
        }
    }
    if (!has_flag(s, SG_FLAG_JETLAG_ON)) {
        out->jetlag_status = 2;
    } else if (ref < 0) {
        out->jetlag_status = 0;
    } else if (wd >= 0 && is_weekend(wd) && delta > SG_JETLAG_TRIGGER_MIN) {
        out->jetlag_status = 3;
        out->jetlag_delta_min = delta;
    } else {
        out->jetlag_status = 1;
    }

    (void)sg_log_week(s, today.date, &out->week);

    for (key = 1; key <= 11; key++) {
        out->settings[key] = sg_core_get(s, key);
    }
    out->settings[0] = 0;
    return SG_OK;
}

void sg_core_ui_flatten(const SgUiModel *m, int64_t out[SG_UI_LEN])
{
    int i;
    int k;

    if (m == NULL || out == NULL) {
        return;
    }
    out[SG_UI_ALARM_REL] = m->alarm_rel;
    out[SG_UI_NEXT_ALARM_MS] = m->next_alarm_ms;
    out[SG_UI_ALARM_MOD] = m->alarm_mod;
    out[SG_UI_ALARM_DATE] = m->alarm_date;
    out[SG_UI_NEXT_REMINDER_MS] = m->next_reminder_ms;
    out[SG_UI_REMINDER_KIND] = m->reminder_kind;
    out[SG_UI_BED_SUGGEST_MOD] = m->bed_suggest_mod;
    out[SG_UI_JETLAG_STATUS] = m->jetlag_status;
    out[SG_UI_JETLAG_DELTA] = m->jetlag_delta_min;
    out[SG_UI_WEEKDAY_REF_MOD] = m->weekday_ref_mod;
    out[SG_UI_DEBT_MIN] = m->week.debt_min;
    out[SG_UI_LOGGED_COUNT] = m->week.logged_count;

    for (k = 0; k < 12; k++) {
        out[SG_UI_SETTINGS + k] = m->settings[k];
    }

    for (i = 0; i < SG_DEBT_WINDOW_NIGHTS; i++) {
        const SgNightView *nv = &m->week.night[i];
        int base = SG_UI_NIGHTS + i * SG_UI_NIGHT_WORDS;

        out[base + 0] = nv->date;
        out[base + 1] = nv->wday;
        out[base + 2] = nv->status;
        out[base + 3] = nv->bed_mod;
        out[base + 4] = nv->wake_mod;
        out[base + 5] = nv->est_sleep_min;
    }
}

/* ------------------------------------------------------ command helpers */

void sg_cmd_init(SgCmdList *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof *l);
    }
}

/* Reserve the next slot (zeroed) or record a drop. */
static SgCmd *cmd_next(SgCmdList *l)
{
    SgCmd *c;

    if (l->count < 0 || l->count >= SG_MAX_CMDS) {
        if (l->dropped < INT32_MAX) {
            l->dropped++;
        }
        return NULL;
    }
    c = &l->cmd[l->count];
    memset(c, 0, sizeof *c);
    l->count++;
    return c;
}

int sg_cmd_schedule(SgCmdList *l, int alarm_id, int64_t at_ms, int mode)
{
    SgCmd *c;

    if (l == NULL || alarm_id < 1 || alarm_id > SG_ALARM_ID_MAX) {
        return -1;
    }
    c = cmd_next(l);
    if (c == NULL) {
        return -1;
    }
    c->type = SG_CMD_SCHEDULE;
    c->a[0] = alarm_id;
    c->a[1] = at_ms;
    c->a[2] = mode;
    return 0;
}

int sg_cmd_cancel_alarm(SgCmdList *l, int alarm_id)
{
    SgCmd *c;

    if (l == NULL || alarm_id < 1 || alarm_id > SG_ALARM_ID_MAX) {
        return -1;
    }
    c = cmd_next(l);
    if (c == NULL) {
        return -1;
    }
    c->type = SG_CMD_CANCEL_ALARM;
    c->a[0] = alarm_id;
    return 0;
}

int sg_cmd_notify(SgCmdList *l, int kind, int variant, int64_t a2, int64_t a3, int64_t a4,
                  int actions, int timeout_min)
{
    SgCmd *c;

    if (l == NULL) {
        return -1;
    }
    c = cmd_next(l);
    if (c == NULL) {
        return -1;
    }
    c->type = SG_CMD_NOTIFY;
    c->a[0] = kind;
    c->a[1] = variant;
    c->a[2] = a2;
    c->a[3] = a3;
    c->a[4] = a4;
    c->a[5] = actions;
    c->a[6] = timeout_min;
    return 0;
}

int sg_cmd_cancel_notify(SgCmdList *l, int kind)
{
    SgCmd *c;

    if (l == NULL) {
        return -1;
    }
    c = cmd_next(l);
    if (c == NULL) {
        return -1;
    }
    c->type = SG_CMD_CANCEL_NOTIFY;
    c->a[0] = kind;
    return 0;
}

int32_t sg_cmd_flatten(const SgCmdList *l, int64_t *out, int32_t out_cap)
{
    int32_t i;
    int32_t j;
    int32_t n;

    if (l == NULL || out == NULL || l->count < 0 || l->count > SG_MAX_CMDS) {
        return -1;
    }
    n = l->count * SG_CMD_WORDS;
    if (out_cap < n) {
        return -1;
    }
    for (i = 0; i < l->count; i++) {
        int32_t base = i * SG_CMD_WORDS;

        out[base] = l->cmd[i].type;
        for (j = 0; j < SG_CMD_WORDS - 1; j++) {
            out[base + 1 + j] = l->cmd[i].a[j];
        }
    }
    return n;
}
