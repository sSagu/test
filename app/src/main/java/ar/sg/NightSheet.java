package ar.sg;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.TextView;

/**
 * v3 "Anotar una noche a mano": bottom sheet in a framework Dialog (docs/ADVICE-v3.md
 * sections 1 D2, 3, 5). Draft state is only the picked week index and the minutes shown.
 * Dates, weekdays, defaults and bounds come from the C UI model passed in at open time;
 * the save goes through one Native.nativeLogNight call. Java computes no time or date.
 * Owner: ui-main.
 */
final class NightSheet {

    /** Called after a successful save (the caller re-renders the main screen). */
    interface Saved {
        void onSaved();
    }

    /** Nights in the picker: the 7 week indices 0..NIGHT_TODAY. */
    private static final int NIGHTS = Native.NIGHT_TODAY + 1;

    private final Activity act;
    private final long[] ui;
    private final Saved cb;
    private final Dialog dialog;

    private TextView txtNightDate;
    private TextView txtNightSpan;
    private ImageButton btnNightPrev;
    private ImageButton btnNightNext;
    private Button btnMinus;
    private Button btnPlus;
    private TextView txtMinutes;

    private int night;     // week index 0 (today-6) .. 6 (today)
    private int minutes;   // draft "Dormiste" value

    private NightSheet(Activity act, long[] ui, Saved cb) {
        this.act = act;
        this.ui = ui;
        this.cb = cb;
        this.dialog = new Dialog(act, R.style.SgSheetDialog);
    }

    /** Builds and shows the sheet. ui must be a full UI model with UI_MANUAL_OK == 1. */
    static Dialog show(Activity a, long[] ui, Saved cb) {
        NightSheet s = new NightSheet(a, ui, cb);
        s.build();
        s.dialog.show();
        return s.dialog;
    }

    private void build() {
        dialog.setContentView(R.layout.sheet_manual_night);
        dialog.setTitle(R.string.str_manual_title);
        dialog.setCancelable(true);
        dialog.setCanceledOnTouchOutside(true);

        Window w = dialog.getWindow();
        if (w != null) {
            w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            w.setGravity(Gravity.BOTTOM);
            w.setDecorFitsSystemWindows(false);
            // A3R2-1: a floating dialog window loses FLAG_LAYOUT_IN_SCREEN in generateLayout, and
            // DecorView then consumes the system-bar insets itself. Set it back (after
            // setContentView) so the insets reach sheet_root.
            w.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN);
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.setFitInsetsTypes(0);
            w.setAttributes(lp);
        }

