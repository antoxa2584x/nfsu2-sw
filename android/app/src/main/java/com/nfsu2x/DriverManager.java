package com.nfsu2x;

import android.content.Context;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Custom Vulkan drivers (Turnip, from the usual "adrenotools" .zip packages:
 * a meta.json plus the driver library). Each one is unpacked into
 * files/drivers/&lt;id&gt;/ -- it must be internal storage, the system will not
 * dlopen a library from anywhere else. Used by the Vulkan renderer only.
 */
public final class DriverManager {

    private DriverManager() { }

    /** One installed driver. */
    public static final class Info {
        public String id, name, library, version;
        public File dir;
        @Override public String toString() {
            return name + (version == null || version.isEmpty() ? "" : "  (" + version + ")");
        }
    }

    public static File root(Context c) {
        return new File(c.getFilesDir(), "drivers");
    }

    public static List<Info> list(Context c) {
        List<Info> out = new ArrayList<Info>();
        File[] dirs = root(c).listFiles();
        if (dirs == null) return out;
        for (File d : dirs) {
            Info i = read(d);
            if (i != null) out.add(i);
        }
        return out;
    }

    public static Info find(Context c, String id) {
        if (id == null || id.isEmpty()) return null;
        return read(new File(root(c), id));
    }

    private static Info read(File dir) {
        File meta = new File(dir, "meta.json");
        if (!dir.isDirectory() || !meta.isFile()) return null;
        try {
            JSONObject j = new JSONObject(new String(readAll(new java.io.FileInputStream(meta)), "UTF-8"));
            Info i = new Info();
            i.dir = dir;
            i.id = dir.getName();
            i.library = j.optString("libraryName", "");
            i.name = j.optString("name", i.id);
            i.version = j.optString("driverVersion", "");
            if (i.library.isEmpty() || !new File(dir, i.library).isFile()) return null;
            return i;
        } catch (Throwable t) {
            return null;
        }
    }

    public static boolean delete(Context c, String id) {
        File d = new File(root(c), id);
        if (!d.getParentFile().equals(root(c))) return false;
        deleteTree(d);
        return !d.exists();
    }

    /** Unpacks a driver package; returns it, or throws with a readable message. */
    public static Info install(Context c, InputStream zip) throws IOException {
        File root = root(c);
        root.mkdirs();
        File tmp = new File(root, ".incoming");
        deleteTree(tmp);
        if (!tmp.mkdirs()) throw new IOException("cannot create " + tmp);
        try {
            String tmpPath = tmp.getCanonicalPath() + File.separator;
            ZipInputStream z = new ZipInputStream(zip);
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            long total = 0;
            while ((e = z.getNextEntry()) != null) {
                File out = new File(tmp, e.getName());
                if (!out.getCanonicalPath().startsWith(tmpPath))
                    throw new IOException("bad path in zip: " + e.getName());     // zip-slip
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                FileOutputStream f = new FileOutputStream(out);
                try {
                    int n;
                    while ((n = z.read(buf)) > 0) {
                        total += n;
                        if (total > (512L << 20)) throw new IOException("package too large");
                        f.write(buf, 0, n);
                    }
                } finally {
                    f.close();
                }
                out.setReadable(true, false);
            }
            File metaDir = findMeta(tmp, 0);
            if (metaDir == null) throw new IOException("no meta.json in the zip (not a driver package)");
            Info probe = readLoose(metaDir);
            if (probe == null) throw new IOException("meta.json has no usable libraryName");
            String id = probe.name.replaceAll("[^A-Za-z0-9._-]+", "_");
            if (id.isEmpty() || id.startsWith(".")) id = "driver";
            File dest = new File(root, id);
            deleteTree(dest);
            if (!metaDir.renameTo(dest)) throw new IOException("cannot move the driver into place");
            Info i = read(dest);
            if (i == null) throw new IOException("the installed driver is incomplete");
            return i;
        } finally {
            deleteTree(tmp);
        }
    }

    private static Info readLoose(File dir) {
        Info i = read(dir);
        if (i != null) return i;
        return null;
    }

    /** The directory holding meta.json: the zip's root or one level down. */
    private static File findMeta(File dir, int depth) {
        if (new File(dir, "meta.json").isFile()) return dir;
        if (depth >= 2) return null;
        File[] kids = dir.listFiles();
        if (kids == null) return null;
        for (File k : kids)
            if (k.isDirectory()) {
                File r = findMeta(k, depth + 1);
                if (r != null) return r;
            }
        return null;
    }

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null)
            for (File k : kids) deleteTree(k);
        f.delete();
    }

    private static byte[] readAll(InputStream in) throws IOException {
        try {
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] b = new byte[4096];
            int n;
            while ((n = in.read(b)) > 0) o.write(b, 0, n);
            return o.toByteArray();
        } finally {
            in.close();
        }
    }
}
