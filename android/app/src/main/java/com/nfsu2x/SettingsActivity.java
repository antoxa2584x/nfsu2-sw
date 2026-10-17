package com.nfsu2x;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.InputStream;
import java.util.List;

/** Frame rate, screen stretch, render resolution, renderer, driver, controls. */
public class SettingsActivity extends Activity {

    private static final int BG = 0xFF101418, FG = 0xFFFFFFFF, DIM = 0xFF99A0A8;
    private static final int REQ_DRIVER = 7;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private LinearLayout driverBox;      // rebuilt when drivers come and go
    private TextView driverNote;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        float d = getResources().getDisplayMetrics().density;
        int pad = (int) (20 * d);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(pad, pad, pad, pad);

        col.addView(label(getString(R.string.settings), 22, FG, false));

        // ---- frame rate ----
        col.addView(label(getString(R.string.fps_title), 17, FG, true));
        col.addView(label(getString(R.string.fps_note), 13, DIM, false));
        RadioGroup fps = new RadioGroup(this);
        String[] fpsText = { getString(R.string.fps_30), getString(R.string.fps_60),
                getString(R.string.fps_120) };
        for (int i = 0; i < Settings.FPS.length; i++) {
            RadioButton r = radio(fpsText[i], i);
            fps.addView(r);
            if (Settings.FPS[i] == Settings.fps(this))
                r.setChecked(true);
        }
        fps.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup g, int id) {
                Settings.setFps(SettingsActivity.this, Settings.FPS[id]);
            }
        });
        col.addView(fps);

        // ---- stretch ----
        col.addView(label(getString(R.string.stretch_title), 17, FG, true));
        CheckBox stretch = new CheckBox(this);
        stretch.setText(R.string.stretch_note);
        stretch.setTextColor(0xFFDDDDDD);
        stretch.setChecked(Settings.stretch(this));
        stretch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton v, boolean on) {
                Settings.setStretch(SettingsActivity.this, on);
            }
        });
        col.addView(stretch);

        // ---- FPS counter ----
        col.addView(label(getString(R.string.showfps_title), 17, FG, true));
        CheckBox showFps = new CheckBox(this);
        showFps.setText(R.string.showfps_note);
        showFps.setTextColor(0xFFDDDDDD);
        showFps.setChecked(Settings.showFps(this));
        showFps.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton v, boolean on) {
                Settings.setShowFps(SettingsActivity.this, on);
            }
        });
        col.addView(showFps);

        // ---- resolution ----
        col.addView(label(getString(R.string.res_title), 17, FG, true));
        col.addView(label(getString(R.string.res_note), 13, DIM, false));
        RadioGroup res = new RadioGroup(this);
        for (int i = 0; i < Settings.SCALES.length; i++) {
            RadioButton r = radio(Settings.scaleLabel(i), i);
            res.addView(r);
            if (i == Settings.scaleIndex(this))
                r.setChecked(true);
        }
        res.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup g, int id) {
                Settings.setScaleIndex(SettingsActivity.this, id);
            }
        });
        col.addView(res);

        // ---- Vulkan driver ----
        col.addView(label(getString(R.string.driver_title), 17, FG, true));
        driverNote = label(getString(R.string.driver_note), 13, DIM, false);
        col.addView(driverNote);
        driverBox = new LinearLayout(this);
        driverBox.setOrientation(LinearLayout.VERTICAL);
        col.addView(driverBox);
        Button add = new Button(this);
        add.setText(R.string.driver_add);
        add.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                startActivityForResult(i, REQ_DRIVER);
            }
        });
        col.addView(add);
        refreshDrivers();

        // ---- controls ----
        col.addView(label(getString(R.string.controls_title), 17, FG, true));
        Button edit = new Button(this);
        edit.setText(R.string.edit_controls);
        edit.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(SettingsActivity.this, ControlEditorActivity.class));
            }
        });
        col.addView(edit);

        Button back = new Button(this);
        back.setText(R.string.back);
        back.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { finish(); }
        });
        col.addView(back);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(BG);
        sv.addView(col);
        setContentView(sv);
    }

    /** "System driver" plus every installed package, one is selected; long-press removes. */
    private void refreshDrivers() {
        driverBox.removeAllViews();
        RadioGroup g = new RadioGroup(this);
        final List<DriverManager.Info> drivers = DriverManager.list(this);
        String chosen = Settings.driver(this);
        boolean found = false;

        RadioButton sys = radio(getString(R.string.driver_system), 0);
        g.addView(sys);
        for (int i = 0; i < drivers.size(); i++) {
            RadioButton r = radio(drivers.get(i).toString(), i + 1);
            final String id = drivers.get(i).id;
            r.setOnLongClickListener(new View.OnLongClickListener() {
                @Override public boolean onLongClick(View v) {
                    DriverManager.delete(SettingsActivity.this, id);
                    if (id.equals(Settings.driver(SettingsActivity.this)))
                        Settings.setDriver(SettingsActivity.this, "");
                    Toast.makeText(SettingsActivity.this, R.string.driver_removed, Toast.LENGTH_SHORT).show();
                    refreshDrivers();
                    return true;
                }
            });
            g.addView(r);
            if (id.equals(chosen)) {
                r.setChecked(true);
                found = true;
            }
        }
        if (!found)
            sys.setChecked(true);
        g.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup grp, int id) {
                Settings.setDriver(SettingsActivity.this, id == 0 ? "" : drivers.get(id - 1).id);
            }
        });
        driverBox.addView(g);
        driverNote.setText(drivers.isEmpty() ? R.string.driver_note : R.string.driver_note_remove);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_DRIVER || res != RESULT_OK || data == null || data.getData() == null)
            return;
        final Uri uri = data.getData();
        Toast.makeText(this, R.string.driver_installing, Toast.LENGTH_SHORT).show();
        new Thread(new Runnable() {
            @Override public void run() {
                String problem = null;
                DriverManager.Info info = null;
                try {
                    InputStream in = getContentResolver().openInputStream(uri);
                    if (in == null) throw new java.io.IOException("cannot open the file");
                    try {
                        info = DriverManager.install(SettingsActivity.this, in);
                    } finally {
                        in.close();
                    }
                } catch (Throwable t) {
                    problem = String.valueOf(t.getMessage());
                }
                final String err = problem;
                final DriverManager.Info ok = info;
                ui.post(new Runnable() {
                    @Override public void run() {
                        if (err != null) {
                            Toast.makeText(SettingsActivity.this,
                                    getString(R.string.driver_failed, err), Toast.LENGTH_LONG).show();
                            return;
                        }
                        Settings.setDriver(SettingsActivity.this, ok.id);
                        Toast.makeText(SettingsActivity.this,
                                getString(R.string.driver_installed, ok.name), Toast.LENGTH_LONG).show();
                        refreshDrivers();
                    }
                });
            }
        }, "driver-install").start();
    }

    private TextView label(String s, int sp, int color, boolean topGap) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setGravity(Gravity.START);
        t.setPadding(0, topGap ? (int) (14 * getResources().getDisplayMetrics().density) : 0, 0, 6);
        return t;
    }

    private RadioButton radio(String text, int id) {
        RadioButton r = new RadioButton(this);
        r.setId(id);
        r.setText(text);
        r.setTextColor(0xFFDDDDDD);
        return r;
    }
}
