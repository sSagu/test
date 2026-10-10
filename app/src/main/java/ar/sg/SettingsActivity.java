package ar.sg;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.ImageButton;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

/**
 * Settings screen (docs/ADVICE-v2.md section 6, card ui-settings). Java only renders the
 * C view model and forwards user changes: every change is ONE Native.nativeSet call, then
 * Sg.run, then bind. No state, no decisions here. Explicit-only (exported=false).
 */
public final class SettingsActivity extends Activity {
    private static final int STEP_MIN = 15;

    private boolean binding;          // guards listeners while binding

    private ScrollView root;
    private TextView txtError;
    private Switch swEnabled, swLate, swWinddown, swJetlag, swJetlagNoalarm, swOnlyClock;
    private Button btnLeadMinus, btnLeadPlus, btnTargetMinus, btnTargetPlus;
    private Button btnWinddownMinus, btnWinddownPlus, btnClearLog;
    private ImageButton btnBack;
    private TextView txtLeadValue, txtTargetValue, txtWinddownValue;
    private View rowWinddownMin;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setDecorFitsSystemWindows(false);   // edge-to-edge
        setContentView(R.layout.activity_settings);
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
        bind(Sg.observe(this));
    }

    private void bindViews() {
        root = (ScrollView) findViewById(R.id.settings_root);
        txtError = (TextView) findViewById(R.id.txt_settings_error);
        btnBack = (ImageButton) findViewById(R.id.btn_back);
        swEnabled = (Switch) findViewById(R.id.sw_enabled);
        swLate = (Switch) findViewById(R.id.sw_late);
        swWinddown = (Switch) findViewById(R.id.sw_winddown);
        swJetlag = (Switch) findViewById(R.id.sw_jetlag);
        swJetlagNoalarm = (Switch) findViewById(R.id.sw_jetlag_noalarm);
        swOnlyClock = (Switch) findViewById(R.id.sw_only_clock);
        btnLeadMinus = (Button) findViewById(R.id.btn_lead_minus);
        btnLeadPlus = (Button) findViewById(R.id.btn_lead_plus);
        txtLeadValue = (TextView) findViewById(R.id.txt_lead_value);
        btnTargetMinus = (Button) findViewById(R.id.btn_target_minus);
        btnTargetPlus = (Button) findViewById(R.id.btn_target_plus);
        txtTargetValue = (TextView) findViewById(R.id.txt_target_value);
        rowWinddownMin = findViewById(R.id.row_winddown_min);
        btnWinddownMinus = (Button) findViewById(R.id.btn_winddown_minus);
        btnWinddownPlus = (Button) findViewById(R.id.btn_winddown_plus);
        txtWinddownValue = (TextView) findViewById(R.id.txt_winddown_value);
        btnClearLog = (Button) findViewById(R.id.btn_clear_log);

        for (Switch sw : new Switch[]{swEnabled, swLate, swWinddown, swJetlag, swJetlagNoalarm, swOnlyClock}) {
            sw.setSaveEnabled(false);   // no restore-time onCheckedChanged before nativeInit
        }
    }

    private void wireListeners() {
        swEnabled.setOnCheckedChangeListener(toggle(Native.SET_ENABLED));
        swLate.setOnCheckedChangeListener(toggle(Native.SET_LATE_ON));
        swWinddown.setOnCheckedChangeListener(toggle(Native.SET_WINDDOWN_ON));
        swJetlag.setOnCheckedChangeListener(toggle(Native.SET_JETLAG_ON));
        swJetlagNoalarm.setOnCheckedChangeListener(toggle(Native.SET_JETLAG_NOALARM));
        swOnlyClock.setOnCheckedChangeListener(toggle(Native.SET_ONLY_CLOCK));

        btnLeadMinus.setOnClickListener(stepper(Native.SET_LEAD_MIN, -STEP_MIN));
        btnLeadPlus.setOnClickListener(stepper(Native.SET_LEAD_MIN, STEP_MIN));
        btnTargetMinus.setOnClickListener(stepper(Native.SET_TARGET_MIN, -STEP_MIN));
        btnTargetPlus.setOnClickListener(stepper(Native.SET_TARGET_MIN, STEP_MIN));
        btnWinddownMinus.setOnClickListener(stepper(Native.SET_WINDDOWN_MIN, -STEP_MIN));
        btnWinddownPlus.setOnClickListener(stepper(Native.SET_WINDDOWN_MIN, STEP_MIN));

        btnClearLog.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                confirmClearLog();
            }
        });
        btnBack.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });
    }

    // ------------------------------------------------------------------ binding

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
            bindSettings(ui);
        } finally {
            binding = false;
        }
    }

    private void bindSettings(long[] ui) {
        swEnabled.setChecked(setting(ui, Native.SET_ENABLED) != 0);
        swLate.setChecked(setting(ui, Native.SET_LATE_ON) != 0);
        swWinddown.setChecked(setting(ui, Native.SET_WINDDOWN_ON) != 0);
        swJetlag.setChecked(setting(ui, Native.SET_JETLAG_ON) != 0);
        swJetlagNoalarm.setChecked(setting(ui, Native.SET_JETLAG_NOALARM) != 0);
        swOnlyClock.setChecked(setting(ui, Native.SET_ONLY_CLOCK) != 0);

        txtLeadValue.setText(fmtDur(setting(ui, Native.SET_LEAD_MIN)));
        txtTargetValue.setText(fmtDur(setting(ui, Native.SET_TARGET_MIN)));
        txtWinddownValue.setText(fmtDur(setting(ui, Native.SET_WINDDOWN_MIN)));
        rowWinddownMin.setVisibility(setting(ui, Native.SET_WINDDOWN_ON) != 0 ? View.VISIBLE : View.GONE);
    }

    private static int setting(long[] ui, int key) {
        return (int) ui[Native.UI_SETTINGS + key];
    }

    /** Minutes to "H h MM min", or "N min" below one hour (rendering only). */
    private String fmtDur(int min) {
        if (min < 60) {
            return getString(R.string.str_fmt_duration_min, min);
        }
        return getString(R.string.str_fmt_duration, min / 60, min % 60);
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
            public void onClick(View v) {
                step(key, delta);
            }
        };
    }

    private DialogInterface.OnClickListener setOnConfirm(final int key, final int value) {
        return new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface d, int w) {
                set(key, value);
            }
        };
    }

    /** Stepper: the value comes from the current model; C clamps to range and step. */
    private void step(int key, int delta) {
        Sg.Observation o = Sg.observe(this);
        long[] ui = Native.nativeUiModel(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed);
        if (ui == null || ui.length < Native.UI_LEN) return;
        set(key, setting(ui, key) + delta);
    }

    /** One setting change: C decides, Java runs the commands and re-binds. */
    private void set(int key, int value) {
        Sg.Observation o = Sg.observe(this);
        long[] cmds = Native.nativeSet(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed, o.notifAllowed, key, value);
        if (cmds != null) Sg.run(this, cmds);
        bind(Sg.observe(this));
    }

    private void confirmClearLog() {
        new AlertDialog.Builder(this)
                .setMessage(R.string.str_settings_clear_confirm)
                .setPositiveButton(R.string.str_btn_confirm, setOnConfirm(Native.SET_CLEAR_LOG, 1))
                .setNegativeButton(R.string.str_btn_cancel, null)
                .show();
    }
}
