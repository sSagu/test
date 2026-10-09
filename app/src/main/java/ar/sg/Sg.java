package ar.sg;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Icon;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Command executor: observes the system, calls the native core once, runs the
 * returned commands. Owner: card J1. Holds no state of its own.
 */
public final class Sg {
    private Sg() {}

    static final String STATE_FILE = "sg_state.bin";
    static final String ACTION_ALARM = "ar.sg.ALARM";
    static final String ACTION_ACTION = "ar.sg.ACTION";
    static final String EXTRA_ID = "id";
    static final int ALARM_ID_MIN = 1, ALARM_ID_MAX = 7;
    static final int ACTION_ID_MIN = 1, ACTION_ID_MAX = 2;
    static final int ACTION_ID_SLEEP = 1, ACTION_ID_SNOOZE = 2;

    static final int REQ_CONTENT = 200;
    static final int REQ_ACTION_BASE = 100;

    static final String CH_REMINDER = "sg_reminder";
    static final String CH_WINDDOWN = "sg_winddown";
    static final String CH_LATE = "sg_late";
    static final String CH_HINTS = "sg_hints";

    static final int NOTIF_ID_F1 = 1001, NOTIF_ID_F2 = 1002, NOTIF_ID_F3 = 1003, NOTIF_ID_F5 = 1005;

    private static final int SHORT_BODY_MAX = 90;

    /** Snapshot taken right before a native call (the 5 scalars of Native). */
    static final class Observation {
        final long nowMs;
        final long nextAlarmMs;
        final int creator;
        final int exactAllowed;
        final int notifAllowed;

        Observation(long nowMs, long nextAlarmMs, int creator, int exactAllowed, int notifAllowed) {
            this.nowMs = nowMs;
            this.nextAlarmMs = nextAlarmMs;
            this.creator = creator;
            this.exactAllowed = exactAllowed;
            this.notifAllowed = notifAllowed;
        }
    }

    /** Points the native core at the app-private state file. Idempotent. */
    static int init(Context ctx) {
        return Native.nativeInit(ctx.getFilesDir().getAbsolutePath() + "/" + STATE_FILE);
    }

    static Observation observe(Context ctx) {
        long now = System.currentTimeMillis();
        long next = 0;
        int creator = Native.CREATOR_NONE;
        AlarmManager am = ctx.getSystemService(AlarmManager.class);
        AlarmManager.AlarmClockInfo info = am.getNextAlarmClock();
        if (info != null) {
            next = info.getTriggerTime();
            creator = classify(ctx, info.getShowIntent());
        }
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        int exact = am.canScheduleExactAlarms() ? 1 : 0;
        int notif = nm.areNotificationsEnabled() ? 1 : 0;
        return new Observation(now, next, creator, exact, notif);
    }

    /** Classifies the creator of the next alarm; unknown creator fails open. */
    private static int classify(Context ctx, PendingIntent show) {
        if (show == null) {
            return Native.CREATOR_NONE;
        }
        String pkg = show.getCreatorPackage();
        if (pkg == null) {
            return Native.CREATOR_NONE;
        }
        List<String> allow = Arrays.asList(ctx.getResources().getStringArray(R.array.sg_clock_packages));
        return allow.contains(pkg) ? Native.CREATOR_ALLOWED : Native.CREATOR_OTHER;
    }

    /** Full re-read for a system trigger: observe, one native sync, run commands. */
    static void resync(Context ctx, int reason) {
        init(ctx);
        Observation o = observe(ctx);
        run(ctx, Native.nativeSync(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed, reason));
    }

    /** Executes a flattened command list (CMD_WORDS longs per command). */
    static void run(Context ctx, long[] cmds) {
        if (cmds == null || cmds.length == 0 || cmds.length % Native.CMD_WORDS != 0) {
            return;
        }
        AlarmManager am = ctx.getSystemService(AlarmManager.class);
        int n = cmds.length / Native.CMD_WORDS;
        for (int i = 0; i < n; i++) {
            int b = i * Native.CMD_WORDS;
            switch ((int) cmds[b]) {
                case Native.CMD_SCHEDULE:
                    schedule(ctx, am, (int) cmds[b + 1], cmds[b + 2], (int) cmds[b + 3]);
                    break;
                case Native.CMD_CANCEL_ALARM:
                    cancelAlarm(ctx, am, (int) cmds[b + 1]);
                    break;
                case Native.CMD_NOTIFY:
                    notify(ctx, (int) cmds[b + 1], (int) cmds[b + 2], cmds[b + 3], cmds[b + 4],
                            cmds[b + 5], (int) cmds[b + 6], (int) cmds[b + 7]);
                    break;
                case Native.CMD_CANCEL_NOTIFY:
                    cancelNotify(ctx, (int) cmds[b + 1]);
                    break;
                default:
                    break;
            }
        }
    }

