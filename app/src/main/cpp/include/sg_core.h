/* sg_core.h - the decision engine (F1, F2, F3, F5, F4 gating). Implementation:
 * sg_core.c (card C3). Pure: input = SgState + SgObs + event, output = mutated
 * SgState + SgCmdList. No I/O, no clock reads, no heap, no globals.
 * Thread-safety: none required; the JNI glue serialises all calls with one mutex.
 * Error codes: every int-returning function returns SG_OK (0) or SG_E_ARG (-1) for
 * NULL/invalid ids; logic never "fails", it just emits commands.
 *
 * Gate G (relevant alarm) is implemented in sg_core_relevant(). A re-read trigger of
 * any kind goes through sg_core_sync(), which:
 *  1. runs sg_state_sanitize; closes due night records; records F5 weekday samples;
 *  2. decides relevance of obs->next_alarm_ms;
 *  3. if T changed vs last_seen_T: cancel stale F1/F2/F5 alarms and notifications,
 *     reset snooze_count, follow alarm in the open night record, clear last_f1_for_T
 *     when the new F1_at is in the future, and either schedule F1/F2 (F1_at future or
 *     within grace) or start/refresh the F3 debounce (SG_ALARM_DEBOUNCE at now+90s);
 *  4. if T unchanged: ensure F1/F2 alarms are (re)scheduled (idempotent re-issue is
 *     fine: same request code replaces);
 *  5. always re-arm SG_ALARM_RECHECK at now + SG_RECHECK_MIN (inexact) when enabled,
 *     schedule SG_ALARM_HOUSEKEEP at wake_ms+1min if a record is open, and
 *     SG_ALARM_F5_NOALARM at next Fri/Sat 21:30 when jetlag_noalarm is on;
 *  6. if !enabled: cancel every alarm id and every notification kind and return.
 *
 * v2 additions (docs/ADVICE-v2.md is the authority; summary):
 *  L. "Logged means handled": in the relevant-alarm branch, after the T-changed
 *     bookkeeping (which includes sg_log_follow_alarm) and before any F1/F2/debounce
 *     decision, if sg_log_open_bed_for(s, T) > 0 then set last_f1_for_T = last_f2_for_T
 *     = T, clear the debounce, make sure SG_ALARM_F1/SG_ALARM_F2/SG_ALARM_DEBOUNCE are
 *     cancelled, and schedule nothing else for this T (the tail of step 5 still runs).
 *  R. Reboot re-post: only when reason is SG_REASON_BOOT or SG_REASON_PKG_REPLACED, T is
 *     relevant and unchanged (T == last_seen_T), last_f1_for_T == T, (SG_FLAG_F1_POSTED
 *     set or snooze_count > 0), sg_log_open_bed_for(s, T) == 0, now is inside the bed
 *     window [T - lead - SG_BED_WINDOW_BEFORE_MIN, T - SG_BED_WINDOW_AFTER_MIN], and
 *     0 <= now - last_notified_ms < SG_F1_TIMEOUT_MIN: emit ONE SG_CMD_NOTIFY for SG_NK_F1
 *     with the same variant/args fire_f1 would use at `now`, actions SG_ACT_SLEEP
 *     (| SG_ACT_SNOOZE if snooze_count < SG_SNOOZE_MAX) | SG_NOTIFY_SILENT, timeout =
 *     SG_F1_TIMEOUT_MIN - minutes elapsed since last_notified_ms (>= 1); set
 *     SG_FLAG_F1_POSTED. last_notified_ms, last_f1_for_T, snooze_count, F5 samples are NOT
 *     touched. Every other reason (RECHECK, APP_OPEN, BROADCAST, ...) never re-posts.
 *
 * sg_core_action(SG_ACTION_SLEEP), v2: when enabled, T relevant and now inside the bed
 * window: r = sg_log_bed_retap(s, now, T); r == 1 -> sample_if_weekday(T); r != 0 ->
 * schedule housekeeping; then ALWAYS (in window) set last_f1_for_T = last_f2_for_T = T,
 * clear the debounce, cancel alarms SG_ALARM_F1 (pending F1 or snooze), SG_ALARM_F2,
 * SG_ALARM_DEBOUNCE, cancel notifications SG_NK_F1, SG_NK_F2, SG_NK_F3 and clear
 * SG_FLAG_F1_POSTED. Outside the window (or T not relevant): unchanged v1 behaviour
 * (cancel SG_NK_F1 + SG_NK_F3 notifications, clear SG_FLAG_F1_POSTED, log nothing).
 * The in-app button and the notification action use this same path.
 */
