package ar.sg;

import android.Manifest;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import java.util.Calendar;

/**
 * The only screen. Java formats; C decides. Every entry: observe -> one Native call
 * -> run the returned commands -> bind the UI model returned by nativeUiModel.
 * No threads, handlers or timers.
 */
public final class MainActivity extends android.app.Activity {

    private static final int REQ_NOTIF = 1;
    private static final int STEP_MIN = 15;

    private boolean binding;          // guards switch listeners while binding
    private boolean askedThisRun;     // permission dialog shown at most once per launch

    private ScrollView root;
    private TextView txtError, txtNextAlarm, txtNextReminder, txtDebtValue, txtJetlag;
    private TextView txtLeadValue, txtTargetValue, txtWinddownValue;
    private TextView[] nights;
    private LinearLayout boxNotif;
    private Switch swEnabled, swWinddown, swLate, swJetlag, swJetlagNoalarm, swOnlyClock;
    private Button btnLeadMinus, btnLeadPlus, btnTargetMinus, btnTargetPlus;
    private Button btnWinddownMinus, btnWinddownPlus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setDecorFitsSystemWindows(false);   // edge-to-edge
        setContentView(R.layout.activity_main);
        bindViews();
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets ins) {
                android.graphics.Insets i = ins.getInsets(WindowInsets.Type.systemBars());
                v.setPadding(0, i.top, 0, i.bottom);
                return ins;
            }
        });
        wireListeners();
    }

    @Override
    protected void onResume() {
        super.onResume();
        Sg.ensureChannels(this);
        Native.nativeInit(statePath());
        Sg.Observation o = Sg.observe(this);
        run(Native.nativeSync(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed, Native.REASON_APP_OPEN));
        bind(Sg.observe(this));
        maybeAskNotifPermission(o);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_NOTIF) {
            Sg.Observation o = Sg.observe(this);
            run(Native.nativeSync(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed, Native.REASON_APP_OPEN));
            bind(Sg.observe(this));
        }
    }

    // ------------------------------------------------------------------ binding

    private void bindViews() {
        root = findViewById(R.id.root);
        txtError = findViewById(R.id.txt_error);
        txtNextAlarm = findViewById(R.id.txt_next_alarm);
        txtNextReminder = findViewById(R.id.txt_next_reminder);
        txtDebtValue = findViewById(R.id.txt_debt_value);
        txtJetlag = findViewById(R.id.txt_jetlag);
        txtLeadValue = findViewById(R.id.txt_lead_value);
        txtTargetValue = findViewById(R.id.txt_target_value);
        txtWinddownValue = findViewById(R.id.txt_winddown_value);
        boxNotif = findViewById(R.id.box_notif_banner);
        nights = new TextView[]{
                findViewById(R.id.night_0), findViewById(R.id.night_1), findViewById(R.id.night_2),
                findViewById(R.id.night_3), findViewById(R.id.night_4), findViewById(R.id.night_5),
                findViewById(R.id.night_6)};
        swEnabled = findViewById(R.id.sw_enabled);
        swWinddown = findViewById(R.id.sw_winddown);
        swLate = findViewById(R.id.sw_late);
        swJetlag = findViewById(R.id.sw_jetlag);
        swJetlagNoalarm = findViewById(R.id.sw_jetlag_noalarm);
        swOnlyClock = findViewById(R.id.sw_only_clock);
        btnLeadMinus = findViewById(R.id.btn_lead_minus);
        btnLeadPlus = findViewById(R.id.btn_lead_plus);
        btnTargetMinus = findViewById(R.id.btn_target_minus);
        btnTargetPlus = findViewById(R.id.btn_target_plus);
        btnWinddownMinus = findViewById(R.id.btn_winddown_minus);
        btnWinddownPlus = findViewById(R.id.btn_winddown_plus);
    }

    private void wireListeners() {
        swEnabled.setOnCheckedChangeListener(toggle(Native.SET_ENABLED));
        swWinddown.setOnCheckedChangeListener(toggle(Native.SET_WINDDOWN_ON));
        swLate.setOnCheckedChangeListener(toggle(Native.SET_LATE_ON));
        swJetlag.setOnCheckedChangeListener(toggle(Native.SET_JETLAG_ON));
        swJetlagNoalarm.setOnCheckedChangeListener(toggle(Native.SET_JETLAG_NOALARM));
        swOnlyClock.setOnCheckedChangeListener(toggle(Native.SET_ONLY_CLOCK));

        btnLeadMinus.setOnClickListener(stepper(Native.SET_LEAD_MIN, -STEP_MIN));
        btnLeadPlus.setOnClickListener(stepper(Native.SET_LEAD_MIN, STEP_MIN));
        btnTargetMinus.setOnClickListener(stepper(Native.SET_TARGET_MIN, -STEP_MIN));
        btnTargetPlus.setOnClickListener(stepper(Native.SET_TARGET_MIN, STEP_MIN));
        btnWinddownMinus.setOnClickListener(stepper(Native.SET_WINDDOWN_MIN, -STEP_MIN));
        btnWinddownPlus.setOnClickListener(stepper(Native.SET_WINDDOWN_MIN, STEP_MIN));

        findViewById(R.id.btn_notif_settings).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { openNotificationSettings(); }
        });
        findViewById(R.id.btn_clear_log).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { confirmClearLog(); }
        });
    }

    /** Renders the C view model. A null model (allocation failure) shows the generic error. */
    private void bind(Sg.Observation o) {
        long[] ui = Native.nativeUiModel(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed);
        if (ui == null || ui.length < Native.UI_LEN) {
            txtError.setVisibility(View.VISIBLE);
            return;
        }
        txtError.setVisibility(View.GONE);
        binding = true;
        try {
            bindNextAlarm(ui);
            bindNextReminder(ui);
            bindNights(ui);
            bindDebt(ui);
            bindJetlag(ui);
            bindSettings(ui);
            boxNotif.setVisibility(o.notifAllowed == 0 ? View.VISIBLE : View.GONE);
        } finally {
            binding = false;
        }
    }

    private void bindNextAlarm(long[] ui) {
        int rel = (int) ui[Native.UI_ALARM_REL];
        if (rel == Native.REL_NONE) {
            txtNextAlarm.setText(R.string.str_screen_next_alarm_none);
        } else if (rel == Native.REL_OTHER_APP) {
            txtNextAlarm.setText(getString(R.string.str_screen_next_alarm_ignored, fmtMs(ui[Native.UI_NEXT_ALARM_MS])));
        } else {
            int mod = (int) ui[Native.UI_ALARM_MOD];
            String t = mod >= 0 ? fmtMod(mod) : fmtMs(ui[Native.UI_NEXT_ALARM_MS]);
            txtNextAlarm.setText(getString(R.string.str_screen_next_alarm, t));
        }
    }

    private void bindNextReminder(long[] ui) {
        long ms = ui[Native.UI_NEXT_REMINDER_MS];
        if (ms == 0) {
            txtNextReminder.setText(R.string.str_screen_next_reminder_none);
        } else {
            txtNextReminder.setText(getString(R.string.str_screen_next_reminder, fmtMs(ms)));
        }
    }

    private void bindNights(long[] ui) {
        for (int i = 0; i < nights.length; i++) {
            int base = Native.UI_NIGHTS + i * Native.UI_NIGHT_WORDS;
            long date = ui[base];
            int status = (int) ui[base + 2];
            String day = fmtDate(date);
            String text;
            if (status == 1) {
                text = getString(R.string.str_screen_night_row, day,
                        fmtMod((int) ui[base + 3]), fmtMod((int) ui[base + 4]), fmtDur((int) ui[base + 5]));
            } else if (status == 2) {
                text = day + ": " + getString(R.string.str_screen_night_nowake);
            } else {
                text = day + ": " + getString(R.string.str_screen_night_empty);
            }
            nights[i].setText(text);
        }
    }

    private void bindDebt(long[] ui) {
        long debt = ui[Native.UI_DEBT_MIN];
        int logged = (int) ui[Native.UI_LOGGED_COUNT];
        if (logged == 0) {
            txtDebtValue.setText(R.string.str_screen_debt_empty);
        } else if (debt == 0) {
            txtDebtValue.setText(R.string.str_screen_debt_zero);
        } else {
            txtDebtValue.setText(getString(R.string.str_screen_debt_value, fmtDur((int) debt), logged));
        }
    }

    private void bindJetlag(long[] ui) {
        int status = (int) ui[Native.UI_JETLAG_STATUS];
        switch (status) {
            case 0:
                txtJetlag.setVisibility(View.VISIBLE);
                txtJetlag.setText(R.string.str_screen_jetlag_nodata);
                break;
            case 1:
                txtJetlag.setVisibility(View.VISIBLE);
                txtJetlag.setText(R.string.str_screen_jetlag_ok);
                break;
            case 3: {
                int ref = (int) ui[Native.UI_WEEKDAY_REF_MOD];
                int suggest = ((ref + 60 + STEP_MIN / 2) / STEP_MIN * STEP_MIN) % 1440;
                txtJetlag.setVisibility(View.VISIBLE);
                txtJetlag.setText(getString(R.string.str_f5_title_alarm) + "\n"
                        + getString(R.string.str_f5_body_alarm, fmtMod((int) ui[Native.UI_ALARM_MOD]),
                        fmtDur((int) ui[Native.UI_JETLAG_DELTA]), fmtMod(suggest)));
                break;
            }
            default:
                txtJetlag.setVisibility(View.GONE);
                break;
        }
    }

    private void bindSettings(long[] ui) {
        boolean winddownOn = setting(ui, Native.SET_WINDDOWN_ON) != 0;
        swEnabled.setChecked(setting(ui, Native.SET_ENABLED) != 0);
        swWinddown.setChecked(winddownOn);
        swLate.setChecked(setting(ui, Native.SET_LATE_ON) != 0);
        swJetlag.setChecked(setting(ui, Native.SET_JETLAG_ON) != 0);
        swJetlagNoalarm.setChecked(setting(ui, Native.SET_JETLAG_NOALARM) != 0);
        swOnlyClock.setChecked(setting(ui, Native.SET_ONLY_CLOCK) != 0);

        int lead = setting(ui, Native.SET_LEAD_MIN);
        int target = setting(ui, Native.SET_TARGET_MIN);
        int wind = setting(ui, Native.SET_WINDDOWN_MIN);
        txtLeadValue.setText(getString(R.string.str_settings_lead_summary, fmtDur(lead)));
        txtTargetValue.setText(fmtDur(target));
        txtWinddownValue.setText(fmtDur(wind));
        btnWinddownMinus.setEnabled(winddownOn);
        btnWinddownPlus.setEnabled(winddownOn);
        txtWinddownValue.setEnabled(winddownOn);
    }

    private static int setting(long[] ui, int key) {
        return (int) ui[Native.UI_SETTINGS + key];
    }

    // ------------------------------------------------------------------ actions

    private CompoundButton.OnCheckedChangeListener toggle(final int key) {
        return new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton b, boolean on) {
                if (!binding) set(key, on ? 1 : 0);
            }
        };
    }

    private View.OnClickListener stepper(final int key, final int delta) {
        return new View.OnClickListener() {
            @Override
            public void onClick(View v) { step(key, delta); }
        };
    }

    private DialogInterface.OnClickListener setOnConfirm(final int key, final int value) {
        return new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface d, int w) { set(key, value); }
        };
    }

    private void step(int key, int delta) {
        Sg.Observation o = Sg.observe(this);
        long[] ui = Native.nativeUiModel(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed);
        if (ui == null || ui.length < Native.UI_LEN) return;
        set(key, setting(ui, key) + delta);   // C clamps to range and step
    }

    /** One setting change: C decides, Java runs the commands and re-binds. */
    private void set(int key, int value) {
        Sg.Observation o = Sg.observe(this);
        run(Native.nativeSet(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed, key, value));
        bind(Sg.observe(this));
    }

    private void run(long[] cmds) {
        if (cmds != null) Sg.run(this, cmds);
    }

    private void confirmClearLog() {
        new AlertDialog.Builder(this)
                .setMessage(R.string.str_settings_clear_confirm)
                .setPositiveButton(R.string.str_btn_confirm, setOnConfirm(Native.SET_CLEAR_LOG, 1))
                .setNegativeButton(R.string.str_btn_cancel, null)
                .show();
    }

    private void maybeAskNotifPermission(Sg.Observation o) {
        if (askedThisRun || o.notifAllowed != 0 || Build.VERSION.SDK_INT < 33) return;
        if (Native.nativeGet(Native.SET_NOTIF_PROMPTED) != 0) return;
        askedThisRun = true;
        new AlertDialog.Builder(this)
                .setTitle(R.string.str_perm_notif_title)
                .setMessage(R.string.str_perm_notif_body)
                .setCancelable(false)
                .setPositiveButton(R.string.str_perm_notif_allow, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface d, int w) {
                        set(Native.SET_NOTIF_PROMPTED, 1);
                        requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIF);
                    }
                })
                .setNegativeButton(R.string.str_perm_notif_later, setOnConfirm(Native.SET_NOTIF_PROMPTED, 1))
                .show();
    }

    private void openNotificationSettings() {
        Intent i = new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                .putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName());
        try {
            startActivity(i);
        } catch (ActivityNotFoundException e) {
            txtError.setText(R.string.str_error_generic);
            txtError.setVisibility(View.VISIBLE);
        }
    }

    // ------------------------------------------------------------------ formatting

    private String statePath() {
        return getFilesDir().getAbsolutePath() + "/sg_state.bin";
    }

    /** mod = minute of day. */
    private String fmtMod(int mod) {
        return getString(R.string.str_fmt_time, mod / 60, mod % 60);
    }

    private String fmtMs(long ms) {
        Calendar c = Calendar.getInstance();
        c.setTimeInMillis(ms);
        return getString(R.string.str_fmt_time, c.get(Calendar.HOUR_OF_DAY), c.get(Calendar.MINUTE));
    }

    private String fmtDur(int min) {
        if (min < 60) return getString(R.string.str_fmt_duration_min, min);
        return getString(R.string.str_fmt_duration, min / 60, min % 60);
    }

    /** yyyymmdd -> "lun 9/10" style (weekday, day, month). */
    private String fmtDate(long yyyymmdd) {
        if (yyyymmdd == 0) return "";
        int y = (int) (yyyymmdd / 10000);
        int m = (int) (yyyymmdd / 100 % 100);
        int d = (int) (yyyymmdd % 100);
        Calendar c = Calendar.getInstance();
        c.clear();
        c.set(y, m - 1, d);
        String[] wd = getResources().getStringArray(R.array.str_wday_short);
        String wday = wd[c.get(Calendar.DAY_OF_WEEK) - 1];
        return getString(R.string.str_fmt_date_short, wday, d, m);
    }
}
