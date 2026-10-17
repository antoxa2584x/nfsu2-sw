package com.nfsu2x;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * A virtual Xbox pad: floating left stick, A/B/X/Y, triggers, bumpers
 * (Black/White on the original), Start/Back. Multi-touch; a finger may slide
 * from one button to the next. Reports XInput-layout state:
 *
 *   mask: 0x0010 start, 0x0020 back, 0x0100 LB, 0x0200 RB,
 *         0x1000 A, 0x2000 B, 0x4000 X, 0x8000 Y (see src/android_host.c)
 */
public class TouchControlsView extends View {

    public interface Listener {
        void onState(int mask, int lt, int rt, int lx, int ly);
    }

    private static final int START = 0x0010, BACK = 0x0020, LB = 0x0100, RB = 0x0200,
            A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000, LT = 0x10000, RT = 0x20000;

    private static final class Btn {
        final String label;
        final int bit;
        final RectF r = new RectF();
        boolean round;
        Btn(String label, int bit, boolean round) { this.label = label; this.bit = bit; this.round = round; }
    }

    private final Listener listener;
    private ControlLayout layout;
    private boolean editMode;
    private int selected = -1;                  // editor: button index, STICK, or -1
    private Runnable onSelect;
    private float dragDx, dragDy;
    private final RectF[] defaults = new RectF[ControlLayout.BUTTONS];
    private final Btn[] btns = {
            new Btn("A", A, true), new Btn("B", B, true), new Btn("X", X, true), new Btn("Y", Y, true),
            new Btn("LT", LT, false), new Btn("RT", RT, false),
            new Btn("LB", LB, false), new Btn("RB", RB, false),
            new Btn("BACK", BACK, false), new Btn("START", START, false),
    };

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);

    // floating stick
    private int stickPointer = -1;
    private float stickCx, stickCy, stickX, stickY, stickRadius;
    private float stickZoneRight;               // fingers left of this start the stick
    private int pressed;                        // bit set of buttons currently held
    private int lastMask = -1, lastLx, lastLy;

    public TouchControlsView(Context c, Listener l) {
        super(c);
        listener = l;
        layout = ControlLayout.load(c);
        setAlpha(layout.opacity);
        setFocusable(false);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setColor(0xCCFFFFFF);
        text.setColor(0xFFFFFFFF);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float u = Math.min(w, h) / 100f;           // 1 unit = 1% of the short side
        float m = 4 * u;
        stroke.setStrokeWidth(0.35f * u);
        text.setTextSize(4.2f * u);
        stickRadius = 13 * u * layout.stickScale * layout.globalScale;
        stickZoneRight = w * 0.42f;

        // face buttons, diamond in the lower right
        float s = 9.5f * u, cx = w - 22 * u, cy = h - 25 * u, d = 11.5f * u;
        place(btns[0], cx, cy + d, s);              // A bottom
        place(btns[1], cx + d, cy, s);              // B right
        place(btns[2], cx - d, cy, s);              // X left
        place(btns[3], cx, cy - d, s);              // Y top
        // triggers and bumpers, top corners
        float bw = 22 * u, bh = 11 * u;
        btns[4].r.set(m, m, m + bw, m + bh);                         // LT
        btns[5].r.set(w - m - bw, m, w - m, m + bh);                 // RT
        btns[6].r.set(m, m + bh + 2 * u, m + bw, m + 2 * bh + 2 * u);     // LB
        btns[7].r.set(w - m - bw, m + bh + 2 * u, w - m, m + 2 * bh + 2 * u); // RB
        // back / start, top centre
        float sw = 14 * u, sh = 7 * u;
        btns[8].r.set(w / 2f - sw - u, m, w / 2f - u, m + sh);
        btns[9].r.set(w / 2f + u, m, w / 2f + sw + u, m + sh);
        for (int i = 0; i < btns.length; i++)
            defaults[i] = new RectF(btns[i].r);
        applyLayout();
    }

    /** Default rectangles, scaled and moved by the saved layout. */
    private void applyLayout() {
        int w = getWidth(), h = getHeight();
        if (w == 0 || defaults[0] == null)
            return;
        float u = Math.min(w, h) / 100f;
        stickRadius = 13 * u * layout.stickScale * layout.globalScale;
        for (int i = 0; i < btns.length; i++) {
            RectF d = defaults[i];
            float k = layout.scale[i] * layout.globalScale;
            float cx = d.centerX(), cy = d.centerY();
            if (!Float.isNaN(layout.x[i]) && !Float.isNaN(layout.y[i])) {
                cx = layout.x[i] * w;
                cy = layout.y[i] * h;
            }
            float hw = d.width() / 2 * k, hh = d.height() / 2 * k;
            btns[i].r.set(cx - hw, cy - hh, cx + hw, cy + hh);
        }
    }

    // ---- editor API (ControlEditorActivity) -----------------------------

    public void setEditMode(boolean on, Runnable selectionChanged) {
        editMode = on;
        onSelect = selectionChanged;
        setAlpha(on ? 1f : layout.opacity);
        invalidate();
    }

    public int getSelected() { return selected; }

    /** 50..200 (%) of the selected button, or of the stick; -1 when nothing is selected. */
    public int getSelectedSizePercent() {
        if (selected < 0) return -1;
        float v = selected == ControlLayout.STICK ? layout.stickScale : layout.scale[selected];
        return Math.round(v * 100);
    }

    public void setSelectedSizePercent(int pct) {
        if (selected < 0) return;
        float v = pct / 100f;
        if (selected == ControlLayout.STICK) layout.stickScale = v;
        else layout.scale[selected] = v;
        applyLayout();
        invalidate();
    }

    public int getGlobalSizePercent() { return Math.round(layout.globalScale * 100); }

    public void setGlobalSizePercent(int pct) {
        layout.globalScale = pct / 100f;
        applyLayout();
        invalidate();
    }

    public int getOpacityPercent() { return Math.round(layout.opacity * 100); }

    public void setOpacityPercent(int pct) {
        layout.opacity = pct / 100f;
    }

    public void resetLayout() {
        layout.reset();
        applyLayout();
        invalidate();
    }

    public void saveLayout() {
        layout.save(getContext());
    }

    public String selectedLabel() {
        if (selected < 0) return "";
        return selected == ControlLayout.STICK ? "STICK" : btns[selected].label;
    }

    private boolean onEditTouch(MotionEvent e) {
        float x = e.getX(), y = e.getY();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: {
                int hit = -1;
                for (int i = btns.length - 1; i >= 0; i--)
                    if (btns[i].r.contains(x, y)) { hit = i; break; }
                if (hit < 0 && Math.hypot(x - stickRadius * 1.6f,
                        y - (getHeight() - stickRadius * 1.6f)) <= stickRadius)
                    hit = ControlLayout.STICK;
                selected = hit;
                if (hit >= 0 && hit < ControlLayout.BUTTONS) {
                    dragDx = btns[hit].r.centerX() - x;
                    dragDy = btns[hit].r.centerY() - y;
                }
                if (onSelect != null) onSelect.run();
                invalidate();
                break;
            }
            case MotionEvent.ACTION_MOVE:
                if (selected >= 0 && selected < ControlLayout.BUTTONS && getWidth() > 0) {
                    float cx = Math.max(0, Math.min(getWidth(), x + dragDx));
                    float cy = Math.max(0, Math.min(getHeight(), y + dragDy));
                    layout.x[selected] = cx / getWidth();
                    layout.y[selected] = cy / getHeight();
                    applyLayout();
                    invalidate();
                }
                break;
            default:
                break;
        }
        return true;
    }

    private void place(Btn b, float cx, float cy, float rad) {
        b.r.set(cx - rad, cy - rad, cx + rad, cy + rad);
    }

    @Override
    protected void onDraw(Canvas c) {
        for (Btn b : btns) {
            boolean on = (pressed & b.bit) != 0;
            fill.setColor(on ? 0x88FFFFFF : 0x33000000);
            if (b.round) {
                c.drawCircle(b.r.centerX(), b.r.centerY(), b.r.width() / 2, fill);
                c.drawCircle(b.r.centerX(), b.r.centerY(), b.r.width() / 2, stroke);
            } else {
                float rr = b.r.height() / 3;
                c.drawRoundRect(b.r, rr, rr, fill);
                c.drawRoundRect(b.r, rr, rr, stroke);
            }
            c.drawText(b.label, b.r.centerX(), b.r.centerY() + text.getTextSize() / 3, text);
        }
        if (editMode) {
            stroke.setColor(0xFFFFCC00);
            if (selected >= 0 && selected < ControlLayout.BUTTONS) {
                RectF r = btns[selected].r;
                c.drawRect(r.left - 6, r.top - 6, r.right + 6, r.bottom + 6, stroke);
            } else if (selected == ControlLayout.STICK) {
                c.drawCircle(stickRadius * 1.6f, getHeight() - stickRadius * 1.6f,
                        stickRadius + 6, stroke);
            }
            stroke.setColor(0xCCFFFFFF);
        }
        if (stickPointer >= 0) {
            fill.setColor(0x22FFFFFF);
            c.drawCircle(stickCx, stickCy, stickRadius, fill);
            c.drawCircle(stickCx, stickCy, stickRadius, stroke);
            fill.setColor(0x88FFFFFF);
            c.drawCircle(stickCx + stickX * stickRadius, stickCy + stickY * stickRadius,
                    stickRadius * 0.4f, fill);
        } else {
            fill.setColor(0x18FFFFFF);
            c.drawCircle(stickRadius * 1.6f, getHeight() - stickRadius * 1.6f, stickRadius, fill);
            c.drawCircle(stickRadius * 1.6f, getHeight() - stickRadius * 1.6f, stickRadius, stroke);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (editMode)
            return onEditTouch(e);
        int action = e.getActionMasked();
        int up = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP)
                ? e.getPointerId(e.getActionIndex()) : -1;
        boolean cancel = action == MotionEvent.ACTION_CANCEL;

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int i = e.getActionIndex();
            if (stickPointer < 0 && e.getX(i) < stickZoneRight && !hitButton(e.getX(i), e.getY(i))) {
                stickPointer = e.getPointerId(i);
                stickCx = e.getX(i);
                stickCy = e.getY(i);
                stickX = stickY = 0;
            }
        }

        int mask = 0;
        if (cancel) {
            stickPointer = -1;
            stickX = stickY = 0;
        } else {
            for (int i = 0; i < e.getPointerCount(); i++) {
                int id = e.getPointerId(i);
                if (id == up)
                    continue;
                float x = e.getX(i), y = e.getY(i);
                if (id == stickPointer) {
                    float dx = x - stickCx, dy = y - stickCy;
                    float len = (float) Math.hypot(dx, dy);
                    if (len > stickRadius) {         // drag the base along
                        stickCx += dx * (len - stickRadius) / len;
                        stickCy += dy * (len - stickRadius) / len;
                        dx = x - stickCx;
                        dy = y - stickCy;
                        len = stickRadius;
                    }
                    stickX = dx / stickRadius;
                    stickY = dy / stickRadius;
                } else {
                    for (Btn b : btns)
                        if (b.r.contains(x, y))
                            mask |= b.bit;
                }
            }
            if (up >= 0 && up == stickPointer) {
                stickPointer = -1;
                stickX = stickY = 0;
            }
        }
        pressed = mask;
        publish();
        invalidate();
        return true;
    }

    private boolean hitButton(float x, float y) {
        for (Btn b : btns)
            if (b.r.contains(x, y))
                return true;
        return false;
    }

    private void publish() {
        int lx = (int) (clamp(stickX) * 32767);
        int ly = (int) (-clamp(stickY) * 32767);      // screen y is down, the pad's is up
        int dead = 3500;                                // small dead zone
        if (Math.abs(lx) < dead) lx = 0;
        if (Math.abs(ly) < dead) ly = 0;
        int mask = pressed & 0xFFFF;
        int lt = (pressed & LT) != 0 ? 255 : 0;
        int rt = (pressed & RT) != 0 ? 255 : 0;
        int key = mask | (lt << 16) | (rt << 24);
        if (key == lastMask && lx == lastLx && ly == lastLy)
            return;
        lastMask = key; lastLx = lx; lastLy = ly;
        listener.onState(mask, lt, rt, lx, ly);
    }

    private static float clamp(float v) {
        return v < -1 ? -1 : (v > 1 ? 1 : v);
    }
}
