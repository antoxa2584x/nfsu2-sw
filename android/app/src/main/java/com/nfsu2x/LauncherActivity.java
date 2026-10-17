package com.nfsu2x;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.text.format.Formatter;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.RandomAccessFile;
import java.nio.channels.FileChannel;

/**
 * First screen. The game needs the extracted disc in files/game/; the person
 * gives us an ISO instead:
 *
 *   - drop it into Android/data/com.nfsu2x.nfsu2/files/iso/ (or files/), or
 *   - pick it with the button (no copy is made, it is read in place).
 *
 * It is extracted into files/game/ and the ISO is deleted. Once
 * files/game/default.xbe exists this screen shows Start and Settings; the game
 * only begins when Start is pressed.
 */
public class LauncherActivity extends Activity {

    private static final int REQ_PICK = 1;
    private static final long STABLE_MS = 3000;     // an ISO still being copied keeps growing

    private final Handler ui = new Handler(Looper.getMainLooper());
    private File base, gameDir, isoDir;
    private TextView title, message, detail;
    private ProgressBar bar;
    private Button pick, retry, start, settings;
    private volatile boolean busy, cancelled;
    private File waitingFor;
    private long waitingSize = -1;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        File ext = getExternalFilesDir(null);
        base = ext != null ? ext : getFilesDir();
        gameDir = new File(base, "game");
        isoDir = new File(base, "iso");
        isoDir.mkdirs();
        gameDir.mkdirs();
        new File(base, "save").mkdirs();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(0xFF101418);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        title = text(getString(R.string.app_name), 22, 0xFFFFFFFF);
        message = text("", 16, 0xFFDDDDDD);
        detail = text("", 13, 0xFF99A0A8);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        pick = new Button(this);
        pick.setText(R.string.pick_iso);
        pick.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { pickIso(); }
        });
        retry = new Button(this);
        retry.setText(R.string.retry);
        retry.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { check(); }
        });

        start = new Button(this);
        start.setText(R.string.start);
        start.setTextSize(20);
        start.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { launch(); }
        });
        settings = new Button(this);
        settings.setText(R.string.settings);
        settings.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(LauncherActivity.this, SettingsActivity.class));
            }
        });

        root.addView(title);
        root.addView(message);
        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(detail);
        root.addView(start);
        root.addView(pick);
        root.addView(retry);
        root.addView(settings);
        setContentView(root);
        show(false, false, false);
    }

    private TextView text(String s, int sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        t.setPadding(0, 12, 0, 12);
        return t;
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!busy)
            check();
    }

    @Override
    protected void onPause() {
        super.onPause();
        ui.removeCallbacksAndMessages(null);
    }

    /**
     * Controls shown: the progress bar while extracting; once the game is
     * installed the Start button (nothing starts by itself); otherwise the
     * "pick" / "retry" buttons. Settings is there whenever nothing is running.
     */
    private void show(boolean progress, boolean error, boolean ready) {
        bar.setVisibility(progress ? View.VISIBLE : View.GONE);
        start.setVisibility(ready && !progress ? View.VISIBLE : View.GONE);
        pick.setVisibility(progress || ready ? View.GONE : View.VISIBLE);
        retry.setVisibility(error ? View.VISIBLE : View.GONE);
        settings.setVisibility(progress ? View.GONE : View.VISIBLE);
    }

    // ---- what to do now ------------------------------------------------

    private void check() {
        File iso = findIso();
        if (iso != null) {
            waitForCopy(iso);
            return;
        }
        waitingFor = null;
        if (new File(gameDir, "default.xbe").isFile()) {
            message.setText(R.string.ready);
            detail.setText("");
            show(false, false, true);
            return;
        }
        message.setText(getString(R.string.put_iso, isoDir.getAbsolutePath()));
        detail.setText("");
        show(false, false, false);
    }

    private File findIso() {
        for (File dir : new File[] { isoDir, base }) {
            File[] list = dir.listFiles();
            if (list == null) continue;
            for (File f : list)
                if (f.isFile() && f.getName().toLowerCase().endsWith(".iso"))
                    return f;
        }
        return null;
    }

    /** An ISO that is still being copied in (adb push) must not be read yet. */
    private void waitForCopy(final File iso) {
        long size = iso.length();
        if (iso.equals(waitingFor) && size == waitingSize && size > 0) {
            waitingFor = null;
            startInstall(iso, null);
            return;
        }
        waitingFor = iso;
        waitingSize = size;
        message.setText(R.string.waiting_copy);
        detail.setText(iso.getName() + "  " + Formatter.formatFileSize(this, size));
        show(false, false, false);
        ui.postDelayed(new Runnable() {
            @Override public void run() { if (!busy) check(); }
        }, STABLE_MS);
    }

    private void pickIso() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        startActivityForResult(i, REQ_PICK);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_PICK && res == RESULT_OK && data != null && data.getData() != null)
            startInstall(null, data.getData());
    }

    // ---- extraction ----------------------------------------------------

    private void startInstall(final File isoFile, final Uri isoUri) {
        busy = true;
        cancelled = false;
        message.setText(R.string.extracting);
        detail.setText("");
        bar.setProgress(0);
        show(true, false, false);

        new Thread(new Runnable() {
            @Override public void run() {
                String problem = null;
                boolean deleted = false;
                ParcelFileDescriptor pfd = null;
                try {
                    FileChannel ch;
                    RandomAccessFile raf = null;
                    if (isoFile != null) {
                        raf = new RandomAccessFile(isoFile, "r");
                        ch = raf.getChannel();
                    } else {
                        pfd = getContentResolver().openFileDescriptor(isoUri, "r");
                        ch = new FileInputStream(pfd.getFileDescriptor()).getChannel();
                    }
                    try {
                        IsoInstaller.install(ch, gameDir, new IsoInstaller.Listener() {
                            private long lastUi;
                            @Override public void progress(long done, long total, String file) {
                                long now = System.currentTimeMillis();
                                if (now - lastUi < 250 && done < total) return;
                                lastUi = now;
                                postProgress(done, total, file);
                            }
                            @Override public boolean cancelled() { return cancelled; }
                        });
                    } finally {
                        ch.close();
                        if (raf != null) raf.close();
                        if (pfd != null) pfd.close();
                    }
                    // Done: the ISO is not needed any more.
                    if (isoFile != null)
                        deleted = isoFile.delete();
                    else
                        deleted = deleteDocument(isoUri);
                } catch (IsoInstaller.NotEnoughSpace e) {
                    problem = getString(R.string.low_space,
                            Formatter.formatFileSize(LauncherActivity.this, e.needed));
                } catch (Throwable t) {
                    problem = getString(R.string.failed, String.valueOf(t.getMessage()));
                }
                installDone(problem, deleted, isoFile == null);
            }
        }, "iso-extract").start();
    }

    private boolean deleteDocument(Uri uri) {
        try {
            return DocumentsContract.deleteDocument(getContentResolver(), uri);
        } catch (Throwable t) {
            return false;
        }
    }

    private void postProgress(final long done, final long total, final String file) {
        ui.post(new Runnable() {
            @Override public void run() {
                bar.setProgress(total > 0 ? (int) (done * 1000 / total) : 0);
                message.setText(getString(R.string.extracting_pct,
                        total > 0 ? (int) (done * 100 / total) : 0));
                detail.setText(Formatter.formatFileSize(LauncherActivity.this, done) + " / "
                        + Formatter.formatFileSize(LauncherActivity.this, total) + "\n" + file);
            }
        });
    }

    private void installDone(final String problem, final boolean deleted, final boolean picked) {
        ui.post(new Runnable() {
            @Override public void run() {
                busy = false;
                if (problem != null) {
                    message.setText(problem);
                    detail.setText("");
                    show(false, true, false);
                    return;
                }
                if (!deleted && picked)     // a picked ISO may live where we cannot delete
                    android.widget.Toast.makeText(LauncherActivity.this,
                            R.string.iso_not_deleted, android.widget.Toast.LENGTH_LONG).show();
                check();            // shows Start; the game does not begin by itself
            }
        });
    }

    private void launch() {
        Settings.writeEnv(this, base);      // fps / stretch / resolution for this run
        startActivity(new Intent(this, NFSU2Activity.class));
        finish();
    }

    @Override
    public void onBackPressed() {
        if (busy) {
            cancelled = true;       // stops after the current chunk; a rerun resumes
            return;
        }
        super.onBackPressed();
    }
}