#ifndef SG_CORE_H
#define SG_CORE_H

#include <stdint.h>
#include "sg_state.h"
#include "sg_cmd.h"
#include "sg_sleeplog.h"

#define SG_OK     0
#define SG_E_ARG  (-1)

/* How Java classified AlarmClockInfo.getShowIntent().getCreatorPackage() */
enum {
    SG_CREATOR_NONE    = 0,  /* no alarm, or showIntent/creator null -> treated as allowed */
    SG_CREATOR_ALLOWED = 1,  /* package in sg_clock_packages */
    SG_CREATOR_OTHER   = 2   /* some other app's alarm clock */
};

/* Why sync is being called (affects nothing but F3 eligibility and logging). */
enum {
    SG_REASON_BROADCAST   = 0,  /* ACTION_NEXT_ALARM_CLOCK_CHANGED */
    SG_REASON_BOOT        = 1,
    SG_REASON_TIME_SET    = 2,
    SG_REASON_TZ_CHANGED  = 3,  /* also clears F5 reference samples */
    SG_REASON_PKG_REPLACED= 4,
    SG_REASON_APP_OPEN    = 5,
    SG_REASON_RECHECK     = 6,  /* SG_ALARM_RECHECK fired */
    SG_REASON_SETTING     = 7   /* a setting changed */
};

/* Observation snapshot taken by Java immediately before each native call. */
typedef struct {
    int64_t now_ms;          /* System.currentTimeMillis() */
    int64_t next_alarm_ms;   /* getNextAlarmClock().getTriggerTime(), 0 if null */
    uint8_t creator;         /* SG_CREATOR_* */
    uint8_t exact_allowed;   /* canScheduleExactAlarms() ? 1 : 0 (informational) */
    uint8_t notif_allowed;   /* POST_NOTIFICATIONS granted ? 1 : 0 (informational) */
} SgObs;

/* Relevance result */
enum {
    SG_REL_NONE       = 0,  /* no alarm or T <= now */
    SG_REL_OK         = 1,
    SG_REL_OTHER_APP  = 2,  /* filtered by only_clock */
    SG_REL_WINDOW     = 3,  /* outside 04:00..12:00 local */
    SG_REL_HORIZON    = 4   /* > SG_MAX_HORIZON_MIN away (kept, re-evaluated later) */
};
int sg_core_relevant(const SgState *s, const SgObs *o);

/* F1 fire time for alarm T under current settings: T - lead_min. */
int64_t sg_core_f1_at(const SgState *s, int64_t T_ms);

/* Possible sleep in minutes if lying down now: floor((T - now)/60000 - SG_LATENCY_MIN,
 * SG_ROUND_STEP_MIN); never negative. */
int32_t sg_core_avail_min(int64_t T_ms, int64_t now_ms);

/* Events. All return SG_OK / SG_E_ARG. `out` is reset by the callee. */
int sg_core_sync(SgState *s, const SgObs *o, int reason, SgCmdList *out);
int sg_core_alarm_fired(SgState *s, const SgObs *o, int alarm_id, SgCmdList *out);
int sg_core_action(SgState *s, const SgObs *o, int action_id, SgCmdList *out);

/* Settings keys for get/set. Values: minutes for *_MIN keys, 0/1 for flags. set()
 * clamps to range/step, then behaves like sg_core_sync(reason=SG_REASON_SETTING).
 * SG_SET_CLEAR_LOG: value ignored, erases the log (F4 "Borrar registro"). */