    private static void schedule(Context ctx, AlarmManager am, int id, long atMs, int mode) {
        if (id < ALARM_ID_MIN || id > ALARM_ID_MAX) {
            return;
        }
        PendingIntent pi = alarmIntent(ctx, id,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        switch (mode) {
            case Native.SCHED_INEXACT_IDLE:
                am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi);
                break;
            case Native.SCHED_EXACT:
                try {
                    am.setExact(AlarmManager.RTC_WAKEUP, atMs, pi);
                } catch (SecurityException e) {
                    am.set(AlarmManager.RTC_WAKEUP, atMs, pi);
                }
                break;
            case Native.SCHED_EXACT_IDLE:
            default:
                try {
                    am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi);
                } catch (SecurityException e) {
                    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi);
                }
                break;
        }
    }

    private static void cancelAlarm(Context ctx, AlarmManager am, int id) {
        if (id < ALARM_ID_MIN || id > ALARM_ID_MAX) {
            return;
        }
        PendingIntent pi = alarmIntent(ctx, id, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_NO_CREATE);
        if (pi != null) {
            am.cancel(pi);
        }
    }

    /** Explicit-component PendingIntent for an alarm; request code == alarm id. */
    static PendingIntent alarmIntent(Context ctx, int id, int flags) {
        Intent i = new Intent(ctx, AlarmReceiver.class).setAction(ACTION_ALARM).putExtra(EXTRA_ID, id);
        return PendingIntent.getBroadcast(ctx, id, i, flags);
    }

    /** Explicit-component PendingIntent for a notification action; request code 100 + id. */
    static PendingIntent actionIntent(Context ctx, int id) {
        Intent i = new Intent(ctx, ActionReceiver.class).setAction(ACTION_ACTION).putExtra(EXTRA_ID, id);
        return PendingIntent.getBroadcast(ctx, REQ_ACTION_BASE + id, i,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    static PendingIntent contentIntent(Context ctx) {
        Intent i = new Intent(ctx, MainActivity.class)
                .setAction(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER);
        return PendingIntent.getActivity(ctx, REQ_CONTENT, i, PendingIntent.FLAG_IMMUTABLE);
    }

    /** Creates the four channels (idempotent). Safe to call before every notify. */
    static void ensureChannels(Context ctx) {
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        List<NotificationChannel> list = new ArrayList<>(4);
        list.add(channel(ctx, CH_REMINDER, R.string.str_ch_reminder_name,
                R.string.str_ch_reminder_desc, NotificationManager.IMPORTANCE_HIGH));
        list.add(channel(ctx, CH_WINDDOWN, R.string.str_ch_winddown_name,
                R.string.str_ch_winddown_desc, NotificationManager.IMPORTANCE_DEFAULT));
        list.add(channel(ctx, CH_LATE, R.string.str_ch_late_name,
                R.string.str_ch_late_desc, NotificationManager.IMPORTANCE_DEFAULT));
        list.add(channel(ctx, CH_HINTS, R.string.str_ch_hints_name,
                R.string.str_ch_hints_desc, NotificationManager.IMPORTANCE_LOW));
        nm.createNotificationChannels(list);
    }

    private static NotificationChannel channel(Context ctx, String id, int nameRes, int descRes, int importance) {
        NotificationChannel c = new NotificationChannel(id, ctx.getString(nameRes), importance);
        c.setDescription(ctx.getString(descRes));
        return c;
    }

    private static void notify(Context ctx, int kind, int variant, long a2, long a3, long a4,
                               int actions, int timeoutMin) {
        String channel;
        int id = notifId(kind);
        switch (kind) {
            case Native.NK_F1: channel = CH_REMINDER; break;
            case Native.NK_F2: channel = CH_WINDDOWN; break;
            case Native.NK_F3: channel = CH_LATE; break;
            case Native.NK_F5: channel = CH_HINTS; break;
            default: return;
        }
        String title;
        String body;
        switch (kind) {
            case Native.NK_F1:
                if (variant == 1) {
                    title = ctx.getString(R.string.str_f1_title);
                    body = ctx.getString(R.string.str_f1_body_late, hm(ctx, a2), dur(ctx, a3));
                } else {
                    title = ctx.getString(R.string.str_f1_title);
                    body = ctx.getString(R.string.str_f1_body, hm(ctx, a2), dur(ctx, a3), hm(ctx, a4));
                }
                break;
            case Native.NK_F2:
                title = ctx.getString(R.string.str_f2_title);
                body = ctx.getString(R.string.str_f2_body, hm(ctx, a2));
                break;
            case Native.NK_F3:
                title = ctx.getString(R.string.str_f3_title);
                if (variant == 1) {
                    body = ctx.getString(R.string.str_f3_body_ok, hm(ctx, a2), dur(ctx, a3));
                } else {
                    body = ctx.getString(R.string.str_f3_body, hm(ctx, a2), dur(ctx, a3));
                }
                break;
            default: // NK_F5
                if (variant == 1) {
                    title = ctx.getString(R.string.str_f5_title_noalarm);
                    body = ctx.getString(R.string.str_f5_body_noalarm, hm(ctx, a2));
                } else {
                    title = ctx.getString(R.string.str_f5_title_alarm);
                    body = ctx.getString(R.string.str_f5_body_alarm, hm(ctx, a2), dur(ctx, a3), hm(ctx, a4));
                }
                break;
        }

        ensureChannels(ctx);
        Notification.Builder b = new Notification.Builder(ctx, channel)
                .setSmallIcon(R.drawable.ic_moon)
                .setContentTitle(title)
                .setContentText(shortText(body))
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setContentIntent(contentIntent(ctx))
                .setAutoCancel(true)
                .setCategory(Notification.CATEGORY_REMINDER)
                .setVisibility(Notification.VISIBILITY_PUBLIC);
        if (timeoutMin > 0) {
            b.setTimeoutAfter(timeoutMin * 60000L);
        }
        if ((actions & Native.ACT_SLEEP) != 0) {
            b.addAction(action(ctx, ACTION_ID_SLEEP, R.string.str_f1_action_sleep));
        }
        if ((actions & Native.ACT_SNOOZE) != 0) {
            b.addAction(action(ctx, ACTION_ID_SNOOZE, R.string.str_f1_action_snooze));
        }
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        nm.notify(id, b.build());
    }

    private static Notification.Action action(Context ctx, int actionId, int labelRes) {
        return new Notification.Action.Builder(
                Icon.createWithResource(ctx, R.drawable.ic_moon),
                ctx.getString(labelRes),
                actionIntent(ctx, actionId)).build();
    }

    private static void cancelNotify(Context ctx, int kind) {
        int id = notifId(kind);
        if (id == 0) {
            return;
        }
        ctx.getSystemService(NotificationManager.class).cancel(id);
    }

    private static int notifId(int kind) {
        switch (kind) {
            case Native.NK_F1: return NOTIF_ID_F1;
            case Native.NK_F2: return NOTIF_ID_F2;
            case Native.NK_F3: return NOTIF_ID_F3;
            case Native.NK_F5: return NOTIF_ID_F5;
            default: return 0;
        }
    }

    /** Minute-of-day to "HH:mm". */
    private static String hm(Context ctx, long mod) {
        int m = clampMin(mod);
        return ctx.getString(R.string.str_fmt_time, m / 60, m % 60);
    }

    /** Minutes to "H h MM min", or "N min" below one hour. */
    private static String dur(Context ctx, long min) {
        int m = clampMin(min);
        if (m < 60) {
            return ctx.getString(R.string.str_fmt_duration_min, m);
        }
        return ctx.getString(R.string.str_fmt_duration, m / 60, m % 60);
    }

    private static int clampMin(long v) {
        if (v < 0) {
            return 0;
        }
        return v > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) v;
    }

    private static String shortText(String body) {
        if (body.length() <= SHORT_BODY_MAX) {
            return body;
        }
        return body.substring(0, SHORT_BODY_MAX - 3) + "…";
    }
}
