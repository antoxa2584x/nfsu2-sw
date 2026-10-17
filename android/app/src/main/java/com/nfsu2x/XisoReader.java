package com.nfsu2x;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Reads the file table of an original-Xbox disc image (XDVDFS, "XISO").
 * No Android dependencies, so it can be tested on a desktop JVM
 * (android/tools/XisoSelfTest.java).
 *
 * A disc image has the volume descriptor at sector 32 of the game partition;
 * the partition starts at byte 0 for a trimmed XISO and at a fixed offset in
 * a raw dump (video partition in front), so a few known offsets are tried.
 * Directories are AVL trees stored in 2048-byte sectors.
 */
final class XisoReader {

    static final class Entry {
        final String path;      // '/'-separated, relative to the disc root
        final long offset;      // byte offset in the image
        final long size;
        final boolean dir;
        Entry(String path, long offset, long size, boolean dir) {
            this.path = path; this.offset = offset; this.size = size; this.dir = dir;
        }
    }

    private static final int SECTOR = 2048;
    private static final long[] BASES = { 0L, 0x18300000L, 0x0FD90000L, 0x02090000L };
    private static final byte[] MAGIC = "MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);
    private static final int MAX_ENTRIES = 100000;
    private static final int MAX_DIR_BYTES = 16 * 1024 * 1024;

    private final FileChannel ch;
    private long base = -1;
    private long rootSector, rootSize;

    XisoReader(FileChannel ch) throws IOException {
        this.ch = ch;
        ByteBuffer b = ByteBuffer.allocate(SECTOR).order(ByteOrder.LITTLE_ENDIAN);
        long len = ch.size();
        for (long off : BASES) {
            long at = off + 32L * SECTOR;
            if (at + SECTOR > len)
                continue;
            b.clear();
            readFully(b, at);
            byte[] m = new byte[MAGIC.length];
            b.position(0);
            b.get(m);
            if (java.util.Arrays.equals(m, MAGIC)) {
                base = off;
                rootSector = b.getInt(0x14) & 0xFFFFFFFFL;
                rootSize = b.getInt(0x18) & 0xFFFFFFFFL;
                return;
            }
        }
        throw new IOException("not an Xbox disc image");
    }

    private void readFully(ByteBuffer b, long pos) throws IOException {
        while (b.hasRemaining()) {
            int n = ch.read(b, pos + b.position());
            if (n < 0)
                throw new IOException("image is truncated");
        }
        b.flip();
    }

    /** Every file and directory on the disc. Throws if the image is cut short. */
    List<Entry> list() throws IOException {
        List<Entry> out = new ArrayList<>();
        walkDir(rootSector, rootSize, "", out, 0);
        long len = ch.size();
        for (Entry e : out)
            if (!e.dir && e.offset + e.size > len)
                throw new IOException("image is incomplete (" + e.path + ")");
        return out;
    }

    private static int u16(byte[] d, int o) { return (d[o] & 0xFF) | ((d[o + 1] & 0xFF) << 8); }
    private static long u32(byte[] d, int o) {
        return (u16(d, o) | ((long) u16(d, o + 2) << 16)) & 0xFFFFFFFFL;
    }

    private void walkDir(long sector, long size, String prefix, List<Entry> out, int depth)
            throws IOException {
        if (depth > 32)
            throw new IOException("directories nested too deep");
        if (size == 0 || size > MAX_DIR_BYTES)
            return;
        byte[] d = new byte[(int) size];
        ByteBuffer b = ByteBuffer.wrap(d);
        readFully(b, base + sector * SECTOR);
        node(d, 0, prefix, out, depth, 0);
    }

    private void node(byte[] d, int off, String prefix, List<Entry> out, int depth, int level)
            throws IOException {
        if (level > 64 || off < 0 || off + 14 > d.length)
            return;
        int left = u16(d, off), right = u16(d, off + 2);
        long sector = u32(d, off + 4), size = u32(d, off + 8);
        int attr = d[off + 12] & 0xFF, nameLen = d[off + 13] & 0xFF;
        if (left == 0xFFFF && right == 0xFFFF)          // an empty (0xFF filled) directory
            return;
        if (nameLen == 0 || off + 14 + nameLen > d.length)
            throw new IOException("corrupt directory");
        if (left != 0)
            node(d, left * 4, prefix, out, depth, level + 1);

        String name = new String(d, off + 14, nameLen, StandardCharsets.ISO_8859_1);
        if (name.equals(".") || name.equals("..") || name.indexOf('/') >= 0
                || name.indexOf('\\') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("unsafe file name on the disc");
        String path = prefix + name;
        if (out.size() >= MAX_ENTRIES)
            throw new IOException("too many files");
        if ((attr & 0x10) != 0) {
            out.add(new Entry(path, 0, 0, true));
            walkDir(sector, size, path + "/", out, depth + 1);
        } else {
            out.add(new Entry(path, base + sector * SECTOR, size, false));
        }

        if (right != 0)
            node(d, right * 4, prefix, out, depth, level + 1);
    }
}