enum {
    SG_SET_ENABLED         = 1,
    SG_SET_LEAD_MIN        = 2,
    SG_SET_TARGET_MIN      = 3,
    SG_SET_WINDDOWN_ON     = 4,
    SG_SET_WINDDOWN_MIN    = 5,
    SG_SET_LATE_ON         = 6,
    SG_SET_JETLAG_ON       = 7,
    SG_SET_JETLAG_NOALARM  = 8,
    SG_SET_ONLY_CLOCK      = 9,
    SG_SET_NOTIF_PROMPTED  = 10,
    SG_SET_CLEAR_LOG       = 11
};
int sg_core_set(SgState *s, const SgObs *o, int key, int32_t value, SgCmdList *out);
int32_t sg_core_get(const SgState *s, int key);   /* -1 for unknown key */

/* UI view model. Flattened by JNI into long[SG_UI_LEN] using the SG_UI_* indices. */
typedef struct {
    int32_t alarm_rel;        /* SG_REL_* */
    int64_t next_alarm_ms;    /* 0 none */
    int32_t alarm_mod;        /* -1 none */
    int32_t alarm_date;       /* yyyymmdd, 0 none */
    int64_t next_reminder_ms; /* next F1 or F2 fire time, 0 none */
    int32_t reminder_kind;    /* 0 none, 1 F1, 2 F2 */
    int32_t bed_suggest_mod;  /* T - (target + latency) as minute-of-day, -1 none */
    int32_t jetlag_status;    /* 0 no data, 1 ok, 2 feature off, 3 weekend drift (delta in jetlag_delta_min) */
    int32_t jetlag_delta_min;
    int32_t weekday_ref_mod;  /* -1 if invalid */
    SgWeek  week;
    int32_t settings[12];     /* settings[key] = sg_core_get(key) for key 1..11, [0] unused */

    /* ---- v2 fields (docs/ADVICE-v2.md section 3). Flattened AFTER the v1 66 words. ---- */
    int32_t hero;             /* SG_HERO_* : which top card the main screen shows */
    int32_t alarm_wday;       /* 0=Sunday..6 of local date of T when alarm_rel != NONE, else -1 */
    int32_t remind_state;     /* SG_REMIND_* (F1 row of the hero card) */
    int32_t remind_mod;       /* UPCOMING: mod of T - lead; SENT: mod of last_notified_ms; else -1 */
    int32_t bed_state;        /* SG_BED_* (the "Me voy a dormir" control) */
    int32_t bed_mod;          /* BEFORE: mod of window start; AVAILABLE/CLOSED: mod of window
                                 end; LOGGED: mod of the logged bed_ms; HIDDEN: -1 */
    int32_t bed_can_update;   /* 1 iff LOGGED and now is inside the bed window ("Actualizar") */
    int32_t debt_state;       /* SG_DEBT_* */
    int32_t notif_banner;     /* 1 iff enabled && !o->notif_allowed */
    int32_t ask_notif;        /* 1 iff !o->notif_allowed && !SG_FLAG_NOTIF_PROMPTED */
    int32_t target_permille;  /* target_sleep_min * 1000 / SG_UI_CHART_MAX_MIN (dashed line) */
    int32_t label_night;      /* largest i in 0..6 with week.night[i].status == 1, else -1 */
    int32_t bar_permille[SG_DEBT_WINDOW_NIGHTS]; /* status==1: min(max(est,0),MAX)*1000/MAX
                                                    (integer division, 0..1000); else -1 */
} SgUiModel;

/* Chart scale: a full-height bar is 10 h of estimated sleep. */
#define SG_UI_CHART_MAX_MIN 600

/* hero: priority top to bottom (first match wins) */
enum {
    SG_HERO_NO_ALARM     = 0,  /* enabled, alarm_rel == SG_REL_NONE (also: rel != NONE but the
                                  local time of T cannot be computed, so alarm_mod < 0) */
    SG_HERO_ALARM        = 1,  /* enabled, SG_REL_OK: alarm card with bell/moon rows */
    SG_HERO_OTHER_APP    = 2,  /* enabled, SG_REL_OTHER_APP (alarm_mod = that alarm) */
    SG_HERO_OUT_OF_WINDOW= 3,  /* enabled, SG_REL_WINDOW */
    SG_HERO_FAR          = 4,  /* enabled, SG_REL_HORIZON (> 36 h away) */
    SG_HERO_DISABLED     = 5   /* master toggle off (checked FIRST, any alarm_rel) */
};

