package com.nfsu2x;

import android.content.Context;
import android.hardware.input.InputManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.widget.TextView;
import java.util.Locale;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity plus the on-screen controls. The game itself (libmain.so,
 * SDL_main in src/main.c) runs on SDL's thread; this class only decides
 * whether the touch overlay is shown and forwards its state over JNI
 * (src/android_host.c). A physical gamepad hides the overlay and drives the
 * game through SDL's game-controller layer instead.
 */
public class NFSU2Activity extends SDLActivity implements InputManager.InputDeviceListener {

    private static native void nativeTouchPad(int mask, int lt, int rt,
                                              int lx, int ly, int rx, int ry);
    private static native void nativeTouchEnabled(boolean on);
    private static native int nativeFrames();

    private TextView fpsView;
    private final Handler fpsHandler = new Handler(Looper.getMainLooper());
    private int fpsLast;
    private long fpsLastMs;
    private final Runnable fpsTick = new Runnable() {
        @Override public void run() {
            long now = SystemClock.elapsedRealtime();
            int f = nativeFrames();
            if (fpsLastMs != 0 && now > fpsLastMs) {
                float fps = (f - fpsLast) * 1000f / (now - fpsLastMs);
                fpsView.setText(String.format(Locale.US, "%.0f FPS  %.1f ms", fps,
                        fps > 0 ? 1000f / fps : 0f));
            }
            fpsLast = f;
            fpsLastMs = now;
            fpsHandler.postDelayed(this, 500);
        }
    };

    private TouchControlsView controls;
    private InputManager inputManager;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        controls = new TouchControlsView(this, new TouchControlsView.Listener() {
            @Override
            public void onState(int mask, int lt, int rt, int lx, int ly) {
                nativeTouchPad(mask, lt, rt, lx, ly, 0, 0);
            }
        });
        View content = SDLActivity.getContentView();
        if (content instanceof ViewGroup) {
            ((ViewGroup) content).addView(controls, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }

        if (Settings.showFps(this) && content instanceof ViewGroup) {
            fpsView = new TextView(this);
            fpsView.setTextColor(Color.WHITE);
            fpsView.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
            fpsView.setTextSize(13);
            fpsView.setBackgroundColor(0x88000000);
            fpsView.setPadding(16, 6, 16, 6);
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.TOP | Gravity.END);
            ((ViewGroup) content).addView(fpsView, lp);
            fpsHandler.postDelayed(fpsTick, 500);
        }

        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        inputManager.registerInputDeviceListener(this, null);
        refreshOverlay();
    }

    @Override
    protected void onDestroy() {
        fpsHandler.removeCallbacksAndMessages(null);
        if (inputManager != null)
            inputManager.unregisterInputDeviceListener(this);
        super.onDestroy();
    }

    private static boolean hasGamepad() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual())
                continue;
            int src = d.getSources();
            if ((src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
                return true;
        }
        return false;
    }

    private void refreshOverlay() {
        final boolean show = !hasGamepad();
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (controls != null)
                    controls.setVisibility(show ? View.VISIBLE : View.GONE);
            }
        });
        nativeTouchEnabled(show);
        if (!show)
            nativeTouchPad(0, 0, 0, 0, 0, 0, 0);
    }

    @Override public void onInputDeviceAdded(int deviceId)   { refreshOverlay(); }
    @Override public void onInputDeviceRemoved(int deviceId) { refreshOverlay(); }
    @Override public void onInputDeviceChanged(int deviceId) { refreshOverlay(); }
}
