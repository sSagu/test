package ar.sg;

/**
 * JNI contract. Loaded once per process. Every method is static, synchronized on
 * the native side (one mutex), and never throws except IllegalStateException when
 * the library failed to initialise. Owner: card J1 (sg_jni.c implements these).
 * Mirrors app/src/main/cpp/include/sg_core.h and sg_cmd.h; keep constants identical.
 */
public final class Native {
    private Native() {}

    static { System.loadLibrary("sg"); }

    // ---- SgObs is passed as 5 scalars in this order on every call ----
    // nowMs, nextAlarmMs (0 = none), creator (CREATOR_*), exactAllowed (0/1), notifAllowed (0/1)

    public static final int CREATOR_NONE = 0, CREATOR_ALLOWED = 1, CREATOR_OTHER = 2;

    public static final int REASON_BROADCAST = 0, REASON_BOOT = 1, REASON_TIME_SET = 2,
            REASON_TZ_CHANGED = 3, REASON_PKG_REPLACED = 4, REASON_APP_OPEN = 5,
            REASON_RECHECK = 6, REASON_SETTING = 7;

    // command encoding (sg_cmd.h)
    public static final int CMD_WORDS = 8;
    public static final int CMD_SCHEDULE = 1, CMD_CANCEL_ALARM = 2, CMD_NOTIFY = 3, CMD_CANCEL_NOTIFY = 4;
    public static final int SCHED_EXACT_IDLE = 0, SCHED_INEXACT_IDLE = 1, SCHED_EXACT = 2;
    public static final int ALARM_F1 = 1, ALARM_F2 = 2, ALARM_DEBOUNCE = 3, ALARM_RECHECK = 4,
            ALARM_F5_HINT = 5, ALARM_F5_NOALARM = 6, ALARM_HOUSEKEEP = 7;
    public static final int NK_F1 = 1, NK_F2 = 2, NK_F3 = 3, NK_F5 = 5;
    public static final int ACT_SLEEP = 1, ACT_SNOOZE = 2;          // bitmask in a5
    public static final int NOTIFY_SILENT = 1 << 8;                 // a5 flag -> Notification.Builder#setSilent(true)
    public static final int ACTION_SLEEP = 1, ACTION_SNOOZE = 2;    // ids for nativeAction

    // settings keys (sg_core.h SG_SET_*)
    public static final int SET_ENABLED = 1, SET_LEAD_MIN = 2, SET_TARGET_MIN = 3, SET_WINDDOWN_ON = 4,
            SET_WINDDOWN_MIN = 5, SET_LATE_ON = 6, SET_JETLAG_ON = 7, SET_JETLAG_NOALARM = 8,
            SET_ONLY_CLOCK = 9, SET_NOTIF_PROMPTED = 10, SET_CLEAR_LOG = 11;

    // UI model indices (sg_core.h SG_UI_*)
    public static final int UI_ALARM_REL = 0, UI_NEXT_ALARM_MS = 1, UI_ALARM_MOD = 2, UI_ALARM_DATE = 3,
            UI_NEXT_REMINDER_MS = 4, UI_REMINDER_KIND = 5, UI_BED_SUGGEST_MOD = 6, UI_JETLAG_STATUS = 7,
            UI_JETLAG_DELTA = 8, UI_WEEKDAY_REF_MOD = 9, UI_DEBT_MIN = 10, UI_LOGGED_COUNT = 11,
            UI_SETTINGS = 12, UI_NIGHTS = 24, UI_NIGHT_WORDS = 6;
    // v2 (appended; docs/ADVICE-v2.md section 3)
    public static final int UI_HERO = 66, UI_ALARM_WDAY = 67, UI_REMIND_STATE = 68, UI_REMIND_MOD = 69,
            UI_BED_STATE = 70, UI_BED_MOD = 71, UI_BED_CAN_UPDATE = 72, UI_DEBT_STATE = 73,
            UI_NOTIF_BANNER = 74, UI_ASK_NOTIF = 75, UI_TARGET_PERMILLE = 76, UI_LABEL_NIGHT = 77,
            UI_BARS = 78, UI_LEN = 85;
    // night word offsets inside each UI_NIGHT_WORDS block
    public static final int NIGHT_DATE = 0, NIGHT_WDAY = 1, NIGHT_STATUS = 2, NIGHT_BED_MOD = 3,
            NIGHT_WAKE_MOD = 4, NIGHT_EST_MIN = 5;
    public static final int NIGHT_EMPTY = 0, NIGHT_LOGGED = 1, NIGHT_NOWAKE = 2;
    public static final int REL_NONE = 0, REL_OK = 1, REL_OTHER_APP = 2, REL_WINDOW = 3, REL_HORIZON = 4;
    public static final int HERO_NO_ALARM = 0, HERO_ALARM = 1, HERO_OTHER_APP = 2, HERO_OUT_OF_WINDOW = 3,
            HERO_FAR = 4, HERO_DISABLED = 5;
    public static final int REMIND_HIDDEN = 0, REMIND_UPCOMING = 1, REMIND_SENT = 2, REMIND_NONE = 3;
    public static final int BED_HIDDEN = 0, BED_BEFORE = 1, BED_AVAILABLE = 2, BED_LOGGED = 3, BED_CLOSED = 4;
    public static final int DEBT_EMPTY = 0, DEBT_ZERO = 1, DEBT_SOME = 2;
    /** Bars and the target line map linearly: height = permille * chartPlotPx / PERMILLE_FULL. */
    public static final int PERMILLE_FULL = 1000;

    /** Load state from statePath (getFilesDir()+"/sg_state.bin"). Returns SG_STORE_* code
     *  (0 ok, 1 no file yet, 2 io, 3 corrupt -> defaults in use). Idempotent. */
    public static native int nativeInit(String statePath);

    /** Re-read trigger. Returns flattened commands (length multiple of CMD_WORDS, may be 0). */
    public static native long[] nativeSync(long nowMs, long nextAlarmMs, int creator,
                                           int exactAllowed, int notifAllowed, int reason);

    /** One of our AlarmManager PendingIntents fired (alarmId = request code). */
    public static native long[] nativeAlarmFired(long nowMs, long nextAlarmMs, int creator,
                                                 int exactAllowed, int notifAllowed, int alarmId);

    /** Notification action tapped. */
    public static native long[] nativeAction(long nowMs, long nextAlarmMs, int creator,
                                             int exactAllowed, int notifAllowed, int actionId);

    /** Change a setting (clamped in C); returns commands like nativeSync. */
    public static native long[] nativeSet(long nowMs, long nextAlarmMs, int creator,
                                          int exactAllowed, int notifAllowed, int key, int value);

    /** Current value of a setting, -1 unknown. */
    public static native int nativeGet(int key);

    /** UI view model, long[UI_LEN]. */
    public static native long[] nativeUiModel(long nowMs, long nextAlarmMs, int creator,
                                              int exactAllowed, int notifAllowed);
}
