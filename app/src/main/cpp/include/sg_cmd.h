/* sg_cmd.h - the command list the C core returns to Java.
 * The C core never touches Android APIs. Every decision is emitted as a command;
 * Java (ar.sg.Native caller) executes commands in order. JNI flattens an SgCmdList
 * into a long[] of SG_CMD_WORDS * count elements: for command i, element
 * [i*SG_CMD_WORDS + 0] = type, [+1..+7] = a[0..6]. Unused a[] are 0.
 */
#ifndef SG_CMD_H
#define SG_CMD_H

#include <stdint.h>

#define SG_CMD_WORDS   8
#define SG_MAX_CMDS    12

/* command types (a[0..] meaning per type) */
enum {
    SG_CMD_NONE          = 0,
    /* a0 = alarm id (SG_ALARM_*), a1 = trigger epoch ms (RTC_WAKEUP), a2 = SG_SCHED_* mode */
    SG_CMD_SCHEDULE      = 1,
    /* a0 = alarm id */
    SG_CMD_CANCEL_ALARM  = 2,
    /* a0 = notification kind (SG_NK_*), a1 = text variant (SG_TXT_*), a2..a4 = args
     * (minute-of-day 0..1439 for times, minutes for durations), a5 = action bitmask
     * (SG_ACT_*), a6 = timeout minutes */
    SG_CMD_NOTIFY        = 3,
    /* a0 = notification kind (SG_NK_*) */
    SG_CMD_CANCEL_NOTIFY = 4
};

/* alarm ids == PendingIntent request codes, all RTC_WAKEUP, target AlarmReceiver */
enum {
    SG_ALARM_F1        = 1,  /* main reminder (also used for snooze re-post) */
    SG_ALARM_F2        = 2,  /* wind-down */
    SG_ALARM_DEBOUNCE  = 3,  /* F3 evaluation after SG_DEBOUNCE_S */
    SG_ALARM_RECHECK   = 4,  /* inexact safety re-read every SG_RECHECK_MIN */
    SG_ALARM_F5_HINT   = 5,  /* weekend hint 1 min after F1 */
    SG_ALARM_F5_NOALARM= 6,  /* Fri/Sat 21:30 no-alarm check */
    SG_ALARM_HOUSEKEEP = 7   /* close open night record right after wake_ms */
};
#define SG_ALARM_ID_MAX 7

/* scheduling mode */
enum {
    SG_SCHED_EXACT_IDLE   = 0,  /* setExactAndAllowWhileIdle; fallback setAndAllowWhileIdle if !canScheduleExactAlarms */
    SG_SCHED_INEXACT_IDLE = 1,  /* setAndAllowWhileIdle */
    SG_SCHED_EXACT        = 2   /* setExact (short debounce, device awake); fallback set() */
};

/* notification kinds -> Java maps to channel + id (SG_NOTIF_ID_*) */
enum {
    SG_NK_F1 = 1,   /* channel sg_reminder, id 1001 */
    SG_NK_F2 = 2,   /* channel sg_winddown, id 1002 */
    SG_NK_F3 = 3,   /* channel sg_late,     id 1003 */
    SG_NK_F5 = 5    /* channel sg_hints,    id 1005 */
};

/* text variants; Java maps (kind, variant) -> (title, body) resources. Args per variant: */
enum {
    SG_TXT_F1_NORMAL   = 0, /* str_f1_body: a2 alarm_mod, a3 target_sleep_min (dur), a4 bed_mod */
    SG_TXT_F1_LATE     = 1, /* str_f1_body_late: a2 alarm_mod, a3 avail_min (dur) */
    SG_TXT_F2          = 0, /* str_f2_body: a2 alarm_mod */
    SG_TXT_F3_LATE     = 0, /* str_f3_body: a2 alarm_mod, a3 avail_min */
    SG_TXT_F3_OK       = 1, /* str_f3_body_ok: a2 alarm_mod, a3 avail_min */
    SG_TXT_F5_ALARM    = 0, /* str_f5_body_alarm: a2 alarm_mod, a3 delta_min, a4 suggest_mod */
    SG_TXT_F5_NOALARM  = 1  /* str_f5_body_noalarm: a2 suggest_mod */
};

/* action buttons bitmask (a5) */
#define SG_ACT_SLEEP   (1 << 0)   /* "Me voy a dormir" -> ActionReceiver, SG_ACTION_SLEEP */
#define SG_ACT_SNOOZE  (1 << 1)   /* "En 15 min"       -> ActionReceiver, SG_ACTION_SNOOZE */

/* action ids delivered back via sg_core_action() */
enum {
    SG_ACTION_SLEEP  = 1,
    SG_ACTION_SNOOZE = 2
};

typedef struct {
    int64_t type;
    int64_t a[SG_CMD_WORDS - 1];
} SgCmd;

typedef struct {
    int32_t count;                 /* 0..SG_MAX_CMDS */
    int32_t dropped;               /* commands that did not fit (bug indicator; must be 0) */
    SgCmd   cmd[SG_MAX_CMDS];
} SgCmdList;

/* Zero the list. */
void sg_cmd_init(SgCmdList *l);
/* Append helpers; return 0 on success, -1 if full (dropped++). Implemented in sg_core.c. */
int sg_cmd_schedule(SgCmdList *l, int alarm_id, int64_t at_ms, int mode);
int sg_cmd_cancel_alarm(SgCmdList *l, int alarm_id);
int sg_cmd_notify(SgCmdList *l, int kind, int variant, int64_t a2, int64_t a3, int64_t a4,
                  int actions, int timeout_min);
int sg_cmd_cancel_notify(SgCmdList *l, int kind);
/* Flatten into out[SG_CMD_WORDS * SG_MAX_CMDS]; returns number of int64 written
 * (count * SG_CMD_WORDS). */
int32_t sg_cmd_flatten(const SgCmdList *l, int64_t *out, int32_t out_cap);

#endif /* SG_CMD_H */
