package com.nfsu2x;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * Drag the on-screen buttons where you want them. Tap a button to select it
 * (yellow frame), drag to move it, and use the sliders for its size, the size
 * of everything, and the opacity.
 */
public class ControlEditorActivity extends Activity {

    private TouchControlsView view;
    private TextView selLabel;
    private SeekBar selSize, allSize, opacity;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        float d = getResources().getDisplayMetrics().density;

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xFF1B2128);

        view = new TouchControlsView(this, new TouchControlsView.Listener() {
            @Override public void onState(int mask, int lt, int rt, int lx, int ly) { }
        });
        view.setEditMode(true, new Runnable() {
            @Override public void run() { syncSliders(); }
        });
        root.addView(view, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(0xCC000000);
        int p = (int) (10 * d);
        panel.setPadding(p, p, p, p);

        TextView hint = new TextView(this);
        hint.setText(R.string.editor_hint);
        hint.setTextColor(0xFFDDDDDD);
        hint.setTextSize(12);
        panel.addView(hint);

        selLabel = new TextView(this);
        selLabel.setTextColor(0xFFFFCC00);
        panel.addView(selLabel);
        selSize = slider(panel, getString(R.string.size_selected), 50, 200);
        allSize = slider(panel, getString(R.string.size_all), 50, 200);
        opacity = slider(panel, getString(R.string.opacity), 20, 100);
        allSize.setProgress(view.getGlobalSizePercent() - 50);
        opacity.setProgress(view.getOpacityPercent() - 20);

        selSize.setOnSeekBarChangeListener(new Watch() {
            @Override void changed(int v) { view.setSelectedSizePercent(v + 50); }
        });
        allSize.setOnSeekBarChangeListener(new Watch() {
            @Override void changed(int v) { view.setGlobalSizePercent(v + 50); }
        });
        opacity.setOnSeekBarChangeListener(new Watch() {
            @Override void changed(int v) { view.setOpacityPercent(v + 20); }
        });

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button reset = new Button(this);
        reset.setText(R.string.reset);
        reset.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                view.resetLayout();
                allSize.setProgress(view.getGlobalSizePercent() - 50);
                opacity.setProgress(view.getOpacityPercent() - 20);
                syncSliders();
            }
        });
        Button done = new Button(this);
        done.setText(R.string.done);
        done.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                view.saveLayout();
                finish();
            }
        });
        row.addView(reset, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        row.addView(done, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        panel.addView(row);

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                (int) (320 * d), FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        root.addView(panel, lp);
        setContentView(root);
        syncSliders();
    }

    private SeekBar slider(LinearLayout parent, String name, int min, int max) {
        TextView t = new TextView(this);
        t.setText(name);
        t.setTextColor(0xFFFFFFFF);
        t.setTextSize(12);
        parent.addView(t);
        SeekBar s = new SeekBar(this);
        s.setMax(max - min);
        parent.addView(s);
        return s;
    }

    private void syncSliders() {
        int pct = view.getSelectedSizePercent();
        boolean has = pct >= 0;
        selLabel.setText(has ? getString(R.string.selected, view.selectedLabel())
                : getString(R.string.nothing_selected));
        selSize.setEnabled(has);
        selSize.setProgress(has ? pct - 50 : 50);
    }

    private abstract static class Watch implements SeekBar.OnSeekBarChangeListener {
        abstract void changed(int value);
        @Override public void onProgressChanged(SeekBar s, int v, boolean fromUser) {
            if (fromUser) changed(v);
        }
        @Override public void onStartTrackingTouch(SeekBar s) { }
        @Override public void onStopTrackingTouch(SeekBar s) { }
    }

    @Override
    public void onBackPressed() {
        view.saveLayout();      // leaving with Back keeps what you did
        super.onBackPressed();
    }
}
