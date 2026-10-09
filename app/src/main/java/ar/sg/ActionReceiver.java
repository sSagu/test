package ar.sg;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Notification action buttons: "Me voy a dormir" (1) and "En 15 min" (2). */
public class ActionReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (intent == null || !Sg.ACTION_ACTION.equals(intent.getAction())) {
            return;
        }
        int id = intent.getIntExtra(Sg.EXTRA_ID, 0);
        if (id < Sg.ACTION_ID_MIN || id > Sg.ACTION_ID_MAX) {
            return;
        }
        Sg.init(ctx);
        Sg.Observation o = Sg.observe(ctx);
        Sg.run(ctx, Native.nativeAction(o.nowMs, o.nextAlarmMs, o.creator,
                o.exactAllowed, o.notifAllowed, id));
    }
}
