package com.nfsu2x;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.channels.FileChannel;
import java.util.List;

/**
 * Extracts a disc image into the game folder (the layout the runtime reads:
 * default.xbe, NFSUNDER/, ...). Files already extracted with the right size
 * are skipped, so an interrupted run resumes; each file is written under a
 * .part name and renamed when complete, so default.xbe only appears once it
 * is whole. Saves live in game/UDATA and are never touched.
 */
final class IsoInstaller {

    interface Listener {
        void progress(long done, long total, String file);
        boolean cancelled();
    }

    static final class NotEnoughSpace extends IOException {
        final long needed;
        NotEnoughSpace(long needed) { super("not enough free space"); this.needed = needed; }
    }

    private static final long CHUNK = 8L * 1024 * 1024;

    static void install(FileChannel iso, File gameDir, Listener l) throws IOException {
        XisoReader reader = new XisoReader(iso);
        List<XisoReader.Entry> entries = reader.list();

        long total = 0, todo = 0;
        for (XisoReader.Entry e : entries) {
            if (e.dir) continue;
            total += e.size;
            File dest = target(gameDir, e.path);
            if (!(dest.isFile() && dest.length() == e.size))
                todo += e.size;
        }
        if (!gameDir.isDirectory() && !gameDir.mkdirs())
            throw new IOException("cannot create " + gameDir);
        long margin = 64L * 1024 * 1024;
        if (gameDir.getUsableSpace() < todo + margin)
            throw new NotEnoughSpace(todo + margin - gameDir.getUsableSpace());

        long done = 0;
        for (XisoReader.Entry e : entries) {
            if (l.cancelled())
                throw new IOException("cancelled");
            File dest = target(gameDir, e.path);
            if (e.dir) {
                if (!dest.isDirectory() && !dest.mkdirs())
                    throw new IOException("cannot create " + dest);
                continue;
            }
            if (dest.isFile() && dest.length() == e.size) {
                done += e.size;
                l.progress(done, total, e.path);
                continue;
            }
            File parent = dest.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                throw new IOException("cannot create " + parent);
            File part = new File(dest.getPath() + ".part");
            try (FileOutputStream os = new FileOutputStream(part)) {
                FileChannel out = os.getChannel();
                long pos = 0;
                while (pos < e.size) {
                    if (l.cancelled())
                        throw new IOException("cancelled");
                    long n = iso.transferTo(e.offset + pos, Math.min(CHUNK, e.size - pos), out);
                    if (n <= 0)
                        throw new IOException("image is truncated (" + e.path + ")");
                    pos += n;
                    l.progress(done + pos, total, e.path);
                }
            }
            if (dest.exists() && !dest.delete())
                throw new IOException("cannot replace " + dest);
            if (!part.renameTo(dest))
                throw new IOException("cannot rename " + part);
            done += e.size;
        }
        if (!new File(gameDir, "default.xbe").isFile())
            throw new IOException("no default.xbe on the disc: not an Xbox game image");
    }

    /** gameDir/path, refusing anything that would land outside gameDir. */
    private static File target(File gameDir, String path) throws IOException {
        File f = new File(gameDir, path);
        String root = gameDir.getCanonicalPath() + File.separator;
        if (!f.getCanonicalPath().startsWith(root))
            throw new IOException("unsafe path on the disc: " + path);
        return f;
    }
}