        // sheet_root is the scrolling ScrollView (A3-1): its bottom padding is only the system
        // bars (navigation bar, gesture area) and its sides the bars and cutout (A3R2-1). The top
        // takes the status-bar inset too: it is 0 unless the sheet is tall enough to reach it
        // (landscape, large font, split screen). The sheet's own bottom padding sits inside
        // sheet_content, so it scrolls with the rows. Insets are returned unconsumed.
        final View sheetRoot = dialog.findViewById(R.id.sheet_root);
        sheetRoot.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets ins) {
                android.graphics.Insets i = ins.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                v.setPadding(i.left, i.top, i.right, i.bottom);
                return ins;
            }
        });

        txtNightDate = dialog.findViewById(R.id.txt_night_date);
        txtNightSpan = dialog.findViewById(R.id.txt_night_span);
        btnNightPrev = dialog.findViewById(R.id.btn_night_prev);
        btnNightNext = dialog.findViewById(R.id.btn_night_next);
        btnMinus = dialog.findViewById(R.id.btn_minutes_minus);
        btnPlus = dialog.findViewById(R.id.btn_minutes_plus);
        txtMinutes = dialog.findViewById(R.id.txt_minutes_value);

        int step = (int) ui[Native.UI_MANUAL_STEP];
        btnMinus.setContentDescription(act.getString(R.string.str_cd_manual_minus, step));
        btnPlus.setContentDescription(act.getString(R.string.str_cd_manual_plus, step));

        btnNightPrev.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { moveNight(night - 1); }
        });
        btnNightNext.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { moveNight(night + 1); }
        });
        btnMinus.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { stepMinutes(-step); }
        });
        btnPlus.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { stepMinutes(step); }
        });
        dialog.findViewById(R.id.btn_sheet_cancel).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { dialog.dismiss(); }
        });
        dialog.findViewById(R.id.btn_sheet_save).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { save(); }
        });

        night = Native.NIGHT_TODAY;
        minutes = (int) ui[Native.UI_MANUAL_DEFAULT + night];
        render();
    }

    /** Picks another night; its default minutes come from C (status 1 = its logged value). */
    private void moveNight(int to) {
        if (to < 0 || to >= NIGHTS) {
            return;
        }
        night = to;
        minutes = (int) ui[Native.UI_MANUAL_DEFAULT + night];
        render();
    }

    /** One 15-minute step; the stepper stops at the C bounds (C clamps again on save). */
    private void stepMinutes(int delta) {
        int lo = (int) ui[Native.UI_MANUAL_MIN];
        int hi = (int) ui[Native.UI_MANUAL_MAX];
        minutes = Math.max(lo, Math.min(hi, minutes + delta));
        render();
    }

    private void render() {
        int base = Native.UI_NIGHTS + night * Native.UI_NIGHT_WORDS;
        long date = ui[base + Native.NIGHT_DATE];
        int wday = (int) ui[base + Native.NIGHT_WDAY];
        String day = fmtDate(act, date, wday);
        Sg.setTextIfChanged(txtNightDate, night == Native.NIGHT_TODAY
                ? act.getString(R.string.str_manual_date_today, day) : day);

        int prev = (int) ui[Native.UI_MANUAL_PREV_WDAY + night];
        if (prev < 0) {
            txtNightSpan.setVisibility(View.GONE);
        } else {
            txtNightSpan.setVisibility(View.VISIBLE);
            Sg.setTextIfChanged(txtNightSpan, act.getString(R.string.str_manual_night_span,
                    wdayShort(act, prev), wdayShort(act, wday)));
        }

        btnNightPrev.setEnabled(night > 0);
        btnNightNext.setEnabled(night < Native.NIGHT_TODAY);
        btnMinus.setEnabled(minutes > (int) ui[Native.UI_MANUAL_MIN]);
        btnPlus.setEnabled(minutes < (int) ui[Native.UI_MANUAL_MAX]);
        Sg.setTextIfChanged(txtMinutes, fmtDur(act, minutes));
    }

    /** Sends the picked date and the minutes to C, runs what it returns, closes the sheet. */
    private void save() {
        int base = Native.UI_NIGHTS + night * Native.UI_NIGHT_WORDS;
        int date = (int) ui[base + Native.NIGHT_DATE];
        Sg.Observation o = Sg.observe(act);
        long[] cmds = Native.nativeLogNight(o.nowMs, o.nextAlarmMs, o.creator, o.exactAllowed,
                o.notifAllowed, date, minutes);
        Sg.run(act, cmds);
        dialog.dismiss();
        if (cb != null) {
            cb.onSaved();
        }
    }

    // ------------------------------------------------------------------ rendering helpers
    // Same rules as MainActivity (ADVICE-v2 section 3): formatting only, no arithmetic on dates.

    static String fmtDur(Context c, int min) {
        if (min < 60) {
            return c.getString(R.string.str_fmt_duration_min, min);
        }
        return c.getString(R.string.str_fmt_duration, min / 60, min % 60);
    }

    /** yyyymmdd and weekday (0 = dom .. 6 = sáb) to "vie 9/10". */
    static String fmtDate(Context c, long yyyymmdd, int wday) {
        if (yyyymmdd == 0 || wday < 0 || wday > 6) {
            return "";
        }
        int m = (int) (yyyymmdd / 100 % 100);
        int d = (int) (yyyymmdd % 100);
        return c.getString(R.string.str_fmt_date_short, wdayShort(c, wday), d, m);
    }

    static String wdayShort(Context c, int wday) {
        if (wday < 0 || wday > 6) {
            return "";
        }
        return c.getResources().getStringArray(R.array.str_wday_short)[wday];
    }
}
