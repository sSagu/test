package ar.sg;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** System re-read triggers (manifest-declared, exported=false; system senders only). */
public class SystemReceiver extends BroadcastReceiver {
    private static final String A_NEXT_ALARM = "android.app.action.NEXT_ALARM_CLOCK_CHANGED";
    private static final String A_BOOT = "android.intent.action.BOOT_COMPLETED";
    private static final String A_TIME_SET = "android.intent.action.TIME_SET";
    private static final String A_TZ = "android.intent.action.TIMEZONE_CHANGED";
    private static final String A_PKG_REPLACED = "android.intent.action.MY_PACKAGE_REPLACED";

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (intent == null) {
            return;
        }
        String action = intent.getAction();
        if (action == null) {
            return;
        }
        int reason;
        if (A_NEXT_ALARM.equals(action)) {
            reason = Native.REASON_BROADCAST;
        } else if (A_BOOT.equals(action)) {
            reason = Native.REASON_BOOT;
        } else if (A_TIME_SET.equals(action)) {
            reason = Native.REASON_TIME_SET;
        } else if (A_TZ.equals(action)) {
            reason = Native.REASON_TZ_CHANGED;
        } else if (A_PKG_REPLACED.equals(action)) {
            reason = Native.REASON_PKG_REPLACED;
        } else {
            return;
        }
        Sg.resync(ctx, reason);
    }
}
