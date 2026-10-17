package com.nfsu2x;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;

/**
 * The launcher's settings. They live in SharedPreferences and are turned into
 * environment variables for the game: Start writes files/launcher_env.txt,
 * which src/android_host.c reads before the person's own nfsu2x_env.txt.
 */
public final class Settings {

    /** Render-resolution multiples of the title's 640x480 (RECOMP_GL_SCALE). */
    public static final float[] SCALES = { 0.5f, 0.75f, 1f, 1.5f, 2f, 3f };
    public static final int DEFAULT_SCALE = 2;          // 1x
    /** Race frame-rate targets. */
    public static final int[] FPS = { 30, 60, 120 };

    private static final String PREFS = "nfsu2x_settings";

    private Settings() { }

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** FPS counter overlay; off by default. Read by NFSU2Activity. */
    public static boolean showFps(Context c) {
        return prefs(c).getBoolean("show_fps", false);
    }

    public static void setShowFps(Context c, boolean on) {
        prefs(c).edit().putBoolean("show_fps", on).apply();
    }

    public static int fps(Context c) {
        int v = prefs(c).getInt("fps", 30);
        return v == 60 || v == 120 ? v : 30;
    }

    public static void setFps(Context c, int fps) {
        prefs(c).edit().putInt("fps", fps).apply();
    }

    /** Fill the whole screen, ignoring the aspect ratio. Off by default. */
    public static boolean stretch(Context c) {
        return prefs(c).getBoolean("stretch", false);
    }

    public static void setStretch(Context c, boolean on) {
        prefs(c).edit().putBoolean("stretch", on).apply();
    }

    public static int scaleIndex(Context c) {
        int i = prefs(c).getInt("scale", DEFAULT_SCALE);
        return i < 0 || i >= SCALES.length ? DEFAULT_SCALE : i;
    }

    public static void setScaleIndex(Context c, int i) {
        prefs(c).edit().putInt("scale", i).apply();
    }

    public static String scaleLabel(int i) {
        float s = SCALES[i];
        return trim(s) + "x  (" + Math.round(640 * s) + " x " + Math.round(480 * s) + ")";
    }

    private static String trim(float f) {
        return f == (int) f ? String.valueOf((int) f) : String.valueOf(f);
    }

    // ---- renderer ------------------------------------------------------

    public static final String GLES = "gles", VULKAN = "vulkan";

    /** OpenGL ES unless the person chose Vulkan. */
    public static String renderer(Context c) {
        return VULKAN;   // the launcher is Vulkan-only; native falls back to GL if it cannot start
    }

    public static void setRenderer(Context c, String r) {
        prefs(c).edit().putString("renderer", VULKAN.equals(r) ? VULKAN : GLES).apply();
    }

    /** Id of the chosen custom Vulkan driver (DriverManager), "" for the system's. */
    public static String driver(Context c) {
        return prefs(c).getString("driver", "");
    }

    public static void setDriver(Context c, String id) {
        prefs(c).edit().putString("driver", id == null ? "" : id).apply();
    }

    /** Writes files/launcher_env.txt from the current settings. */
    public static void writeEnv(Context c, File base) {
        StringBuilder sb = new StringBuilder();
        sb.append("# written by the launcher; edit nfsu2x_env.txt instead\n");
        sb.append("RECOMP_GL_SCALE=").append(trim(SCALES[scaleIndex(c)])).append('\n');
        // NFSU2 flips every second vblank in a race, so the display rate the
        // game sees is twice the frame rate asked for.
        int fps = fps(c);
        if (fps == 60)  sb.append("RECOMP_VBLANK_HZ=119.88\n");
        if (fps == 120) sb.append("RECOMP_VBLANK_HZ=239.76\n");
        if (stretch(c)) sb.append("RECOMP_STRETCH=1\n");
        if (VULKAN.equals(renderer(c))) {
            sb.append("RECOMP_RENDERER=vulkan\n");
            DriverManager.Info d = DriverManager.find(c, driver(c));
            if (d != null) {
                sb.append("RECOMP_VK_DRIVER_DIR=").append(d.dir.getAbsolutePath()).append("/\n");
                sb.append("RECOMP_VK_DRIVER_NAME=").append(d.library).append('\n');
                sb.append("RECOMP_VK_HOOK_DIR=").append(c.getApplicationInfo().nativeLibraryDir).append('\n');
                sb.append("RECOMP_VK_TMP_DIR=").append(c.getCodeCacheDir().getAbsolutePath()).append('\n');
            }
        }
        try {
            Writer w = new OutputStreamWriter(
                    new FileOutputStream(new File(base, "launcher_env.txt")), "UTF-8");
            try { w.write(sb.toString()); } finally { w.close(); }
        } catch (Throwable ignored) {
            // The game then runs with its defaults.
        }
    }
}
