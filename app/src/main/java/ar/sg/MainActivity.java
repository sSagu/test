package ar.sg;

import android.Manifest;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The only main screen. Java renders; C decides. Every entry: observe -> one Native call
 * -> run the returned commands -> bind the UI model returned by nativeUiModel.
 * No threads, handlers or timers. Minutes and dates are formatted here, never computed.
 * Owner: ui-main (docs/ADVICE-v2.md sections 3, 5, 6).
 */
public final class MainActivity extends android.app.Activity {

    private static final int REQ_NOTIF = 1;
    private static final int CHART_COLS = 7;

    private boolean askedThisRun;     // permission dialog shown at most once per launch

    /** Re-renders each minute while visible: the bed window opens and closes with the clock.
     *  Dynamic registration only (no manifest entry); it renders and runs no commands. */
    private final BroadcastReceiver tick = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent i) {
            bind(Sg.observe(MainActivity.this));
        }
    };

    private ScrollView root;
    private TextView txtError;
    private LinearLayout boxNotif;

    // hero: alarm card
    private LinearLayout boxHeroAlarm, rowRemind, rowBedSuggest;
    private TextView txtAlarmDay, txtAlarmTime, txtRemindLabel, txtRemindTime;
    private TextView txtBedSuggestLabel, txtBedSuggestTime;

    // hero: message card
    private LinearLayout boxHeroMsg;
    private TextView txtHeroMsgTitle, txtHeroMsgBody;

    // bedtime control
    private LinearLayout boxBed, btnBed, boxBedLogged;
    private ImageView imgBedMoon;
    private TextView txtBedHint, txtBedLogged, txtBedLoggedSub;
    private Button btnBedUpdate;

    // week chart
    private View chartTargetLine;
    private TextView txtTargetLabel, txtDebtSub, txtDebtValue;
    private LinearLayout[] cols;
    private TextView[] labels;
    private View[] bars;
    private View[] dashes;
    private TextView[] days;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setDecorFitsSystemWindows(false);   // edge-to-edge
        setContentView(R.layout.activity_main);
        Sg.init(this);
        bindViews();
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets ins) {
                android.graphics.Insets i = ins.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                v.setPadding(i.left, i.top, i.right, i.bottom);
                return ins;
            }
        });
        wireListeners();
    }

    @Override
    protected void onResume() {
        super.onResume();
        Sg.init(this);
        Sg.ensureChannels(this);
        Sg.Observation o = Sg.observe(this);
        run(Native.nativeSync(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed,
                Native.REASON_APP_OPEN));
        maybeAskNotifPermission(bind(Sg.observe(this)));
        registerReceiver(tick, new IntentFilter(Intent.ACTION_TIME_TICK), Context.RECEIVER_NOT_EXPORTED);
    }

    @Override
    protected void onPause() {
        unregisterReceiver(tick);
        super.onPause();
    }

    /** A heads-up or shade action (e.g. "Me voy a dormir") can change the state while this
     *  screen stays in front without a resume: re-render whenever it regains focus. */
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            bind(Sg.observe(this));
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_NOTIF) {
            Sg.Observation o = Sg.observe(this);
            run(Native.nativeSync(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed,
                    Native.REASON_APP_OPEN));
            bind(Sg.observe(this));
        }
    }

    // ------------------------------------------------------------------ views

    private void bindViews() {
        root = findViewById(R.id.root);
        txtError = findViewById(R.id.txt_error);
        boxNotif = findViewById(R.id.box_notif_banner);

        boxHeroAlarm = findViewById(R.id.box_hero_alarm);
        txtAlarmDay = findViewById(R.id.txt_alarm_day);
        txtAlarmTime = findViewById(R.id.txt_alarm_time);
        rowRemind = findViewById(R.id.row_remind);
        txtRemindLabel = findViewById(R.id.txt_remind_label);
        txtRemindTime = findViewById(R.id.txt_remind_time);
        rowBedSuggest = findViewById(R.id.row_bed_suggest);
        txtBedSuggestLabel = findViewById(R.id.txt_bed_suggest_label);
        txtBedSuggestTime = findViewById(R.id.txt_bed_suggest_time);

        boxHeroMsg = findViewById(R.id.box_hero_msg);
        txtHeroMsgTitle = findViewById(R.id.txt_hero_msg_title);
        txtHeroMsgBody = findViewById(R.id.txt_hero_msg_body);

        boxBed = findViewById(R.id.box_bed);
        btnBed = findViewById(R.id.btn_bed);
        imgBedMoon = findViewById(R.id.img_bed_moon);
        txtBedHint = findViewById(R.id.txt_bed_hint);
        boxBedLogged = findViewById(R.id.box_bed_logged);
        txtBedLogged = findViewById(R.id.txt_bed_logged);
        txtBedLoggedSub = findViewById(R.id.txt_bed_logged_sub);
        btnBedUpdate = findViewById(R.id.btn_bed_update);

        chartTargetLine = findViewById(R.id.chart_target_line);
        txtTargetLabel = findViewById(R.id.txt_target_label);
        txtDebtSub = findViewById(R.id.txt_debt_sub);
        txtDebtValue = findViewById(R.id.txt_debt_value);

        cols = new LinearLayout[]{
                findViewById(R.id.col_0), findViewById(R.id.col_1), findViewById(R.id.col_2),
                findViewById(R.id.col_3), findViewById(R.id.col_4), findViewById(R.id.col_5),
                findViewById(R.id.col_6)};
        labels = new TextView[]{
                findViewById(R.id.lbl_0), findViewById(R.id.lbl_1), findViewById(R.id.lbl_2),
                findViewById(R.id.lbl_3), findViewById(R.id.lbl_4), findViewById(R.id.lbl_5),
                findViewById(R.id.lbl_6)};
        bars = new View[]{
                findViewById(R.id.bar_0), findViewById(R.id.bar_1), findViewById(R.id.bar_2),
                findViewById(R.id.bar_3), findViewById(R.id.bar_4), findViewById(R.id.bar_5),
                findViewById(R.id.bar_6)};
        dashes = new View[]{
                findViewById(R.id.dash_0), findViewById(R.id.dash_1), findViewById(R.id.dash_2),
                findViewById(R.id.dash_3), findViewById(R.id.dash_4), findViewById(R.id.dash_5),
                findViewById(R.id.dash_6)};
        days = new TextView[]{
                findViewById(R.id.day_0), findViewById(R.id.day_1), findViewById(R.id.day_2),
                findViewById(R.id.day_3), findViewById(R.id.day_4), findViewById(R.id.day_5),
                findViewById(R.id.day_6)};
    }

    private void wireListeners() {
        ImageButton btnSettings = findViewById(R.id.btn_settings);
        btnSettings.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { openSettings(); }
        });
        findViewById(R.id.btn_notif_settings).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { openNotificationSettings(); }
        });
        btnBed.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { sleep(); }
        });
        // Rendering only: the pill is a LinearLayout, so announce it with the Button role (UI-3).
        btnBed.setAccessibilityDelegate(new View.AccessibilityDelegate() {
            @Override public void onInitializeAccessibilityNodeInfo(View host,
                    android.view.accessibility.AccessibilityNodeInfo info) {
                super.onInitializeAccessibilityNodeInfo(host, info);
                info.setClassName(Button.class.getName());
            }
        });
        btnBedUpdate.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { sleep(); }
        });
    }

    // ------------------------------------------------------------------ binding

    /** Renders the C view model. Returns it, or null when the model is unavailable. */
    private long[] bind(Sg.Observation o) {
        long[] ui = Native.nativeUiModel(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed);
        if (ui == null || ui.length < Native.UI_LEN) {
            txtError.setVisibility(View.VISIBLE);
            return null;
        }
        txtError.setVisibility(View.GONE);
        boxNotif.setVisibility(visible(ui[Native.UI_NOTIF_BANNER] != 0));
        bindHero(ui);
        bindBed(ui);
        bindWeek(ui);
        return ui;
    }

    private void bindHero(long[] ui) {
        int hero = (int) ui[Native.UI_HERO];
        boolean alarm = hero == Native.HERO_ALARM;
        boxHeroAlarm.setVisibility(visible(alarm));
        boxHeroMsg.setVisibility(visible(!alarm));
        if (alarm) {
            bindAlarmCard(ui);
        } else {
            bindMessage(ui, hero);
        }
    }

    private void bindAlarmCard(long[] ui) {
        txtAlarmDay.setText(getString(R.string.str_hero_alarm_day,
                fmtDate(ui[Native.UI_ALARM_DATE], (int) ui[Native.UI_ALARM_WDAY])));
        txtAlarmTime.setText(fmtMod((int) ui[Native.UI_ALARM_MOD]));

        int remind = (int) ui[Native.UI_REMIND_STATE];
        rowRemind.setVisibility(visible(remind != Native.REMIND_HIDDEN));
        txtRemindLabel.setText(remindLabel(remind));
        int remindMod = (int) ui[Native.UI_REMIND_MOD];
        txtRemindTime.setVisibility(visible(remindMod >= 0));
        txtRemindTime.setText(fmtMod(remindMod));

        int suggest = (int) ui[Native.UI_BED_SUGGEST_MOD];
        rowBedSuggest.setVisibility(visible(suggest >= 0));
        txtBedSuggestLabel.setText(getString(R.string.str_bed_suggest_label,
                fmtHours(setting(ui, Native.SET_TARGET_MIN))));
        txtBedSuggestTime.setText(fmtMod(suggest));
    }

    private void bindMessage(long[] ui, int hero) {
        int mod = (int) ui[Native.UI_ALARM_MOD];
        String title;
        String body;
        switch (hero) {
            case Native.HERO_NO_ALARM:
                title = getString(R.string.str_hero_none_title);
                body = getString(R.string.str_hero_none_body);
                break;
            case Native.HERO_OTHER_APP:
                title = getString(R.string.str_hero_other_title, fmtMod(mod));
                body = getString(R.string.str_hero_other_body);
                break;
            case Native.HERO_OUT_OF_WINDOW:
                title = getString(R.string.str_hero_window_title, fmtMod(mod));
                body = getString(R.string.str_hero_window_body);
                break;
            case Native.HERO_FAR:
                title = getString(R.string.str_hero_far_title,
                        fmtDate(ui[Native.UI_ALARM_DATE], (int) ui[Native.UI_ALARM_WDAY]), fmtMod(mod));
                body = getString(R.string.str_hero_far_body);
                break;
            default:   // HERO_DISABLED
                title = getString(R.string.str_hero_off_title);
                body = getString(R.string.str_hero_off_body);
                break;
        }
        txtHeroMsgTitle.setText(title);
        txtHeroMsgBody.setText(body);
    }

    private void bindBed(long[] ui) {
        int bed = (int) ui[Native.UI_BED_STATE];
        int mod = (int) ui[Native.UI_BED_MOD];

        boolean control = bed == Native.BED_BEFORE || bed == Native.BED_AVAILABLE
                || bed == Native.BED_CLOSED;
        boxBed.setVisibility(visible(control));
        boolean tappable = bed == Native.BED_AVAILABLE;
        btnBed.setEnabled(tappable);
        imgBedMoon.setVisibility(visible(tappable));
        txtBedHint.setText(bedHint(bed, mod));

        boolean logged = bed == Native.BED_LOGGED;
        boxBedLogged.setVisibility(visible(logged));
        txtBedLogged.setText(getString(R.string.str_bed_logged, fmtMod(mod)));
        boolean canUpdate = ui[Native.UI_BED_CAN_UPDATE] != 0;
        txtBedLoggedSub.setText(canUpdate ? R.string.str_bed_logged_sub : R.string.str_bed_logged_done);
        btnBedUpdate.setVisibility(visible(canUpdate));
    }

    private void bindWeek(long[] ui) {
        // dashed target line and its label, positioned from TARGET_PERMILLE
        int target = (int) ui[Native.UI_TARGET_PERMILLE];
        int lineMargin = permillePx(target) + getResources().getDimensionPixelSize(R.dimen.sg_baseline_h);
        setBottomMargin(chartTargetLine, lineMargin);
        setBottomMargin(txtTargetLabel,
                lineMargin + getResources().getDimensionPixelSize(R.dimen.sg_bar_label_gap));
        txtTargetLabel.setText(getString(R.string.str_week_target,
                fmtHours(setting(ui, Native.SET_TARGET_MIN))));

        int label = (int) ui[Native.UI_LABEL_NIGHT];
        int minBar = getResources().getDimensionPixelSize(R.dimen.sg_bar_min_h);
        for (int i = 0; i < CHART_COLS; i++) {
            int base = Native.UI_NIGHTS + i * Native.UI_NIGHT_WORDS;
            long date = ui[base + Native.NIGHT_DATE];
            int wday = (int) ui[base + Native.NIGHT_WDAY];
            int status = (int) ui[base + Native.NIGHT_STATUS];
            int est = (int) ui[base + Native.NIGHT_EST_MIN];
            int permille = (int) ui[Native.UI_BARS + i];

            days[i].setText(wdayShort(wday));
            if (permille < 0) {
                bars[i].setVisibility(View.GONE);
                dashes[i].setVisibility(View.VISIBLE);
            } else {
                bars[i].setVisibility(View.VISIBLE);
                dashes[i].setVisibility(View.GONE);
                setHeight(bars[i], Math.max(permillePx(permille), minBar));
            }

            boolean showLabel = i == label;
            labels[i].setVisibility(visible(showLabel));
            if (showLabel) {
                labels[i].setText(fmtShort(est));
            }

            String when = fmtDate(date, wday);
            String desc;
            if (status == Native.NIGHT_LOGGED) {
                desc = getString(R.string.str_cd_night_value, when, fmtDur(est));
            } else if (status == Native.NIGHT_NOWAKE) {
                desc = getString(R.string.str_cd_night_nowake, when);
            } else {
                desc = getString(R.string.str_cd_night_empty, when);
            }
            cols[i].setContentDescription(desc);
        }

        int debtState = (int) ui[Native.UI_DEBT_STATE];
        int logged = (int) ui[Native.UI_LOGGED_COUNT];
        if (debtState == Native.DEBT_EMPTY) {
            txtDebtSub.setText(R.string.str_debt_empty);
            txtDebtValue.setVisibility(View.GONE);
        } else {
            txtDebtSub.setText(getResources().getQuantityString(R.plurals.str_debt_over_nights, logged, logged));
            txtDebtValue.setVisibility(View.VISIBLE);
            if (debtState == Native.DEBT_ZERO) {
                txtDebtValue.setText(R.string.str_debt_none);
            } else {
                txtDebtValue.setText(fmtDur((int) ui[Native.UI_DEBT_MIN]));
            }
        }
    }

    private static int setting(long[] ui, int key) {
        return (int) ui[Native.UI_SETTINGS + key];
    }

    private static int visible(boolean on) {
        return on ? View.VISIBLE : View.GONE;
    }

    private int remindLabel(int remind) {
        switch (remind) {
            case Native.REMIND_UPCOMING: return R.string.str_remind_upcoming;
            case Native.REMIND_SENT: return R.string.str_remind_sent;
            default: return R.string.str_remind_none;
        }
    }

    private String bedHint(int bed, int mod) {
        switch (bed) {
            case Native.BED_BEFORE: return getString(R.string.str_bed_before, fmtMod(mod));
            case Native.BED_AVAILABLE: return getString(R.string.str_bed_available, fmtMod(mod));
            case Native.BED_CLOSED: return getString(R.string.str_bed_closed, fmtMod(mod));
            default: return "";
        }
    }

    // ------------------------------------------------------------------ actions

    /** "Me voy a dormir" / "Actualizar": same C entry point as the notification button. */
    private void sleep() {
        Sg.Observation o = Sg.observe(this);
        run(Native.nativeAction(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed,
                Native.ACTION_SLEEP));
        bind(Sg.observe(this));
    }

    private void run(long[] cmds) {
        if (cmds != null) Sg.run(this, cmds);
    }

    /** One setting change through C (used by the permission dialog only). */
    private void set(int key, int value) {
        Sg.Observation o = Sg.observe(this);
        run(Native.nativeSet(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed, key, value));
        bind(Sg.observe(this));
    }

    private void openSettings() {
        startActivity(new Intent(this, SettingsActivity.class));
    }

    private void maybeAskNotifPermission(long[] ui) {
        if (askedThisRun || ui == null || ui[Native.UI_ASK_NOTIF] == 0) return;
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
                .setNegativeButton(R.string.str_perm_notif_later, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface d, int w) {
                        set(Native.SET_NOTIF_PROMPTED, 1);
                    }
                })
                .show();
    }

    private void openNotificationSettings() {
        Intent i = new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                .putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName());
        try {
            startActivity(i);
        } catch (ActivityNotFoundException e) {
            txtError.setVisibility(View.VISIBLE);
        }
    }

    // ------------------------------------------------------------------ rendering helpers

    /** mod = minute of day; a negative value renders as nothing. */
    private String fmtMod(int mod) {
        if (mod < 0) return "";
        return getString(R.string.str_fmt_time, mod / 60, mod % 60);
    }

    /** Minutes to "N min" below one hour, else "H h MM min". */
    private String fmtDur(int min) {
        if (min < 60) return getString(R.string.str_fmt_duration_min, min);
        return getString(R.string.str_fmt_duration, min / 60, min % 60);
    }

    /** Whole hours as "H h" (target line, suggestion), otherwise the full duration. */
    private String fmtHours(int min) {
        if (min % 60 == 0) return getString(R.string.str_fmt_duration_hours, min / 60);
        return fmtDur(min);
    }

    /** Chart value label: "H h MM". */
    private String fmtShort(int min) {
        return getString(R.string.str_fmt_duration_short, min / 60, min % 60);
    }

    /** yyyymmdd and weekday (0 = dom .. 6 = sáb) to "vie 9/10". */
    private String fmtDate(long yyyymmdd, int wday) {
        if (yyyymmdd == 0 || wday < 0 || wday > 6) return "";
        int m = (int) (yyyymmdd / 100 % 100);
        int d = (int) (yyyymmdd % 100);
        return getString(R.string.str_fmt_date_short, wdayShort(wday), d, m);
    }

    private String wdayShort(int wday) {
        if (wday < 0 || wday > 6) return "";
        return getResources().getStringArray(R.array.str_wday_short)[wday];
    }

    /** Pixel height of a permille value on the chart plot (permille * plotPx / 1000). */
    private int permillePx(int permille) {
        int plot = getResources().getDimensionPixelSize(R.dimen.sg_chart_plot_h);
        return permille * plot / Native.PERMILLE_FULL;
    }

    private static void setHeight(View v, int px) {
        ViewGroup.LayoutParams lp = v.getLayoutParams();
        lp.height = px;
        v.setLayoutParams(lp);
    }

    private static void setBottomMargin(View v, int px) {
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) v.getLayoutParams();
        lp.bottomMargin = px;
        v.setLayoutParams(lp);
    }
}