/* remind_state: only for SG_HERO_ALARM, otherwise SG_REMIND_HIDDEN. f1 = T - lead. */
enum {
    SG_REMIND_HIDDEN   = 0,
    SG_REMIND_UPCOMING = 1,  /* last_f1_for_T != T && now <= f1 + SG_F1_GRACE_MIN */
    SG_REMIND_SENT     = 2,  /* last_f1_for_T == T && f1 - SG_BED_WINDOW_BEFORE_MIN <=
                                last_notified_ms <= now && last_notified_ms < T */
    SG_REMIND_NONE     = 3   /* any other case (alarm set late, logged before F1, ...) */
};

/* bed_state: HIDDEN unless enabled && alarm_rel == SG_REL_OK. lo = T - lead -
 * SG_BED_WINDOW_BEFORE_MIN, hi = T - SG_BED_WINDOW_AFTER_MIN (same window as the action). */
enum {
    SG_BED_HIDDEN    = 0,
    SG_BED_BEFORE    = 1,  /* not logged, now < lo */
    SG_BED_AVAILABLE = 2,  /* not logged, lo <= now <= hi */
    SG_BED_LOGGED    = 3,  /* sg_log_open_bed_for(s, T) > 0 (checked first) */
    SG_BED_CLOSED    = 4   /* not logged, now > hi */
};

/* debt_state */
enum {
    SG_DEBT_EMPTY = 0,     /* week.logged_count == 0 */
    SG_DEBT_ZERO  = 1,     /* logged_count > 0 && debt_min == 0 */
    SG_DEBT_SOME  = 2      /* debt_min > 0 */
};

int sg_core_ui(const SgState *s, const SgObs *o, SgUiModel *out);

/* Flat long[] indices (Java mirrors these in Native.java). */
enum {
    SG_UI_ALARM_REL = 0, SG_UI_NEXT_ALARM_MS = 1, SG_UI_ALARM_MOD = 2, SG_UI_ALARM_DATE = 3,
    SG_UI_NEXT_REMINDER_MS = 4, SG_UI_REMINDER_KIND = 5, SG_UI_BED_SUGGEST_MOD = 6,
    SG_UI_JETLAG_STATUS = 7, SG_UI_JETLAG_DELTA = 8, SG_UI_WEEKDAY_REF_MOD = 9,
    SG_UI_DEBT_MIN = 10, SG_UI_LOGGED_COUNT = 11,
    SG_UI_SETTINGS = 12,           /* 12 slots: [12 + key] */
    SG_UI_NIGHTS = 24,             /* 7 nights x SG_UI_NIGHT_WORDS */
    SG_UI_NIGHT_WORDS = 6,         /* date, wday, status, bed_mod, wake_mod, est_sleep_min */
    /* v2, appended (v1 indices above never move) */
    SG_UI_HERO = 66, SG_UI_ALARM_WDAY = 67, SG_UI_REMIND_STATE = 68, SG_UI_REMIND_MOD = 69,
    SG_UI_BED_STATE = 70, SG_UI_BED_MOD = 71, SG_UI_BED_CAN_UPDATE = 72,
    SG_UI_DEBT_STATE = 73, SG_UI_NOTIF_BANNER = 74, SG_UI_ASK_NOTIF = 75,
    SG_UI_TARGET_PERMILLE = 76, SG_UI_LABEL_NIGHT = 77,
    SG_UI_BARS = 78,               /* 7 words: bar_permille[0..6], oldest first */
    SG_UI_LEN = 85
};
/* Fill out[SG_UI_LEN] from m. */
void sg_core_ui_flatten(const SgUiModel *m, int64_t out[SG_UI_LEN]);

#endif /* SG_CORE_H */
