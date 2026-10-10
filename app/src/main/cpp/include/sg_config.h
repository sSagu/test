/* sg_config.h - Sueño-Guía compile-time constants.
 * SOURCE OF TRUTH: docs/ADVICE-features.md section 7. Do not change values here
 * without changing that table. All durations are minutes unless suffixed _S / _MS.
 * Pure header: no includes except stdint, no code. Safe for host and device.
 */
#ifndef SG_CONFIG_H
#define SG_CONFIG_H

#include <stdint.h>

/* ---- user settings: defaults and ranges ---- */
#define SG_LEAD_DEFAULT_MIN          570   /* 9h30 */
#define SG_LEAD_MIN_MIN              480
#define SG_LEAD_MAX_MIN              660
#define SG_LEAD_STEP_MIN             15
#define SG_TARGET_SLEEP_DEFAULT_MIN  480
#define SG_TARGET_SLEEP_MIN_MIN      420
#define SG_TARGET_SLEEP_MAX_MIN      540
#define SG_TARGET_SLEEP_STEP_MIN     15
#define SG_WINDDOWN_DEFAULT_ON       0
#define SG_WINDDOWN_DEFAULT_MIN      30
#define SG_WINDDOWN_MIN_MIN          15
#define SG_WINDDOWN_MAX_MIN          60
#define SG_WINDDOWN_STEP_MIN         15
#define SG_LATE_DEFAULT_ON           1
#define SG_JETLAG_DEFAULT_ON         1
#define SG_JETLAG_NOALARM_DEFAULT_ON 1
#define SG_ONLY_CLOCK_DEFAULT        1
#define SG_ENABLED_DEFAULT           1

/* ---- fixed behaviour constants ---- */
#define SG_LATENCY_MIN               20
#define SG_F1_GRACE_MIN              30
#define SG_F1_TIMEOUT_MIN            180
#define SG_F2_TIMEOUT_MIN            60
#define SG_F3_TIMEOUT_MIN            120
#define SG_F5_TIMEOUT_MIN            180
#define SG_SNOOZE_MIN                15
#define SG_SNOOZE_MAX                2
#define SG_WAKE_WIN_START_MIN        240   /* 04:00 local */
#define SG_WAKE_WIN_END_MIN          720   /* 12:00 local */
#define SG_MAX_HORIZON_MIN           2160  /* 36 h */
#define SG_DEBOUNCE_S                90
#define SG_LATE_MIN_SLEEP_MIN        240
#define SG_COOLDOWN_MIN              180
#define SG_ROUND_STEP_MIN            15
#define SG_BED_WINDOW_BEFORE_MIN     120
#define SG_BED_WINDOW_AFTER_MIN      60
#define SG_TAP_DEDUP_MIN             30
#define SG_LOG_RETAIN_NIGHTS         90
#define SG_DEBT_WINDOW_NIGHTS        7
#define SG_REF_SAMPLES_MAX           10
#define SG_REF_SAMPLES_MIN           3
#define SG_JETLAG_TRIGGER_MIN        90
#define SG_JETLAG_SUGGEST_MIN        60
#define SG_NOALARM_HOUR_MIN          1290  /* 21:30 local, Fri/Sat */
#define SG_F5_AFTER_F1_MIN           1     /* F5 alarm-hint fires 1 min after F1 */
#define SG_RECHECK_MIN               60    /* inexact safety re-read (features advisor) */
#define SG_MAX_OPPORTUNITY_MIN       960   /* clamp bed->wake to 16 h when computing est_sleep */

/* ---- v3 manual night (docs/ADVICE-v3.md, features section 9) ---- */
#define SG_MANUAL_MIN_MIN            60    /* "Dormiste" lower bound (1 h) */
#define SG_MANUAL_MAX_MIN            840   /* upper bound (14 h); 840 + latency <= SG_MAX_OPPORTUNITY_MIN */
#define SG_MANUAL_STEP_MIN           15    /* stepper increment only; C stores any value in range */
#define SG_MANUAL_DEFAULT_MIN        360   /* draft for a night without a record (approved mockup) */
#define SG_MANUAL_WAKE_MOD           420   /* 07:00 local: synthetic wake of a manual record */

/* ---- notification ids (Java uses the same values) ---- */
#define SG_NOTIF_ID_F1               1001
#define SG_NOTIF_ID_F2               1002
#define SG_NOTIF_ID_F3               1003
#define SG_NOTIF_ID_F5               1005

/* ---- ms helpers (int64 arithmetic only) ---- */
#define SG_MS_PER_MIN                INT64_C(60000)
#define SG_MS_PER_S                  INT64_C(1000)
#define SG_MIN_TO_MS(m)              ((int64_t)(m) * SG_MS_PER_MIN)

/* Package allow-list (SG_PKG_ALLOW) lives in Java (res/values/config.xml
 * string-array sg_clock_packages) because package comparison is a Java-side
 * input; C only receives the resulting SG_CREATOR_* classification. */

#endif /* SG_CONFIG_H */
