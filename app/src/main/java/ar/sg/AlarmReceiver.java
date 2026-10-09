package ar.sg;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Fires for our own AlarmManager PendingIntents (request code == alarm id). */
public class AlarmReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (intent == null || !Sg.ACTION_ALARM.equals(intent.getAction())) {
            return;
        }
        int id = intent.getIntExtra(Sg.EXTRA_ID, 0);
        if (id < Sg.ALARM_ID_MIN || id > Sg.ALARM_ID_MAX) {
            return;
        }
        Sg.init(ctx);
        Sg.Observation o = Sg.observe(ctx);
        Sg.run(ctx, Native.nativeAlarmFired(o.nowMs, o.nextAlarmMs, o.creator,
                o.exactAllowed, o.notifAllowed, id));
    }
}
