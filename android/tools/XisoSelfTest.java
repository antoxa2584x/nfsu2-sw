// Desktop JVM test for the extractor (package-private classes, hence the package):
//   javac -d /tmp/xt android/app/src/main/java/com/nfsu2x/{XisoReader,IsoInstaller}.java android/tools/XisoSelfTest.java
//   java -cp /tmp/xt com.nfsu2x.XisoSelfTest image.iso outdir
package com.nfsu2x;

import java.io.File;
import java.io.RandomAccessFile;

public class XisoSelfTest {
    public static void main(String[] a) throws Exception {
        try (RandomAccessFile f = new RandomAccessFile(a[0], "r")) {
            IsoInstaller.install(f.getChannel(), new File(a[1]), new IsoInstaller.Listener() {
                long last;
                public void progress(long done, long total, String file) { last = done; }
                public boolean cancelled() { return false; }
            });
        }
        System.out.println("extracted");
    }
}
