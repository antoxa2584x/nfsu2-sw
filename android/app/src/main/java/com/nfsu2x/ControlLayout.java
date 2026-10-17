package com.nfsu2x;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Where the on-screen buttons sit. A button without a saved position keeps the
 * default layout; a moved one is stored as a fraction of the screen size, so it
 * survives a different resolution. Sizes are multipliers of the default.
 */
public final class ControlLayout {

    public static final int BUTTONS = 10;      // A B X Y LT RT LB RB BACK START
    public static final int STICK = BUTTONS;   // index of the stick in the editor

    public final float[] x = new float[BUTTONS];       // NaN = default
    public final float[] y = new float[BUTTONS];
    public final float[] scale = new float[BUTTONS];
    public float stickScale = 1f;
    public float globalScale = 1f;
    public float opacity = 0.85f;

    private ControlLayout() { }

    public static ControlLayout load(Context c) {
        SharedPreferences p = Settings.prefs(c);
        ControlLayout l = new ControlLayout();
        for (int i = 0; i < BUTTONS; i++) {
            l.x[i] = p.getFloat("c" + i + "x", Float.NaN);
            l.y[i] = p.getFloat("c" + i + "y", Float.NaN);
            l.scale[i] = p.getFloat("c" + i + "s", 1f);
        }
        l.stickScale = p.getFloat("cstick", 1f);
        l.globalScale = p.getFloat("cglobal", 1f);
        l.opacity = p.getFloat("copacity", 0.85f);
        return l;
    }

    public void save(Context c) {
        SharedPreferences.Editor e = Settings.prefs(c).edit();
        for (int i = 0; i < BUTTONS; i++) {
            if (Float.isNaN(x[i]) || Float.isNaN(y[i])) {
                e.remove("c" + i + "x");
                e.remove("c" + i + "y");
            } else {
                e.putFloat("c" + i + "x", x[i]);
                e.putFloat("c" + i + "y", y[i]);
            }
            e.putFloat("c" + i + "s", scale[i]);
        }
        e.putFloat("cstick", stickScale);
        e.putFloat("cglobal", globalScale);
        e.putFloat("copacity", opacity);
        e.apply();
    }

    public void reset() {
        for (int i = 0; i < BUTTONS; i++) {
            x[i] = y[i] = Float.NaN;
            scale[i] = 1f;
        }
        stickScale = 1f;
        globalScale = 1f;
        opacity = 0.85f;
    }
}
